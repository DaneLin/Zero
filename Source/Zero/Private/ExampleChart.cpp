// Fill out your copyright notice in the Description page of Project Settings.


#include "ExampleChart.h"

#include "VisMeshComponent.h"


// Sets default values
AExampleChart::AExampleChart()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	VisMeshComponent = CreateDefaultSubobject<UVisMeshComponent>(TEXT("SceneRoot"));
}

// Called when the game starts or when spawned
void AExampleChart::BeginPlay()
{
	Super::BeginPlay();

	DrawSimpleTriangle();
}

// Called every frame
void AExampleChart::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AExampleChart::DrawSimpleTriangle()
{

	if (!VisMeshComponent) return;

	// 顶点：位于本地空间 XY 平面上，法线指向 +Z
	TArray<FVector> Vertices;
	Vertices.Add(FVector(0.f,   0.f,   0.f));
	Vertices.Add(FVector(100.f, 0.f,   0.f));
	Vertices.Add(FVector(0.f,   100.f, 0.f));

	// 单个三角形（注意绕序，若正面不可见可改成 {0,2,1}）
	TArray<int32> Triangles;
	Triangles.Add(0); Triangles.Add(2); Triangles.Add(1);

	// 法线
	TArray<FVector> Normals;
	Normals.Init(FVector(0.f, 0.f, 1.f), 3);

	// UV
	TArray<FVector2D> UV0;
	UV0.Add(FVector2D(0.f, 0.f));
	UV0.Add(FVector2D(1.f, 0.f));
	UV0.Add(FVector2D(0.f, 1.f));

	// 顶点色（可选）
	TArray<FColor> Colors;
	Colors.Add(FColor::Red);
	Colors.Add(FColor::Green);
	Colors.Add(FColor::Blue);

	// 切线（可选）
	TArray<FVisMeshTangent> Tangents;
	Tangents.Init(FVisMeshTangent(FVector(1.f, 0.f, 0.f), false), 3);

	if (Material)
	{
		VisMeshComponent->SetMaterial(0, Material);
	}
	
	// 创建第 0 个 Section；关闭碰撞以节省开销
	VisMeshComponent->CreateMeshSection(
		/*SectionIndex*/ 0,
		/*Vertices   */ Vertices,
		/*Triangles  */ Triangles,
		/*Normals    */ Normals,
		/*UV0        */ UV0,
		/*VertexColors*/ Colors,
		/*Tangents   */ Tangents,
		/*bCreateCollision*/ false
	);
}

