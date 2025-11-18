// Copyright ZJU CAD. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "VisMeshComponent.generated.h"

struct FKConvexElem;

class FPrimitiveSceneProxy;

/**
*	Note: Codes from UvisMeshComponent.h
*	Struct used to specify a tangent vector for a vertex
*	The Y tangent is computed from the cross product of the vertex normal (Tangent Z) and the TangentX member.
*/
USTRUCT(BlueprintType)
struct FVisMeshTangent
{
	GENERATED_BODY()
public:

	/** Direction of X tangent for this vertex */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Tangent)
	FVector TangentX;

	/** Bool that indicates whether we should flip the Y tangent when we compute it using cross product */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Tangent)
	bool bFlipTangentY;

	FVisMeshTangent()
		: TangentX(1.f, 0.f, 0.f)
		, bFlipTangentY(false)
	{}

	FVisMeshTangent(float X, float Y, float Z)
		: TangentX(X, Y, Z)
		, bFlipTangentY(false)
	{}

	FVisMeshTangent(FVector InTangentX, bool bInFlipTangentY)
		: TangentX(InTangentX)
		, bFlipTangentY(bInFlipTangentY)
	{}
};

/** One vertex for the vis mesh, used for storing data internally */
USTRUCT(BlueprintType)
struct FVisMeshVertex
{
	GENERATED_BODY()
public:

	/** Vertex position */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector Position;

	/** Vertex normal */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector Normal;

	/** Vertex tangent */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVisMeshTangent Tangent;

	/** Vertex color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FColor Color;

	/** Vertex texture co-ordinate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector2D UV0;

	/** Vertex texture co-ordinate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector2D UV1;

	/** Vertex texture co-ordinate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector2D UV2;

	/** Vertex texture co-ordinate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector2D UV3;


	FVisMeshVertex()
		: Position(0.f, 0.f, 0.f)
		, Normal(0.f, 0.f, 1.f)
		, Tangent(FVector(1.f, 0.f, 0.f), false)
		, Color(255, 255, 255)
		, UV0(0.f, 0.f)
		, UV1(0.f, 0.f)
		, UV2(0.f, 0.f)
		, UV3(0.f, 0.f)
	{}
};

/** One section of the vis mesh. Each material has its own section. */
USTRUCT()
struct FVisMeshSection
{
	GENERATED_BODY()
public:

	/** Vertex buffer for this section */
	UPROPERTY()
	TArray<FVisMeshVertex> ProcVertexBuffer;

	/** Index buffer for this section */
	UPROPERTY()
	TArray<uint32> ProcIndexBuffer;
	/** Local bounding box of section */
	UPROPERTY()
	FBox SectionLocalBox;

	/** Should we build collision data for triangles in this section */
	UPROPERTY()
	bool bEnableCollision;

	/** Should we display this section */
	UPROPERTY()
	bool bSectionVisible;

	FVisMeshSection()
		: SectionLocalBox(ForceInit)
		, bEnableCollision(false)
		, bSectionVisible(true)
	{}

