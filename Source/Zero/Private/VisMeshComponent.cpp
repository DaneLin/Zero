// Copyright ZJU CAD. All Rights Reserved.

#include "VisMeshComponent.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "DynamicMeshBuilder.h"
#include "MaterialDomain.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "Engine/InstancedStaticMesh.h"
#include "Materials/MaterialRenderProxy.h"
#include "VisMeshInstanceSceneProxy.h"
#include "PhysicsEngine/BodySetup.h"



#include UE_INLINE_GENERATED_CPP_BY_NAME(VisMeshComponent)

DECLARE_STATS_GROUP(TEXT("VisMesh"), STATGROUP_VisMesh, STATCAT_Advanced);

// Cycle 统计项
DECLARE_CYCLE_STAT(TEXT("Create VisMesh Proxy"), STAT_VisMesh_CreateSceneProxy, STATGROUP_VisMesh);
DECLARE_CYCLE_STAT(TEXT("Create Mesh Section"), STAT_VisMesh_CreateMeshSection, STATGROUP_VisMesh);
DECLARE_CYCLE_STAT(TEXT("UpdateSection GT"), STAT_VisMesh_UpdateSectionGT, STATGROUP_VisMesh);
DECLARE_CYCLE_STAT(TEXT("UpdateSection RT"), STAT_VisMesh_UpdateSectionRT, STATGROUP_VisMesh);
DECLARE_CYCLE_STAT(TEXT("Get VisMesh Elements"), STAT_VisMesh_GetMeshElements, STATGROUP_VisMesh);
DECLARE_CYCLE_STAT(TEXT("Update Collision"), STAT_VisMesh_UpdateCollision, STATGROUP_VisMesh);

DEFINE_LOG_CATEGORY_STATIC(LogVisComponent, Log, All);






/** Class representing a single section of the proc mesh */
class FVisMeshProxySection
{
public:
	/** Material applied to this section */
	UMaterialInterface* Material;
	/** Vertex buffer for this section */
	FStaticMeshVertexBuffers VertexBuffers;
	/** Index buffer for this section */
	FDynamicMeshIndexBuffer32 IndexBuffer;
	/** Vertex factory for this section */
	FLocalVertexFactory VertexFactory;
	/** Whether this section is currently visible */
	bool bSectionVisible;

	FVisMeshProxySection(ERHIFeatureLevel::Type InFeatureLevel)
		: Material(NULL)
		  , VertexFactory(InFeatureLevel, "FVisMeshProxySection")
		  , bSectionVisible(true)
	{
	}
};


/** 
 *	Struct used to send update to mesh data 
 *	Arrays may be empty, in which case no update is performed.
 */


static void ConvertProcMeshToDynMeshVertex(FDynamicMeshVertex& Vert, const FVisMeshVertex& ProcVert)
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

/** Vis mesh scene proxy */
class FVisMeshSceneProxy final : public FPrimitiveSceneProxy
{
public:
	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FVisMeshSceneProxy(UVisMeshComponent* Component)
		: FPrimitiveSceneProxy(Component)
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
				FVisMeshProxySection* NewSection = new FVisMeshProxySection(GetScene().GetFeatureLevel());

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
					ConvertProcMeshToDynMeshVertex(Vert, ProcVert);
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
	}

	virtual ~FVisMeshSceneProxy() override
	{
		for (FVisMeshProxySection* Section : Sections)
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

	void UpdateSection_RenderThread(FRHICommandListBase& RHICmdList, FVisMeshSectionUpdateData* SectionData)
	{
		SCOPE_CYCLE_COUNTER(STAT_VisMesh_UpdateSectionRT);

		// Check if we have data
		if (SectionData != nullptr)
		{
			// Check it references a valid section
			if (SectionData->TargetSection < Sections.Num() &&
				Sections[SectionData->TargetSection] != nullptr)
			{
				FVisMeshProxySection* Section = Sections[SectionData->TargetSection];
				// Lock vertex buffer
				const int32 NumVerts = SectionData->NewVertexBuffer.Num();

				// Iterate through vertex data, copying in new info
				for (int32 i = 0; i < NumVerts; i++)
				{
					const FVisMeshVertex& ProcVert = SectionData->NewVertexBuffer[i];
					FDynamicMeshVertex Vertex;
					ConvertProcMeshToDynMeshVertex(Vertex, ProcVert);

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
			}

			// Free data sent from game thread
			delete SectionData;
		}
	}

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
	                                    uint32 VisibilityMap, class FMeshElementCollector& Collector) const override
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

		for (const FVisMeshProxySection* Section : Sections)
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
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				// Draw simple collision as wireframe if 'show collision', and collision is enabled, and we are not using the complex as the simple
				if (ViewFamily.EngineShowFlags.Collision && IsCollisionEnabled() && BodySetup->GetCollisionTraceFlag()
					!= ECollisionTraceFlag::CTF_UseComplexAsSimple)
				{
					FTransform GeomTransform(GetLocalToWorld());
					BodySetup->AggGeom.GetAggGeom(GeomTransform,
					                              GetSelectionColor(FColor(157, 149, 223, 255), IsSelected(),
					                                                IsHovered()).ToFColor(true), NULL, false, false,
					                              AlwaysHasVelocity(), ViewIndex, Collector);
				}

				// Render bounds
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}
		}
