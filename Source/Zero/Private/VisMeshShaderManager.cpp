#include "VisMeshShaderManager.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "RenderGraphUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

constexpr uint32 MAX_COMPUTEINSTANCE_COUNT = 1000;


class  FInstanceDataGenCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FInstanceDataGenCS);
	SHADER_USE_PARAMETER_STRUCT(FInstanceDataGenCS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, RWOutputArgs)
		SHADER_PARAMETER_UAV(RWBuffer<float3>, RWOutputPositionBuffer)
		SHADER_PARAMETER_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputTextureSampler)
		SHADER_PARAMETER(float, Threshold)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters , OutEnvironment);
		OutEnvironment.SetDefine(TEXT("BUFFER_SAMPLE_X") , 32);
		OutEnvironment.SetDefine(TEXT("BUFFER_SAMPLE_Y") , 32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FInstanceDataGenCS , "/ZeroPlugin/InstanceDataGenCS.usf" , "MainCS" , SF_Compute);


class  FInstanceBufferGenCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FInstanceBufferGenCS);
	SHADER_USE_PARAMETER_STRUCT(FInstanceBufferGenCS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
		SHADER_PARAMETER(FMatrix44f, ProjectionMatrix)
		SHADER_PARAMETER(FMatrix44f , ViewMatrix)
		SHADER_PARAMETER(FVector3f , CameraPosition)
		SHADER_PARAMETER_UAV(RWBuffer<float4> ,RWInstanceOriginBuffer)
		SHADER_PARAMETER_UAV(RWBuffer<float4> ,RWInstanceTransformBuffer)
		SHADER_PARAMETER_UAV(RWBuffer<uint> ,RWCSInstanceArgs)
		// 从 InstanceDataGenCS中读取输出
		SHADER_PARAMETER_UAV(RWBuffer<uint>, RWGenCSInstanceCount)
		SHADER_PARAMETER_SRV(Buffer<float4> ,  PossibleInstancePos)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters , OutEnvironment);
		OutEnvironment.SetDefine(TEXT("BUFFER_GEN_THREAD_COUNT") , 256);
	}
};

IMPLEMENT_GLOBAL_SHADER(FInstanceBufferGenCS , "/ZeroPlugin/InstanceBuffersGenCS.usf" , "MainCS" , SF_Compute);

class  FDrawIndirectArgsGenCS:public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDrawIndirectArgsGenCS);
	SHADER_USE_PARAMETER_STRUCT(FDrawIndirectArgsGenCS, FGlobalShader);
public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, DrawIndirectArgs)
		SHADER_PARAMETER_UAV(RWBuffer<uint> , InstanceArgs)
		SHADER_PARAMETER(uint32 , NumIndicesPerInstance)
		SHADER_PARAMETER_UAV(RWBuffer<uint> , RWInstanceCounts)
	END_SHADER_PARAMETER_STRUCT()
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		Environment.SetDefine(TEXT("ARGS_GEN_THREAD_COUNT"),TEXT("1"));
	}
	
};

IMPLEMENT_GLOBAL_SHADER(FDrawIndirectArgsGenCS, "/ZeroPlugin/DrawIndirectArgsGenCS.usf", "MainCS",SF_Compute);

FVisMeshShaderManager::FVisMeshShaderManager()
	: GenInfo(FVisMeshArgGenInfo(0, 0, 0))
{
}

FVisMeshShaderManager::FVisMeshShaderManager(ACameraActor* InCameraActor)
	: GenInfo(FVisMeshArgGenInfo(0, 0, 0))
{
	CameraActor = InCameraActor;
	GetCameraMaxtrix();
}

FVisMeshShaderManager::~FVisMeshShaderManager()
{
	ReleaseRHI();
}

void FVisMeshShaderManager::InitRHI()
{
}

void FVisMeshShaderManager::ReleaseRHI()
{
	// DrawIndirect info
	DrawIndirectBuffer.Release();
}

