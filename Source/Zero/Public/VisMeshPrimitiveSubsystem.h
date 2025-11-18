// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/Subsystem.h"
#include "VisMeshPrimitiveSubsystem.generated.h"

class FComputeShaderManager;

/**
 * 
 */
UCLASS()
class ZERO_API UVisMeshPrimitiveSubsystem : public USubsystem
{
	GENERATED_BODY()

public:
	
private:
	FComputeShaderManager* InternalComputeShaderManager;
};