#endif
	}

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
	TArray<FVisMeshProxySection*> Sections;

	UBodySetup* BodySetup;

	FMaterialRelevance MaterialRelevance;
};





UVisMeshComponent::UVisMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseComplexAsSimpleCollision = true;
}

void UVisMeshComponent::CreateMeshSection(int32 SectionIndex, const TArray<FVector>& Vertices,
                                          const TArray<int32>& Triangles, const TArray<FVector>& Normals,
                                          const TArray<FVector2D>& UV0,
                                          const TArray<FVector2D>& UV1, const TArray<FVector2D>& UV2,
                                          const TArray<FVector2D>& UV3,
                                          const TArray<FColor>& VertexColors, const TArray<FVisMeshTangent>& Tangents,
                                          bool bCreateCollision)
{
	SCOPE_CYCLE_COUNTER(STAT_VisMesh_CreateMeshSection);

	// Ensure sections array is long enough
	if (SectionIndex >= VisMeshSections.Num())
	{
		VisMeshSections.SetNum(SectionIndex + 1, false);
	}

	// Reset this section (in case it already existed)
	FVisMeshSection& NewSection = VisMeshSections[SectionIndex];
	NewSection.Reset();

	// Copy data to vertex buffer
	const int32 NumVerts = Vertices.Num();
	NewSection.ProcVertexBuffer.Reset();
	NewSection.ProcVertexBuffer.AddUninitialized(NumVerts);
	for (int32 VertIdx = 0; VertIdx < NumVerts; VertIdx++)
	{
		FVisMeshVertex& Vertex = NewSection.ProcVertexBuffer[VertIdx];

		Vertex.Position = Vertices[VertIdx];
		Vertex.Normal = (Normals.Num() == NumVerts) ? Normals[VertIdx] : FVector(0.f, 0.f, 1.f);
		Vertex.UV0 = (UV0.Num() == NumVerts) ? UV0[VertIdx] : FVector2D(0.f, 0.f);
		Vertex.UV1 = (UV1.Num() == NumVerts) ? UV1[VertIdx] : FVector2D(0.f, 0.f);
		Vertex.UV2 = (UV2.Num() == NumVerts) ? UV2[VertIdx] : FVector2D(0.f, 0.f);
		Vertex.UV3 = (UV3.Num() == NumVerts) ? UV3[VertIdx] : FVector2D(0.f, 0.f);
		Vertex.Color = (VertexColors.Num() == NumVerts) ? VertexColors[VertIdx] : FColor(255, 255, 255);
		Vertex.Tangent = (Tangents.Num() == NumVerts) ? Tangents[VertIdx] : FVisMeshTangent();

		// Update bounding box
		NewSection.SectionLocalBox += Vertex.Position;
	}

	// Get triangle indices, clamping to vertex range
	const int32 MaxIndex = NumVerts - 1;
	const auto GetTriIndices = [&Triangles, MaxIndex](int32 Idx)
	{
		return TTuple<int32, int32, int32>(FMath::Min(Triangles[Idx], MaxIndex),
		                                   FMath::Min(Triangles[Idx + 1], MaxIndex),
		                                   FMath::Min(Triangles[Idx + 2], MaxIndex));
	};

	const int32 NumTriIndices = (Triangles.Num() / 3) * 3; // Ensure number of triangle indices is multiple of three

	// Detect degenerate triangles, i.e. non-unique vertex indices within the same triangle
	int32 NumDegenerateTriangles = 0;
	for (int32 IndexIdx = 0; IndexIdx < NumTriIndices; IndexIdx += 3)
	{
		int32 a, b, c;
		Tie(a, b, c) = GetTriIndices(IndexIdx);
		NumDegenerateTriangles += a == b || a == c || b == c;
	}
	if (NumDegenerateTriangles > 0)
	{
		UE_LOG(LogVisComponent, Warning,
		       TEXT(
			       "Detected %d degenerate triangle%s with non-unique vertex indices for created mesh section in '%s'; degenerate triangles will be dropped."
		       ),
		       NumDegenerateTriangles, NumDegenerateTriangles > 1 ? TEXT("s") : TEXT(""), *GetFullName());
	}

	// Copy index buffer for non-degenerate triangles
	NewSection.ProcIndexBuffer.Reset();
	NewSection.ProcIndexBuffer.AddUninitialized(NumTriIndices - NumDegenerateTriangles * 3);
	int32 CopyIndexIdx = 0;
	for (int32 IndexIdx = 0; IndexIdx < NumTriIndices; IndexIdx += 3)
	{
		int32 a, b, c;
		Tie(a, b, c) = GetTriIndices(IndexIdx);

		if (a != b && a != c && b != c)
		{
			NewSection.ProcIndexBuffer[CopyIndexIdx++] = a;
			NewSection.ProcIndexBuffer[CopyIndexIdx++] = b;
			NewSection.ProcIndexBuffer[CopyIndexIdx++] = c;
		}
		else
		{
			--NumDegenerateTriangles;
		}
	}
	check(NumDegenerateTriangles == 0);
	check(CopyIndexIdx == NewSection.ProcIndexBuffer.Num());

	NewSection.bEnableCollision = bCreateCollision;

	UpdateLocalBounds(); // Update overall bounds
	UpdateCollision(); // Mark collision as dirty
	MarkRenderStateDirty(); // New section requires recreating scene proxy
}