void FVisMeshShaderManager::InitInstanceBufferGenResource(FRHICommandListBase& RHICmdList)
{
		
	TResourceArray<FVector4f> OriginInitData;
	OriginInitData.SetNumZeroed( MAX_COMPUTEINSTANCE_COUNT );
	InstanceOriginBuffer.Initialize(RHICmdList , TEXT("YH_CustomOriginBuffer") , sizeof(FVector4f) , MAX_COMPUTEINSTANCE_COUNT ,EPixelFormat::PF_A32B32G32R32F,
		BUF_Static | BUF_VertexBuffer | BUF_UnorderedAccess , &OriginInitData);
	
	TResourceArray<FVector4f> TransformInitData;
	TransformInitData.SetNumZeroed(MAX_COMPUTEINSTANCE_COUNT * 3);
	InstanceTransformBuffer.Initialize( RHICmdList , TEXT("YH_CustomTransformBuffer"), sizeof(FVector4f) , MAX_COMPUTEINSTANCE_COUNT * 3 ,EPixelFormat::PF_A32B32G32R32F,
		BUF_Static | BUF_VertexBuffer |BUF_UnorderedAccess , &TransformInitData);
	
	TResourceArray<uint32> CountResource;
	CountResource.SetNumZeroed(InstanceArgsCount);
	InstanceArgsBuffer.Initialize(RHICmdList , TEXT("YH_CustomInstanceCount") , sizeof(uint32) ,InstanceArgsCount ,EPixelFormat::PF_R32_UINT,
		BUF_Static | BUF_UnorderedAccess , &CountResource);

	TResourceArray<uint32> DataGenInstanceCount;
	DataGenInstanceCount.SetNumZeroed(InstanceArgsCount);
	DataGenInstanceArgsBuffer.Initialize(RHICmdList , TEXT("YH_CustomDataGenInstanceCount") , sizeof(uint32) ,InstanceArgsCount ,EPixelFormat::PF_R32_UINT,
		BUF_Static | BUF_UnorderedAccess , &DataGenInstanceCount);
	
	TResourceArray<FVector4f> DataGenOriginInitData;
	DataGenOriginInitData.SetNumZeroed( MAX_COMPUTEINSTANCE_COUNT );
	DataGenPositionBuffer.Initialize(RHICmdList , TEXT("YH_CustomDataGenOriginBuffer") , sizeof(FVector4f) , MAX_COMPUTEINSTANCE_COUNT ,EPixelFormat::PF_A32B32G32R32F,
		BUF_Static | BUF_VertexBuffer | BUF_UnorderedAccess , &DataGenOriginInitData);
}

uint32 FVisMeshShaderManager::AddDrawIndirect(FRHICommandListBase& RHICmdList, uint32 NumIndicesPerInstance,
                                              uint32 InstanceCount, uint32 StartIndexLocation)
{
	GenInfo.NumIndicesPerInstance = NumIndicesPerInstance;
	GenInfo.InstanceCount = InstanceCount;
	GenInfo.StartIndexLocation = StartIndexLocation;

	if (OldInstanceCount != InstanceCount)
	{
		bFlag = false;
		OldInstanceCount = InstanceCount;
	}

	// CPU 填充 UAV
	TResourceArray<uint32> InitData;
	InitData.Add(NumIndicesPerInstance);
	InitData.Add(InstanceCount);
	InitData.Add(StartIndexLocation);
	InitData.Add(0);
	InitData.Add(0);
	if (!bFlag)
	{
		DrawIndirectBuffer.Initialize(RHICmdList,TEXT("Test_DrawIndirectBuffer"),sizeof(uint32),5,PF_R32_UINT,BUF_Static|BUF_DrawIndirect,&InitData);
		bFlag = true;
	}
	return INDEX_NONE;
}

void FVisMeshShaderManager::IssueDrawIndirectTask(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel)
{
	UE_LOG(LogTemp, Warning, TEXT("IssueDrawIndirectTask Executed"));

	TShaderMapRef<FDrawIndirectArgsGenCS> DrawIndirectArgsGenCS(GetGlobalShaderMap(FeatureLevel) /*, PermutationVector*/);

	FDrawIndirectArgsGenCS::FParameters ArgsGenParameters;
	ArgsGenParameters.RWInstanceCounts = InstanceArgsBuffer.UAV;
	ArgsGenParameters.NumIndicesPerInstance = GenInfo.NumIndicesPerInstance;
	ArgsGenParameters.DrawIndirectArgs = DrawIndirectBuffer.UAV;

	RHICmdList.Transition( FRHITransitionInfo(DrawIndirectBuffer.UAV ,ERHIAccess::UAVCompute) );
	FComputeShaderUtils::Dispatch(RHICmdList , DrawIndirectArgsGenCS , ArgsGenParameters , FIntVector(1,1,1) );
	RHICmdList.Transition( FRHITransitionInfo(DrawIndirectBuffer.UAV ,ERHIAccess::IndirectArgs) );
}

