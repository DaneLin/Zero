#include "VisMeshInstanceSceneProxy.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"

// 强制链接 LocalVertexFactory 相关符号
#include "LocalVertexFactory.h"

constexpr int32 InstancedVisMeshMaxTexCoord = 8;


IMPLEMENT_TYPE_LAYOUT(FInstancedVisMeshVertexFactoryShaderParameters);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FInstancedVisMeshVertexFactoryUniformShaderParameters, "VisInstanceVF");

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_Vertex, FInstancedVisMeshVertexFactoryShaderParameters);

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_Pixel, FInstancedVisMeshVertexFactoryShaderParameters);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_RayHitGroup, FInstancedVisMeshVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_Compute, FInstancedVisMeshVertexFactoryShaderParameters);
#endif


IMPLEMENT_VERTEX_FACTORY_TYPE(FInstancedVisMeshVertexFactory, "/ZeroPlugin/VisMeshLocalVertexFactory.ush",
                              EVertexFactoryFlags::UsedWithMaterials
                              | EVertexFactoryFlags::SupportsStaticLighting
                              | EVertexFactoryFlags::SupportsDynamicLighting
                              | EVertexFactoryFlags::SupportsPrecisePrevWorldPos
                              | EVertexFactoryFlags::SupportsCachingMeshDrawCommands
                              | EVertexFactoryFlags::SupportsRayTracing
                              | EVertexFactoryFlags::SupportsRayTracingDynamicGeometry
                              | EVertexFactoryFlags::SupportsLightmapBaking
                              | EVertexFactoryFlags::SupportsPrimitiveIdStream
                              | EVertexFactoryFlags::DoesNotSupportNullPixelShader
                              | EVertexFactoryFlags::SupportsManualVertexFetch
                              | EVertexFactoryFlags::SupportsPSOPrecaching
                              | EVertexFactoryFlags::SupportsLumenMeshCards
);

void FInstancedVisMeshVertexFactory::ModifyCompilationEnvironment(
	const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	if (RHISupportsManualVertexFetch(Parameters.Platform))
	{
		OutEnvironment.SetDefineIfUnset(TEXT("MANUAL_VERTEX_FETCH"), TEXT("1"));
	}


	if (UseGPUScene(Parameters.Platform))
	{
		// USE_INSTANCE_CULLING - set up additional instancing attributes (basic instancing is the default)
		OutEnvironment.SetDefine(TEXT("USE_INSTANCE_CULLING"), TEXT("1"));
	}
	else
	{
		OutEnvironment.SetDefine(TEXT("USE_INSTANCING"), TEXT("1"));
	}


	FLocalVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
}

void FInstancedVisMeshVertexFactory::GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType,
                                                                       FVertexDeclarationElementList& Elements)
{
	// Fallback to local vertex factory because manual vertex fetch is supported
	FLocalVertexFactory::GetPSOPrecacheVertexFetchElements(VertexInputStreamType, Elements);
}

void FInstancedVisMeshVertexFactory::GetVertexElements(ERHIFeatureLevel::Type FeatureLevel,
                                                       EVertexInputStreamType InputStreamType,
                                                       bool bSupportsManualVertexFetch, FDataType& Data,
                                                       FInstancedStaticMeshDataType& InstanceData,
                                                       FVertexDeclarationElementList& Elements)
{
	FVertexStreamList VertexStreams;
	GetVertexElements(FeatureLevel, InputStreamType, bSupportsManualVertexFetch, Data, InstanceData, Elements,
	                  VertexStreams);
}

void FInstancedVisMeshVertexFactory::Copy(const FInstancedVisMeshVertexFactory& Other)
{
	FInstancedVisMeshVertexFactory* VertexFactory = this;
	const FLocalVertexFactory::FDataType* DataCopy = &Other.Data;
	const FInstancedStaticMeshDataType* InstanceDataCopy = &Other.InstanceData;
	ENQUEUE_RENDER_COMMAND(FInstancedVisMeshVertexFactoryCopyData)(
		[VertexFactory, DataCopy, InstanceDataCopy](FRHICommandListBase&)
		{
			VertexFactory->Data = *DataCopy;
			VertexFactory->InstanceData = *InstanceDataCopy;
		});
	BeginUpdateResourceRHI(this);
}

void FInstancedVisMeshVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("FInstancedVisMeshVertexFactory::InitRHI");


	check(HasValidFeatureLevel());

	const ERHIFeatureLevel::Type ThisFeatureLevel = GetFeatureLevel();
	const bool bCanUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform, ThisFeatureLevel);
	const bool bUseManualVertexFetch = GetType()->SupportsManualVertexFetch(ThisFeatureLevel);

	FVertexDeclarationElementList Elements;
	GetVertexElements(ThisFeatureLevel, EVertexInputStreamType::Default, bUseManualVertexFetch, Data, InstanceData,
	                  Elements, Streams);

	// on mobile with GPUScene enabled instanced attributes[8-12] are used for a general auto-instancing
	// so we add them only for desktop or if mobile has GPUScene disabled
	// FIXME mobile: instanced attributes encode some editor related data as well (selection etc), need to split it into separate SRV as it's not supported with auto-instancing
	// FIXME: Need to capture PrimitiveId elements for PSO precaching
	uint8 AutoInstancingAttr_Mobile = 8;
	const bool bMobileUsesGPUScene = MobileSupportsGPUScene();
	if (ThisFeatureLevel > ERHIFeatureLevel::ES3_1 || !bMobileUsesGPUScene)
	{
		// Do not add general auto-instancing attributes for mobile
		AutoInstancingAttr_Mobile = 0xff;
	}

	AddPrimitiveIdStreamElement(EVertexInputStreamType::Default, Elements, 13, AutoInstancingAttr_Mobile);

	// we don't need per-vertex shadow or lightmap rendering
	InitDeclaration(Elements);

	if (!bCanUseGPUScene)
	{
		FInstancedVisMeshVertexFactoryUniformShaderParameters UniformParameters;
		UniformParameters.VertexFetch_InstanceOriginBuffer = GetInstanceOriginSRV();
		UniformParameters.VertexFetch_InstanceTransformBuffer = GetInstanceTransformSRV();
		UniformParameters.VertexFetch_InstanceLightmapBuffer = GetInstanceLightmapSRV();
		UniformParameters.InstanceCustomDataBuffer = GetInstanceCustomDataSRV();
		UniformParameters.NumCustomDataFloats = InstanceData.NumCustomDataFloats;
		UniformBuffer = TUniformBufferRef<FInstancedVisMeshVertexFactoryUniformShaderParameters>::CreateUniformBufferImmediate(UniformParameters, UniformBuffer_MultiFrame, EUniformBufferValidation::None);
	}
}

void FInstancedVisMeshVertexFactory::GetVertexElements(ERHIFeatureLevel::Type FeatureLevel,
                                                       EVertexInputStreamType InputStreamType,
                                                       bool bSupportsManualVertexFetch, FDataType& Data,
                                                       FInstancedStaticMeshDataType& InstanceData,
                                                       FVertexDeclarationElementList& Elements,
                                                       FVertexStreamList& Streams)
{
	if (Data.PositionComponent.VertexBuffer != NULL)
	{
		Elements.Add(AccessStreamComponent(Data.PositionComponent, 0, Streams));
	}

	if (!bSupportsManualVertexFetch)
	{
		// only tangent,normal are used by the stream. the binormal is derived in the shader
		uint8 TangentBasisAttributes[2] = {1, 2};
		for (int32 AxisIndex = 0; AxisIndex < 2; AxisIndex++)
		{
			if (Data.TangentBasisComponents[AxisIndex].VertexBuffer != NULL)
			{
				Elements.Add(AccessStreamComponent(Data.TangentBasisComponents[AxisIndex],
				                                   TangentBasisAttributes[AxisIndex], Streams));
			}
		}

		if (Data.ColorComponentsSRV == nullptr)
		{
			Data.ColorComponentsSRV = GNullColorVertexBuffer.VertexBufferSRV;
			Data.ColorIndexMask = 0;
		}

		if (Data.ColorComponent.VertexBuffer)
		{
			Elements.Add(AccessStreamComponent(Data.ColorComponent, 3, Streams));
		}
		else
		{
			//If the mesh has no color component, set the null color buffer on a new stream with a stride of 0.
			//This wastes 4 bytes of bandwidth per vertex, but prevents having to compile out twice the number of vertex factories.
			FVertexStreamComponent NullColorComponent(&GNullColorVertexBuffer, 0, 0, VET_Color,
			                                          EVertexStreamUsage::ManualFetch);
			Elements.Add(AccessStreamComponent(NullColorComponent, 3, Streams));
		}

		if (Data.TextureCoordinates.Num())
		{
			const int32 BaseTexCoordAttribute = 4;
			for (int32 CoordinateIndex = 0; CoordinateIndex < Data.TextureCoordinates.Num(); CoordinateIndex++)
			{
				Elements.Add(AccessStreamComponent(
					Data.TextureCoordinates[CoordinateIndex],
					BaseTexCoordAttribute + CoordinateIndex,
					Streams
				));
			}

			for (int32 CoordinateIndex = Data.TextureCoordinates.Num(); CoordinateIndex < (InstancedVisMeshMaxTexCoord +
				     1) / 2; CoordinateIndex++)
			{
				Elements.Add(AccessStreamComponent(
					Data.TextureCoordinates[Data.TextureCoordinates.Num() - 1],
					BaseTexCoordAttribute + CoordinateIndex,
					Streams
				));
			}
		}

		// PreSkinPosition attribute is only used for GPUSkinPassthrough variation of local vertex factory.
		// It is not used by ISM so fill with dummy buffer.
		if (FLocalVertexFactory::IsGPUSkinPassThroughSupported(GMaxRHIShaderPlatform))
		{
			FVertexStreamComponent NullComponent(&GNullVertexBuffer, 0, 0, VET_Float4);
			Elements.Add(AccessStreamComponent(NullComponent, 14, Streams));
		}

		if (Data.LightMapCoordinateComponent.VertexBuffer)
		{
			Elements.Add(AccessStreamComponent(Data.LightMapCoordinateComponent, 15, Streams));
		}
		else if (Data.TextureCoordinates.Num())
		{
			Elements.Add(AccessStreamComponent(Data.TextureCoordinates[0], 15, Streams));
		}
	}

	const bool bCanUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel);
	const bool bMobileUsesGPUScene = MobileSupportsGPUScene();

	if (FeatureLevel > ERHIFeatureLevel::ES3_1 || !bMobileUsesGPUScene)
	{
		// toss in the instanced location stream
		check(bCanUseGPUScene || InstanceData.InstanceOriginComponent.VertexBuffer);
		if (InstanceData.InstanceOriginComponent.VertexBuffer)
		{
			Elements.Add(AccessStreamComponent(InstanceData.InstanceOriginComponent, 8, Streams));
		}

		check(bCanUseGPUScene || InstanceData.InstanceTransformComponent[0].VertexBuffer);
		if (InstanceData.InstanceTransformComponent[0].VertexBuffer)
		{
			Elements.Add(AccessStreamComponent(InstanceData.InstanceTransformComponent[0], 9, Streams));
			Elements.Add(AccessStreamComponent(InstanceData.InstanceTransformComponent[1], 10, Streams));
			Elements.Add(AccessStreamComponent(InstanceData.InstanceTransformComponent[2], 11, Streams));
		}

		if (InstanceData.InstanceLightmapAndShadowMapUVBiasComponent.VertexBuffer)
		{
			Elements.Add(AccessStreamComponent(InstanceData.InstanceLightmapAndShadowMapUVBiasComponent, 12, Streams));
		}
	}
}

