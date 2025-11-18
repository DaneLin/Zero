#pragma once

#include "Containers/StridedView.h"
#include "VisMeshCommon.h"
#include "VisMeshEmptyUAVPool.h"
#include "VisMeshGpuComputeDataManager.h"
#include "VisMeshGPUInstanceCountManager.h"
#include "FXSystem.h"

class FGlobalDistanceFieldParameterData;
class FRDGBuilder;
class FRDGExternalAccessQueue;
class FSceneView;

class FVisMeshAsyncGpuTraceHelper;
struct FVisMeshComputeExecutionContext;
class FVisMeshGpuComputeDebug;
class FVisMeshGpuComputeDebugInterface;
class FVisMeshGPUInstanceCountManager;
class FVisMeshGpuReadbackManager;
class FVisMeshRayTracingHelper;
struct FVisMeshScriptDebuggerInfo;
class FVisMeshSystemGpuComputeProxy;

using FVisMeshDataChannelDataProxyPtr = TSharedPtr<struct FVisMeshDataChannelDataProxy>;

class FVisMeshGpuComputeDispatchInterface : public FFXSystemInterface
{
public:

	DECLARE_EVENT_OneParam(FVisMeshGpuComputeDispatchInterface, FOnPreInitViewsEvent, FRDGBuilder&);
	DECLARE_EVENT_OneParam(FVisMeshGpuComputeDispatchInterface, FOnPostPreRenderEvent, FRDGBuilder&);

	static ZERO_API FVisMeshGpuComputeDispatchInterface* Get(class UWorld* World);
	static ZERO_API FVisMeshGpuComputeDispatchInterface* Get(class FSceneInterface* Scene);
	static ZERO_API FVisMeshGpuComputeDispatchInterface* Get(class FFXSystemInterface* FXSceneInterface);

	ZERO_API explicit FVisMeshGpuComputeDispatchInterface(EShaderPlatform InShaderPlatform, ERHIFeatureLevel::Type InFeatureLevel);
	ZERO_API virtual ~FVisMeshGpuComputeDispatchInterface();

	/** Get ShaderPlatform the batcher is bound to */
	EShaderPlatform GetShaderPlatform() const { return ShaderPlatform; }
	/** Get FeatureLevel the batcher is bound to */
	ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }

	/** Add system instance proxy to the batcher for tracking. */
	virtual void AddGpuComputeProxy(FVisMeshSystemGpuComputeProxy* ComputeProxy) = 0;
	/** Remove system instance proxy from the batcher. */
	virtual void RemoveGpuComputeProxy(FVisMeshSystemGpuComputeProxy* ComputeProxy) = 0;

	/** Add NDC Data to the batcher for tracking */
	virtual void AddNDCDataProxy(FVisMeshDataChannelDataProxyPtr NDCDataProxy) = 0;
	/** Add NDC Data to the batcher for tracking */
	virtual void RemoveNDCDataProxy(FVisMeshDataChannelDataProxyPtr NDCDataProxy) = 0;

		/**
	 * Register work for GPU sorting (using the GPUSortManager).
	 * The constraints of the sort request are defined in SortInfo.SortFlags.
	 * The sort task bindings are set in SortInfo.AllocationInfo.
	 * The initial keys and values are generated in the GenerateSortKeys() callback.
	 *
	 * Return true if the work was registered, or false it GPU sorting is not available or impossible.
	 */
	virtual bool AddSortedGPUSimulation(FRHICommandListBase& RHICmdList, struct FVisMeshGPUSortInfo& SortInfo) = 0;

	UE_DEPRECATED(5.4, "AddSortedGPUSimulation requires an RHI command list")
	virtual bool AddSortedGPUSimulation(struct FVisMeshGPUSortInfo& SortInfo) final { return false; }

	/** Get or create the a data manager, must be done on the rendering thread only. */
	template<typename TManager>
	TManager& GetOrCreateDataManager()
	{
		check(IsInParallelRenderingThread());

		UE::TScopeLock ScopeLock(ComputeManagerGuard);
		const FName ManagerName = TManager::GetManagerName();
		for (auto& DataManager : GpuDataManagers)
		{
			if (DataManager.Key == ManagerName)
			{
				return *static_cast<TManager*>(DataManager.Value.Get());
			}
		}

		TManager* Manager = new TManager(this);
		GpuDataManagers.Emplace(ManagerName, Manager);
		return *Manager;
	}

	/**
	Get access to the Views the simulation is being rendered with.
	List is only valid during graph building (i.e. during ExecuteTicks) and for simulations in PostInitViews / PostRenderOpaque.
	*/
	TConstStridedView<FSceneView> GetSimulationSceneViews() const { return SimulationSceneViews; }

	/**
	* Get access to the global distance field data
	* This will return nullptr if you attempt to access at an invalid point (i.e. before GDF is prepared or the GDF is not available)
	*/
	virtual const FGlobalDistanceFieldParameterData* GetGlobalDistanceFieldData() const = 0;

	/** Get access to the instance count manager. */
	FORCEINLINE FVisMeshGPUInstanceCountManager& GetGPUInstanceCounterManager() { check(IsInParallelRenderingThread()); return GPUInstanceCounterManager; }
	FORCEINLINE const FVisMeshGPUInstanceCountManager& GetGPUInstanceCounterManager() const { check(IsInParallelRenderingThread()); return GPUInstanceCounterManager; }

