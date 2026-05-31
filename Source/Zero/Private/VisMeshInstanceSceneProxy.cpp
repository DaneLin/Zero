#include "VisMeshInstanceSceneProxy.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"

// 强制链接 LocalVertexFactory 相关符号
#include "DynamicMeshBuilder.h"
#include "LocalVertexFactory.h"
#include "MaterialDomain.h"
#include "PrimitiveSceneInfo.h"
#include "VisMeshComponent.h"
#include "Materials/MaterialRenderProxy.h"
#include "PhysicsEngine/BodySetup.h"

constexpr int32 InstancedVisMeshMaxTexCoord = 8;


IMPLEMENT_TYPE_LAYOUT(FInstancedVisMeshVertexFactoryShaderParameters);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FInstancedVisMeshVertexFactoryUniformShaderParameters, "VisInstanceVF");

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_Vertex,
                                        FInstancedVisMeshVertexFactoryShaderParameters);

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_Pixel,
                                        FInstancedVisMeshVertexFactoryShaderParameters);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_RayHitGroup,
                                        FInstancedVisMeshVertexFactoryShaderParameters);

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_Compute,
                                        FInstancedVisMeshVertexFactoryShaderParameters);
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


class FVisMeshDummyFloatBuffer : public FVertexBufferWithSRV
{
public:
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		/// Create the texture RHI.  		
		FRHIResourceCreateInfo CreateInfo(TEXT("DummyFloatBuffer"));

		const int32 NumFloats = 4;
		VertexBufferRHI = RHICmdList.CreateVertexBuffer(sizeof(float) * NumFloats, BUF_Static | BUF_ShaderResource,
														CreateInfo);

		float* BufferData = (float*)RHICmdList.LockBuffer(VertexBufferRHI, 0, sizeof(float) * NumFloats, RLM_WriteOnly);
		FMemory::Memzero(BufferData, sizeof(float) * NumFloats);
		RHICmdList.UnlockBuffer(VertexBufferRHI);

		// Create a view of the buffer
		ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(VertexBufferRHI, sizeof(float), PF_R32_FLOAT);
	}
};

TGlobalResource<FVisMeshDummyFloatBuffer> GVisMeshDummyFloatBuffer;

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


		FInstancedVisMeshVertexFactoryUniformShaderParameters UniformParameters;
		UniformParameters.VertexFetch_InstanceOriginBuffer = GetInstanceOriginSRV();
		UniformParameters.VertexFetch_InstanceTransformBuffer = GetInstanceTransformSRV();
		UniformParameters.VertexFetch_InstanceLightmapBuffer = GetInstanceLightmapSRV();
		UniformParameters.InstanceCustomDataBuffer = GetInstanceCustomDataSRV();
		UniformParameters.NumCustomDataFloats = InstanceData.NumCustomDataFloats;
		UniformBuffer = TUniformBufferRef<
			FInstancedVisMeshVertexFactoryUniformShaderParameters>::CreateUniformBufferImmediate(
			UniformParameters, UniformBuffer_MultiFrame, EUniformBufferValidation::None);
	
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
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLocalVertexFactoryUniformShaderParameters>(),
		                   VertexFactoryUniformBuffer);
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

void FVisMeshInstanceBuffer::CreateVertexBuffer(FRHICommandListBase& RHICmdList,
	FResourceArrayInterface* InResourceArray, EBufferUsageFlags InUsage, uint32 InStride, uint8 InFormat,
	FBufferRHIRef& OutVertexBufferRHI, FShaderResourceViewRHIRef& OutInstanceSRV)
{
	check(InResourceArray);
	check(InResourceArray->GetResourceDataSize() > 0);

	// TODO: possibility over allocated the vertex buffer when we support partial update for when working in the editor
	FRHIResourceCreateInfo CreateInfo(TEXT("FVisMeshInstanceBuffer"), InResourceArray);
	OutVertexBufferRHI = RHICmdList.CreateVertexBuffer(InResourceArray->GetResourceDataSize(), InUsage, CreateInfo);
	
	if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
	{
		OutInstanceSRV = RHICmdList.CreateShaderResourceView(OutVertexBufferRHI, InStride, InFormat);
	}
}

void FVisMeshInstanceBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	{
		check(InstanceData);
		if (InstanceData->GetNumInstances() > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FStaticMeshInstanceBuffer_InitRHI);

			LLM_SCOPE(ELLMTag::InstancedMesh);
			auto AccessFlags = BUF_Static;
			CreateVertexBuffer(RHICmdList, InstanceData->GetOriginResourceArray(), AccessFlags | BUF_ShaderResource, 16, PF_A32B32G32R32F, InstanceOriginBuffer.VertexBufferRHI, InstanceOriginSRV);
			CreateVertexBuffer(RHICmdList, InstanceData->GetTransformResourceArray(), AccessFlags | BUF_ShaderResource, InstanceData->GetTranslationUsesHalfs() ? 8 : 16, InstanceData->GetTranslationUsesHalfs() ? PF_FloatRGBA : PF_A32B32G32R32F, InstanceTransformBuffer.VertexBufferRHI, InstanceTransformSRV);
			CreateVertexBuffer(RHICmdList, InstanceData->GetLightMapResourceArray(), AccessFlags | BUF_ShaderResource, 8, PF_R16G16B16A16_SNORM, InstanceLightmapBuffer.VertexBufferRHI, InstanceLightmapSRV);
			if (InstanceData->GetNumCustomDataFloats() > 0)
			{
				CreateVertexBuffer(RHICmdList, InstanceData->GetCustomDataResourceArray(), AccessFlags | BUF_ShaderResource, 4, PF_R32_FLOAT, InstanceCustomDataBuffer.VertexBufferRHI, InstanceCustomDataSRV);
				// Make sure we still create custom data SRV on platforms that do not support/use MVF 
				if (InstanceCustomDataSRV == nullptr)
				{
					InstanceCustomDataSRV = RHICmdList.CreateShaderResourceView(InstanceCustomDataBuffer.VertexBufferRHI, 4, PF_R32_FLOAT);
				}
			}
			else
			{
				InstanceCustomDataSRV = GVisMeshDummyFloatBuffer.ShaderResourceViewRHI;
			}
		}
	}
}

static void ConvertMeshToDynMeshVertex(FDynamicMeshVertex& Vert, const FVisMeshVertex& ProcVert)
{
	Vert.Position = (FVector3f)ProcVert.Position;
	Vert.Color = ProcVert.Color;
	Vert.TextureCoordinate[0] = FVector2f(ProcVert.UV0); // LWC_TODO: Precision loss
	Vert.TextureCoordinate[1] = FVector2f(ProcVert.UV1); // LWC_TODO: Precision loss
	Vert.TextureCoordinate[2] = FVector2f(ProcVert.UV2); // LWC_TODO: Precision loss
	Vert.TextureCoordinate[3] = FVector2f(ProcVert.UV3); // LWC_TODO: Precision loss
	Vert.TangentX = ProcVert.Tangent.TangentX;
	Vert.TangentZ = ProcVert.Normal;
	Vert.TangentZ.Vector.W = ProcVert.Tangent.bFlipTangentY ? -127 : 127;
}

