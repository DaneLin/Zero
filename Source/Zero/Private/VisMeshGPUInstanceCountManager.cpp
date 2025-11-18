#include "VisMeshGPUInstanceCountManager.h"

#include "GPUSortManager.h"
#include "RenderGraphUtils.h"
#include "VisMeshEmptyUAVPool.h"
#include "VisMeshGpuComputeDispatchInterface.h"

int32 GVisMeshMinGPUInstanceCount = 2048;
static FAutoConsoleVariableRef CVarVisMeshMinGPUInstanceCount(
	TEXT("VisMesh.MinGPUInstanceCount"),
	GVisMeshMinGPUInstanceCount,
	TEXT("Minimum number of instance count entries allocated in the global buffer. (default=2048)"),
	ECVF_Default
	);

int32 GVisMeshGPUCountManagerAllocateIncrement = 64;
static FAutoConsoleVariableRef CVarVisMeshGPUCountManagerAllocateIncrement(
	TEXT("VisMesh.GPUCountManager.AllocateIncrement"),
	GVisMeshGPUCountManagerAllocateIncrement,
	TEXT("If we run out of space for allocations this is how many allocate rather than a single entry. (default=64)"),
	ECVF_Default
);

int32 GVisMeshMinCulledGPUInstanceCount = 2048;
static FAutoConsoleVariableRef CVarVisMeshMinCulledGPUInstanceCount(
	TEXT("VisMesh.MinCulledGPUInstanceCount"),
	GVisMeshMinCulledGPUInstanceCount,
	TEXT("Minimum number of culled (per-view) instance count entries allocated in the global buffer. (default=2048)"),
	ECVF_Default
);

float GVisMeshGPUCountBufferSlack = 1.5f;
static FAutoConsoleVariableRef CVarVisMeshGPUCountBufferSlack(
	TEXT("VisMesh.GPUCountBufferSlack"),
	GVisMeshGPUCountBufferSlack,
	TEXT("Multiplier of the GPU count buffer size to prevent frequent re-allocation."),
	ECVF_Default
);

int32 GVisMeshIndirectArgsPoolMinSize = 256;
static FAutoConsoleVariableRef CVarVisMeshIndirectArgsPoolMinSize(
	TEXT("fx.VisMesh.IndirectArgsPool.MinSize"),
	GVisMeshIndirectArgsPoolMinSize,
	TEXT("Minimum number of draw indirect args allocated into the pool. (default=256)"),
	ECVF_Default
);

float GVisMeshIndirectArgsPoolBlockSizeFactor = 2.0f;
static FAutoConsoleVariableRef CVisMeshIndirectArgsPoolBlockSizeFactor(
	TEXT("fx.VisMesh.IndirectArgsPool.BlockSizeFactor"),
	GVisMeshIndirectArgsPoolBlockSizeFactor,
	TEXT("Multiplier on the indirect args pool size when needing to increase it from running out of space. (default=2.0)"),
	ECVF_Default
);

int32 GVisMeshIndirectArgsPoolAllowShrinking = 1;
static FAutoConsoleVariableRef CVarVisMeshIndirectArgsPoolAllowShrinking(
	TEXT("fx.VisMesh.IndirectArgsPool.AllowShrinking"),
	GVisMeshIndirectArgsPoolAllowShrinking,
	TEXT("Allow the indirect args pool to shrink after a number of frames below a low water mark."),
	ECVF_Default
);

float GVisMeshIndirectArgsPoolLowWaterAmount = 0.5f;
static FAutoConsoleVariableRef CVarVisMeshIndirectArgsPoolLowWaterAmount(
	TEXT("fx.VisMesh.IndirectArgsPool.LowWaterAmount"),
	GVisMeshIndirectArgsPoolLowWaterAmount,
	TEXT("Percentage (0-1) of the indirect args pool that is considered low and worthy of shrinking"),
	ECVF_Default
);

int32 GVisMeshIndirectArgsPoolLowWaterFrames = 150;
static FAutoConsoleVariableRef CVarVisMeshIndirectArgsPoolLowWaterFrames(
	TEXT("fx.VisMesh.IndirectArgsPool.LowWaterFrames"),
	GVisMeshIndirectArgsPoolLowWaterFrames,
	TEXT("The number of frames to wait to shrink the indirect args pool for being below the low water mark. (default=150)"),
	ECVF_Default
);