#if VISMESH_COMPUTEDEBUG_ENABLED
	/** Public interface to VisMesh compute debugging. */
	ZERO_API FVisMeshGpuComputeDebugInterface GetGpuComputeDebugInterface() const;

	/** Get access to VisMesh's GpuComputeDebug this is for internal use */
	FVisMeshGpuComputeDebug* GetGpuComputeDebugPrivate() const { return GpuComputeDebugPtr.Get(); }
#endif

#if WITH_VISMESH_GPU_PROFILER
	/** Access to VisMesh's GPU Profiler */
	virtual class FVisMeshGPUProfilerInterface* GetGPUProfiler() const = 0;
#endif

	/** Get access to VisMesh's GpuReadbackManager. */
	// TODO:
	//FVisMeshGpuReadbackManager* GetGpuReadbackManager() const { return GpuReadbackManagerPtr.Get(); }

	/** Get access to VisMesh's GpuReadbackManager. */
	//UE_DEPRECATED(5.1, "The UAV Pool will be removed, please update your code to support RenderGraph.")
	FVisMeshEmptyUAVPool* GetEmptyUAVPool() const { return EmptyUAVPoolPtr.Get(); }

	/** Convenience wrapper to get a UAV from the pool. */
	//UE_DEPRECATED(5.1, "The UAV Pool will be removed, please update your code to support RenderGraph.")
	FRHIUnorderedAccessView* GetEmptyUAVFromPool(FRHICommandList& RHICmdList, EPixelFormat Format, EVisMeshEmptyUAVType Type) const { return EmptyUAVPoolPtr->GetEmptyUAVFromPool(RHICmdList, Format, Type); }

	/** Helper function to return an RDG Texture where the texture contains 0 for all channels. */
	ZERO_API FRDGTextureRef GetBlackTexture(FRDGBuilder& GraphBuilder, ETextureDimension TextureDimension) const;

	/** Helper function to return a RDG Texture SRV where the texture contains 0 for all channels. */
	ZERO_API FRDGTextureSRVRef GetBlackTextureSRV(FRDGBuilder& GraphBuilder, ETextureDimension TextureDimension) const;

	/** Helper function to return a RDG Texture UAV you don't care about the contents of or the results, i.e. to use as a dummy binding. */
	ZERO_API FRDGTextureUAVRef GetEmptyTextureUAV(FRDGBuilder& GraphBuilder, EPixelFormat Format, ETextureDimension TextureDimension) const;

	/** Helper function to return a Buffer UAV you don't care about the contents of or the results, i.e. to use as a dummy binding. */
	ZERO_API FRDGBufferUAVRef GetEmptyBufferUAV(FRDGBuilder& GraphBuilder, EPixelFormat Format) const;

	/** Helper function to return a Buffer SRV which will contain 1 element of 0 value, i.e. to use as a dummy binding. */
	ZERO_API FRDGBufferSRVRef GetEmptyBufferSRV(FRDGBuilder& GraphBuilder, EPixelFormat Format) const;

	/**
	Call this to force all pending ticks to be flushed from the batcher.
	Doing so will execute them outside of a view context which may result in undesirable results.
	*/
	virtual void FlushPendingTicks_GameThread() = 0;

	/**
	This will flush all pending ticks & readbacks from the dispatcher.
	Note: This is a GameThread blocking call and will impact performance
	*/
	virtual void FlushAndWait_GameThread() = 0;

	/** Debug only function to readback data. */
	// TODO:
	//virtual void AddDebugReadback(FVisMeshSystemInstanceID InstanceID, TSharedPtr<FVisMeshScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo, FVisMeshComputeExecutionContext* Context) = 0;

	/** Processes all pending debug readbacks */
	virtual void ProcessDebugReadbacks(FRHICommandList& RHICmdList, bool bWaitCompletion) = 0;

	virtual FVisMeshAsyncGpuTraceHelper& GetAsyncGpuTraceHelper() const = 0;

	FORCEINLINE bool IsOutsideSceneRenderer() const { return bIsOutsideSceneRenderer; }

	FORCEINLINE bool IsFirstViewFamily() const { return bIsFirstViewFamily; }
	FORCEINLINE bool IsLastViewFamily() const { return bIsLastViewFamily; }

	/**
	Event that broadcast when we enter PreInitViews.
	*/
	FOnPreInitViewsEvent& GetOnPreInitViewsEvent() { check(IsInRenderingThread()); return OnPreInitViewsEvent; }
	/**
	Event that broadcast when we endter PreRender.
	This is called before we prepare any work or add passes for simulating.
	*/
	FOnPostPreRenderEvent& GetOnPreRenderEvent() { check(IsInRenderingThread()); return OnPreRenderEvent; }
	/**
	Event that broadcast at the end of PostRenderOpaque.
	This is called after all simulation passes have been added.
	*/
	FOnPostPreRenderEvent& GetOnPostRenderEvent() { check(IsInRenderingThread()); return OnPostRenderEvent; }

