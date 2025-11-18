#include "VisMeshGpuComputeDispatchInterface.h"

#include "SystemTextures.h"

FVisMeshGpuComputeDispatchInterface::FVisMeshGpuComputeDispatchInterface(EShaderPlatform InShaderPlatform, ERHIFeatureLevel::Type InFeatureLevel)
	: ShaderPlatform(InShaderPlatform)
	, FeatureLevel(InFeatureLevel)
	, GPUInstanceCounterManager(InFeatureLevel)
{
}

FVisMeshGpuComputeDispatchInterface::~FVisMeshGpuComputeDispatchInterface()
{
}

FVisMeshGpuComputeDispatchInterface* FVisMeshGpuComputeDispatchInterface::Get(UWorld* World)
{
	return World ? Get(World->Scene) : nullptr;
}

FVisMeshGpuComputeDispatchInterface* FVisMeshGpuComputeDispatchInterface::Get(FSceneInterface* Scene)
{
	return Scene ? Get(Scene->GetFXSystem()) : nullptr;
}

FVisMeshGpuComputeDispatchInterface* FVisMeshGpuComputeDispatchInterface::Get(FFXSystemInterface* FXSceneInterface)
{
	// TODO:
	return nullptr;
}

#if NIAGARA_COMPUTEDEBUG_ENABLED
FVisMeshGpuComputeDebugInterface FVisMeshGpuComputeDispatchInterface::GetGpuComputeDebugInterface() const
{
	return FVisMeshGpuComputeDebugInterface(GpuComputeDebugPtr.Get());
}
#endif

FRDGTextureRef FVisMeshGpuComputeDispatchInterface::GetBlackTexture(FRDGBuilder& GraphBuilder, ETextureDimension TextureDimension) const
{
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	switch (TextureDimension)
	{
		case ETextureDimension::Texture2D:			return SystemTextures.Black;
		case ETextureDimension::Texture2DArray:		return SystemTextures.BlackArray;
		case ETextureDimension::Texture3D:			return SystemTextures.VolumetricBlack;
		case ETextureDimension::TextureCube:		return SystemTextures.CubeBlack;
		case ETextureDimension::TextureCubeArray:	return SystemTextures.CubeArrayBlack;
		default: checkNoEntry(); return nullptr;
	}
}

FRDGTextureSRVRef FVisMeshGpuComputeDispatchInterface::GetBlackTextureSRV(FRDGBuilder& GraphBuilder, ETextureDimension TextureDimension) const
{
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	switch (TextureDimension)
	{
		case ETextureDimension::Texture2D:			return GraphBuilder.CreateSRV(SystemTextures.Black);
		case ETextureDimension::Texture2DArray:		return GraphBuilder.CreateSRV(SystemTextures.BlackArray);
		case ETextureDimension::Texture3D:			return GraphBuilder.CreateSRV(SystemTextures.VolumetricBlack);
		case ETextureDimension::TextureCube:		return GraphBuilder.CreateSRV(SystemTextures.CubeBlack);
		case ETextureDimension::TextureCubeArray:	return GraphBuilder.CreateSRV(SystemTextures.CubeArrayBlack);
		default: checkNoEntry(); return nullptr;
	}
}

FRDGTextureUAVRef FVisMeshGpuComputeDispatchInterface::GetEmptyTextureUAV(FRDGBuilder& GraphBuilder, EPixelFormat Format, ETextureDimension TextureDimension) const
{
	return EmptyUAVPoolPtr->GetEmptyRDGUAVFromPool(GraphBuilder, Format, TextureDimension);
}

FRDGBufferUAVRef FVisMeshGpuComputeDispatchInterface::GetEmptyBufferUAV(FRDGBuilder& GraphBuilder, EPixelFormat Format) const
{
	return EmptyUAVPoolPtr->GetEmptyRDGUAVFromPool(GraphBuilder, Format);
}

FRDGBufferSRVRef FVisMeshGpuComputeDispatchInterface::GetEmptyBufferSRV(FRDGBuilder& GraphBuilder, EPixelFormat Format) const
{
	return GraphBuilder.CreateSRV(GSystemTextures.GetDefaultBuffer(GraphBuilder, GPixelFormats[Format].BlockBytes, FUintVector4(0, 0, 0, 0)), Format);
}