#ifndef ENABLE_VISMESH_INDIRECT_ARG_POOL_LOG
#define ENABLE_VISMESH_INDIRECT_ARG_POOL_LOG 0
#endif

#if ENABLE_VISMESH_INDIRECT_ARG_POOL_LOG
#define INDIRECT_ARG_POOL_LOG(Format, ...) UE_LOG(LogVisMesh, Log, TEXT("VisMesh INDIRECT ARG POOL: ") TEXT(Format), __VA_ARGS__)
#else
#define INDIRECT_ARG_POOL_LOG(Format, ...) do {} while(0)
#endif

const ERHIAccess FVisMeshGPUInstanceCountManager::kCountBufferDefaultState = ERHIAccess::SRVMask | ERHIAccess::CopySrc;
const ERHIAccess FVisMeshGPUInstanceCountManager::kIndirectArgsDefaultState = ERHIAccess::IndirectArgs | ERHIAccess::SRVMask;

FVisMeshGPUInstanceCountManager::FVisMeshGPUInstanceCountManager(ERHIFeatureLevel::Type FeatureLevel)
	:FeatureLevel(FeatureLevel)
{
}

FVisMeshGPUInstanceCountManager::~FVisMeshGPUInstanceCountManager()
{
	ReleaseRHI();
}

void FVisMeshGPUInstanceCountManager::InitRHI(FRHICommandListBase& RHICmdList)
{
}

void FVisMeshGPUInstanceCountManager::ReleaseRHI()
{
	ReleaseCounts();

	for (auto& PoolEntry : DrawIndirectPool)
	{
		PoolEntry->Buffer.Release();
	}
	DrawIndirectPool.Empty();
}

uint32 FVisMeshGPUInstanceCountManager::AcquireEntry()
{
	check(IsInParallelRenderingThread());

	UE::TScopeLock LockGuard(AcquireEntryGuard);

	if (FreeEntries.Num())
	{
		return FreeEntries.Pop();
	}
	else if (UsedInstanceCounts < AllocatedInstanceCounts)
	{
		// We can't reallocate on the fly, the buffer must be correctly resized before any tick gets scheduled.
		return UsedInstanceCounts++;
	}
	else
	{
		return INDEX_NONE;
	}
}

uint32 FVisMeshGPUInstanceCountManager::AcquireOrAllocateEntry(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());

	// Free Entries
	if (FreeEntries.Num())
	{
		return FreeEntries.Pop();
	}
	else if (UsedInstanceCounts < AllocatedInstanceCounts)
	{
		return UsedInstanceCounts++;
	}

	// Need to resize
	ResizeBuffers(RHICmdList, AllocatedInstanceCounts + GVisMeshGPUCountManagerAllocateIncrement);

	check(UsedInstanceCounts < AllocatedInstanceCounts);
	return UsedInstanceCounts++;
}

void FVisMeshGPUInstanceCountManager::FreeEntry(uint32& BufferOffset)
{
	check(IsInRenderingThread());

	if (BufferOffset != INDEX_NONE)
	{
		checkf(!FreeEntries.Contains(BufferOffset), TEXT("BufferOffset %u exists in FreeEntries"), BufferOffset);
		checkf(!InstanceCountClearTasks.Contains(BufferOffset), TEXT("BufferOffset %u exists in InstanceCountClearTasks"), BufferOffset);

		InstanceCountClearTasks.Add(BufferOffset);
		BufferOffset = INDEX_NONE;
	}
}

void FVisMeshGPUInstanceCountManager::FreeEntryArray(TConstArrayView<uint32> EntryArray)
{
	check(IsInRenderingThread());

	const int32 NumToFree = EntryArray.Num();
	if (NumToFree > 0)
	{
#if DO_CHECK
		for (uint32 BufferOffset : EntryArray)
		{
			checkf(!FreeEntries.Contains(BufferOffset), TEXT("BufferOffset %u exists in FreeEntries"), BufferOffset);
			checkf(!InstanceCountClearTasks.Contains(BufferOffset), TEXT("BufferOffset %u exists in InstanceCountClearTasks"), BufferOffset);
		}
#endif
		InstanceCountClearTasks.Append(EntryArray.GetData(), NumToFree);
	}
}