protected:
	EShaderPlatform							ShaderPlatform;
	ERHIFeatureLevel::Type					FeatureLevel;
#if VISMESH_COMPUTEDEBUG_ENABLED
	TUniquePtr<FVisMeshGpuComputeDebug>		GpuComputeDebugPtr;
#endif
	// TODO:
	//TUniquePtr<FVisMeshGpuReadbackManager>	GpuReadbackManagerPtr;
	TUniquePtr<FVisMeshEmptyUAVPool>		EmptyUAVPoolPtr;

	// GPU emitter instance count buffer. Contains the actual particle / instance count generate in the GPU tick.
	FVisMeshGPUInstanceCountManager			GPUInstanceCounterManager;

	TArray<TPair<FName, TUniquePtr<FVisMeshGpuComputeDataManager>>> GpuDataManagers;

	TConstStridedView<FSceneView>			SimulationSceneViews;

	bool									bIsOutsideSceneRenderer = false;
	bool									bIsFirstViewFamily = true;
	bool									bIsLastViewFamily = true;

	FOnPreInitViewsEvent					OnPreInitViewsEvent;
	FOnPostPreRenderEvent					OnPreRenderEvent;
	FOnPostPreRenderEvent					OnPostRenderEvent;

	UE::FMutex								ComputeManagerGuard;
};
