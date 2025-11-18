// #include "InstancedVisMesh.h"
//
// #include "MeshDrawShaderBindings.h"
// #include "MeshMaterialShader.h"
//
// const int32 InstancedVisMeshMaxTexCoord = 8;
//
// IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FInstancedVisMeshVertexFactoryUniformShaderParameters, "InstanceVF");
// IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FInstancedVisMeshVFLooseUniformShaderParameters, "InstancedVFLooseParameters");
//
//
// IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_Vertex, FInstancedVisMeshVertexFactoryShaderParameters);
// // pixel shader may need access to InstanceCustomDataBuffer in non-GPUScene case
// IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_Pixel, FInstancedVisMeshVertexFactoryShaderParameters);
// #if RHI_RAYTRACING
// IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_RayHitGroup, FInstancedVisMeshVertexFactoryShaderParameters);
// IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FInstancedVisMeshVertexFactory, SF_Compute, FInstancedVisMeshVertexFactoryShaderParameters);
// #endif
//
// IMPLEMENT_VERTEX_FACTORY_TYPE(FInstancedVisMeshVertexFactory,"/Engine/Private/LocalVertexFactory.ush",
// 	  EVertexFactoryFlags::UsedWithMaterials
// 	| EVertexFactoryFlags::SupportsStaticLighting
// 	| EVertexFactoryFlags::SupportsDynamicLighting
// 	| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
// 	| EVertexFactoryFlags::SupportsCachingMeshDrawCommands
// 	| EVertexFactoryFlags::SupportsRayTracing
// 	| EVertexFactoryFlags::SupportsRayTracingDynamicGeometry
// 	| EVertexFactoryFlags::SupportsLightmapBaking
// 	| EVertexFactoryFlags::SupportsPrimitiveIdStream
// 	| EVertexFactoryFlags::DoesNotSupportNullPixelShader
// 	| EVertexFactoryFlags::SupportsManualVertexFetch
// 	| EVertexFactoryFlags::SupportsPSOPrecaching
// 	| EVertexFactoryFlags::SupportsLumenMeshCards
// )
//
// class FVisMeshDummyFloatBuffer : public FVertexBufferWithSRV
// {
// public:
// 	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
// 	{
// 		// Create the texture RHI.
// 		const int32 NumFloats = 4;
//
// 		const FRHIBufferCreateDesc CreateDesc =
// 			FRHIBufferCreateDesc::CreateVertex(TEXT("DummyFloatBuffer"), sizeof(float) * NumFloats)
// 			.AddUsage(EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource)
// 			.DetermineInitialState()
// 			.SetInitActionZeroData();
//
// 		VertexBufferRHI = RHICmdList.CreateBuffer(CreateDesc);
//
// 		// Create a view of the buffer
// 		ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(
// 			VertexBufferRHI, 
// 			FRHIViewDesc::CreateBufferSRV()
// 				.SetType(FRHIViewDesc::EBufferType::Typed)
// 				.SetFormat(PF_R32_FLOAT));
// 	}
// };
//
// TGlobalResource<FVisMeshDummyFloatBuffer> GDummyFloatBuffer;
//
// /** Delete existing resources */
// void FVisMeshInstanceBuffer::CleanUp()
// {
// 	InstanceData.Reset();
// }
//
// void FVisMeshInstanceBuffer::InitFromPreallocatedData(FStaticMeshInstanceData& Other)
// {
// 	QUICK_SCOPE_CYCLE_COUNTER(STAT_FStaticMeshInstanceBuffer_InitFromPreallocatedData);
//
// 	InstanceData = MakeShared<FStaticMeshInstanceData, ESPMode::ThreadSafe>();
// 	Swap(Other, *InstanceData.Get());
// 	InstanceData->SetAllowCPUAccess(RequireCPUAccess);
// }
//
// /**
//  * Specialized assignment operator, only used when importing LOD's.  
//  */
// void FVisMeshInstanceBuffer::operator=(const FVisMeshInstanceBuffer &Other)
// {
// 	checkf(0, TEXT("Unexpected assignment call"));
// }
//
// void FVisMeshInstanceBuffer::InitRHI(FRHICommandListBase& RHICmdList)
// {
// 	TRACE_CPUPROFILER_EVENT_SCOPE_STR("FStaticMeshInstanceBuffer::InitRHI");
//
// 	check(InstanceData);
// 	if (InstanceData->GetNumInstances() > 0)
// 	{
// 		QUICK_SCOPE_CYCLE_COUNTER(STAT_FStaticMeshInstanceBuffer_InitRHI);
//
// 		LLM_SCOPE(ELLMTag::InstancedMesh);
// 		auto AccessFlags = BUF_Static;
// 		CreateVertexBuffer(RHICmdList, InstanceData->GetOriginResourceArray(), AccessFlags | BUF_ShaderResource, 16, PF_A32B32G32R32F, InstanceOriginBuffer.VertexBufferRHI, InstanceOriginSRV);
// 		CreateVertexBuffer(RHICmdList, InstanceData->GetTransformResourceArray(), AccessFlags | BUF_ShaderResource, InstanceData->GetTranslationUsesHalfs() ? 8 : 16, InstanceData->GetTranslationUsesHalfs() ? PF_FloatRGBA : PF_A32B32G32R32F, InstanceTransformBuffer.VertexBufferRHI, InstanceTransformSRV);
// 		CreateVertexBuffer(RHICmdList, InstanceData->GetLightMapResourceArray(), AccessFlags | BUF_ShaderResource, 8, PF_R16G16B16A16_SNORM, InstanceLightmapBuffer.VertexBufferRHI, InstanceLightmapSRV);
// 		if (InstanceData->GetNumCustomDataFloats() > 0)
// 		{
// 			CreateVertexBuffer(RHICmdList, InstanceData->GetCustomDataResourceArray(), AccessFlags | BUF_ShaderResource, 4, PF_R32_FLOAT, InstanceCustomDataBuffer.VertexBufferRHI, InstanceCustomDataSRV);
// 			// Make sure we still create custom data SRV on platforms that do not support/use MVF 
// 			if (InstanceCustomDataSRV == nullptr)
// 			{
// 				InstanceCustomDataSRV = RHICmdList.CreateShaderResourceView(
// 					InstanceCustomDataBuffer.VertexBufferRHI, 
// 					FRHIViewDesc::CreateBufferSRV()
// 						.SetType(FRHIViewDesc::EBufferType::Typed)
// 						.SetFormat(PF_R32_FLOAT));
// 			}
// 		}
// 		else
// 		{
// 			InstanceCustomDataSRV = GDummyFloatBuffer.ShaderResourceViewRHI;
// 		}
// 	}
// }
//
// void FVisMeshInstanceBuffer::ReleaseRHI()
// {
// 	InstanceOriginSRV.SafeRelease();
// 	InstanceTransformSRV.SafeRelease();
// 	InstanceLightmapSRV.SafeRelease();
// 	InstanceCustomDataSRV.SafeRelease();
//
// 	InstanceOriginBuffer.ReleaseRHI();
// 	InstanceTransformBuffer.ReleaseRHI();
// 	InstanceLightmapBuffer.ReleaseRHI();
// 	InstanceCustomDataBuffer.ReleaseRHI();
// }
//
// void FVisMeshInstanceBuffer::InitResource(FRHICommandListBase& RHICmdList)
// {
// 	FRenderResource::InitResource(RHICmdList);
// 	InstanceOriginBuffer.InitResource(RHICmdList);
// 	InstanceTransformBuffer.InitResource(RHICmdList);
// 	InstanceLightmapBuffer.InitResource(RHICmdList);
// 	InstanceCustomDataBuffer.InitResource(RHICmdList);
// }
//
// void FVisMeshInstanceBuffer::ReleaseResource()
// {
// 	FRenderResource::ReleaseResource();
// 	InstanceOriginBuffer.ReleaseResource();
// 	InstanceTransformBuffer.ReleaseResource();
// 	InstanceLightmapBuffer.ReleaseResource();
// 	InstanceCustomDataBuffer.ReleaseResource();
// }
//
// SIZE_T FVisMeshInstanceBuffer::GetResourceSize() const
// {
// 	if (InstanceData && InstanceData->GetNumInstances() > 0)
// 	{
// 		return InstanceData->GetResourceSize();
// 	}
// 	return 0;
// }
//
// void FVisMeshInstanceBuffer::CreateVertexBuffer(FRHICommandListBase& RHICmdList, FResourceArrayInterface* InResourceArray, EBufferUsageFlags InUsage, uint32 InStride, uint8 InFormat, FBufferRHIRef& OutVertexBufferRHI, FShaderResourceViewRHIRef& OutInstanceSRV)
// {
// 	check(InResourceArray);
// 	check(InResourceArray->GetResourceDataSize() > 0);
//
// 	const FRHIBufferCreateDesc CreateDesc =
// 		FRHIBufferCreateDesc::CreateVertex(TEXT("FStaticMeshInstanceBuffer"), InResourceArray->GetResourceDataSize())
// 		.AddUsage(InUsage)
// 		.SetInitActionResourceArray(InResourceArray)
// 		.DetermineInitialState();
//
// 	// TODO: possibility over allocated the vertex buffer when we support partial update for when working in the editor
// 	OutVertexBufferRHI = RHICmdList.CreateBuffer(CreateDesc);
//
// 	if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
// 	{
// 		OutInstanceSRV = RHICmdList.CreateShaderResourceView(
// 			OutVertexBufferRHI, 
// 			FRHIViewDesc::CreateBufferSRV()
// 				.SetType(FRHIViewDesc::EBufferType::Typed)
// 				.SetFormat(static_cast<EPixelFormat>(InFormat)));
// 	}
// }
//
// void FVisMeshInstanceBuffer::BindInstanceVertexBuffer(const class FVertexFactory* VertexFactory, FInstancedStaticMeshDataType& InstancedStaticMeshData) const
// {
// 	if (InstanceData->GetNumInstances())
// 	{
// 		if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
// 		{
// 			check(InstanceOriginSRV);
// 			check(InstanceTransformSRV);
// 			check(InstanceLightmapSRV);
// 		}
// 		check(InstanceCustomDataSRV); // Should not be nullptr, but can be assigned a dummy buffer
// 	}
//
// 	{
// 		InstancedStaticMeshData.InstanceOriginSRV = InstanceOriginSRV;
// 		InstancedStaticMeshData.InstanceTransformSRV = InstanceTransformSRV;
// 		InstancedStaticMeshData.InstanceLightmapSRV = InstanceLightmapSRV;
// 		InstancedStaticMeshData.InstanceCustomDataSRV = InstanceCustomDataSRV;
// 		InstancedStaticMeshData.NumCustomDataFloats = InstanceData->GetNumCustomDataFloats();
// 	}
//
// 	{
// 		InstancedStaticMeshData.InstanceOriginComponent = FVertexStreamComponent(
// 			&InstanceOriginBuffer,
// 			0,
// 			16,
// 			VET_Float4,
// 			EVertexStreamUsage::ManualFetch | EVertexStreamUsage::Instancing
// 		);
//
// 		EVertexElementType TransformType = InstanceData->GetTranslationUsesHalfs() ? VET_Half4 : VET_Float4;
// 		uint32 TransformStride = InstanceData->GetTranslationUsesHalfs() ? 8 : 16;
//
// 		InstancedStaticMeshData.InstanceTransformComponent[0] = FVertexStreamComponent(
// 			&InstanceTransformBuffer,
// 			0 * TransformStride,
// 			3 * TransformStride,
// 			TransformType,
// 			EVertexStreamUsage::ManualFetch | EVertexStreamUsage::Instancing
// 		);
// 		InstancedStaticMeshData.InstanceTransformComponent[1] = FVertexStreamComponent(
// 			&InstanceTransformBuffer,
// 			1 * TransformStride,
// 			3 * TransformStride,
// 			TransformType,
// 			EVertexStreamUsage::ManualFetch | EVertexStreamUsage::Instancing
// 		);
// 		InstancedStaticMeshData.InstanceTransformComponent[2] = FVertexStreamComponent(
// 			&InstanceTransformBuffer,
// 			2 * TransformStride,
// 			3 * TransformStride,
// 			TransformType,
// 			EVertexStreamUsage::ManualFetch | EVertexStreamUsage::Instancing
// 		);
//
// 		InstancedStaticMeshData.InstanceLightmapAndShadowMapUVBiasComponent = FVertexStreamComponent(
// 			&InstanceLightmapBuffer,
// 			0,
// 			8,
// 			VET_Short4N,
// 			EVertexStreamUsage::ManualFetch | EVertexStreamUsage::Instancing
// 		);
// 	}
// }
//
// void FVisMeshInstanceBuffer::FlushGPUUpload(FRHICommandListBase& RHICmdList)
// {
// 	if (bFlushToGPUPending)
// 	{
// 		if (!IsInitialized())
// 		{
// 			InitResource(RHICmdList);
// 		}
// 		else
// 		{
// 			UpdateRHI(RHICmdList);
// 		}
// 		bFlushToGPUPending = false;
// 	}
// }
//
//
// void FStaticMeshInstanceData::Serialize(FArchive& Ar)
// {	
// 	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
//
// 	const bool bCookConvertTransformsToFullFloat = Ar.IsCooking() && bUseHalfFloat && !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::HalfFloatVertexFormat);
//
// 	if (bCookConvertTransformsToFullFloat)
// 	{
// 		bool bSaveUseHalfFloat = false;
// 		Ar << bSaveUseHalfFloat;
// 	}
// 	else
// 	{
// 		Ar << bUseHalfFloat;
// 	}
//
// 	Ar << NumInstances;
//
// 	if (!Ar.IsLoading() || Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::PerInstanceCustomData)
// 	{
// 		Ar << NumCustomDataFloats;
// 	}
//
// 	if (Ar.IsLoading())
// 	{
// 		const int64 NumTotalCustomDataFloats = (int64)NumCustomDataFloats * NumInstances;
// 		if (!IntFitsIn<int32>(NumTotalCustomDataFloats))
// 		{
// 			// Sanitize inputs. Allocate no custom data to avoid out of range access
// 			NumCustomDataFloats = 0;
// 			ensureMsgf(false, TEXT("Total Custom Instance Data Floats count is out of range."));
// 		}
//
// 		AllocateBuffers(NumInstances);
// 	}
//
// 	InstanceOriginData->Serialize(Ar);
// 	InstanceLightmapData->Serialize(Ar);
//
// 	if (bCookConvertTransformsToFullFloat)
// 	{
// 		TStaticMeshVertexData<FInstanceTransformMatrix<float>> FullInstanceTransformData;
// 		FullInstanceTransformData.ResizeBuffer(NumInstances);
//
// 		FInstanceTransformMatrix<FFloat16>* Src = (FInstanceTransformMatrix<FFloat16>*)InstanceTransformData->GetDataPointer();
// 		FInstanceTransformMatrix<float>* Dest = (FInstanceTransformMatrix<float>*)FullInstanceTransformData.GetDataPointer();
// 		for (int32 Idx = 0; Idx < NumInstances; Idx++)
// 		{
// 			Dest->InstanceTransform1[0] = Src->InstanceTransform1[0];
// 			Dest->InstanceTransform1[1] = Src->InstanceTransform1[1];
// 			Dest->InstanceTransform1[2] = Src->InstanceTransform1[2];
// 			Dest->InstanceTransform1[3] = Src->InstanceTransform1[3];
// 			Dest->InstanceTransform2[0] = Src->InstanceTransform2[0];
// 			Dest->InstanceTransform2[1] = Src->InstanceTransform2[1];
// 			Dest->InstanceTransform2[2] = Src->InstanceTransform2[2];
// 			Dest->InstanceTransform2[3] = Src->InstanceTransform2[3];
// 			Dest->InstanceTransform3[0] = Src->InstanceTransform3[0];
// 			Dest->InstanceTransform3[1] = Src->InstanceTransform3[1];
// 			Dest->InstanceTransform3[2] = Src->InstanceTransform3[2];
// 			Dest->InstanceTransform3[3] = Src->InstanceTransform3[3];
// 			Src++;
// 			Dest++;
// 		}
//
// 		FullInstanceTransformData.Serialize(Ar);
// 	}
// 	else
// 	{
// 		InstanceTransformData->Serialize(Ar);
// 	}
//
// 	if (!Ar.IsLoading() || Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::PerInstanceCustomData)
// 	{
// 		InstanceCustomData->Serialize(Ar);
// 	}
//
// 	if (Ar.IsLoading())
// 	{
// 		InstanceOriginDataPtr = InstanceOriginData->GetDataPointer();
// 		InstanceLightmapDataPtr = InstanceLightmapData->GetDataPointer();
// 		InstanceTransformDataPtr = InstanceTransformData->GetDataPointer();
// 		InstanceCustomDataPtr = InstanceCustomData->GetDataPointer();
// 	}
// }
//
//
//
// bool FInstancedVisMeshVertexFactory::ShouldCompilePermutation(
// 	const FVertexFactoryShaderPermutationParameters& Parameters)
// {
// 	return (Parameters.MaterialParameters.bIsUsedWithInstancedStaticMeshes || Parameters.MaterialParameters.bIsSpecialEngineMaterial)
// 		&& FLocalVertexFactory::ShouldCompilePermutation(Parameters);
// }
//
// void FInstancedVisMeshVertexFactory::ModifyCompilationEnvironment(
// 	const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
// {
// 	if (RHISupportsManualVertexFetch(Parameters.Platform))
// 	{
// 		OutEnvironment.SetDefineIfUnset(TEXT("MANUAL_VERTEX_FETCH"), TEXT("1"));
// 	}
//
// 	if (UseGPUScene(Parameters.Platform))
// 	{
// 		// USE_INSTANCE_CULLING - set up additional instancing attributes (basic instancing is the default)
// 		OutEnvironment.SetDefine(TEXT("USE_INSTANCE_CULLING"), TEXT("1"));
// 	}
// 	else
// 	{
// 		OutEnvironment.SetDefine(TEXT("USE_INSTANCING"), TEXT("1"));
// 	}
//
// 	FLocalVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
// }
//
// void FInstancedVisMeshVertexFactory::GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType,
// 	FVertexDeclarationElementList& Elements)
// {
// 	// Fallback to local vertex factory because manual vertex fetch is supported
// 	FLocalVertexFactory::GetPSOPrecacheVertexFetchElements(VertexInputStreamType, Elements);
// }
//
// void FInstancedVisMeshVertexFactory::GetVertexElements(ERHIFeatureLevel::Type FeatureLevel,
// 	EVertexInputStreamType InputStreamType, bool bSupportsManualVertexFetch, FDataType& Data,
// 	FInstancedStaticMeshDataType& InstanceData, FVertexDeclarationElementList& Elements)
// {
// 	FVertexStreamList VertexStreams;
// 	GetVertexElements(FeatureLevel, InputStreamType, bSupportsManualVertexFetch, Data, InstanceData, Elements, VertexStreams);
//
// 	if (UseGPUScene(GMaxRHIShaderPlatform, GMaxRHIFeatureLevel) 
// 		&& !PlatformGPUSceneUsesUniformBufferView(GMaxRHIShaderPlatform))
// 	{
// 		Elements.Add(FVertexElement(VertexStreams.Num(), 0, VET_UInt, 13, sizeof(uint32), true));
// 	}
// }
//
// void FInstancedVisMeshVertexFactory::InitInstancedVisMeshVertexFactoryComponents(
// 	const FStaticMeshVertexBuffers& VertexBuffers, const FColorVertexBuffer* ColorVertexBuffer,
// 	const FVisMeshInstanceBuffer* InstanceBuffer, const FInstancedVisMeshVertexFactory* VertexFactory,
// 	int32 LightMapCoordinateIndex, bool bRHISupportsManualVertexFetch,
// 	FInstancedVisMeshVertexFactory::FDataType& OutData, FInstancedStaticMeshDataType& OutInstanceData)
// {
// 	VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(VertexFactory, OutData);
// 	VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(VertexFactory, OutData);
// 	VertexBuffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(VertexFactory, OutData);
// 	
// 	if (LightMapCoordinateIndex < (int32)VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() && LightMapCoordinateIndex >= 0)
// 	{
// 		VertexBuffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(VertexFactory, OutData, LightMapCoordinateIndex);
// 	}
//
// 	if (ColorVertexBuffer != nullptr)
// 	{
// 		ColorVertexBuffer->BindColorVertexBuffer(VertexFactory, OutData);
// 	}
// 	else
// 	{
// 		// shouldn't this check if ISM component actually has a color data for override?
// 		FColorVertexBuffer::BindDefaultColorVertexBuffer(VertexFactory, OutData, bRHISupportsManualVertexFetch ? FColorVertexBuffer::NullBindStride::FColorSizeForComponentOverride : FColorVertexBuffer::NullBindStride::ZeroForDefaultBufferBind);
// 	}
//
// 	if (InstanceBuffer)
// 	{
// 		InstanceBuffer->BindInstanceVertexBuffer(VertexFactory, OutInstanceData);
// 	}
// }
//
// void FInstancedVisMeshVertexFactory::Copy(const FInstancedVisMeshVertexFactory& Other)
// {
// 	FInstancedVisMeshVertexFactory* VertexFactory = this;
// 	const FLocalVertexFactory::FDataType* DataCopy = &Other.Data;
// 	const FInstancedStaticMeshDataType* InstanceDataCopy = &Other.InstanceData;
// 	ENQUEUE_RENDER_COMMAND(FInstancedVisMeshVertexFactoryCopyData)(
// 	[VertexFactory, DataCopy, InstanceDataCopy] (FRHICommandListBase&)
// 	{
// 		VertexFactory->Data = *DataCopy;
// 		VertexFactory->InstanceData = *InstanceDataCopy;
// 	});
// 	BeginUpdateResourceRHI(this);
// }
//
// void FInstancedVisMeshVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
// {
// 	TRACE_CPUPROFILER_EVENT_SCOPE_STR("FInstancedStaticMeshVertexFactory::InitRHI");
//
//
// 	check(HasValidFeatureLevel());
//
// 	const ERHIFeatureLevel::Type ThisFeatureLevel = GetFeatureLevel();
// 	const bool bCanUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform, ThisFeatureLevel);
// 	const bool bUseManualVertexFetch = GetType()->SupportsManualVertexFetch(ThisFeatureLevel);
//
// 	FVertexDeclarationElementList Elements;
// 	GetVertexElements(ThisFeatureLevel, EVertexInputStreamType::Default, bUseManualVertexFetch, Data, InstanceData, Elements, Streams);
//
// 	AddPrimitiveIdStreamElement(EVertexInputStreamType::Default, Elements, 13, 13);
//
// 	// we don't need per-vertex shadow or lightmap rendering
// 	InitDeclaration(Elements);
//
// 	if (!bCanUseGPUScene)
// 	{
// 		FInstancedVisMeshVertexFactoryUniformShaderParameters UniformParameters;
// 		UniformParameters.VertexFetch_InstanceOriginBuffer = GetInstanceOriginSRV();
// 		UniformParameters.VertexFetch_InstanceTransformBuffer = GetInstanceTransformSRV();
// 		UniformParameters.VertexFetch_InstanceLightmapBuffer = GetInstanceLightmapSRV();
// 		UniformParameters.InstanceCustomDataBuffer = GetInstanceCustomDataSRV();
// 		UniformParameters.NumCustomDataFloats = InstanceData.NumCustomDataFloats;
// 		UniformBuffer = TUniformBufferRef<FInstancedVisMeshVertexFactoryUniformShaderParameters>::CreateUniformBufferImmediate(UniformParameters, UniformBuffer_MultiFrame, EUniformBufferValidation::None);
// 	}
// }
//
// void FInstancedVisMeshVertexFactory::GetVertexElements(ERHIFeatureLevel::Type FeatureLevel,
//                                                        EVertexInputStreamType InputStreamType, bool bSupportsManualVertexFetch, FDataType& Data,
//                                                        FInstancedStaticMeshDataType& InstanceData, FVertexDeclarationElementList& Elements, FVertexStreamList& Streams)
// {
// 	if (Data.PositionComponent.VertexBuffer != NULL)
// 	{
// 		Elements.Add(AccessStreamComponent(Data.PositionComponent, 0, Streams));
// 	}
//
// 	if (!bSupportsManualVertexFetch)
// 	{
// 		// only tangent,normal are used by the stream. the binormal is derived in the shader
// 		uint8 TangentBasisAttributes[2] = { 1, 2 };
// 		for (int32 AxisIndex = 0; AxisIndex < 2; AxisIndex++)
// 		{
// 			if (Data.TangentBasisComponents[AxisIndex].VertexBuffer != NULL)
// 			{
// 				Elements.Add(AccessStreamComponent(Data.TangentBasisComponents[AxisIndex], TangentBasisAttributes[AxisIndex], Streams));
// 			}
// 		}
//
// 		if (Data.ColorComponentsSRV == nullptr)
// 		{
// 			Data.ColorComponentsSRV = GNullColorVertexBuffer.VertexBufferSRV;
// 			Data.ColorIndexMask = 0;
// 		}
//
// 		if (Data.ColorComponent.VertexBuffer)
// 		{
// 			Elements.Add(AccessStreamComponent(Data.ColorComponent, 3, Streams));
// 		}
// 		else
// 		{
// 			//If the mesh has no color component, set the null color buffer on a new stream with a stride of 0.
// 			//This wastes 4 bytes of bandwidth per vertex, but prevents having to compile out twice the number of vertex factories.
// 			FVertexStreamComponent NullColorComponent(&GNullColorVertexBuffer, 0, 0, VET_Color, EVertexStreamUsage::ManualFetch);
// 			Elements.Add(AccessStreamComponent(NullColorComponent, 3, Streams));
// 		}
//
// 		if (Data.TextureCoordinates.Num())
// 		{
// 			const int32 BaseTexCoordAttribute = 4;
// 			for (int32 CoordinateIndex = 0; CoordinateIndex < Data.TextureCoordinates.Num(); CoordinateIndex++)
// 			{
// 				Elements.Add(AccessStreamComponent(
// 					Data.TextureCoordinates[CoordinateIndex],
// 					BaseTexCoordAttribute + CoordinateIndex,
// 					Streams
// 				));
// 			}
//
// 			for (int32 CoordinateIndex = Data.TextureCoordinates.Num(); CoordinateIndex < (InstancedVisMeshMaxTexCoord + 1) / 2; CoordinateIndex++)
// 			{
// 				Elements.Add(AccessStreamComponent(
// 					Data.TextureCoordinates[Data.TextureCoordinates.Num() - 1],
// 					BaseTexCoordAttribute + CoordinateIndex,
// 					Streams
// 				));
// 			}
// 		}
//
// 		// PreSkinPosition attribute is only used for GPUSkinPassthrough variation of local vertex factory.
// 		// It is not used by ISM so fill with dummy buffer.
// 		if (IsGPUSkinPassThroughSupported(GMaxRHIShaderPlatform))
// 		{
// 			FVertexStreamComponent NullComponent(&GNullVertexBuffer, 0, 0, VET_Float4);
// 			Elements.Add(AccessStreamComponent(NullComponent, 14, Streams));
// 		}
//
// 		if (Data.LightMapCoordinateComponent.VertexBuffer)
// 		{
// 			Elements.Add(AccessStreamComponent(Data.LightMapCoordinateComponent, 15, Streams));
// 		}
// 		else if (Data.TextureCoordinates.Num())
// 		{
// 			Elements.Add(AccessStreamComponent(Data.TextureCoordinates[0], 15, Streams));
// 		}
// 	}
//
// 	const bool bCanUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel);
// 	const bool bMobileUsesGPUScene = MobileSupportsGPUScene();
// 	
// 	if (FeatureLevel > ERHIFeatureLevel::ES3_1 || !bMobileUsesGPUScene)
// 	{
// 		// toss in the instanced location stream
// 		check(bCanUseGPUScene || InstanceData.InstanceOriginComponent.VertexBuffer);
// 		if (InstanceData.InstanceOriginComponent.VertexBuffer)
// 		{
// 			Elements.Add(AccessStreamComponent(InstanceData.InstanceOriginComponent, 8, Streams));
// 		}
//
// 		check(bCanUseGPUScene || InstanceData.InstanceTransformComponent[0].VertexBuffer);
// 		if (InstanceData.InstanceTransformComponent[0].VertexBuffer)
// 		{
// 			Elements.Add(AccessStreamComponent(InstanceData.InstanceTransformComponent[0], 9, Streams));
// 			Elements.Add(AccessStreamComponent(InstanceData.InstanceTransformComponent[1], 10, Streams));
// 			Elements.Add(AccessStreamComponent(InstanceData.InstanceTransformComponent[2], 11, Streams));
// 		}
//
// 		if (InstanceData.InstanceLightmapAndShadowMapUVBiasComponent.VertexBuffer)
// 		{
// 			Elements.Add(AccessStreamComponent(InstanceData.InstanceLightmapAndShadowMapUVBiasComponent, 12, Streams));
// 		}
// 	}
// }
//
// void FInstancedVisMeshVertexFactoryShaderParameters::GetElementShaderBindings(const class FSceneInterface* Scene,
// 	const FSceneView* View, const FMeshMaterialShader* Shader, const EVertexInputStreamType InputStreamType,
// 	ERHIFeatureLevel::Type FeatureLevel, const FVertexFactory* VertexFactory, const FMeshBatchElement& BatchElement,
// 	FMeshDrawSingleShaderBindings& ShaderBindings, FVertexInputStreamArray& VertexStreams) const
// {
// 		// Decode VertexFactoryUserData as VertexFactoryUniformBuffer
// 	FRHIUniformBuffer* VertexFactoryUniformBuffer = static_cast<FRHIUniformBuffer*>(BatchElement.VertexFactoryUserData);
// 	FLocalVertexFactoryShaderParametersBase::GetElementShaderBindingsBase(Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, VertexFactoryUniformBuffer, ShaderBindings, VertexStreams);
//
// 	const FInstancingUserData* InstancingUserData = (const FInstancingUserData*)BatchElement.UserData;
// 	const auto* InstancedVertexFactory = static_cast<const FInstancedVisMeshVertexFactory*>(VertexFactory);
// 	const int32 InstanceOffsetValue = BatchElement.UserIndex;
//
// 	ShaderBindings.Add(InstanceOffset, InstanceOffsetValue);
// 	
// 	if (!UseGPUScene(Scene ? Scene->GetShaderPlatform() : GMaxRHIShaderPlatform))
// 	{
// 		ShaderBindings.Add(Shader->GetUniformBufferParameter<FInstancedVisMeshVertexFactoryUniformShaderParameters>(), InstancedVertexFactory->GetUniformBuffer());
// 		if (InstancedVertexFactory->SupportsManualVertexFetch(FeatureLevel))
// 		{
// 			ShaderBindings.Add(VertexFetch_InstanceOriginBufferParameter, InstancedVertexFactory->GetInstanceOriginSRV());
// 			ShaderBindings.Add(VertexFetch_InstanceTransformBufferParameter, InstancedVertexFactory->GetInstanceTransformSRV());
// 			ShaderBindings.Add(VertexFetch_InstanceLightmapBufferParameter, InstancedVertexFactory->GetInstanceLightmapSRV());
// 		}
// 		if (InstanceOffsetValue > 0 && VertexStreams.Num() > 0)
// 		{
// 			// GPUCULL_TODO: This here can still work together with the instance attributes for index, but note that all instance attributes then must assume they are offset wrt the on-the-fly generate buffer
// 			//          so with the new scheme there is no clear way this can work in the vanilla instancing way as there is an indirection. So either other attributes must be loaded in the shader or they
// 			//          would have to be copied as the instance ID is now - not good.
// 			VertexFactory->OffsetInstanceStreams(InstanceOffsetValue, InputStreamType, VertexStreams);
// 		}
// 	}
//
// 	FVector4f InstancingOffset(ForceInit);
// 	// InstancedLODRange is only set for HierarchicalInstancedStaticMeshes
// 	if (InstancingUserData && BatchElement.InstancedLODRange)
// 	{
// 		InstancingOffset = (FVector3f)InstancingUserData->InstancingOffset; // LWC_TODO: precision loss
// 	}
// 	ShaderBindings.Add(InstancingOffsetParameter, InstancingOffset);
//
// 	ShaderBindings.Add(Shader->GetUniformBufferParameter<FInstancedVisMeshVFLooseUniformShaderParameters>(), BatchElement.LooseParametersUniformBuffer);
// };
//