	/** Reset this section, clear all mesh info. */
	void Reset()
	{
		ProcVertexBuffer.Empty();
		ProcIndexBuffer.Empty();
		SectionLocalBox.Init();
		bEnableCollision = false;
		bSectionVisible = true;
	}
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FInstancedVisMeshVertexFactoryUniformShaderParameters, )
	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceOriginBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceTransformBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceLightmapBuffer)
	SHADER_PARAMETER_SRV(Buffer<float>, InstanceCustomDataBuffer)
	SHADER_PARAMETER(int32, NumCustomDataFloats)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FVisMeshInstanceBuffer : public FRenderResource
{
public:

	/** Default constructor. */
	 FVisMeshInstanceBuffer(ERHIFeatureLevel::Type InFeatureLevel, bool InRequireCPUAccess);

	/** Destructor. */
	 ~FVisMeshInstanceBuffer();

	/**
	 * Initializes the buffer with the component's data.
	 * @param Other - instance data, this call assumes the memory, so this will be empty after the call
	 */
	 void InitFromPreallocatedData(FStaticMeshInstanceData& Other);

	/**
	 * Specialized assignment operator, only used when importing LOD's. 
	 */
	 void operator=(const FVisMeshInstanceBuffer &Other);

	// Other accessors.
	FORCEINLINE uint32 GetNumInstances() const
	{
		return InstanceData->GetNumInstances();
	}

	FORCEINLINE void GetInstanceTransform(int32 InstanceIndex, FRenderTransform& Transform) const
	{
		InstanceData->GetInstanceTransform(InstanceIndex, Transform);
	}

	FORCEINLINE void GetInstanceRandomID(int32 InstanceIndex, float& RandomInstanceID) const
	{
		InstanceData->GetInstanceRandomID(InstanceIndex, RandomInstanceID);
	}

#if WITH_EDITOR
	FORCEINLINE void GetInstanceEditorData(int32 InstanceIndex, FColor& HitProxyColorOut, bool& bSelectedOut) const
	{
		InstanceData->GetInstanceEditorData(InstanceIndex, HitProxyColorOut, bSelectedOut);
	}
#endif 


	FORCEINLINE void GetInstanceLightMapData(int32 InstanceIndex, FVector4f& InstanceLightmapAndShadowMapUVBias) const
	{
		InstanceData->GetInstanceLightMapData(InstanceIndex, InstanceLightmapAndShadowMapUVBias);
	}
	
	FORCEINLINE void GetInstanceCustomDataValues(int32 InstanceIndex, TArray<float>& InstanceCustomData) const
	{
		InstanceData->GetInstanceCustomDataValues(InstanceIndex, InstanceCustomData);
	}
	
	FORCEINLINE FStaticMeshInstanceData* GetInstanceData() const
	{
		return InstanceData.Get();
	}

	// FRenderResource interface.
	 virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	 virtual void ReleaseRHI() override;
	 virtual void InitResource(FRHICommandListBase& RHICmdList) override;
	 virtual void ReleaseResource() override;
	virtual FString GetFriendlyName() const override { return TEXT("Static-mesh instances"); }
	SIZE_T GetResourceSize() const;

	 void BindInstanceVertexBuffer(const class FVertexFactory* VertexFactory, struct FInstancedVisMeshDataType& InstancedStaticMeshData) const;

	/**
	 * Call to flush any pending GPU data copies, if bFlushToGPUPending is false it does nothing. Should be called by the Proxy on the render thread
	 * for example in CreateRenderThreadResources().
	 */
	void FlushGPUUpload(FRHICommandListBase& RHICmdList);

public:
	/** The vertex data storage type */
	TSharedPtr<FStaticMeshInstanceData, ESPMode::ThreadSafe> InstanceData;

	/** Keep CPU copy of instance data */
	bool RequireCPUAccess;

	FBufferRHIRef GetInstanceOriginBuffer()
	{
		check(!bFlushToGPUPending);
		return InstanceOriginBuffer.VertexBufferRHI;
	}

	FBufferRHIRef GetInstanceTransformBuffer()
	{
		check(!bFlushToGPUPending);
		return InstanceTransformBuffer.VertexBufferRHI;
	}

	FBufferRHIRef GetInstanceLightmapBuffer()
	{
		check(!bFlushToGPUPending);
		return InstanceLightmapBuffer.VertexBufferRHI;
	}

	/**
	 * Set flush to GPU as pending.
	 */
	void SetFlushToGPUPending()
	{
			bFlushToGPUPending = true;
		}
private:

	/** If true, then we have updates to the host data not yet committed to the GPU. This in turn means
	 * that bDeferGPUUpload is true, and the Proxy is expected to either call FlushGPUUpload() OR never 
	 * use the instance data buffers (either is fine).
	 */
	bool bFlushToGPUPending;

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

	/** Delete existing resources */
	void CleanUp();

	void CreateVertexBuffer(FRHICommandListBase& RHICmdList, FResourceArrayInterface* InResourceArray, EBufferUsageFlags InUsage, uint32 InStride, uint8 InFormat, FBufferRHIRef& OutVertexBufferRHI, FShaderResourceViewRHIRef& OutInstanceSRV);
};

struct FInstancingVisMeshUserData
{
	class FInstancedVisMeshRenderData* RenderData;
	class FStaticMeshRenderData* MeshRenderData;