FVisMeshInstancedSceneProxy::FVisMeshInstancedSceneProxy(UVisMeshComponent* Component)

	: FPrimitiveSceneProxy(Component)
	  , InstanceNum(Component->InstanceNum)
	  , BodySetup(Component->GetBodySetup())
	  , MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
{
	// Static copy each section
	const int32 NumSections = Component->VisMeshSections.Num();
	
	Sections.AddZeroed(NumSections);
	for (int SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
	{
		FVisMeshSection& SrcSection = Component->VisMeshSections[SectionIdx];
		if (SrcSection.ProcIndexBuffer.Num() > 0 && SrcSection.ProcVertexBuffer.Num() > 0)
		{
			FVisMeshInstancedProxySection* NewSection = new FVisMeshInstancedProxySection(
				GetScene().GetFeatureLevel());

			// Copy data from vertex buffer
			const int32 NumVerts = SrcSection.ProcVertexBuffer.Num();

			// Allocate verts
			TArray<FDynamicMeshVertex> Vertices;
			Vertices.SetNumUninitialized(NumVerts);
			// Copy verts
			for (int VertIdx = 0; VertIdx < NumVerts; VertIdx++)
			{
				const FVisMeshVertex& ProcVert = SrcSection.ProcVertexBuffer[VertIdx];
				FDynamicMeshVertex& Vert = Vertices[VertIdx];
				ConvertMeshToDynMeshVertex(Vert, ProcVert);
			}

			// Copy index buffer
			NewSection->IndexBuffer.Indices = SrcSection.ProcIndexBuffer;

			NewSection->VertexBuffers.InitFromDynamicVertex(&NewSection->VertexFactory, Vertices, 4);

			// Enqueue initialization of render resource
			BeginInitResource(&NewSection->VertexBuffers.PositionVertexBuffer);
			BeginInitResource(&NewSection->VertexBuffers.StaticMeshVertexBuffer);
			BeginInitResource(&NewSection->VertexBuffers.ColorVertexBuffer);
			BeginInitResource(&NewSection->IndexBuffer);
			BeginInitResource(&NewSection->VertexFactory);

			//NewSection->InstanceBuffer.BindInstanceVertexBuffer(&NewSection->VertexFactory,InstanceData);

			// Grab material
			NewSection->Material = Component->GetMaterial(SectionIdx);
			if (NewSection->Material == nullptr)
			{
				NewSection->Material = UMaterial::GetDefaultMaterial(MD_Surface);
			}

			// Copy visibility info
			NewSection->bSectionVisible = SrcSection.bSectionVisible;

			// Save ref to new section
			Sections[SectionIdx] = NewSection;
		}
	}

	const int32 NumInstances = Component->InstanceNum;
	if (NumInstances > 0)
	{
		bSupportsInstanceDataBuffer = true;
		// 调整基类中 protected 数组的大小
		InstanceSceneData.SetNumUninitialized(NumInstances);

		for (int32 InstanceIndex =0 ; InstanceIndex < NumInstances; ++InstanceIndex)
		{
			if (!ensure(InstanceSceneData.IsValidIndex(InstanceIndex)))
			{
				continue;
			}

			FInstanceSceneData& SceneData = InstanceSceneData[InstanceIndex];

			FTransform InstanceTransform = FTransform::Identity;
			InstanceTransform.AddToTranslation(FVector(-1,-1,-1));
			
			SceneData.LocalToPrimitive = InstanceTransform.ToMatrixWithScale();
		}
		// InstanceLocalBounds.SetNumUninitialized(InstanceNum);
		//
		// for (int32 i = 0; i < InstanceNum; ++i)
		// {
		// 	// 获取变换 (这里演示用 Identity，实际请从 Component 获取)
		// 	// 假设 Component 有 GetInstanceTransform(i)
		// 	FMatrix InstanceTransform = FMatrix::Identity;
		// 	InstanceTransform.SetOrigin(FVector(0, i * 150.0, 0)); // 示例排列
		//
		// 	// 填充数据结构
		// 	InstanceSceneData[i].SetTransform(InstanceTransform);
		// 	InstanceSceneData[i].InstanceId = i;
  //           
		// 	// PrimitiveId 很重要，用于 Shader 反查
		// 	InstanceSceneData[i].PrimitiveId = Component->GetPrimitiveSceneId(); 
  //           
		// 	// 必须设置包围盒，否则会被剔除导致不显示
		// 	InstanceLocalBounds[i] = FRenderBounds(FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f));
		// }
	}

	EnableGPUSceneSupportFlags();

}

FVisMeshInstancedSceneProxy::~FVisMeshInstancedSceneProxy()
{
	
		for (FVisMeshInstancedProxySection* Section : Sections)
		{
			if (Section != nullptr)
			{
				Section->VertexBuffers.PositionVertexBuffer.ReleaseResource();
				Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
				Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
				Section->IndexBuffer.ReleaseResource();
				Section->VertexFactory.ReleaseResource();

				delete Section;
			}
		}
	
}

void FVisMeshInstancedSceneProxy::UpdateSection_RenderThread(FRHICommandListBase& RHICmdList,
	class FVisMeshSectionUpdateData* SectionData)
{

		// Check if we have data
		if (SectionData != nullptr)
		{
			// Check it references a valid section
			if (SectionData->TargetSection < Sections.Num() &&
				Sections[SectionData->TargetSection] != nullptr)
			{
				FVisMeshInstancedProxySection* Section = Sections[SectionData->TargetSection];

				// Lock vertex buffer
				const int32 NumVerts = SectionData->NewVertexBuffer.Num();

				// Iterate through vertex data, copying in new info
				for (int32 i = 0; i < NumVerts; i++)
				{
					const FVisMeshVertex& ProcVert = SectionData->NewVertexBuffer[i];
					FDynamicMeshVertex Vertex;
					ConvertMeshToDynMeshVertex(Vertex, ProcVert);

					Section->VertexBuffers.PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
					Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(
						i, Vertex.TangentX.ToFVector3f(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector3f());
					Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 0, Vertex.TextureCoordinate[0]);
					Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 1, Vertex.TextureCoordinate[1]);
					Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 2, Vertex.TextureCoordinate[2]);
					Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 3, Vertex.TextureCoordinate[3]);
					Section->VertexBuffers.ColorVertexBuffer.VertexColor(i) = Vertex.Color;
				}

				{
					auto& VertexBuffer = Section->VertexBuffers.PositionVertexBuffer;
					void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.VertexBufferRHI, 0,
					                                               VertexBuffer.GetNumVertices() * VertexBuffer.
					                                               GetStride(), RLM_WriteOnly);
					FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(),
					                VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
					RHICmdList.UnlockBuffer(VertexBuffer.VertexBufferRHI);
				}

				{
					auto& VertexBuffer = Section->VertexBuffers.ColorVertexBuffer;
					void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.VertexBufferRHI, 0,
					                                               VertexBuffer.GetNumVertices() * VertexBuffer.
					                                               GetStride(), RLM_WriteOnly);
					FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(),
					                VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
					RHICmdList.UnlockBuffer(VertexBuffer.VertexBufferRHI);
				}

				{
					auto& VertexBuffer = Section->VertexBuffers.StaticMeshVertexBuffer;
					void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI, 0,
					                                               VertexBuffer.GetTangentSize(), RLM_WriteOnly);
					FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTangentData(), VertexBuffer.GetTangentSize());
					RHICmdList.UnlockBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI);
				}

				{
					auto& VertexBuffer = Section->VertexBuffers.StaticMeshVertexBuffer;
					void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI, 0,
					                                               VertexBuffer.GetTexCoordSize(), RLM_WriteOnly);
					FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTexCoordData(), VertexBuffer.GetTexCoordSize());
					RHICmdList.UnlockBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI);
				}

				// 1、准备Instance数据
				const int32 NumInstances = this->InstanceNum;

				if (InstanceSceneData.Num() != NumInstances)
				{
					InstanceSceneData.SetNumUninitialized(NumInstances);
				}

				if (NumInstances > 0)
				{
					TResourceArray<FVector4f> InstanceOriginData;
					TResourceArray<FVector4f> InstanceTransformData;

					InstanceOriginData.AddUninitialized(NumInstances);
					InstanceTransformData.AddUninitialized(NumInstances * 3);

					for (int32 i = 0; i < NumInstances; ++i)
					{
						// 构建变换矩阵 (示例：简单的沿Y轴排列)
						FVector3f Pos(0.f, i * 150.f, 0.f);
						FMatrix44f Mat = FMatrix44f::Identity;
						Mat.SetOrigin(Pos);

						// 填充 Origin (通常用于剔除或作为位置偏移，取决于Shader逻辑)
						// 这里假设 Shader 用它做 Bounds center 或直接做 Position Offset
						InstanceOriginData[i] = FVector4f(Pos.X, Pos.Y, Pos.Z, 1.0f); 

						// 填充 Transform (3行，每行一个float4)
						// Unreal 的 InstancedStaticMesh 传递的是 3x4 矩阵 (去掉最后一行 0,0,0,1)
						// X Axis
						InstanceTransformData[i * 3 + 0] = FVector4f(Mat.M[0][0], Mat.M[0][1], Mat.M[0][2], Mat.M[3][0]); 
						// Y Axis
						InstanceTransformData[i * 3 + 1] = FVector4f(Mat.M[1][0], Mat.M[1][1], Mat.M[1][2], Mat.M[3][1]);
						// Z Axis
						InstanceTransformData[i * 3 + 2] = FVector4f(Mat.M[2][0], Mat.M[2][1], Mat.M[2][2], Mat.M[3][2]);
					
						// 注意：上面矩阵填充方式取决于你的 Shader 如何读取 VertexFetch_InstanceTransformBuffer
						// ISM 标准实现是转置过的，或者分别存储 Axis。
						// 如果你的 Shader 直接取 float4，请确保这里的数据布局与 Shader 读取索引一致。
					}

					// 创建/更新 Vertex Buffer RHI
					// Origin Buffer
					if (Section->InstanceOriginBuffer.VertexBufferRHI)
					{
						Section->InstanceOriginBuffer.ReleaseResource();
					}
				
					FRHIResourceCreateInfo OriginCreateInfo(TEXT("VisMeshInstanceOrigin"), &InstanceOriginData);
					Section->InstanceOriginBuffer.VertexBufferRHI = RHICmdList.CreateVertexBuffer(
						InstanceOriginData.GetResourceDataSize(),
						EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource, // 必须标记为 ShaderResource
						OriginCreateInfo
					);

					// Transform Buffer
					if (Section->InstanceTransformBuffer.VertexBufferRHI)
					{
						Section->InstanceTransformBuffer.ReleaseResource();
					}

					FRHIResourceCreateInfo TransformCreateInfo(TEXT("VisMeshInstanceTransform"), &InstanceTransformData);
					Section->InstanceTransformBuffer.VertexBufferRHI = RHICmdList.CreateVertexBuffer(
						InstanceTransformData.GetResourceDataSize(),
						EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource,
						TransformCreateInfo
					);

					// ---------------------------------------------------------
					// 3. 创建 SRV (Manual Vertex Fetch 必须)
					// ---------------------------------------------------------
				
					if (Section->InstanceOriginBuffer.VertexBufferRHI)
					{
						Section->InstanceOriginSRV = RHICmdList.CreateShaderResourceView(
							Section->InstanceOriginBuffer.VertexBufferRHI, sizeof(FVector4f), PF_A32B32G32R32F);
					}

					if (Section->InstanceTransformBuffer.VertexBufferRHI)
					{
						Section->InstanceTransformSRV = RHICmdList.CreateShaderResourceView(
							Section->InstanceTransformBuffer.VertexBufferRHI, sizeof(FVector4f), PF_A32B32G32R32F);
					}

					// --- C. 填充 FInstancedStaticMeshDataType 并绑定 ---
                
                FInstancedStaticMeshDataType NewInstanceData;

                // 1. 绑定 SRV (这是最重要的部分，用于 Manual Vertex Fetch)
                NewInstanceData.InstanceOriginSRV = Section->InstanceOriginSRV;
                NewInstanceData.InstanceTransformSRV = Section->InstanceTransformSRV;
                NewInstanceData.InstanceLightmapSRV = nullptr; // 如果没有 Lightmap 数据
                NewInstanceData.InstanceCustomDataSRV = nullptr;
                NewInstanceData.NumCustomDataFloats = 0;

                // 2. 绑定 Stream Components (用于 Input Layout / Fallback)
                // 即使使用 Manual Fetch，设置这些也有助于 RHI 生成正确的 PSO Key

                // Origin Component (Stream Index 1 / Attribute 8)
                NewInstanceData.InstanceOriginComponent = FVertexStreamComponent(
                    &Section->InstanceOriginBuffer,
                    0,                  // Offset
                    sizeof(FVector4f),  // Stride
                    VET_Float4,
                    EVertexStreamUsage::ManualFetch
                );

                // Transform Components (Stream Index 2 / Attributes 9, 10, 11)
                // Transform 数据是交错存储的：Row0, Row1, Row2...
                // 所以 Stride 是 3 * sizeof(FVector4f)
                const uint32 TransformStride = 3 * sizeof(FVector4f);

                // Row 0
                NewInstanceData.InstanceTransformComponent[0] = FVertexStreamComponent(
                    &Section->InstanceTransformBuffer,
                    0,                  // Offset 0
                    TransformStride,
                    VET_Float4,
                    EVertexStreamUsage::ManualFetch
                );

                // Row 1
                NewInstanceData.InstanceTransformComponent[1] = FVertexStreamComponent(
                    &Section->InstanceTransformBuffer,
                    sizeof(FVector4f),  // Offset 16 bytes
                    TransformStride,
                    VET_Float4,
                    EVertexStreamUsage::ManualFetch
                );

                // Row 2
                NewInstanceData.InstanceTransformComponent[2] = FVertexStreamComponent(
                    &Section->InstanceTransformBuffer,
                    2 * sizeof(FVector4f), // Offset 32 bytes
                    TransformStride,
                    VET_Float4,
                    EVertexStreamUsage::ManualFetch
                );

                // 3. 将构建好的数据结构传给 Factory
                // SetData 会更新 Factory 内部的数据副本并触发 UpdateRHI
                Section->VertexFactory.SetInstanceData(RHICmdList, NewInstanceData);
				}
			}


			// Free data sent from game thread
			delete SectionData;
		}
	}