void UVisMeshComponent::CreateMeshSection_LinearColor(int32 SectionIndex, const TArray<FVector>& Vertices,
                                                      const TArray<int32>& Triangles, const TArray<FVector>& Normals,
                                                      const TArray<FVector2D>& UV0,
                                                      const TArray<FVector2D>& UV1, const TArray<FVector2D>& UV2,
                                                      const TArray<FVector2D>& UV3,
                                                      const TArray<FLinearColor>& VertexColors,
                                                      const TArray<FVisMeshTangent>& Tangents, bool bCreateCollision,
                                                      bool bSRGBConversion)
{
	// Convert FLinearColors to FColors
	TArray<FColor> Colors;
	if (VertexColors.Num() > 0)
	{
		Colors.SetNum(VertexColors.Num());

		for (int32 ColorIdx = 0; ColorIdx < VertexColors.Num(); ColorIdx++)
		{
			Colors[ColorIdx] = VertexColors[ColorIdx].ToFColor(bSRGBConversion);
		}
	}

	CreateMeshSection(SectionIndex, Vertices, Triangles, Normals, UV0, UV1, UV2, UV3, Colors, Tangents, bCreateCollision);
}

void UVisMeshComponent::UpdateMeshSection(int32 SectionIndex, const TArray<FVector>& Vertices,
                                          const TArray<FVector>& Normals, const TArray<FVector2D>& UV0,
                                          const TArray<FVector2D>& UV1,
                                          const TArray<FVector2D>& UV2, const TArray<FVector2D>& UV3,
                                          const TArray<FColor>& VertexColors,
                                          const TArray<FVisMeshTangent>& Tangents)
{
	SCOPE_CYCLE_COUNTER(STAT_VisMesh_UpdateSectionGT);

	if (SectionIndex < VisMeshSections.Num())
	{
		FVisMeshSection& Section = VisMeshSections[SectionIndex];
		const int32 NumVerts = Vertices.Num();
		const int32 PreviousNumVerts = Section.ProcVertexBuffer.Num();

		// See if positions are changing
		const bool bSameVertexCount = PreviousNumVerts == NumVerts;

		if (bSameVertexCount)
		{
			Section.SectionLocalBox = Vertices.Num() ? FBox(Vertices) : FBox(ForceInit);

			// Iterate through vertex data, copying in new info
			for (int32 VertIdx = 0; VertIdx < NumVerts; ++VertIdx)
			{
				FVisMeshVertex& ModifyVert = Section.ProcVertexBuffer[VertIdx];

				// Position data
				if (Vertices.Num() == NumVerts)
				{
					ModifyVert.Position = Vertices[VertIdx];
				}

				// Normal data
				if (Normals.Num() == NumVerts)
				{
					ModifyVert.Normal = Normals[VertIdx];
				}

				// Tangent data
				if (Tangents.Num() == NumVerts)
				{
					ModifyVert.Tangent = Tangents[VertIdx];
				}

				// UV0 data
				if (UV0.Num() == NumVerts)
				{
					ModifyVert.UV0 = UV0[VertIdx];
				}
				// UV1 data
				if (UV1.Num() == NumVerts)
				{
					ModifyVert.UV1 = UV1[VertIdx];
				}
				// UV2 data
				if (UV2.Num() == NumVerts)
				{
					ModifyVert.UV2 = UV2[VertIdx];
				}
				// UV3 data
				if (UV3.Num() == NumVerts)
				{
					ModifyVert.UV3 = UV3[VertIdx];
				}

				// Color data
				if (VertexColors.Num() == NumVerts)
				{
					ModifyVert.Color = VertexColors[VertIdx];
				}
			}

			// If we have collision enabled on this section, update that too
			if (Section.bEnableCollision)
			{
				TArray<FVector> CollisionPositions;

				// We have one collision mesh for all sections, so need to build array of _all_ positions
				for (const FVisMeshSection& CollisionSection : VisMeshSections)
				{
					// If section has collision, copy it
					if (CollisionSection.bEnableCollision)
					{
						for (int32 VertIdx = 0; VertIdx < CollisionSection.ProcVertexBuffer.Num(); VertIdx++)
						{
							CollisionPositions.Add(CollisionSection.ProcVertexBuffer[VertIdx].Position);
						}
					}
				}

				// Pass new positions to trimesh
				BodyInstance.UpdateTriMeshVertices(CollisionPositions);
			}

			// If we have a valid proxy and it is not pending recreation
			if (SceneProxy && !IsRenderStateDirty())
			{
				// Create data to update section
				FVisMeshSectionUpdateData* SectionData = new FVisMeshSectionUpdateData;
				SectionData->TargetSection = SectionIndex;
				SectionData->NewVertexBuffer = Section.ProcVertexBuffer;

				// // Enqueue command to send to render thread
				if (bUseInstance)
				{
					FVisMeshInstancedSceneProxy* ProcMeshSceneProxy = (FVisMeshInstancedSceneProxy*)SceneProxy;
					ENQUEUE_RENDER_COMMAND(FVisMeshSectionUpdate)
					([ProcMeshSceneProxy, SectionData](FRHICommandListImmediate& RHICmdList)
					{
						ProcMeshSceneProxy->UpdateSection_RenderThread(RHICmdList, SectionData);
					});
				}
				else
				{
					FVisMeshSceneProxy* ProcMeshSceneProxy = (FVisMeshSceneProxy*)SceneProxy;
					ENQUEUE_RENDER_COMMAND(FVisMeshSectionUpdate)
					([ProcMeshSceneProxy, SectionData](FRHICommandListImmediate& RHICmdList)
					{
						ProcMeshSceneProxy->UpdateSection_RenderThread(RHICmdList, SectionData);
					});
				}
			}

			UpdateLocalBounds(); // Update overall bounds
			MarkRenderTransformDirty(); // Need to send new bounds to render thread
		}
		else
		{
			UE_LOG(LogVisComponent, Error,
			       TEXT(
				       "Trying to update a procedural mesh component section with a different number of vertices [Previous: %i, New: %i] (clear and recreate mesh section instead)"
			       ), PreviousNumVerts, NumVerts);
		}
	}
}