	int32 MinDrawDistance;
	int32 StartCullDistance;
	int32 EndCullDistance;

	float LODDistanceScale;

	int32 MinLOD;

	bool bRenderSelected;
	bool bRenderUnselected;
	FVector AverageInstancesScale;
	FVector InstancingOffset;
};

struct FInstancedVisMeshDataType
{
	/** The stream to read the mesh transform from. */
	FVertexStreamComponent InstanceOriginComponent;

	/** The stream to read the mesh transform from. */
	FVertexStreamComponent InstanceTransformComponent[3];

	/** The stream to read the Lightmap Bias and Random instance ID from. */
	FVertexStreamComponent InstanceLightmapAndShadowMapUVBiasComponent;

	FRHIShaderResourceView* InstanceOriginSRV = nullptr;
	FRHIShaderResourceView* InstanceTransformSRV = nullptr;
	FRHIShaderResourceView* InstanceLightmapSRV = nullptr;
	FRHIShaderResourceView* InstanceCustomDataSRV = nullptr;

	int32 NumCustomDataFloats = 0;
};

struct  FInstancedVisMeshVertexFactory : public FLocalVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FInstancedVisMeshVertexFactory);

public:
	
	FInstancedVisMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, const char* InDebugName)
		: FLocalVertexFactory(InFeatureLevel, InDebugName)
	{
	}

	/**
	 * Should we cache the material's shadertype on this platform with this vertex factory? 
	 */
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	/**
	 * Modify compile environment to enable instancing
	 * @param OutEnvironment - shader compile environment to modify
	 */
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters,
	                                         FShaderCompilerEnvironment& OutEnvironment);

	/**
	 * Get vertex elements used when during PSO precaching materials using this vertex factory type
	 */
	static void GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType,
	                                              FVertexDeclarationElementList& Elements);
	static void GetVertexElements(ERHIFeatureLevel::Type FeatureLevel, EVertexInputStreamType InputStreamType,
	                              bool bSupportsManualVertexFetch, FDataType& Data,
	                              FInstancedVisMeshDataType& InstanceData, FVertexDeclarationElementList& Elements);

	static void InitInstancedVisMeshVertexFactoryComponents(
		const FStaticMeshVertexBuffers& VertexBuffers,
		const FColorVertexBuffer* ColorVertexBuffer,
		const FVisMeshInstanceBuffer* InstanceBuffer,
		const FInstancedVisMeshVertexFactory* VertexFactory,
		int32 LightMapCoordinateIndex,
		bool bRHISupportsManualVertexFetch,
		FInstancedVisMeshVertexFactory::FDataType& OutData,
		FInstancedVisMeshDataType& OutInstanceData);

	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(FRHICommandListBase& RHICmdList, const FDataType& InData,
	             const FInstancedVisMeshDataType* InInstanceData)
	{
		Data = InData;
		if (InInstanceData)
		{
			InstanceData = *InInstanceData;
		}
		UpdateRHI(RHICmdList);
	}

	/**
	 * Copy the data from another vertex factory
	 * @param Other - factory to copy from
	 */
	void Copy(const FInstancedVisMeshVertexFactory& Other);

	// FRenderResource interface.
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

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
	static void GetVertexElements(ERHIFeatureLevel::Type FeatureLevel, EVertexInputStreamType InputStreamType,
	                              bool bSupportsManualVertexFetch, FDataType& Data,
	                              FInstancedVisMeshDataType& InstanceData, FVertexDeclarationElementList& Elements,
	                              FVertexStreamList& Streams);

private:
	FInstancedVisMeshDataType InstanceData;

	TUniformBufferRef<FInstancedVisMeshVertexFactoryUniformShaderParameters> UniformBuffer;
};

class  FInstancedVisMeshVertexFactoryShaderParameters : public FLocalVertexFactoryShaderParametersBase
{
	DECLARE_TYPE_LAYOUT(FInstancedVisMeshVertexFactoryShaderParameters, NonVirtual);

public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		FLocalVertexFactoryShaderParametersBase::Bind(ParameterMap);

