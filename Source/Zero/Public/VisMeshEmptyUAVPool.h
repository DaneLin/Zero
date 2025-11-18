#pragma once

#include "RHI.h"
#include "RenderGraphFwd.h"

// Scoped access for UAVs this is not required when running inside the main dispatch loop
// but is required for external usage that is outside, i.e. if you are doing some custom dispatch setup
struct FVisMeshEmptyUAVPoolScopedAccess
{
	UE_NONCOPYABLE(FVisMeshEmptyUAVPoolScopedAccess)
public:
	explicit FVisMeshEmptyUAVPoolScopedAccess(class FVisMeshEmptyUAVPool* EmptyUAVPool);
	~FVisMeshEmptyUAVPoolScopedAccess();

private:
	class FVisMeshEmptyUAVPool* EmptyUAVPool;
};

// Scoped access for RDG UAVs, this is not required when running inside the main dispatch loop
// but is required for external usage that is outside, i.e. if you are doing some custom dispatch setup
struct FVisMeshEmptyRDGUAVPoolScopedAccess
{
	UE_NONCOPYABLE(FVisMeshEmptyRDGUAVPoolScopedAccess)
public:
	explicit FVisMeshEmptyRDGUAVPoolScopedAccess(class FVisMeshEmptyUAVPool* EmptyUAVPool);
	~FVisMeshEmptyRDGUAVPoolScopedAccess();
private:
	class FVisMeshEmptyUAVPool* EmptyUAVPool;
};

// Type of empty UAV to get
enum class EVisMeshEmptyUAVType
{
	Buffer,
	Texture2D,
	Texture2DArray,
	Texture3D,
	TextureCube,
	TextureCubeArray,
	Num
};

// Empty UAV pool used for ensuring we bind a buffer when one does not exist
class FVisMeshEmptyUAVPool
{
	friend struct FVisMeshEmptyUAVPoolScopedAccess;
	friend struct FVisMeshEmptyRDGUAVPoolScopedAccess;

public:
	/** Must be called before we start to use the UAV pool for a given scene render. */
	void Tick();

	/**
	* Grab a temporary empty RW buffer from the pool.
	* Note: When doing this outside of VisMesh you must be within a FVisMeshUAVPoolAccessScope.
	*/
	ZERO_API FRHIUnorderedAccessView* GetEmptyUAVFromPool(FRHICommandList& RHICmdList, EPixelFormat Format, EVisMeshEmptyUAVType Type);

	/**
	* Grab a temporary empty RDG Buffer UAV from the pool.
	* Note: When doing this outside of VisMesh you must be within a FVisMeshUAVPoolAccessScope.
	*/
	ZERO_API FRDGBufferUAVRef GetEmptyRDGUAVFromPool(FRDGBuilder& GraphBuilder, EPixelFormat Format);
	/**
	* Grab a temporary empty RDG Texture UAV from the pool.
	* Note: When doing this outside of VisMesh you must be within a FVisMeshUAVPoolAccessScope.
	*/
	ZERO_API FRDGTextureUAVRef GetEmptyRDGUAVFromPool(FRDGBuilder& GraphBuilder, EPixelFormat Format, ETextureDimension TextureDimension);

protected:
	/** Returns all used UAVs back to the pool. */
	void ResetEmptyUAVPools();

	/** Returns all the RDG UAVs back to the pool. */
	void ResetEmptyRDGUAVPools();

protected:
	struct FEmptyUAV
	{
		~FEmptyUAV();

		FBufferRHIRef Buffer;
		FTextureRHIRef Texture;
		FUnorderedAccessViewRHIRef UAV;
	};

	struct FEmptyUAVPool
	{
		~FEmptyUAVPool();

		int32 NextFreeIndex = 0;
		TArray<FEmptyUAV> UAVs;
	};

	uint32 UAVAccessCounter = 0;
	TMap<EPixelFormat, FEmptyUAVPool> UAVPools[(int)EVisMeshEmptyUAVType::Num];

	struct FBufferRDGUAVPool
	{
		int32 NextFreeIndex = 0;
		TArray<FRDGBufferUAVRef> UAVs;
	};
	struct FTextureRDGUAVPool
	{
		int32 NextFreeIndex = 0;
		TArray<FRDGTextureUAVRef> UAVs;
	};

	uint32 RDGUAVAccessCounter = 0;
	TMap<EPixelFormat, FBufferRDGUAVPool> BufferRDGUAVPool;
	TMap<TPair<EPixelFormat, ETextureDimension>, FTextureRDGUAVPool> TextureRDGUAVPool;
};