void UVisMeshComponent::UpdateMeshSection_LinearColor(int32 SectionIndex, const TArray<FVector>& Vertices,
                                                      const TArray<FVector>& Normals, const TArray<FVector2D>& UV0,
                                                      const TArray<FVector2D>& UV1,
                                                      const TArray<FVector2D>& UV2, const TArray<FVector2D>& UV3,
                                                      const TArray<FLinearColor>& VertexColors,
                                                      const TArray<FVisMeshTangent>& Tangents, bool bSRGBConversion)
{
	// Convert FLinearColors to FColors
	TArray<FColor> Colors;
	if (VertexColors.Num() > 0)
	{
		Colors.SetNum(VertexColors.Num());

		for (int32 ColorIdx = 0; ColorIdx < VertexColors.Num(); ColorIdx++)
		{
			Colors[ColorIdx] = VertexColors[ColorIdx].ToFColor(bSRGBConversion);
		}
	}

	UpdateMeshSection(SectionIndex, Vertices, Normals, UV0, UV1, UV2, UV3, Colors, Tangents);
}

// Called when the game starts
void UVisMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
}


// Called every frame
void UVisMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                      FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

void UVisMeshComponent::ClearMeshSection(int32 SectionIndex)
{
	if (SectionIndex < VisMeshSections.Num())
	{
		VisMeshSections[SectionIndex].Reset();
		UpdateLocalBounds();
		UpdateCollision();
		MarkRenderStateDirty();
	}
}