		InstancingOffsetParameter.Bind(ParameterMap, TEXT("InstancingOffset"));
		VertexFetch_InstanceOriginBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceOriginBuffer"));
		VertexFetch_InstanceTransformBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceTransformBuffer"));
		VertexFetch_InstanceLightmapBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceLightmapBuffer"));
		InstanceOffset.Bind(ParameterMap, TEXT("InstanceOffset"));
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
};


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), ClassGroup= Rendering)
class ZERO_API UVisMeshComponent : public UMeshComponent, public IInterface_CollisionDataProvider
{
	GENERATED_BODY()

public:
	UVisMeshComponent(const FObjectInitializer& ObjectInitializer);

	/**
	 *	Create/replace a section for this vis mesh component.
	 *	This function is deprecated for Blueprints because it uses the unsupported 'Color' type. Use new 'Create Mesh Section' function which uses LinearColor instead.
	 *	@param	SectionIndex		Index of the section to create or replace.
	 *	@param	Vertices			Vertex buffer of all vertex positions to use for this mesh section.
	 *	@param	Triangles			Index buffer indicating which vertices make up each triangle. Length must be a multiple of 3.
	 *	@param	Normals				Optional array of normal vectors for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	UV0					Optional array of texture co-ordinates for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	VertexColors		Optional array of colors for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	Tangents			Optional array of tangent vector for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	bCreateCollision	Indicates whether collision should be created for this section. This adds significant cost.
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|VisMesh", meta = (DeprecatedFunction, DeprecationMessage = "This function is deprecated for Blueprints because it uses the unsupported 'Color' type. Use new 'Create Mesh Section' function which uses LinearColor instead.", DisplayName = "Create Mesh Section FColor", AutoCreateRefTerm = "Normals,UV0,VertexColors,Tangents"))
	void CreateMeshSection(int32 SectionIndex, const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const TArray<FVector2D>& UV0, const TArray<FColor>& VertexColors, const TArray<FVisMeshTangent>& Tangents, bool bCreateCollision)
	{
		TArray<FVector2D> EmptyArray;
		CreateMeshSection(SectionIndex, Vertices, Triangles, Normals, UV0, EmptyArray, EmptyArray, EmptyArray, VertexColors, Tangents, bCreateCollision);
	}

	void CreateMeshSection(int32 SectionIndex, const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const TArray<FVector2D>& UV0, const TArray<FVector2D>& UV1, const TArray<FVector2D>& UV2, const TArray<FVector2D>& UV3, const TArray<FColor>& VertexColors, const TArray<FVisMeshTangent>& Tangents, bool bCreateCollision);

	/**
	 *	Create/replace a section for this vis mesh component.
	 *	@param	SectionIndex		Index of the section to create or replace.
	 *	@param	Vertices			Vertex buffer of all vertex positions to use for this mesh section.
	 *	@param	Triangles			Index buffer indicating which vertices make up each triangle. Length must be a multiple of 3.
	 *	@param	Normals				Optional array of normal vectors for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	UV0					Optional array of texture co-ordinates for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	VertexColors		Optional array of colors for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	Tangents			Optional array of tangent vector for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	bCreateCollision	Indicates whether collision should be created for this section. This adds significant cost.
	 *	@param	bSRGBConversion		Whether to do sRGB conversion when converting FLinearColor to FColor
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|VisMesh", meta = (DisplayName = "Create Mesh Section", AutoCreateRefTerm = "Normals,UV0,UV1,UV2,UV3,VertexColors,Tangents", AdvancedDisplay = "UV1,UV2,UV3"))
	void CreateMeshSection_LinearColor(int32 SectionIndex, const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const TArray<FVector2D>& UV0, const TArray<FVector2D>& UV1, const TArray<FVector2D>& UV2, const TArray<FVector2D>& UV3,
		const TArray<FLinearColor>& VertexColors, const TArray<FVisMeshTangent>& Tangents, bool bCreateCollision, UPARAM(DisplayName = "SRGB Conversion") bool bSRGBConversion = false);

	void CreateMeshSection_LinearColor(int32 SectionIndex, const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const TArray<FVector2D>& UV0, const TArray<FLinearColor>& VertexColors, const TArray<FVisMeshTangent>& Tangents, bool bCreateCollision, bool bSRGBConversion = false)
	{
		TArray<FVector2D> EmptyArray;
		CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UV0, EmptyArray, EmptyArray, EmptyArray, VertexColors, Tangents, bCreateCollision, bSRGBConversion);
	}

		/**
	 *	Updates a section of this vis mesh component. This is faster than CreateMeshSection, but does not let you change topology. Collision info is also updated.
	 *	This function is deprecated for Blueprints because it uses the unsupported 'Color' type. Use new 'Create Mesh Section' function which uses LinearColor instead.
	 *	@param	Vertices			Vertex buffer of all vertex positions to use for this mesh section.
	 *	@param	Normals				Optional array of normal vectors for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	UV0					Optional array of texture co-ordinates for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	VertexColors		Optional array of colors for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	Tangents			Optional array of tangent vector for each vertex. If supplied, must be same length as Vertices array.
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|VisMesh", meta = (DeprecatedFunction, DeprecationMessage = "This function is deprecated for Blueprints because it uses the unsupported 'Color' type. Use new 'Update Mesh Section' function which uses LinearColor instead.", DisplayName = "Update Mesh Section FColor", AutoCreateRefTerm = "Normals,UV0,VertexColors,Tangents"))
	void UpdateMeshSection(int32 SectionIndex, const TArray<FVector>& Vertices, const TArray<FVector>& Normals, const TArray<FVector2D>& UV0, const TArray<FColor>& VertexColors, const TArray<FVisMeshTangent>& Tangents)
	{
		TArray<FVector2D> EmptyArray;
		UpdateMeshSection(SectionIndex, Vertices, Normals, UV0, EmptyArray, EmptyArray, EmptyArray, VertexColors, Tangents);
	}

	void UpdateMeshSection(int32 SectionIndex, const TArray<FVector>& Vertices, const TArray<FVector>& Normals, const TArray<FVector2D>& UV0, const TArray<FVector2D>& UV1, const TArray<FVector2D>& UV2, const TArray<FVector2D>& UV3, const TArray<FColor>& VertexColors, const TArray<FVisMeshTangent>& Tangents);

	/**
	 *	Updates a section of this vis mesh component. This is faster than CreateMeshSection, but does not let you change topology. Collision info is also updated.
	 *	@param	Vertices			Vertex buffer of all vertex positions to use for this mesh section.
	 *	@param	Normals				Optional array of normal vectors for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	UV0					Optional array of texture co-ordinates for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	VertexColors		Optional array of colors for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	Tangents			Optional array of tangent vector for each vertex. If supplied, must be same length as Vertices array.
	 *	@param	bSRGBConversion		Whether to do sRGB conversion when converting FLinearColor to FColor
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|VisMesh", meta = (DisplayName = "Update Mesh Section", AutoCreateRefTerm = "Normals,UV0,UV1,UV2,UV3,VertexColors,Tangents", AdvancedDisplay = "UV1,UV2,UV3"))
	void UpdateMeshSection_LinearColor(int32 SectionIndex, const TArray<FVector>& Vertices, const TArray<FVector>& Normals, const TArray<FVector2D>& UV0, const TArray<FVector2D>& UV1, const TArray<FVector2D>& UV2, const TArray<FVector2D>& UV3,
		const TArray<FLinearColor>& VertexColors, const TArray<FVisMeshTangent>& Tangents, UPARAM(DisplayName = "SRGB Conversion") bool bSRGBConversion = true);

	void UpdateMeshSection_LinearColor(int32 SectionIndex, const TArray<FVector>& Vertices, const TArray<FVector>& Normals, const TArray<FVector2D>& UV0, const TArray<FLinearColor>& VertexColors, const TArray<FVisMeshTangent>& Tangents, bool bSRGBConversion = true)
	{
		TArray<FVector2D> EmptyArray;
		UpdateMeshSection_LinearColor(SectionIndex, Vertices, Normals, UV0, EmptyArray, EmptyArray, EmptyArray, VertexColors, Tangents, bSRGBConversion);
	}


protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	/** Clear a section of the procedural mesh. Other sections do not change index. */
	UFUNCTION(BlueprintCallable, Category = "Components|VisMesh")
	void ClearMeshSection(int32 SectionIndex);