FRWBuffer* FVisMeshGPUInstanceCountManager::AcquireCulledCountsBuffer(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());

	if (RequiredCulledCounts > 0)
	{
		if (!bAcquiredCulledCounts)
		{
			const int32 RecommendedCulledCounts = FMath::Max(GVisMeshMinCulledGPUInstanceCount, (int32)(RequiredCulledCounts * GVisMeshGPUCountBufferSlack));
			if (RecommendedCulledCounts > AllocatedCulledCounts)
			{
				// We need a bigger buffer
				CulledCountBuffer.Release();

				AllocatedCulledCounts = RecommendedCulledCounts;
				CulledCountBuffer.Initialize(RHICmdList, TEXT("VisMeshCulledGPUInstanceCounts"),sizeof(uint32), AllocatedCulledCounts, EPixelFormat::PF_R32_UINT, ERHIAccess::UAVCompute);
				CulledCountsRHIAccess = ERHIAccess::UAVCompute;
			}
			else if (CulledCountsRHIAccess != ERHIAccess::UAVCompute)
			{
				RHICmdList.Transition((FRHITransitionInfo(CulledCountBuffer.UAV, CulledCountsRHIAccess, ERHIAccess::UAVCompute)));
			}

			// Initialize the buffer by clearing it to zero then transition it to be ready to write to
			RHICmdList.ClearUAVUint(CulledCountBuffer.UAV, FUintVector4(EForceInit::ForceInitToZero));
			RHICmdList.Transition(FRHITransitionInfo(CulledCountBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));

			bAcquiredCulledCounts = true;
		}
		else if (CulledCountsRHIAccess != ERHIAccess::UAVCompute)
		{
			RHICmdList.Transition(FRHITransitionInfo(CulledCountBuffer.UAV, CulledCountsRHIAccess, ERHIAccess::UAVCompute));
			CulledCountsRHIAccess = ERHIAccess::UAVCompute;
		}

		return &CulledCountBuffer;
	}
	return nullptr;
}

const uint32* FVisMeshGPUInstanceCountManager::GetGPUReadback()
{
	check(IsInRenderingThread());

	if (CountReadback && CountReadbackSize && CountReadback->IsReady())
	{
		// TODO: Add SCOPE
		// SCOPE_CYCLE_COUNTER(STAT_VisMeshGPUReadbackLock);
		return (uint32*)(CountReadback->Lock(CountReadbackSize * sizeof(uint32)));
	}
	else
	{
		return nullptr;
	}
}

void FVisMeshGPUInstanceCountManager::ReleaseGPUReadback()
{
	check(IsInRenderingThread());
	check(CountReadback && CountReadbackSize);
	CountReadback->Unlock();
	// Readback can only ever be done once, to prevent misusage with index lifetime
	CountReadbackSize = 0;
}

void FVisMeshGPUInstanceCountManager::EnqueueGPUReadback(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());
	if (UsedInstanceCounts > 0 && (UsedInstanceCounts != FreeEntries.Num()))
	{
		if (!CountReadback)
		{
			CountReadback = new FRHIGPUBufferReadback(TEXT("VisMesh GPU Instance Count Readback"));
		}
		CountReadbackSize = UsedInstanceCounts;

		// No need for a transition, FVisMeshGpuComputeDispatch ensures that the buffer is left in the correct state after the simulation.
		CountReadback->EnqueueCopy(RHICmdList, CountBuffer.Buffer);
	}
}

bool FVisMeshGPUInstanceCountManager::HasPendingGPUReadback() const
{
	check(IsInRenderingThread());
	return CountReadback && CountReadbackSize;
}

