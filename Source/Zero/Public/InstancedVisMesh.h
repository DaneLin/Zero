// #pragma once
//
// #include "CoreMinimal.h"
// #include "Containers/IndirectArray.h"
// #include "Stats/Stats.h"
// #include "HAL/IConsoleManager.h"
// #include "RenderingThread.h"
// #include "RenderResource.h"
// #include "RayTracingGeometry.h"
// #include "PrimitiveViewRelevance.h"
// #include "ShaderParameters.h"
// #include "SceneView.h"
// #include "VertexFactory.h"
// #include "LocalVertexFactory.h"
// #include "MaterialShared.h"
// #include "Materials/Material.h"
// #include "VisMeshComponent.h"
//
// BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FInstancedVisMeshVertexFactoryUniformShaderParameters, )
// 	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceOriginBuffer)
// 	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceTransformBuffer)
// 	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceLightmapBuffer)
// 	SHADER_PARAMETER_SRV(Buffer<float>, InstanceCustomDataBuffer)
// 	SHADER_PARAMETER(int32, NumCustomDataFloats)
// END_GLOBAL_SHADER_PARAMETER_STRUCT()
//
// BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FInstancedVisMeshVFLooseUniformShaderParameters, )
// 	SHADER_PARAMETER(FVector4f, InstancingViewZCompareZero)
// 	SHADER_PARAMETER(FVector4f, InstancingViewZCompareOne)
// 	SHADER_PARAMETER(FVector4f, InstancingViewZConstant)
// 	SHADER_PARAMETER(FVector4f, InstancingTranslatedWorldViewOriginZero)
// 	SHADER_PARAMETER(FVector4f, InstancingTranslatedWorldViewOriginOne)
// 	SHADER_PARAMETER(FVector4f, InstancingFadeOutParams)
// END_GLOBAL_SHADER_PARAMETER_STRUCT()
//
// /*-----------------------------------------------------------------------------
// 	FStaticMeshInstanceBuffer
// -----------------------------------------------------------------------------*/
//
// class FVisMeshInstanceBuffer : public FRenderResource
// {
// public:
// 	/** Default constructor. */
// 	FVisMeshInstanceBuffer(ERHIFeatureLevel::Type InFeatureLevel, bool InRequireCPUAccess)
// 		: FRenderResource(InFeatureLevel)
// 		  , RequireCPUAccess(InRequireCPUAccess)
// 		  , bFlushToGPUPending(false)
// 	{
// 	}
//
// 	/** Destructor. */
// 	~FVisMeshInstanceBuffer()
// 	{
// 		CleanUp();
// 	}
//
// 	/**
// 	 * Initializes the buffer with the component's data.
// 	 * @param Other - instance data, this call assumes the memory, so this will be empty after the call
// 	 */
// 	void InitFromPreallocatedData(FStaticMeshInstanceData& Other);
//
// 	/**
// 	 * Specialized assignment operator, only used when importing LOD's. 
// 	 */
// 	void operator=(const FVisMeshInstanceBuffer& Other);
//
// 	// Other accessors.
// 	FORCEINLINE uint32 GetNumInstances() const
// 	{
// 		return InstanceData->GetNumInstances();
// 	}
//
// 	FORCEINLINE void GetInstanceTransform(int32 InstanceIndex, FRenderTransform& Transform) const
// 	{
// 		InstanceData->GetInstanceTransform(InstanceIndex, Transform);
// 	}
//
// 	FORCEINLINE void GetInstanceRandomID(int32 InstanceIndex, float& RandomInstanceID) const
// 	{
// 		InstanceData->GetInstanceRandomID(InstanceIndex, RandomInstanceID);
// 	}
//
// #if WITH_EDITOR
// 	FORCEINLINE void GetInstanceEditorData(int32 InstanceIndex, FColor& HitProxyColorOut, bool& bSelectedOut) const
// 	{
// 		InstanceData->GetInstanceEditorData(InstanceIndex, HitProxyColorOut, bSelectedOut);
// 	}
// #endif
//
//
// 	FORCEINLINE void GetInstanceLightMapData(int32 InstanceIndex, FVector4f& InstanceLightmapAndShadowMapUVBias) const
// 	{
// 		InstanceData->GetInstanceLightMapData(InstanceIndex, InstanceLightmapAndShadowMapUVBias);
// 	}
//
// 	FORCEINLINE void GetInstanceCustomDataValues(int32 InstanceIndex, TArray<float>& InstanceCustomData) const
// 	{
// 		InstanceData->GetInstanceCustomDataValues(InstanceIndex, InstanceCustomData);
// 	}
//
// 	FORCEINLINE FStaticMeshInstanceData* GetInstanceData() const
// 	{
// 		return InstanceData.Get();
// 	}
//
// 	// FRenderResource interface.
// 	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
// 	virtual void ReleaseRHI() override;
// 	virtual void InitResource(FRHICommandListBase& RHICmdList) override;
// 	virtual void ReleaseResource() override;
// 	virtual FString GetFriendlyName() const override { return TEXT("Static-mesh instances"); }
// 	SIZE_T GetResourceSize() const;
//
// 	void BindInstanceVertexBuffer(const class FVertexFactory* VertexFactory,
// 	                              struct FInstancedStaticMeshDataType& InstancedStaticMeshData) const;
//
// 	/**
// 	 * Call to flush any pending GPU data copies, if bFlushToGPUPending is false it does nothing. Should be called by the Proxy on the render thread
// 	 * for example in CreateRenderThreadResources().
// 	 */
// 	void FlushGPUUpload(FRHICommandListBase& RHICmdList);
//
// public:
// 	/** The vertex data storage type */
// 	TSharedPtr<FStaticMeshInstanceData, ESPMode::ThreadSafe> InstanceData;
//
// 	/** Keep CPU copy of instance data */
// 	bool RequireCPUAccess;
//
// 	FBufferRHIRef GetInstanceOriginBuffer()
// 	{
// 		check(!bFlushToGPUPending);
// 		return InstanceOriginBuffer.VertexBufferRHI;
// 	}
//
// 	FBufferRHIRef GetInstanceTransformBuffer()
// 	{
// 		check(!bFlushToGPUPending);
// 		return InstanceTransformBuffer.VertexBufferRHI;
// 	}
//
// 	FBufferRHIRef GetInstanceLightmapBuffer()
// 	{
// 		check(!bFlushToGPUPending);
// 		return InstanceLightmapBuffer.VertexBufferRHI;
// 	}
//
// 	/**
// 	 * Set flush to GPU as pending.
// 	 */
// 	void SetFlushToGPUPending()
// 	{
// 		bFlushToGPUPending = true;
// 	}
//
// private:
// 	/** If true, then we have updates to the host data not yet committed to the GPU. This in turn means
// 	 * that bDeferGPUUpload is true, and the Proxy is expected to either call FlushGPUUpload() OR never 
// 	 * use the instance data buffers (either is fine).
// 	 */
// 	bool bFlushToGPUPending;
//
// 	class FInstanceOriginBuffer : public FVertexBuffer
// 	{
// 		virtual FString GetFriendlyName() const override { return TEXT("FInstanceOriginBuffer"); }
// 	} InstanceOriginBuffer;
//
// 	FShaderResourceViewRHIRef InstanceOriginSRV;
//
// 	class FInstanceTransformBuffer : public FVertexBuffer
// 	{
// 		virtual FString GetFriendlyName() const override { return TEXT("FInstanceTransformBuffer"); }
// 	} InstanceTransformBuffer;
//
// 	FShaderResourceViewRHIRef InstanceTransformSRV;
//
// 	class FInstanceLightmapBuffer : public FVertexBuffer
// 	{
// 		virtual FString GetFriendlyName() const override { return TEXT("FInstanceLightmapBuffer"); }
// 	} InstanceLightmapBuffer;
//
// 	FShaderResourceViewRHIRef InstanceLightmapSRV;
//
// 	class FInstanceCustomDataBuffer : public FVertexBuffer
// 	{
// 		virtual FString GetFriendlyName() const override { return TEXT("FInstanceCustomDataBuffer"); }
// 	} InstanceCustomDataBuffer;
//
// 	FShaderResourceViewRHIRef InstanceCustomDataSRV;
//
// 	/** Delete existing resources */
// 	void CleanUp();
//
// 	void CreateVertexBuffer(FRHICommandListBase& RHICmdList, FResourceArrayInterface* InResourceArray,
// 	                        EBufferUsageFlags InUsage, uint32 InStride, uint8 InFormat,
// 	                        FBufferRHIRef& OutVertexBufferRHI, FShaderResourceViewRHIRef& OutInstanceSRV);
// };
//
// /*-----------------------------------------------------------------------------
// 	FInstancedStaticMeshVertexFactory
// -----------------------------------------------------------------------------*/
//
// struct FInstancingUserData
// {
// 	class FInstancedStaticMeshRenderData* RenderData;
// 	class FStaticMeshRenderData* MeshRenderData;
//
// 	int32 MinDrawDistance;
// 	int32 StartCullDistance;
// 	int32 EndCullDistance;
//
// 	float LODDistanceScale;
//
// 	int32 MinLOD;
//
// 	bool bRenderSelected;
// 	bool bRenderUnselected;
// 	FVector AverageInstancesScale;
// 	FVector InstancingOffset;
// };
//
// struct FInstancedStaticMeshDataType
// {
// 	/** The stream to read the mesh transform from. */
// 	FVertexStreamComponent InstanceOriginComponent;
//
// 	/** The stream to read the mesh transform from. */
// 	FVertexStreamComponent InstanceTransformComponent[3];
//
// 	/** The stream to read the Lightmap Bias and Random instance ID from. */
// 	FVertexStreamComponent InstanceLightmapAndShadowMapUVBiasComponent;
//
// 	FRHIShaderResourceView* InstanceOriginSRV = nullptr;
// 	FRHIShaderResourceView* InstanceTransformSRV = nullptr;
// 	FRHIShaderResourceView* InstanceLightmapSRV = nullptr;
// 	FRHIShaderResourceView* InstanceCustomDataSRV = nullptr;
//
// 	int32 NumCustomDataFloats = 0;
// };
//
// /**
//  * A vertex factory for instanced static meshes
//  */
// struct  FInstancedVisMeshVertexFactory : public FLocalVertexFactory
// {
// 	DECLARE_VERTEX_FACTORY_TYPE(FInstancedVisMeshVertexFactory);
//
// public:
// 	FInstancedVisMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
// 		: FLocalVertexFactory(InFeatureLevel, "FInstancedVisMeshVertexFactory")
// 	{
// 	}
//
// 	/**
// 	 * Should we cache the material's shadertype on this platform with this vertex factory? 
// 	 */
// 	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);
//
// 	/**
// 	 * Modify compile environment to enable instancing
// 	 * @param OutEnvironment - shader compile environment to modify
// 	 */
// 	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters,
// 	                                         FShaderCompilerEnvironment& OutEnvironment);
//
// 	/**
// 	 * Get vertex elements used when during PSO precaching materials using this vertex factory type
// 	 */
// 	static void GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType,
// 	                                              FVertexDeclarationElementList& Elements);
// 	static void GetVertexElements(ERHIFeatureLevel::Type FeatureLevel, EVertexInputStreamType InputStreamType,
// 	                              bool bSupportsManualVertexFetch, FDataType& Data,
// 	                              FInstancedStaticMeshDataType& InstanceData, FVertexDeclarationElementList& Elements);
//
// 	static void InitInstancedVisMeshVertexFactoryComponents(
// 		const FStaticMeshVertexBuffers& VertexBuffers,
// 		const FColorVertexBuffer* ColorVertexBuffer,
// 		const FVisMeshInstanceBuffer* InstanceBuffer,
// 		const FInstancedVisMeshVertexFactory* VertexFactory,
// 		int32 LightMapCoordinateIndex,
// 		bool bRHISupportsManualVertexFetch,
// 		FInstancedVisMeshVertexFactory::FDataType& OutData,
// 		FInstancedStaticMeshDataType& OutInstanceData);
//
// 	/**
// 	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
// 	 */
// 	void SetData(FRHICommandListBase& RHICmdList, const FDataType& InData,
// 	             const FInstancedStaticMeshDataType* InInstanceData)
// 	{
// 		Data = InData;
// 		if (InInstanceData)
// 		{
// 			InstanceData = *InInstanceData;
// 		}
// 		UpdateRHI(RHICmdList);
// 	}
//
// 	/**
// 	 * Copy the data from another vertex factory
// 	 * @param Other - factory to copy from
// 	 */
// 	void Copy(const FInstancedVisMeshVertexFactory& Other);
//
// 	// FRenderResource interface.
// 	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
//
// 	/** Make sure we account for changes in the signature of GetStaticBatchElementVisibility() */
// 	static constexpr uint32 NumBitsForVisibilityMask()
// 	{
// 		return 8 * sizeof(uint64);
// 	}
//
// 	inline FRHIShaderResourceView* GetInstanceOriginSRV() const
// 	{
// 		return InstanceData.InstanceOriginSRV;
// 	}
//
// 	inline FRHIShaderResourceView* GetInstanceTransformSRV() const
// 	{
// 		return InstanceData.InstanceTransformSRV;
// 	}
//
// 	inline FRHIShaderResourceView* GetInstanceLightmapSRV() const
// 	{
// 		return InstanceData.InstanceLightmapSRV;
// 	}
//
// 	inline FRHIShaderResourceView* GetInstanceCustomDataSRV() const
// 	{
// 		return InstanceData.InstanceCustomDataSRV;
// 	}
//
// 	FRHIUniformBuffer* GetUniformBuffer() const
// 	{
// 		return UniformBuffer.GetReference();
// 	}
//
// protected:
// 	static void GetVertexElements(ERHIFeatureLevel::Type FeatureLevel, EVertexInputStreamType InputStreamType,
// 	                              bool bSupportsManualVertexFetch, FDataType& Data,
// 	                              FInstancedStaticMeshDataType& InstanceData, FVertexDeclarationElementList& Elements,
// 	                              FVertexStreamList& Streams);
//
// private:
// 	FInstancedStaticMeshDataType InstanceData;
//
// 	TUniformBufferRef<FInstancedVisMeshVertexFactoryUniformShaderParameters> UniformBuffer;
// };
//
// class  FInstancedVisMeshVertexFactoryShaderParameters : public FLocalVertexFactoryShaderParametersBase
// {
// 	DECLARE_TYPE_LAYOUT(FInstancedVisMeshVertexFactoryShaderParameters, NonVirtual);
//
// public:
// 	void Bind(const FShaderParameterMap& ParameterMap)
// 	{
// 		FLocalVertexFactoryShaderParametersBase::Bind(ParameterMap);
//
// 		InstancingOffsetParameter.Bind(ParameterMap, TEXT("InstancingOffset"));
// 		VertexFetch_InstanceOriginBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceOriginBuffer"));
// 		VertexFetch_InstanceTransformBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceTransformBuffer"));
// 		VertexFetch_InstanceLightmapBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceLightmapBuffer"));
// 		InstanceOffset.Bind(ParameterMap, TEXT("InstanceOffset"));
// 	}
//
// 	void GetElementShaderBindings(
// 		const class FSceneInterface* Scene,
// 		const FSceneView* View,
// 		const FMeshMaterialShader* Shader,
// 		const EVertexInputStreamType InputStreamType,
// 		ERHIFeatureLevel::Type FeatureLevel,
// 		const FVertexFactory* VertexFactory,
// 		const FMeshBatchElement& BatchElement,
// 		FMeshDrawSingleShaderBindings& ShaderBindings,
// 		FVertexInputStreamArray& VertexStreams
// 	) const;
//
// private:
// 	LAYOUT_FIELD(FShaderParameter, InstancingOffsetParameter);
// 	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstanceOriginBufferParameter)
// 	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstanceTransformBufferParameter)
// 	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstanceLightmapBufferParameter)
// 	LAYOUT_FIELD(FShaderParameter, InstanceOffset)
// };