void UVisMeshComponent::ClearAllMeshSections()
{
	VisMeshSections.Empty();
	UpdateLocalBounds();
	UpdateCollision();
	MarkRenderStateDirty();
}

void UVisMeshComponent::SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility)
{
	if (SectionIndex < VisMeshSections.Num())
	{
		// Set game thread state
		VisMeshSections[SectionIndex].bSectionVisible = bNewVisibility;

		if (SceneProxy)
		{
			// Enqueue command to modify render thread info
			FVisMeshSceneProxy* ProcMeshSceneProxy = (FVisMeshSceneProxy*)SceneProxy;
			ENQUEUE_RENDER_COMMAND(FProcMeshSectionVisibilityUpdate)(
				[ProcMeshSceneProxy, SectionIndex, bNewVisibility](FRHICommandListImmediate& RHICmdList)
				{
					ProcMeshSceneProxy->SetSectionVisibility_RenderThread(SectionIndex, bNewVisibility);
				});
		}
	}
}

bool UVisMeshComponent::IsMeshSectionVisible(int32 SectionIndex) const
{
	return (SectionIndex < VisMeshSections.Num()) ? VisMeshSections[SectionIndex].bSectionVisible : false;
}

int32 UVisMeshComponent::GetNumSections() const
{
	return VisMeshSections.Num();
}

void UVisMeshComponent::AddCollisionConvexMesh(TArray<FVector> ConvexVerts)
{
	if (ConvexVerts.Num() >= 4)
	{
		// New element
		FKConvexElem NewConvexElem;
		// Copy in vertex info
		NewConvexElem.VertexData = ConvexVerts;
		// Update bounding box
		NewConvexElem.ElemBox = FBox(ConvexVerts);
		// Add to array of convex elements
		CollisionConvexElems.Add(NewConvexElem);
		// Refresh collision
		UpdateCollision();
	}
}

void UVisMeshComponent::ClearCollisionConvexMeshes()
{
	// Empty simple collision info
	CollisionConvexElems.Empty();
	// Refresh collision
	UpdateCollision();
}

void UVisMeshComponent::SetCollisionConvexMeshes(const TArray<TArray<FVector>>& ConvexMeshes)
{
	CollisionConvexElems.Reset();

	// Create element for each convex mesh
	for (int32 ConvexIndex = 0; ConvexIndex < ConvexMeshes.Num(); ConvexIndex++)
	{
		FKConvexElem NewConvexElem;
		NewConvexElem.VertexData = ConvexMeshes[ConvexIndex];
		NewConvexElem.ElemBox = FBox(NewConvexElem.VertexData);

		CollisionConvexElems.Add(NewConvexElem);
	}

	UpdateCollision();
}

