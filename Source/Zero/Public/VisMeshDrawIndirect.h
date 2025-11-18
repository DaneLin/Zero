#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderPermutation.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterUtils.h"

#define VISMESH_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT 64
#define VISMESH_DRAW_INDIRECT_ARGS_SIZE 5
#define VISMESH_DRAW_INDIRECT_TASK_INFO_SIZE 5
#define VISMESH_INIT_INSTANCE_COUNT_TASK_INFO_SIZE 2

enum class EVisMeshDrawIndirectArgGenTaskFlags : uint32
{
	None = 0,
	UseCulledCounts = 1 << 0,
	InstancedStereo = 1 << 1,
	PostOpaque = 1 << 2,
};

ENUM_CLASS_FLAGS(EVisMeshDrawIndirectArgGenTaskFlags);

/**
* Task info when generating draw indirect frame buffer.
* Task is either about generate Niagara renderers drawindirect buffer,
* or about resetting released instance counters.
*/
struct FVisMeshDrawIndirectArgGenTaskInfo
{
	explicit FVisMeshDrawIndirectArgGenTaskInfo(uint32 InInstanceCountBufferOffset, uint32 InNumIndicesPerInstance, uint32 InStartIndexLocation, EVisMeshDrawIndirectArgGenTaskFlags InFlags)
		: IndirectArgsBufferOffset(INDEX_NONE)
		, InstanceCountBufferOffset(InInstanceCountBufferOffset)
		, NumIndicesPerInstance(InNumIndicesPerInstance)
		, StartIndexLocation(InStartIndexLocation)
		, Flags((uint32)InFlags)
	{
	}
	
	bool operator==(const FVisMeshDrawIndirectArgGenTaskInfo& Rhs) const
	{
		return InstanceCountBufferOffset == Rhs.InstanceCountBufferOffset
			&& NumIndicesPerInstance == Rhs.NumIndicesPerInstance
			&& StartIndexLocation == Rhs.StartIndexLocation
			&& Flags == Rhs.Flags;
	}

	uint32 IndirectArgsBufferOffset;
	uint32 InstanceCountBufferOffset;
	uint32 NumIndicesPerInstance; // when -1 , the counter needs to be reset to 0
	uint32 StartIndexLocation;
	uint32 Flags;
};

/**
 * Compute shader used to generate GPU emitter draw indirect args.
 * It also resets unused instance count entries.
 */

class FVisMeshDrawIndirectArgsGenCS : public FGlobalShader
{
	DECLARE_EXPORTED_GLOBAL_SHADER(FVisMeshDrawIndirectArgsGenCS, ZERO_API);
	SHADER_USE_PARAMETER_STRUCT(FVisMeshDrawIndirectArgsGenCS, FGlobalShader);
	
public:
	class FSupportsTextureRW : SHADER_PERMUTATION_INT("SUPPORTS_TEXTURE_RW", 2);
	using FPermutationDomain = TShaderPermutationDomain<FSupportsTextureRW>;
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ZERO_API)
		SHADER_PARAMETER_SRV(Buffer<uint>,		TaskInfos)
		SHADER_PARAMETER_SRV(Buffer<uint>,		CulledInstanceCounts)

		SHADER_PARAMETER_UAV(RWBuffer<uint>,	RWInstanceCounts)
		SHADER_PARAMETER_UAV(RWBuffer<uint>,	RWDrawIndirectArgs)

		SHADER_PARAMETER(FUintVector4,			TaskCount)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
	
};

/**
 * Compute shader used to reset unused instance count entries.
 * Used if the platform doesn't support RW texture buffers
 */
class FVisMeshDrawIndirectResetCountsCS : public FGlobalShader
{
	DECLARE_EXPORTED_GLOBAL_SHADER(FVisMeshDrawIndirectResetCountsCS, ZERO_API);
	SHADER_USE_PARAMETER_STRUCT(FVisMeshDrawIndirectResetCountsCS, FGlobalShader);

public:
	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ZERO_API)
		SHADER_PARAMETER_SRV(Buffer<uint>,		TaskInfos)
		SHADER_PARAMETER_UAV(RWBuffer<uint>,	RWInstanceCounts)
		SHADER_PARAMETER(FUintVector4,			TaskCount)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
};

/**
 * Compute shader used to initialize instance count entries to specific values. Used if the platform doesn't support RW texture buffers
 */
class FVisMeshInstanceCountsInitCS : public FGlobalShader
{
	DECLARE_EXPORTED_GLOBAL_SHADER(FVisMeshInstanceCountsInitCS, ZERO_API);
	SHADER_USE_PARAMETER_STRUCT(FVisMeshInstanceCountsInitCS, FGlobalShader);

public:
	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ZERO_API)
		SHADER_PARAMETER_SRV(Buffer<uint>,		TaskInfos)
		SHADER_PARAMETER_UAV(RWBuffer<uint>,	RWInstanceCounts)
		SHADER_PARAMETER(FUintVector4,			TaskCount)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
};