FVisMeshGPUInstanceCountManager::FIndirectArgSlot FVisMeshGPUInstanceCountManager::AddDrawIndirect(
	FRHICommandListBase& RHICmdList, uint32 InstanceCountBufferOffset, uint32 NumIndicesPerInstance,
	uint32 StartIndexLocation, bool bIsInstancedStereoEnabled, bool bCulled,EVisMeshGpuComputeTickStage::Type ReadyTickStage)
{
	UE::TScopeLock Lock(AddDrawIndirectGuard);

	const EVisMeshDrawIndirectArgGenTaskFlags TaskFlags =
		(bIsInstancedStereoEnabled ? EVisMeshDrawIndirectArgGenTaskFlags::InstancedStereo : EVisMeshDrawIndirectArgGenTaskFlags::None)
		| (bCulled ? EVisMeshDrawIndirectArgGenTaskFlags::UseCulledCounts : EVisMeshDrawIndirectArgGenTaskFlags::None)
		| (ReadyTickStage == EVisMeshGpuComputeTickStage::PostOpaqueRender ? EVisMeshDrawIndirectArgGenTaskFlags::PostOpaque : EVisMeshDrawIndirectArgGenTaskFlags::None);
	FVisMeshDrawIndirectArgGenTaskInfo Info(InstanceCountBufferOffset, NumIndicesPerInstance, StartIndexLocation,TaskFlags);

	FVisMeshDrawIndirectArgGenSlotInfo* SlotInfo = DrawIndirectArgMap.Find(Info);
	if (SlotInfo == nullptr)
	{
		// Attempt to allocate a new slot from the pool, or add to the pool if is's full
		FIndirectArgsPoolEntry* PoolEntry = DrawIndirectPool.Num() > 0 ? DrawIndirectPool.Last().Get() : nullptr;
		if (PoolEntry == nullptr || PoolEntry->UsedEntriesTotal >= PoolEntry->AllocatedEntries)
		{
			FIndirectArgsPoolEntryPtr NewEntry = MakeUnique<FIndirectArgsPoolEntry>();
			NewEntry->AllocatedEntries = PoolEntry ? uint32(PoolEntry->AllocatedEntries * GVisMeshIndirectArgsPoolBlockSizeFactor) : uint32(GVisMeshIndirectArgsPoolMinSize);

			INDIRECT_ARG_POOL_LOG("Increasing pool from size %d to %d", PoolEntry ? PoolEntry->AllocatedEntries : 0, NewEntry->AllocatedEntries);

			TResourceArray<uint32> InitData;
			InitData.AddZeroed(NewEntry->AllocatedEntries * VISMESH_DRAW_INDIRECT_ARGS_SIZE);
			NewEntry->Buffer.Initialize(RHICmdList, TEXT("VisMeshGPUDrawIndirectArgs"),sizeof(uint32), NewEntry->AllocatedEntries * VISMESH_DRAW_INDIRECT_ARGS_SIZE,EPixelFormat::PF_R32_UINT, kIndirectArgsDefaultState, BUF_Static | BUF_DrawIndirect, &InitData);

			PoolEntry = NewEntry.Get();
			DrawIndirectPool.Emplace(MoveTemp(NewEntry));
		}

		Info.IndirectArgsBufferOffset = PoolEntry->UsedEntriesTotal * VISMESH_DRAW_INDIRECT_ARGS_SIZE;
		++PoolEntry->UsedEntriesTotal;

		SlotInfo = &DrawIndirectArgMap.Add(Info);
		SlotInfo->PoolIndex = DrawIndirectPool.Num() - 1;
		SlotInfo->BufferOffset = Info.IndirectArgsBufferOffset * sizeof(uint32);

		const EVisMeshGPUCountUpdatePhase::Type CountPhase = ReadyTickStage == EVisMeshGpuComputeTickStage::PostOpaqueRender ? EVisMeshGPUCountUpdatePhase::PostOpaque : EVisMeshGPUCountUpdatePhase::PreOpaque;
		DrawIndirectArgGenTasks[CountPhase].Add(Info);
		++PoolEntry->UsedEntries[CountPhase];
	}
	return FIndirectArgSlot(DrawIndirectPool[SlotInfo->PoolIndex]->Buffer.Buffer, DrawIndirectPool[SlotInfo->PoolIndex]->Buffer.SRV, SlotInfo->BufferOffset);
}