	/** Clear all mesh sections and reset to empty state */
	UFUNCTION(BlueprintCallable, Category = "Components|VisMesh")
	void ClearAllMeshSections();

	/** Control visibility of a particular section */
	UFUNCTION(BlueprintCallable, Category = "Components|VisMesh")
	void SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility);

	/** Returns whether a particular section is currently visible */
	UFUNCTION(BlueprintCallable, Category = "Components|VisMesh")
	bool IsMeshSectionVisible(int32 SectionIndex) const;

	/** Returns number of sections currently created for this component */
	UFUNCTION(BlueprintCallable, Category = "Components|VisMesh")
	int32 GetNumSections() const;

	/** Add simple collision convex to this component */
	UFUNCTION(BlueprintCallable, Category = "Components|VisMesh")
	void AddCollisionConvexMesh(TArray<FVector> ConvexVerts);

	/** Remove collision meshes from this component */
	UFUNCTION(BlueprintCallable, Category = "Components|VisMesh")
	void ClearCollisionConvexMeshes();

	/** Function to replace _all_ simple collision in one go */
	void SetCollisionConvexMeshes(const TArray< TArray<FVector> >& ConvexMeshes);

	//~ Begin Interface_CollisionDataProvider Interface
	virtual bool GetTriMeshSizeEstimates(struct FTriMeshCollisionDataEstimates& OutTriMeshEstimates, bool bInUseAllTriData) const override;
	virtual bool GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData) override;
	virtual bool ContainsPhysicsTriMeshData(bool InUseAllTriData) const override;
	virtual bool WantsNegXTriMesh() override{ return false; }
	//~ End Interface_CollisionDataProvider Interface
	
	/** 
	 *	Controls whether the complex (Per poly) geometry should be treated as 'simple' collision. 
	 *	Should be set to false if this component is going to be given simple collision and simulated.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vis Mesh")
	bool bUseComplexAsSimpleCollision;

	/**
	*	Controls whether the physics cooking should be done off the game thread. This should be used when collision geometry doesn't have to be immediately up to date (For example streaming in far away objects)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vis Mesh")
	bool bUseAsyncCooking;

	/** Collision data */
	UPROPERTY(Instanced)
	TObjectPtr<class UBodySetup> VisMeshBodySetup;

	/** 
	 *	Get pointer to internal data for one section of this vis mesh component. 
	 *	Note that pointer will becomes invalid if sections are added or removed.
	 */
	FVisMeshSection* GetVisMeshSection(int32 SectionIndex);

	/** Replace a section with new section geometry */
	void SetVisMeshSection(int32 SectionIndex, const FVisMeshSection& Section);

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual class UBodySetup* GetBodySetup() override;
	virtual UMaterialInterface* GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual int32 GetNumMaterials() const override;
	//~ End UMeshComponent Interface.

	//~ Begin UObject Interface
	virtual void PostLoad() override;
	//~ End UObject Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.

	// For instance usage
	UPROPERTY(EditAnywhere, Category = "Components|VisMesh");
	bool bUseInstance = false;

	UPROPERTY(EditAnywhere, Category = "Components|VisMesh");
	int InstanceNum = 10;

private:
	
	/** Update LocalBounds member from the local box of each section */
	void UpdateLocalBounds();
	/** Ensure ProcMeshBodySetup is allocated and configured */
	void CreateVisMeshBodySetup();
	/** Mark collision data as dirty, and re-create on instance if necessary */
	void UpdateCollision();
	/** Once async physics cook is done, create needed state */
	void FinishPhysicsAsyncCook(bool bSuccess, UBodySetup* FinishedBodySetup);

	/** Helper to create new body setup objects */
	UBodySetup* CreateBodySetupHelper();

	/** Array of sections of mesh */
	UPROPERTY()
	TArray<FVisMeshSection> VisMeshSections;

	/** Convex shapes used for simple collision */
	UPROPERTY()
	TArray<FKConvexElem> CollisionConvexElems;

	/** Local space bounds of mesh */
	UPROPERTY()
	FBoxSphereBounds LocalBounds;
	
	/** Queue for async body setups that are being cooked */
	UPROPERTY(transient)
	TArray<TObjectPtr<UBodySetup>> AsyncBodySetupQueue;

	friend class FVisMeshSceneProxy;
	friend class FVisMeshInstancedSceneProxy;
};