void FVisMeshShaderManager::IssueInstanceDataGenTask(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel,
                                                     float InThreshold, FRHITexture2D* InInputTexture)
{
	// UE_LOG(LogTemp, Warning, TEXT("IssueInstanceDataGenTask Executed"));

	/*
	 * FInstanceDataGenCS 需要准备所有可能的位置写入到PositionBuffer中，并写入OutputArgs 代表 InstanceCount
	 * FInstanceBufferGenCS 作为最终的输出要对最后的结果做剔除
	 */
	TShaderMapRef<FInstanceDataGenCS> InstanceDataGenCS(GetGlobalShaderMap(FeatureLevel));
	FInstanceDataGenCS::FParameters DataGenParameters;
	DataGenParameters.Threshold = InThreshold;
	DataGenParameters.InputTexture = InInputTexture;
	DataGenParameters.InputTextureSampler = TStaticSamplerState<>::GetRHI();
	DataGenParameters.RWOutputArgs = DataGenInstanceArgsBuffer.UAV;
	DataGenParameters.RWOutputPositionBuffer = DataGenPositionBuffer.UAV;

	
	// Transition resources to UAVCompute state
	RHICmdList.Transition(FRHITransitionInfo(DataGenInstanceArgsBuffer.UAV, ERHIAccess::UAVCompute));
	RHICmdList.Transition(FRHITransitionInfo(DataGenPositionBuffer.UAV, ERHIAccess::UAVCompute));
    
	FComputeShaderUtils::Dispatch(RHICmdList, InstanceDataGenCS, DataGenParameters, FIntVector(1,1,1));
    
	// Transition resources to their next required state
	RHICmdList.Transition(FRHITransitionInfo(DataGenInstanceArgsBuffer.UAV, ERHIAccess::SRVCompute));
	RHICmdList.Transition(FRHITransitionInfo(DataGenPositionBuffer.UAV, ERHIAccess::SRVCompute));
}

void FVisMeshShaderManager::IssueInstanceBufferGenTask(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel,
                                                       uint32 InInstanceArgsCount)
{
	// UE_LOG(LogTemp, Warning, TEXT("IssueInstanceBufferGenTask Executed"));

	InstanceArgsCount = InInstanceArgsCount;
	
	TShaderMapRef<FInstanceBufferGenCS> InstanceBufferGenCS(GetGlobalShaderMap(FeatureLevel) /*, PermutationVedtor*/);
	
	FInstanceBufferGenCS::FParameters BufferGenParameters;
	BufferGenParameters.ProjectionMatrix = FMatrix44f(ProjectionMatrix);
	BufferGenParameters.ViewMatrix = FMatrix44f(ViewMatrix);
	BufferGenParameters.CameraPosition = FVector3f(CameraPosition.X , CameraPosition.Y , CameraPosition.Z);
	BufferGenParameters.RWInstanceOriginBuffer = InstanceOriginBuffer.UAV;
	BufferGenParameters.RWInstanceTransformBuffer = InstanceTransformBuffer.UAV;
	BufferGenParameters.RWCSInstanceArgs = InstanceArgsBuffer.UAV;

	BufferGenParameters.RWGenCSInstanceCount = DataGenInstanceArgsBuffer.UAV;
	BufferGenParameters.PossibleInstancePos = DataGenPositionBuffer.SRV;
	
	// Transition resources to UAVCompute state
	RHICmdList.Transition(FRHITransitionInfo(DataGenInstanceArgsBuffer.UAV, ERHIAccess::UAVCompute));
    
	FComputeShaderUtils::Dispatch(RHICmdList, InstanceBufferGenCS, BufferGenParameters, FIntVector(256,1,1));
}

void FVisMeshShaderManager::GetCameraMaxtrix()
{
	if(IsValid(CameraActor))
	{
		FMinimalViewInfo ViewInfo;
		CameraActor->GetCameraComponent()->GetCameraView(0.f , ViewInfo);

		//Projection Matrix
		ProjectionMatrix = ViewInfo.CalculateProjectionMatrix();

		FMatrix ViewRotationMatrix = FInverseRotationMatrix(ViewInfo.Rotation) * FMatrix(
		FPlane(0,	0,	1,	0),
		FPlane(1,	0,	0,	0),
		FPlane(0,	1,	0,	0),
		FPlane(0,	0,	0,	1));
		
		
		ViewMatrix = ViewRotationMatrix;

		CameraPosition = CameraActor->GetTransform().GetLocation();
	}
}
