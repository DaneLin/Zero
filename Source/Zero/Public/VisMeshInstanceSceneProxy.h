#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshBuilder.h"
#include "RenderingThread.h"
#include "ShaderParameters.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include "PrimitiveViewRelevance.h"
#include "StaticMeshResources.h"
#include "StaticMeshSceneProxy.h"
#include "SplineMeshShaderParams.h"
#include "NaniteSceneProxy.h"
#include "Engine/InstancedStaticMesh.h"
#include "MeshMaterialShader.h"
#include "RenderGraphUtils.h"


class FColoredMaterialRenderProxy;
class UVisMeshComponent;
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FInstancedVisMeshVertexFactoryUniformShaderParameters, )
	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceOriginBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceTransformBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceLightmapBuffer)
	SHADER_PARAMETER_SRV(Buffer<float>, InstanceCustomDataBuffer)
	SHADER_PARAMETER(int32, NumCustomDataFloats)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

struct FInstancedVisMeshVertexFactory final : public FLocalVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE_API(FInstancedVisMeshVertexFactory, ZERO_API);

public:
	
	FInstancedVisMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
		: FLocalVertexFactory(InFeatureLevel, "FInstancedVisMeshVertexFactory")
	{
	}

	/**
	 * Should we cache the material's shadertype on this platform with this vertex factory? 
	 */
	ZERO_API static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	/**
	 * Modify compile environment to enable instancing
	 * @param OutEnvironment - shader compile environment to modify
	 */
	ZERO_API static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters,
	                                         FShaderCompilerEnvironment& OutEnvironment);

	/**
	 * Get vertex elements used when during PSO precaching materials using this vertex factory type
	 */
	ZERO_API static void GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType,
	                                              FVertexDeclarationElementList& Elements);
	ZERO_API static void GetVertexElements(ERHIFeatureLevel::Type FeatureLevel, EVertexInputStreamType InputStreamType, bool bSupportsManualVertexFetch, FDataType& Data, FInstancedStaticMeshDataType& InstanceData, FVertexDeclarationElementList& Elements);

	
	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(FRHICommandListBase& RHICmdList, const FDataType& InData,
	             const FInstancedStaticMeshDataType* InInstanceData)
	{
		Data = InData;
		if (InInstanceData)
		{
			InstanceData = *InInstanceData;
		}
		UpdateRHI(RHICmdList);
	}

	void SetInstanceData(FRHICommandListBase& RHICmdList, const FInstancedStaticMeshDataType& InInstanceData)
	{
		// 更新实例数据
		InstanceData = InInstanceData;
        
		// 这里的 Data 是父类 FLocalVertexFactory 的 protected 成员
		// 因为我们在类内部，所以可以直接读取它来重新初始化 RHI
		Data.PositionComponent = Data.PositionComponent; // (这行虽然没实际操作，但说明了Data是可访问的)
        
		// 调用 UpdateRHI 重建声明
		UpdateRHI(RHICmdList);
	}

	/**
	 * Copy the data from another vertex factory
	 * @param Other - factory to copy from
	 */
	void Copy(const FInstancedVisMeshVertexFactory& Other);
	
	// FRenderResource interface.
	ZERO_API virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

	/** Make sure we account for changes in the signature of GetStaticBatchElementVisibility() */
	static constexpr uint32 NumBitsForVisibilityMask()
	{
		return 8 * sizeof(uint64);
	}

	inline FRHIShaderResourceView* GetInstanceOriginSRV() const
	{
		return InstanceData.InstanceOriginSRV;
	}

	inline FRHIShaderResourceView* GetInstanceTransformSRV() const
	{
		return InstanceData.InstanceTransformSRV;
	}

	inline FRHIShaderResourceView* GetInstanceLightmapSRV() const
	{
		return InstanceData.InstanceLightmapSRV;
	}

	inline FRHIShaderResourceView* GetInstanceCustomDataSRV() const
	{
		return InstanceData.InstanceCustomDataSRV;
	}

	FRHIUniformBuffer* GetUniformBuffer() const
	{
		return UniformBuffer.GetReference();
	}

protected:
	static  ZERO_API void GetVertexElements(ERHIFeatureLevel::Type FeatureLevel, EVertexInputStreamType InputStreamType,
	                              bool bSupportsManualVertexFetch, FDataType& Data,
	                              FInstancedStaticMeshDataType& InstanceData, FVertexDeclarationElementList& Elements,
	                              FVertexStreamList& Streams);

private:
	FInstancedStaticMeshDataType InstanceData;

	TUniformBufferRef<FInstancedVisMeshVertexFactoryUniformShaderParameters> UniformBuffer;
};

// 修改这里 - 使用 FVertexFactoryShaderParameters 作为基类
class FInstancedVisMeshVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FInstancedVisMeshVertexFactoryShaderParameters, NonVirtual);

public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		InstancingOffsetParameter.Bind(ParameterMap, TEXT("InstancingOffset"));
		VertexFetch_InstanceOriginBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceOriginBuffer"));
		VertexFetch_InstanceTransformBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceTransformBuffer"));
		VertexFetch_InstanceLightmapBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceLightmapBuffer"));
		InstanceOffset.Bind(ParameterMap, TEXT("InstanceOffset"));
		
		// 绑定LocalVertexFactory的参数
		LODParameter.Bind(ParameterMap, TEXT("SplineMeshDir"));
		LODParameter.Bind(ParameterMap, TEXT("SplineMeshAxis"));
		LODParameter.Bind(ParameterMap, TEXT("SplineMeshMinZ"));
		LODParameter.Bind(ParameterMap, TEXT("SplineMeshScaleZ"));
	}

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const;