bool UVisMeshComponent::GetTriMeshSizeEstimates(struct FTriMeshCollisionDataEstimates& OutTriMeshEstimates,
                                                bool bInUseAllTriData) const
{
	return IInterface_CollisionDataProvider::GetTriMeshSizeEstimates(OutTriMeshEstimates, bInUseAllTriData);
}

bool UVisMeshComponent::GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData)
{
	return IInterface_CollisionDataProvider::GetPhysicsTriMeshData(CollisionData, InUseAllTriData);
}

bool UVisMeshComponent::ContainsPhysicsTriMeshData(bool InUseAllTriData) const
{
	return IInterface_CollisionDataProvider::ContainsPhysicsTriMeshData(InUseAllTriData);
}

FVisMeshSection* UVisMeshComponent::GetVisMeshSection(int32 SectionIndex)
{
	if (SectionIndex < VisMeshSections.Num())
	{
		return &VisMeshSections[SectionIndex];
	}
	else
	{
		return nullptr;
	}
}

void UVisMeshComponent::SetVisMeshSection(int32 SectionIndex, const FVisMeshSection& Section)
{
	// Ensure sections array is long enough
	if (SectionIndex >= VisMeshSections.Num())
	{
		VisMeshSections.SetNum(SectionIndex + 1, false);
	}

	VisMeshSections[SectionIndex] = Section;

	UpdateLocalBounds(); // Update overall bounds
	UpdateCollision(); // Mark collision as dirty
	MarkRenderStateDirty(); // New section requires recreating scene proxy
}

void UVisMeshComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!PropertyChangedEvent.Property)
	{
		return;
	}

	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UVisMeshComponent, bUseInstance))
	{
		MarkRenderStateDirty();
	}
}

FPrimitiveSceneProxy* UVisMeshComponent::CreateSceneProxy()
{
	SCOPE_CYCLE_COUNTER(STAT_VisMesh_CreateSceneProxy);

	if (bUseInstance)
		return new FVisMeshInstancedSceneProxy(this);
	return new FVisMeshSceneProxy(this);
}

class UBodySetup* UVisMeshComponent::GetBodySetup()
{
	CreateVisMeshBodySetup();
	return VisMeshBodySetup;
}

UMaterialInterface* UVisMeshComponent::GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const
{
	UMaterialInterface* Result = nullptr;
	SectionIndex = 0;

	if (FaceIndex >= 0)
	{
		// Look for element that corresponds to the supplied face
		int32 TotalFaceCount = 0;
		for (int32 SectionIdx = 0; SectionIdx < VisMeshSections.Num(); SectionIdx++)
		{
			const FVisMeshSection& Section = VisMeshSections[SectionIdx];
			int32 NumFaces = Section.ProcIndexBuffer.Num() / 3;
			TotalFaceCount += NumFaces;

			if (FaceIndex < TotalFaceCount)
			{
				// Grab the material
				Result = GetMaterial(SectionIdx);
				SectionIndex = SectionIdx;
				break;
			}
		}
	}

	return Result;
}

int32 UVisMeshComponent::GetNumMaterials() const
{
	return VisMeshSections.Num();
}

void UVisMeshComponent::PostLoad()
{
	Super::PostLoad();

	if (VisMeshBodySetup && IsTemplate())
	{
		VisMeshBodySetup->SetFlags(RF_Public | RF_ArchetypeObject);
	}
}

FBoxSphereBounds UVisMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBoxSphereBounds Ret(LocalBounds.TransformBy(LocalToWorld));

	Ret.BoxExtent *= BoundsScale;
	Ret.SphereRadius *= BoundsScale;

	return Ret;
}

void UVisMeshComponent::UpdateLocalBounds()
{
	FBox LocalBox(ForceInit);

	for (const FVisMeshSection& Section : VisMeshSections)
	{
		LocalBox += Section.SectionLocalBox;
	}

	LocalBounds = LocalBox.IsValid
		              ? FBoxSphereBounds(LocalBox)
		              : FBoxSphereBounds(FVector::ZeroVector, FVector::ZeroVector, 0);
	// fallback to reset box sphere bounds

	// Update global bounds
	UpdateBounds();
	// Need to send to render thread
	MarkRenderTransformDirty();
}

