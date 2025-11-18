#include "VisMeshEmptyUAVPool.h"

#include "RenderGraphBuilder.h"


FVisMeshEmptyUAVPoolScopedAccess::FVisMeshEmptyUAVPoolScopedAccess(class FVisMeshEmptyUAVPool* EmptyUAVPool)
	: EmptyUAVPool(EmptyUAVPool)
{
	check(IsInRenderingThread());
	check(EmptyUAVPool);
	++EmptyUAVPool->UAVAccessCounter;
}

FVisMeshEmptyUAVPoolScopedAccess::~FVisMeshEmptyUAVPoolScopedAccess()
{
	check(IsInRenderingThread());
	--EmptyUAVPool->UAVAccessCounter;
	if (EmptyUAVPool->UAVAccessCounter == 0)
	{
		EmptyUAVPool->ResetEmptyUAVPools();
	}
}

FVisMeshEmptyRDGUAVPoolScopedAccess::FVisMeshEmptyRDGUAVPoolScopedAccess(class FVisMeshEmptyUAVPool* EmptyUAVPool)
	: EmptyUAVPool(EmptyUAVPool)
{
	check(IsInRenderingThread());
	check(EmptyUAVPool);
	++EmptyUAVPool->UAVAccessCounter;
}

FVisMeshEmptyRDGUAVPoolScopedAccess::~FVisMeshEmptyRDGUAVPoolScopedAccess()
{
	check(IsInRenderingThread());
	--EmptyUAVPool->RDGUAVAccessCounter;
	if (EmptyUAVPool->RDGUAVAccessCounter == 0)
	{
		EmptyUAVPool->ResetEmptyRDGUAVPools();
	}
}

void FVisMeshEmptyUAVPool::Tick()
{
	check(IsInRenderingThread());
	BufferRDGUAVPool.Empty();
	TextureRDGUAVPool.Empty();
}