private:
	LAYOUT_FIELD(FShaderParameter, InstancingOffsetParameter);
	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstanceOriginBufferParameter)
	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstanceTransformBufferParameter)
	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstanceLightmapBufferParameter)
	LAYOUT_FIELD(FShaderParameter, InstanceOffset)
	LAYOUT_FIELD(FShaderParameter, LODParameter) // for LocalVertexFactory compatibility
};

// 简单的辅助类，用于管理Instance Buffer资源
class FVisMeshInstanceBuffer : public FVertexBuffer
{
public:

	void CreateVertexBuffer(FRHICommandListBase& RHICmdList, FResourceArrayInterface* InResourceArray, EBufferUsageFlags InUsage, uint32 InStride, uint8 InFormat, FBufferRHIRef& OutVertexBufferRHI, FShaderResourceViewRHIRef& OutInstanceSRV);
	
	
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

public:
	/** The vertex data storage type */
	TSharedPtr<FStaticMeshInstanceData, ESPMode::ThreadSafe> InstanceData;

private:
	class FInstanceOriginBuffer : public FVertexBuffer
	{
		virtual FString GetFriendlyName() const override { return TEXT("FInstanceOriginBuffer"); }
	} InstanceOriginBuffer;
	FShaderResourceViewRHIRef InstanceOriginSRV;

	class FInstanceTransformBuffer : public FVertexBuffer
	{
		virtual FString GetFriendlyName() const override { return TEXT("FInstanceTransformBuffer"); }
	} InstanceTransformBuffer;
	FShaderResourceViewRHIRef InstanceTransformSRV;

	class FInstanceLightmapBuffer : public FVertexBuffer
	{
		virtual FString GetFriendlyName() const override { return TEXT("FInstanceLightmapBuffer"); }
	} InstanceLightmapBuffer;
	FShaderResourceViewRHIRef InstanceLightmapSRV;

	class FInstanceCustomDataBuffer : public FVertexBuffer
	{
		virtual FString GetFriendlyName() const override { return TEXT("FInstanceCustomDataBuffer"); }
	} InstanceCustomDataBuffer;
	FShaderResourceViewRHIRef InstanceCustomDataSRV;	
};

/** Class representing a single section of the proc mesh */
class FVisMeshInstancedProxySection
{
public:
	/** Material applied to this section */
	UMaterialInterface* Material;
	/** Vertex buffer for this section */
	FStaticMeshVertexBuffers VertexBuffers;

	/** Index buffer for this section */
	FDynamicMeshIndexBuffer32 IndexBuffer;

	/** Vertex factory for this section */
	FInstancedVisMeshVertexFactory VertexFactory;
	/** Whether this section is currently visible */
	bool bSectionVisible;

	// Instance resources
	FVisMeshInstanceBuffer InstanceOriginBuffer;
	FVisMeshInstanceBuffer InstanceTransformBuffer;
	// [新增] Lightmap Buffer
	FVisMeshInstanceBuffer InstanceLightmapBuffer; 

	// 保存SRV传递给Factory
	FShaderResourceViewRHIRef InstanceOriginSRV;
	FShaderResourceViewRHIRef InstanceTransformSRV;
	// [新增] Lightmap SRV
	FShaderResourceViewRHIRef InstanceLightmapSRV;

	FVisMeshInstancedProxySection(ERHIFeatureLevel::Type InFeatureLevel)
		: Material(nullptr)
		  , VertexFactory(InFeatureLevel)
		  , bSectionVisible(true)
	{
	}

	~FVisMeshInstancedProxySection()
	{
		InstanceOriginBuffer.ReleaseResource();
		InstanceTransformBuffer.ReleaseResource();
		InstanceOriginSRV.SafeRelease();
		InstanceTransformSRV.SafeRelease();
		InstanceLightmapBuffer.ReleaseResource();
		InstanceLightmapSRV.SafeRelease();
	}
};


class FVisMeshInstancedSceneProxy final : public FPrimitiveSceneProxy
{
public:
	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FVisMeshInstancedSceneProxy(UVisMeshComponent* Component);
	

	virtual ~FVisMeshInstancedSceneProxy() override;

	void UpdateSection_RenderThread(FRHICommandListBase& RHICmdList, class FVisMeshSectionUpdateData* SectionData);

	virtual void CreateRenderThreadResources() override;
	
	
	void SetSectionVisibility_RenderThread(int32 SectionIndex, bool bNewVisibility)
	{
		check(IsInRenderingThread());

		if (SectionIndex < Sections.Num() &&
			Sections[SectionIndex] != nullptr)
		{
			Sections[SectionIndex]->bSectionVisible = bNewVisibility;
		}
	}

	// 收集每个view下每个LOD的FPrimitiveSceneProxy，并转换成FMeshBatch
	// 设置FMeshBatch中的FMeshBatchElement中的IndexBuffer, NumPrimitive, UniformBuffer等等关于渲染的东西
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
	                                    uint32 VisibilityMap, class FMeshElementCollector& Collector) const override;

	
	// 用于确定View渲染的相关性，可以认为是MeshPass的第一层过滤，用于确定是否参与某些特性的绘制
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bDynamicRelevance = true;
		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;
		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint(void) const override
	{
		return (sizeof(*this) + GetAllocatedSize());
	}

	uint32 GetAllocatedSize(void) const
	{
		return (FPrimitiveSceneProxy::GetAllocatedSize());
	}

private:
	// Array of sections
	TArray<FVisMeshInstancedProxySection*> Sections;

	int InstanceNum;

	UBodySetup* BodySetup;

	FMaterialRelevance MaterialRelevance;

	struct FInstancedStaticMeshDataType InstanceData;

	FShaderResourceViewRHIRef OriginSRV;
	FShaderResourceViewRHIRef TransformSRV;
};