void FVisMeshGPUInstanceCountManager::ResizeBuffers(FRHICommandListImmediate& RHICmdList, int32 ReservedInstanceCounts)
{
	const int32 RequiredInstanceCounts = UsedInstanceCounts + FMath::Max<int32>(ReservedInstanceCounts - FreeEntries.Num(), 0);
	if (RequiredInstanceCounts > 0)
	{
		const int32 RecommendedInstanceCounts = FMath::Max(GVisMeshMinGPUInstanceCount, (int32)(RequiredInstanceCounts + GVisMeshGPUCountBufferSlack));
		// If the buffer is not allocated, allocate it to the recommended size
		if (!AllocatedInstanceCounts)
		{
			AllocatedInstanceCounts = RecommendedInstanceCounts;
			TResourceArray<uint32> InitData;
			InitData.AddZeroed(AllocatedInstanceCounts);
			CountBuffer.Initialize(RHICmdList, TEXT("VisMeshGPUInstanceCounts"), sizeof(uint32), AllocatedInstanceCounts, EPixelFormat::PF_R32_UINT, kCountBufferDefaultState, BUF_Static | BUF_SourceCopy, &InitData);
			//UE_LOG(LogVisMesh, Log, TEXT("FVisMeshGPUInstanceCountManager::ResizeBuffers Alloc AllocatedInstanceCounts: %d ReservedInstanceCounts: %d"), AllocatedInstanceCounts, ReservedInstanceCounts);
		}
		// If we need to increase the buffer size to RecommendedInstanceCounts because the buffer is too small.
		else if (RequiredInstanceCounts > AllocatedInstanceCounts)
		{
			// TODO: Performance Scope

			// Init a bigger buffer filled with 0
			TResourceArray<uint32> InitData;
			InitData.AddZeroed(RecommendedInstanceCounts);
			FRWBuffer NextCountBuffer;
			NextCountBuffer.Initialize(RHICmdList, TEXT("VisMeshGPUInstanceCounts"), sizeof(uint32), RecommendedInstanceCounts, EPixelFormat::PF_R32_UINT, ERHIAccess::UAVCompute, BUF_Static | BUF_SourceCopy, &InitData);

			// Copy the current buffer in the next buffer
			// We don't need to transition any of the buffers, because the current bufffer is transitioned to readable after
			// the simulation, and the new buffer is created in the UAVCompute state
			FRHIUnorderedAccessView* UAVs[] = {NextCountBuffer.UAV};
			int32 UsedIndexCounts[] = {AllocatedInstanceCounts};
			CopyUIntBufferToTargets(RHICmdList, FeatureLevel, CountBuffer.SRV, UAVs, UsedIndexCounts, 0, UE_ARRAY_COUNT(UAVs));

			// FVisMeshGpuComputeDispatch expects the count buffer to be readable and copyable before running the sim.
			RHICmdList.Transition(FRHITransitionInfo(NextCountBuffer.UAV, ERHIAccess::UAVCompute, kCountBufferDefaultState));

			// Swap the buffers
			AllocatedInstanceCounts = RecommendedInstanceCounts;
			Swap(NextCountBuffer, CountBuffer);
			//UE_LOG(LogVisMesh, Log, TEXT("FVisMeshGPUInstanceCountManager::ResizeBuffers Resize AllocatedInstanceCounts: %d ReservedInstanceCounts: %d"), AllocatedInstanceCounts, ReservedInstanceCounts);
		}
		// If we need to shrink the buffer size because use way to much buffer size.
		else if ((int32)(RecommendedInstanceCounts * GVisMeshGPUCountBufferSlack) < AllocatedInstanceCounts)
		{
			// possibly shrink but hard to do because of sparse array allocation.
		}
	}
	else
	{
		ReleaseCounts();
	}

}

