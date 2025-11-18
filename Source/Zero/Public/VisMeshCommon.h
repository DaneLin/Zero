#pragma once

/** Enum used to track stage that GPU compute proxies will execute in. */
UENUM()
namespace EVisMeshGpuComputeTickStage
{
	enum Type : int
	{
		PreInitViews,
		PostInitViews,
		PostOpaqueRender,
		Max UMETA(hidden),
		First = PreInitViews UMETA(hidden),
		Last = PostOpaqueRender UMETA(hidden),
	};
}

enum class EVisMeshResourceAccess : uint8
{
	ReadOnly,
	WriteOnly,
	ReadWrite,
};

UENUM()
enum class EVisMeshConditionalOperator
{
	Equals,
	NotEqual,
	LessThan,
	LessThanOrEqual,
	GreaterThan,
	GreaterThanOrEqual,

	Max UMETA(Hidden),
};

template<typename T>
bool EvalConditional(EVisMeshConditionalOperator Op, const T& A, const T& B)
{
	switch (Op)
	{
	case EVisMeshConditionalOperator::Equals: return A == B;
	case EVisMeshConditionalOperator::NotEqual: return A != B;
	case EVisMeshConditionalOperator::LessThan: return A < B;
	case EVisMeshConditionalOperator::LessThanOrEqual: return A <= B;
	case EVisMeshConditionalOperator::GreaterThan: return A > B;
	case EVisMeshConditionalOperator::GreaterThanOrEqual: return A >= B;
	default:check(0);
	};

	return false;
};