FRHIUnorderedAccessView* FVisMeshEmptyUAVPool::GetEmptyUAVFromPool(FRHICommandList& RHICmdList, EPixelFormat Format, EVisMeshEmptyUAVType Type)
{
	check(IsInRenderingThread());
	checkf(UAVAccessCounter != 0, TEXT("Accessing VisMesh's UAV Pool while not within a scope, this could result in a memory leak!"));

	TMap<EPixelFormat, FEmptyUAVPool>& UAVMap = UAVPools[(int)Type];
	FEmptyUAVPool& Pool = UAVMap.FindOrAdd(Format);
	checkSlow(Pool.NextFreeIndex <= Pool.UAVs.Num());
	if (Pool.NextFreeIndex == Pool.UAVs.Num())
	{
		FEmptyUAV& NewUAV = Pool.UAVs.AddDefaulted_GetRef();

		// Initialize the UAV
		const TCHAR* ResourceName = TEXT("FVisMeshGpuComputeDispatch::EmptyUAV");
		switch ( Type )
		{
			case EVisMeshEmptyUAVType::Buffer:
			{
				const FRHIBufferCreateDesc CreateDesc =
					FRHIBufferCreateDesc::CreateVertex(ResourceName, GPixelFormats[Format].BlockBytes)
					.AddUsage(EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::ShaderResource)
					.DetermineInitialState();

				NewUAV.Buffer = RHICmdList.CreateBuffer(CreateDesc);
				NewUAV.UAV = RHICmdList.CreateUnorderedAccessView(
					NewUAV.Buffer, 
					FRHIViewDesc::CreateBufferUAV()
						.SetType(FRHIViewDesc::EBufferType::Typed)
						.SetFormat(Format));
				break;
			}
			
			case EVisMeshEmptyUAVType::Texture2D:
			{
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::Create2D(ResourceName, 1, 1, Format)
					.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);

				NewUAV.Texture = RHICreateTexture(Desc);
				NewUAV.UAV = RHICmdList.CreateUnorderedAccessView(NewUAV.Texture, FRHIViewDesc::CreateTextureUAV().SetDimensionFromTexture(NewUAV.Texture));
				break;
			}
			
			case EVisMeshEmptyUAVType::Texture2DArray:
			{
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::Create2DArray(ResourceName, 1, 1, 1, Format)
					.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);

				NewUAV.Texture = RHICreateTexture(Desc);
				NewUAV.UAV = RHICmdList.CreateUnorderedAccessView(NewUAV.Texture, FRHIViewDesc::CreateTextureUAV().SetDimensionFromTexture(NewUAV.Texture));
				break;
			}
			
			case EVisMeshEmptyUAVType::Texture3D:
			{
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::Create3D(ResourceName, 1, 1, 1, Format)
					.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);

				NewUAV.Texture = RHICreateTexture(Desc);
				NewUAV.UAV = RHICmdList.CreateUnorderedAccessView(NewUAV.Texture, FRHIViewDesc::CreateTextureUAV().SetDimensionFromTexture(NewUAV.Texture));
				break;
			}
			
			case EVisMeshEmptyUAVType::TextureCube:
			{
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::CreateCube(ResourceName, 1, Format)
					.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);

				NewUAV.Texture = RHICreateTexture(Desc);
				NewUAV.UAV = RHICmdList.CreateUnorderedAccessView(NewUAV.Texture, FRHIViewDesc::CreateTextureUAV().SetDimensionFromTexture(NewUAV.Texture));
				break;
			}


			case EVisMeshEmptyUAVType::TextureCubeArray:
			{
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::CreateCubeArray(ResourceName, 1, 1, Format)
					.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);

				NewUAV.Texture = RHICreateTexture(Desc);
				NewUAV.UAV = RHICmdList.CreateUnorderedAccessView(NewUAV.Texture, FRHIViewDesc::CreateTextureUAV().SetDimensionFromTexture(NewUAV.Texture));
				break;
			}

			default:
			{
				checkNoEntry();
				return nullptr;
			}
		}

		RHICmdList.Transition(FRHITransitionInfo(NewUAV.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));

		// Dispatches which use Empty UAVs are allowed to overlap, since we don't care about the contents of these buffers.
		// We never need to calll EndUAVOverlap() on these.
		RHICmdList.BeginUAVOverlap(NewUAV.UAV);

	}

	FRHIUnorderedAccessView* UAV = Pool.UAVs[Pool.NextFreeIndex].UAV;
	++Pool.NextFreeIndex;
	return UAV;
}


void FVisMeshEmptyUAVPool::ResetEmptyUAVPools()
{
	for (int Type = 0; Type < UE_ARRAY_COUNT(UAVPools); ++Type)
	{
		for (TPair<EPixelFormat, FEmptyUAVPool>& Entry : UAVPools[Type])
		{
			Entry.Value.NextFreeIndex = 0;
		}
	}
}

FRDGBufferUAVRef FVisMeshEmptyUAVPool::GetEmptyRDGUAVFromPool(FRDGBuilder& GraphBuilder, EPixelFormat Format)
{
	check(IsInRenderingThread());
	checkf(RDGUAVAccessCounter != 0, TEXT("Accessing VisMesh's RDG UAV Pool while not within a scope, this could result in a memory leak!"));
	check(UE::PixelFormat::HasCapabilities(Format, EPixelFormatCapabilities::TypedUAVStore));

	FBufferRDGUAVPool& Pool = BufferRDGUAVPool.FindOrAdd(Format);
	if (Pool.NextFreeIndex == Pool.UAVs.Num())
	{
		const uint32 BytesPerElement = GPixelFormats[Format].BlockBytes;
		Pool.UAVs.Add(
			GraphBuilder.CreateUAV(
				GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(BytesPerElement, 1),
					TEXT("EVisMeshEmptyUAVType::Buffer")
				),
				Format,
				ERDGUnorderedAccessViewFlags::SkipBarrier
			)
		);
	}
	return Pool.UAVs[Pool.NextFreeIndex++];
}

