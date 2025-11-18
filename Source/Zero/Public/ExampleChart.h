// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExampleChart.generated.h"

class UVisMeshComponent;

UCLASS()
class ZERO_API AExampleChart : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AExampleChart();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category="Chart")
	void DrawSimpleTriangle();
	
	UFUNCTION(BlueprintCallable, Category="Chart")
	void DrawInstancedTriangle();
	
private:

	UPROPERTY(EditAnywhere, Category="Chart", meta=(AllowPrivateAccess = true))
	UMaterialInterface* Material;

	UPROPERTY(EditAnywhere, Category="Chart", meta=(AllowPrivateAccess = true))
	UVisMeshComponent* VisMeshComponent;

	UPROPERTY(EditAnywhere, Category="Chart", meta=(AllowPrivateAccess = true))
	bool bUseInstance;
};