void FVisMeshGPUInstanceCountManager::FlushIndirectArgsPool(FRHICommandListBase& RHICmdList)
{
	check(IsInRenderingThread());
	checkf(DrawIndirectArgMap.IsEmpty(), TEXT("DrawIndirectArgMap is not empty in FlushIndirectArgsPool.  This means that OnPostRenderOpaque was not called on the GPU sort manager."));

	// Cull indirect draw pool entries so that we only keep the last pool
	while (DrawIndirectPool.Num() > 1)
	{
		FIndirectArgsPoolEntryPtr& PoolEntry = DrawIndirectPool[0];
		PoolEntry->Buffer.Release();

		DrawIndirectPool.RemoveAt(0, EAllowShrinking::No);
	}

	// If shrinking is allowed and we've been under the low water mark
	if (GVisMeshIndirectArgsPoolAllowShrinking && DrawIndirectPool.Num() > 0 && DrawIndirectLowWaterFrames >= uint32(GVisMeshIndirectArgsPoolLowWaterFrames))
	{
		FIndirectArgsPoolEntryPtr& PoolEntry = DrawIndirectPool[0];
		const uint32 NewSize = FMath::Max<uint32>(GVisMeshIndirectArgsPoolMinSize, FMath::FloorToInt(float(PoolEntry->AllocatedEntries) / GVisMeshIndirectArgsPoolBlockSizeFactor));

		INDIRECT_ARG_POOL_LOG("Shrinking pool from size %d to %d", PoolEntry->AllocatedEntries, NewSize);

		PoolEntry->Buffer.Release();
		PoolEntry->AllocatedEntries = NewSize;

		TResourceArray<uint32> InitData;
		InitData.AddZeroed(PoolEntry->AllocatedEntries * VISMESH_DRAW_INDIRECT_ARGS_SIZE);
		PoolEntry->Buffer.Initialize(RHICmdList, TEXT("VisMeshGPUDrawIndirectArgs"), sizeof(uint32), PoolEntry->AllocatedEntries * VISMESH_DRAW_INDIRECT_ARGS_SIZE, EPixelFormat::PF_R32_UINT, kIndirectArgsDefaultState, BUF_Static | BUF_DrawIndirect, &InitData);

		// Reset the timer
		DrawIndirectLowWaterFrames = 0;
	}
}

void FVisMeshGPUInstanceCountManager::UpdateDrawIndirectBuffers(
	FVisMeshGpuComputeDispatchInterface* ComputeDispatchInterface, FRHICommandList& RHICmdList,
	EVisMeshGPUCountUpdatePhase::Type CountPhase)
{
	check(IsInRenderingThread());

	TArray<FVisMeshDrawIndirectArgGenTaskInfo>& ArgTasks = DrawIndirectArgGenTasks[CountPhase];
	const bool bClearCounts = (CountPhase == EVisMeshGPUCountUpdatePhase::PreOpaque) && (InstanceCountClearTasks.Num() > 0) && ComputeDispatchInterface->IsFirstViewFamily();
	// TODO:
}

void FVisMeshGPUInstanceCountManager::CopyToMultiViewCountBuffer(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());
	if (AllocatedInstanceCounts > 0)
	{
		// Need to copy on all GPUs
		SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

		// Set AllocatedInstanceCounts and copy CountBuffer
		if (MultiViewAllocatedInstanceCounts != AllocatedInstanceCounts)
		{
			MultiViewAllocatedInstanceCounts = AllocatedInstanceCounts;
			MultiViewCountBuffer.Initialize(RHICmdList, TEXT("VisMeshGPUInstanceCounts"), sizeof(uint32), MultiViewAllocatedInstanceCounts, EPixelFormat::PF_R32_UINT, ERHIAccess::UAVCompute, BUF_Static | BUF_SourceCopy);
		}
		else
		{
			RHICmdList.Transition(FRHITransitionInfo(MultiViewCountBuffer.UAV, kCountBufferDefaultState, ERHIAccess::UAVCompute));
		}

		FRHIUnorderedAccessView* UAVs[] = { MultiViewCountBuffer.UAV };
		int32 UsedIndexCounts[] = { MultiViewAllocatedInstanceCounts };
		CopyUIntBufferToTargets(RHICmdList, FeatureLevel, CountBuffer.SRV, UAVs, UsedIndexCounts, 0, UE_ARRAY_COUNT(UAVs));

		RHICmdList.Transition(FRHITransitionInfo(MultiViewCountBuffer.UAV, ERHIAccess::UAVCompute, kCountBufferDefaultState));
	}
}