void FInstancedVisMeshVertexFactoryShaderParameters::GetElementShaderBindings(
	const class FSceneInterface* Scene,
	const FSceneView* View,
	const FMeshMaterialShader* Shader,
	const EVertexInputStreamType InputStreamType,
	ERHIFeatureLevel::Type FeatureLevel,
	const FVertexFactory* VertexFactory,
	const FMeshBatchElement& BatchElement,
	FMeshDrawSingleShaderBindings& ShaderBindings,
	FVertexInputStreamArray& VertexStreams
) const
{
	// 1. 绑定LocalVertexFactory的UniformBuffer（如果有的话）
	FRHIUniformBuffer* VertexFactoryUniformBuffer = static_cast<FRHIUniformBuffer*>(BatchElement.VertexFactoryUserData);
	if (VertexFactoryUniformBuffer)
	{
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLocalVertexFactoryUniformShaderParameters>(), VertexFactoryUniformBuffer);
	}

	// 2. 处理Instance相关的绑定
	const FInstancingUserData* InstancingUserData = (const FInstancingUserData*)BatchElement.UserData;
	const auto* InstancedVertexFactory = static_cast<const FInstancedVisMeshVertexFactory*>(VertexFactory);
	const int32 InstanceOffsetValue = BatchElement.UserIndex;

	ShaderBindings.Add(InstanceOffset, InstanceOffsetValue);
	
	// 3. 在非GPU Scene模式下绑定实例数据
	if (!UseGPUScene(Scene ? Scene->GetShaderPlatform() : GMaxRHIShaderPlatform))
	{
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FInstancedVisMeshVertexFactoryUniformShaderParameters>(),
		                   InstancedVertexFactory->GetUniformBuffer());
		if (InstancedVertexFactory->SupportsManualVertexFetch(FeatureLevel))
		{
			ShaderBindings.Add(VertexFetch_InstanceOriginBufferParameter,
			                   InstancedVertexFactory->GetInstanceOriginSRV());
			ShaderBindings.Add(VertexFetch_InstanceTransformBufferParameter,
			                   InstancedVertexFactory->GetInstanceTransformSRV());
			ShaderBindings.Add(VertexFetch_InstanceLightmapBufferParameter,
			                   InstancedVertexFactory->GetInstanceLightmapSRV());
		}
		if (InstanceOffsetValue > 0 && VertexStreams.Num() > 0)
		{
			VertexFactory->OffsetInstanceStreams(InstanceOffsetValue, InputStreamType, VertexStreams);
		}
	}

	// 4. 设置InstancingOffset
	FVector4f InstancingOffset(ForceInit);
	// InstancedLODRange is only set for HierarchicalInstancedStaticMeshes
	if (InstancingUserData && BatchElement.InstancedLODRange)
	{
		InstancingOffset = (FVector3f)InstancingUserData->InstancingOffset;
	}
	ShaderBindings.Add(InstancingOffsetParameter, InstancingOffset);
}

bool FInstancedVisMeshVertexFactory::ShouldCompilePermutation(
	const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (Parameters.MaterialParameters.bIsUsedWithInstancedStaticMeshes || Parameters.MaterialParameters.
			bIsSpecialEngineMaterial)
		&& FLocalVertexFactory::ShouldCompilePermutation(Parameters);
}