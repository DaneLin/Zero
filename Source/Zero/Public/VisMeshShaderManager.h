#pragma once

#include "CoreMinimal.h"

struct FVisMeshArgGenInfo
{
	FVisMeshArgGenInfo(uint32 InNumIndicesPerInstance , uint32 InStartIndexLocation , uint32 InInstanceCount) 
		: NumIndicesPerInstance(InNumIndicesPerInstance)
		, InstanceCount(InInstanceCount)
		, StartIndexLocation(InStartIndexLocation)	
	{}

	uint32 NumIndicesPerInstance = 0;
	uint32 InstanceCount = 0;
	uint32 StartIndexLocation = 0;
	// TOOD: Check
	// BaseVertexLocation
	// StartInstanceLocation

	bool operator==(const FVisMeshArgGenInfo& Rhs) const
	{
		return NumIndicesPerInstance == Rhs.NumIndicesPerInstance
			&& InstanceCount == Rhs.InstanceCount
			&& StartIndexLocation == Rhs.StartIndexLocation;
	}
};

class FVisMeshShaderManager
{
public:
	FVisMeshShaderManager();
	FVisMeshShaderManager(ACameraActor* InCameraActor);
	~FVisMeshShaderManager();

	void InitRHI();
	void ReleaseRHI();

	bool bFlag = false;

	void InitInstanceBufferGenResource(FRHICommandListBase& RHICmdList);

	// TODO : using packed params
	// issue a drawindirect task, using unpacked params
	uint32 AddDrawIndirect(FRHICommandListBase& RHICmdList, uint32 NumIndicesPerInstance , uint32 InstanceCount , uint32 StartIndexLocation);

	// do the actually issue task
	void IssueDrawIndirectTask(FRHICommandList& RHICmdList , ERHIFeatureLevel::Type FeatureLevel);

	void IssueInstanceDataGenTask(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, float InThreshold, FRHITexture2D* InInputTexture);
	
	void IssueInstanceBufferGenTask(FRHICommandList& RHICmdList , ERHIFeatureLevel::Type FeatureLevel , uint32 InInstanceArgsCount);

	FRWBuffer& GetDrawIndirectBuffer(){return DrawIndirectBuffer;}

	FShaderResourceViewRHIRef GetInstanceOriginBufferSRV() {return InstanceOriginBuffer.SRV;}
	FShaderResourceViewRHIRef GetInstanceTransformBufferSRV() {return InstanceTransformBuffer.SRV;}

	void GetCameraMaxtrix();
protected:
	int32 OldInstanceCount = -1;
	FVisMeshArgGenInfo GenInfo;
	/* A buffer holding DrawIndirect data to render GPU emitter renderers. */
	FRWBuffer DrawIndirectBuffer;

	/* Buffer holds instance transform location */
	FRWBuffer InstanceOriginBuffer;
	
	/* Buffer holds instance transform matrix */
	FRWBuffer InstanceTransformBuffer;
	
	/* Buffer holds instance related args. */
	FRWBuffer InstanceArgsBuffer;

	/* Buffer holds possible position buffer */
	FRWBuffer DataGenPositionBuffer;

	/* Buffer holds Possible Instance count */
	FRWBuffer DataGenInstanceArgsBuffer;
	
	/* Num of instance arguments, Currently we only have 1 */
	uint32 InstanceArgsCount = 1;

	/* Camera */
	ACameraActor* CameraActor;
	FMatrix ProjectionMatrix ;
	FMatrix ViewMatrix;
	FVector CameraPosition;
};


