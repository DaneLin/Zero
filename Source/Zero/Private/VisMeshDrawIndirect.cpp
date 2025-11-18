#include "VisMeshDrawIndirect.h"

#include "ShaderParameterUtils.h"

IMPLEMENT_GLOBAL_SHADER(FVisMeshDrawIndirectArgsGenCS, "/ZeroPlugin/VisMeshDrawIndirectArgsGen.usf", "MainCS",SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVisMeshDrawIndirectResetCountsCS, "/ZeroPlugin/VisMeshDrawIndirectArgsGen.usf", "ResetCountsCS",SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVisMeshInstanceCountsInitCS, "/ZeroPlugin/VisMeshDrawIndirectArgsGen.usf", "InitInstanceCountsCS",SF_Compute);

void FVisMeshDrawIndirectArgsGenCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), VISMESH_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT);
	OutEnvironment.SetDefine(TEXT("VisMesh_DRAW_INDIRECT_ARGS_SIZE"), VISMESH_DRAW_INDIRECT_ARGS_SIZE);
	OutEnvironment.SetDefine(TEXT("VisMesh_DRAW_INDIRECT_TASK_INFO_SIZE"), VISMESH_DRAW_INDIRECT_TASK_INFO_SIZE);
	OutEnvironment.SetDefine(TEXT("VisMesh_INIT_INSTANCE_COUNT_TASK_INFO_SIZE"), VISMESH_INIT_INSTANCE_COUNT_TASK_INFO_SIZE);
}

void FVisMeshDrawIndirectResetCountsCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), VISMESH_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT);
	OutEnvironment.SetDefine(TEXT("VisMesh_DRAW_INDIRECT_ARGS_SIZE"), VISMESH_DRAW_INDIRECT_ARGS_SIZE);
	OutEnvironment.SetDefine(TEXT("VisMesh_DRAW_INDIRECT_TASK_INFO_SIZE"), VISMESH_DRAW_INDIRECT_TASK_INFO_SIZE);
	OutEnvironment.SetDefine(TEXT("VisMesh_INIT_INSTANCE_COUNT_TASK_INFO_SIZE"), VISMESH_INIT_INSTANCE_COUNT_TASK_INFO_SIZE);
}

void FVisMeshInstanceCountsInitCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), VISMESH_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT);
	OutEnvironment.SetDefine(TEXT("VisMesh_DRAW_INDIRECT_ARGS_SIZE"), VISMESH_DRAW_INDIRECT_ARGS_SIZE);
	OutEnvironment.SetDefine(TEXT("VisMesh_DRAW_INDIRECT_TASK_INFO_SIZE"), VISMESH_DRAW_INDIRECT_TASK_INFO_SIZE);
	OutEnvironment.SetDefine(TEXT("VisMesh_INIT_INSTANCE_COUNT_TASK_INFO_SIZE"), VISMESH_INIT_INSTANCE_COUNT_TASK_INFO_SIZE);
}