void FVisMeshGPUInstanceCountManager::ProcessInitInstanceCountTasks(
	FVisMeshGpuComputeDispatchInterface* ComputeDispatchInterface, FRHICommandList& RHICmdList)
{
	if (InstanceCountInitTasks.NumBytes() == 0)
	{
		return;
	}

	const int32 NumTasks = InstanceCountInitTasks.Num() / 2;

	// Allocate task buffer
	FReadBuffer TaskInfosBuffer;
	{
		const uint32 TaskBufferSize = InstanceCountInitTasks.Num() * sizeof(uint32);
		TaskInfosBuffer.Initialize(RHICmdList, TEXT("VisMeshInitCountsTaskInfosBuffer"),sizeof(uint32), InstanceCountInitTasks.Num(), EPixelFormat::PF_R32_UINT, BUF_Volatile);

		uint8* TaskBufferData = (uint8*)RHICmdList.LockBuffer(TaskInfosBuffer.Buffer, 0, TaskBufferSize, RLM_WriteOnly);
		FMemory::Memcpy(TaskBufferData, InstanceCountInitTasks.GetData(), TaskBufferSize);
		RHICmdList.UnlockBuffer(TaskInfosBuffer.Buffer);
	}

	FVisMeshEmptyUAVPoolScopedAccess UAVPoolAccessScope(ComputeDispatchInterface->GetEmptyUAVPool());
	TArray<FRHITransitionInfo, TInlineAllocator<10>> Transitions;
	Transitions.Reserve(1);
	FRWBuffer& CurrentCountBuffer = CountBuffer;

	// Get counts buffer
	FUnorderedAccessViewRHIRef CountsUAV = nullptr;
	const bool bCountBufferIsValid = CurrentCountBuffer.UAV.IsValid();
	if (bCountBufferIsValid)
	{
		// treat the incoming UAV as being unknown to be sure a barrier is inserted in the case
		// where the preceding dispatch wrote to the counts buffer
		Transitions.Emplace(CurrentCountBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute);
		CountsUAV = CurrentCountBuffer.UAV;
	}
	else
	{
		// This can happen if there are no InstanceCountClearTasks and all DrawIndirectArgGenTasks_PreOpaque are using culled counts
		CountsUAV = ComputeDispatchInterface->GetEmptyUAVFromPool(RHICmdList, PF_R32_UINT, EVisMeshEmptyUAVType::Buffer);
	}

	RHICmdList.Transition(Transitions);

	FVisMeshInstanceCountsInitCS::FParameters InitCountParameters;
	InitCountParameters.TaskInfos = TaskInfosBuffer.SRV;
	InitCountParameters.RWInstanceCounts = CountsUAV;
	InitCountParameters.TaskCount.X = NumTasks;
	

	FVisMeshInstanceCountsInitCS::FPermutationDomain PermutationVectorResetCounts;
	TShaderMapRef<FVisMeshInstanceCountsInitCS> InitCountsCS(GetGlobalShaderMap(FeatureLevel), PermutationVectorResetCounts);
	FComputeShaderUtils::Dispatch(RHICmdList, InitCountsCS, InitCountParameters, FIntVector(FMath::DivideAndRoundUp(NumTasks, VISMESH_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT), 1, 1));

	// Generate and execute transitions
	Transitions.Reset();
	Transitions.Emplace(CurrentCountBuffer.UAV, ERHIAccess::UAVCompute, kCountBufferDefaultState);
	RHICmdList.Transition(Transitions);

	InstanceCountInitTasks.Reset();
}

void FVisMeshGPUInstanceCountManager::AddInstanceCountInitTask(uint32 Offset, uint32 Value)
{
	InstanceCountInitTasks.Emplace(Offset);
	InstanceCountInitTasks.Emplace(Value);
}

void FVisMeshGPUInstanceCountManager::ReleaseCounts()
{
	CountBuffer.Release();
	CulledCountBuffer.Release();
	MultiViewCountBuffer.Release();

	AllocatedInstanceCounts = 0;
	AllocatedCulledCounts = 0;

	if (CountReadback)
	{
		delete CountReadback;
		CountReadback = nullptr;
		CountReadbackSize = 0;
	}
}