void UVisMeshComponent::CreateVisMeshBodySetup()
{
	if (VisMeshBodySetup == nullptr)
	{
		VisMeshBodySetup = CreateBodySetupHelper();
	}
}

void UVisMeshComponent::UpdateCollision()
{
	SCOPE_CYCLE_COUNTER(STAT_VisMesh_UpdateCollision);

	UWorld* World = GetWorld();
	const bool bUseAsyncCook = World && World->IsGameWorld() && bUseAsyncCooking;

	if (bUseAsyncCook)
	{
		// Abort all previous ones still standing
		for (UBodySetup* OldBody : AsyncBodySetupQueue)
		{
			OldBody->AbortPhysicsMeshAsyncCreation();
		}

		AsyncBodySetupQueue.Add(CreateBodySetupHelper());
	}
	else
	{
		AsyncBodySetupQueue.Empty();
		//If for some reason we modified the async at runtime, just clear any pending async body setups
		CreateVisMeshBodySetup();
	}

	UBodySetup* UseBodySetup = bUseAsyncCook ? AsyncBodySetupQueue.Last() : VisMeshBodySetup;

	// Fill in simple collision convex elements
	UseBodySetup->AggGeom.ConvexElems = CollisionConvexElems;

	// Set trace flag
	UseBodySetup->CollisionTraceFlag = bUseComplexAsSimpleCollision ? CTF_UseComplexAsSimple : CTF_UseDefault;

	if (bUseAsyncCook)
	{
		UseBodySetup->CreatePhysicsMeshesAsync(
			FOnAsyncPhysicsCookFinished::CreateUObject(this, &UVisMeshComponent::FinishPhysicsAsyncCook, UseBodySetup));
	}
	else
	{
		// New GUID as collision has changed
		UseBodySetup->BodySetupGuid = FGuid::NewGuid();
		// Also we want cooked data for this
		UseBodySetup->bHasCookedCollisionData = true;
		UseBodySetup->InvalidatePhysicsData();
		UseBodySetup->CreatePhysicsMeshes();
		RecreatePhysicsState();
	}
}

void UVisMeshComponent::FinishPhysicsAsyncCook(bool bSuccess, UBodySetup* FinishedBodySetup)
{
	TArray<UBodySetup*> NewQueue;
	NewQueue.Reserve(AsyncBodySetupQueue.Num());

	int32 FoundIdx;
	if (AsyncBodySetupQueue.Find(FinishedBodySetup, FoundIdx))
	{
		if (bSuccess)
		{
			//The new body was found in the array meaning it's newer so use it
			VisMeshBodySetup = FinishedBodySetup;
			RecreatePhysicsState();

			//remove any async body setups that were requested before this one
			for (int32 AsyncIdx = FoundIdx + 1; AsyncIdx < AsyncBodySetupQueue.Num(); ++AsyncIdx)
			{
				NewQueue.Add(AsyncBodySetupQueue[AsyncIdx]);
			}

			AsyncBodySetupQueue = NewQueue;
		}
		else
		{
			AsyncBodySetupQueue.RemoveAt(FoundIdx);
		}
	}
}

UBodySetup* UVisMeshComponent::CreateBodySetupHelper()
{
	// The body setup in a template needs to be public since the property is Tnstanced and thus is the archetype of the instance meaning there is a direct reference
	UBodySetup* NewBodySetup = NewObject<UBodySetup>(this, NAME_None,
	                                                 (IsTemplate() ? RF_Public | RF_ArchetypeObject : RF_NoFlags));
	NewBodySetup->BodySetupGuid = FGuid::NewGuid();

	NewBodySetup->bGenerateMirroredCollision = false;
	NewBodySetup->bDoubleSidedGeometry = true;
	NewBodySetup->CollisionTraceFlag = bUseComplexAsSimpleCollision ? CTF_UseComplexAsSimple : CTF_UseDefault;

	return NewBodySetup;
}
