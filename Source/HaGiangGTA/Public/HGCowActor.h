#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGCowActor.generated.h"

class UBoxComponent;
class USoundBase;
class UStaticMeshComponent;

/**
 * Greybox cow that stands in the road and wanders slowly around its home
 * spot, following the terrain. Blocks traffic (Pawn object type, so the AI
 * drivers brake for it) and moos when the player gets close.
 */
UCLASS()
class HAGIANGGTA_API AHGCowActor : public AActor
{
	GENERATED_BODY()

public:
	AHGCowActor();

	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cow")
	float WanderRadius = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cow")
	float WalkSpeed = 45.f;

	/** Never wander onto ground steeper than this (degrees) or into water. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cow")
	float MaxWanderSlopeDeg = 35.f;

	/** Hmong pack horse (Doc Tham Ma "horse-testing slope"): no horns/hump, raised neck, no moo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cow")
	bool bHorse = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cow")
	FLinearColor CoatColor = FLinearColor(0.32f, 0.17f, 0.07f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cow")
	TSoftObjectPtr<USoundBase> MooSound;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UBoxComponent> Body;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> Visual;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Head;

private:
	UStaticMeshComponent* AddPart(FName Name, UStaticMesh* Mesh, USceneComponent* Parent, const FVector& Loc, const FVector& SizeCm, bool bCoat);
	void PickNewTarget();

	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> CoatParts;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> DarkParts;
	UPROPERTY(Transient) TObjectPtr<USoundBase> LoadedMoo;

	FVector Home = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	float IdleTime = 0.f;
	float MooCooldown = 0.f;
	float Phase = 0.f;
};