void FVisMeshInstancedSceneProxy::CreateRenderThreadResources()
{
	FPrimitiveSceneProxy::CreateRenderThreadResources();

	const bool bCanUseGPUScene = UseGPUScene(GetScene().GetShaderPlatform(), GetScene().GetFeatureLevel());

	if (bCanUseGPUScene)
	{
		if (InstanceSceneData.Num() == 0)
		{
			InstanceSceneData.SetNum(InstanceNum);
		}
	}
}

void FVisMeshInstancedSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
                                                         const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, class FMeshElementCollector& Collector) const
	{
		// Set up wireframe material (if needed)
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		FColoredMaterialRenderProxy* WireframeMaterialInstance = nullptr;
		if (bWireframe)
		{
			WireframeMaterialInstance = new FColoredMaterialRenderProxy(
				GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL,
				FLinearColor(0, 0.5f, 1.f)
			);

			Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
		}

		for (const FVisMeshInstancedProxySection* Section : Sections)
		{
			if (Section != nullptr && Section->bSectionVisible)
			{
				FMaterialRenderProxy* MaterialProxy = bWireframe
					                                      ? WireframeMaterialInstance
					                                      : Section->Material->GetRenderProxy();

				// For each view..
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
				{
					if (VisibilityMap & (1 << ViewIndex))
					{
						const FSceneView* View = Views[ViewIndex];
						// Draw the mesh
						FMeshBatch& Mesh = Collector.AllocateMesh();
						FMeshBatchElement& BatchElement = Mesh.Elements[0];
						BatchElement.IndexBuffer = &Section->IndexBuffer;
						Mesh.bWireframe = bWireframe;
						Mesh.VertexFactory = &Section->VertexFactory;
						Mesh.MaterialRenderProxy = MaterialProxy;

						bool bHasPrecomputedVolumetricLightmap;
						FMatrix PreviousLocalToWorld;
						int32 SingleCaptureIndex;
						bool bOutputVelocity;
						GetScene().GetPrimitiveUniformShaderParameters_RenderThread(
							GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld,
							SingleCaptureIndex, bOutputVelocity);
						bOutputVelocity |= AlwaysHasVelocity();

						FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.
							AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
						DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(),
						                                  GetLocalBounds(), GetLocalBounds(), ReceivesDecals(),
						                                  bHasPrecomputedVolumetricLightmap, bOutputVelocity,
						                                  GetCustomPrimitiveData());
						BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

						BatchElement.FirstIndex = 0;
						BatchElement.NumPrimitives = Section->IndexBuffer.Indices.Num() / 3;
						BatchElement.MinVertexIndex = 0;
						BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
						BatchElement.NumInstances = 1;
						BatchElement.UserIndex = 0;

						Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
						Mesh.Type = PT_TriangleList;
						Mesh.DepthPriorityGroup = SDPG_World;
						Mesh.bCanApplyViewModeOverrides = false;
						Collector.AddMesh(ViewIndex, Mesh);
					}
				}
			}
		}

		// Draw bounds
// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
// 		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
// 		{
// 			if (VisibilityMap & (1 << ViewIndex))
// 			{
// 				// Draw simple collision as wireframe if 'show collision', and collision is enabled, and we are not using the complex as the simple
// 				if (ViewFamily.EngineShowFlags.Collision && IsCollisionEnabled() && BodySetup->GetCollisionTraceFlag()
// 					!= ECollisionTraceFlag::CTF_UseComplexAsSimple)
// 				{
// 					FTransform GeomTransform(GetLocalToWorld());
// 					BodySetup->AggGeom.GetAggGeom(GeomTransform,
// 					                              GetSelectionColor(FColor(157, 149, 223, 255), IsSelected(),
// 					                                                IsHovered()).ToFColor(true), NULL, false, false,
// 					                              AlwaysHasVelocity(), ViewIndex, Collector);
// 				}
//
// 				// Render bounds
// 				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
// 			}
// 		}
// #endif
	}


bool FInstancedVisMeshVertexFactory::ShouldCompilePermutation(
	const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (Parameters.MaterialParameters.bIsUsedWithInstancedStaticMeshes || Parameters.MaterialParameters.
			bIsSpecialEngineMaterial)
		&& FLocalVertexFactory::ShouldCompilePermutation(Parameters);
}