FRDGTextureUAVRef FVisMeshEmptyUAVPool::GetEmptyRDGUAVFromPool(FRDGBuilder& GraphBuilder, EPixelFormat Format, ETextureDimension TextureDimension)
{
	check(IsInRenderingThread());
	checkf(RDGUAVAccessCounter != 0, TEXT("Accessing VisMesh's RDG UAV Pool while not within a scope, this could result in a memory leak!"));
	check(UE::PixelFormat::HasCapabilities(Format, EPixelFormatCapabilities::TypedUAVStore));

	FTextureRDGUAVPool& Pool = TextureRDGUAVPool.FindOrAdd(TPairInitializer(Format, TextureDimension));
	if (Pool.NextFreeIndex == Pool.UAVs.Num())
	{
		switch (TextureDimension)
		{
			case ETextureDimension::Texture2D:
				Pool.UAVs.Add(
					GraphBuilder.CreateUAV(
						GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create2D(FIntPoint(1, 1), Format, FClearValueBinding::Black, ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV),
							TEXT("VisMeshEmptyTextureUAV::Texture2D")
						),
						ERDGUnorderedAccessViewFlags::SkipBarrier
					)
				);
				break;

			case ETextureDimension::Texture2DArray:
				Pool.UAVs.Add(
					GraphBuilder.CreateUAV(
						GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create2DArray(FIntPoint(1, 1), Format, FClearValueBinding::Black, ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV, 1),
							TEXT("VisMeshEmptyTextureUAV::Texture2DArray")
						),
						ERDGUnorderedAccessViewFlags::SkipBarrier
					)
				);
				break;

			case ETextureDimension::Texture3D:
				Pool.UAVs.Add(
					GraphBuilder.CreateUAV(
						GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create3D(FIntVector(1, 1, 1), Format, FClearValueBinding::Black, ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV),
							TEXT("VisMeshEmptyTextureUAV::Texture3D")
						),
						ERDGUnorderedAccessViewFlags::SkipBarrier
					)
				);
				break;

			case ETextureDimension::TextureCube:
				Pool.UAVs.Add(
					GraphBuilder.CreateUAV(
						GraphBuilder.CreateTexture(
							FRDGTextureDesc::CreateCube(1, Format, FClearValueBinding::Black, ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV),
							TEXT("VisMeshEmptyTextureUAV::TextureCube")
						),
						ERDGUnorderedAccessViewFlags::SkipBarrier
					)
				);
				break;

			case ETextureDimension::TextureCubeArray:
				Pool.UAVs.Add(
					GraphBuilder.CreateUAV(
						GraphBuilder.CreateTexture(
							FRDGTextureDesc::CreateCubeArray(1, Format, FClearValueBinding::Black, ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV, 1),
							TEXT("VisMeshEmptyTextureUAV::TextureCubeArray")
						),
						ERDGUnorderedAccessViewFlags::SkipBarrier
					)
				);
				break;

			default:
				checkNoEntry();
				return nullptr;
		}
	}
	return Pool.UAVs[Pool.NextFreeIndex++];
}

void FVisMeshEmptyUAVPool::ResetEmptyRDGUAVPools()
{
	for ( auto it=BufferRDGUAVPool.CreateIterator(); it; ++it )
	{
		it.Value().NextFreeIndex = 0;
	}

	for ( auto it=TextureRDGUAVPool.CreateIterator(); it; ++it )
	{
		it.Value().NextFreeIndex = 0;
	}
}

FVisMeshEmptyUAVPool::FEmptyUAV::~FEmptyUAV()
{
	Buffer.SafeRelease();
	Texture.SafeRelease();
	UAV.SafeRelease();
}


FVisMeshEmptyUAVPool::FEmptyUAVPool::~FEmptyUAVPool()
{
}




