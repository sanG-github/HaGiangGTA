#pragma once

#include "CoreMinimal.h"
#include "HGVehicleBase.h"
#include "NhoQueBoatPawn.generated.h"

class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EHGBoatType : uint8
{
	/** Tourist motor boat (cano may du lich). */
	Cano,
	/** Two-seat paddle kayak. */
	Kayak
};

/**
 * Boat on the Nho Que river. Arcade water model: floats on the surface with
 * a buoyancy spring, bow lifts with speed/throttle, rolls into turns, slides
 * off the canyon walls and refuses to beach (needs MinDepth of water).
 */
UCLASS()
class HAGIANGGTA_API ANhoQueBoatPawn : public AHGVehicleBase
{
	GENERATED_BODY()

public:
	ANhoQueBoatPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual float GetSpeedKmh() const override { return Speed * 0.036f; }
	virtual bool IsWaterVehicle() const override { return true; }
	virtual void ResetToSpawn() override;

	/** [R] on the water: back onto the nearest point of the tourist boats' cruise line, facing along it the way
	 *  we were heading, stopped. Falls back to the spawn point when there is no cruise line. */
	void RecoverToRoute();

	/** Cruise line of any auto-cruising boat in the level (the Tu San route), or empty. */
	static const TArray<FVector>* FindCruiseLine(const UWorld* World);

	UFUNCTION(BlueprintPure, Category = "HaGiang|Boat")
	EHGBoatType GetBoatType() const { return BoatType; }

	UFUNCTION(BlueprintPure, Category = "HaGiang|Boat")
	bool IsFloating() const { return bFloating; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boat")
	EHGBoatType BoatType = EHGBoatType::Cano;

	/** Unpossessed boats with this on cruise back and forth along CruisePoints (tourist traffic). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boat|AI")
	bool bAutoCruise = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boat|AI")
	TArray<FVector> CruisePoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boat|AI")
	float CruiseThrottle = 0.5f;

	/** A cruise point counts as reached within this distance (cm). Must stay well under the point spacing
	 *  (~30 m) or the boat skips points and cuts across the bends of the gorge onto the banks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boat|AI")
	float CruiseAcceptRadius = 1200.f;

protected:
	virtual void HandleResetInput() override { RecoverToRoute(); }
	/** Chase camera for the player: orbit / look-back, never below the water surface. */
	void UpdateBoatCamera(float DeltaSeconds);
	virtual void BeginPlay() override;
	virtual FVector GetExitLocation() const override;

	void ApplyBoatType();
	UStaticMeshComponent* AddPart(FName Name, UStaticMesh* Mesh);
	void SetPart(UStaticMeshComponent* Part, bool bVisible, const FVector& Location = FVector::ZeroVector,
		const FRotator& Rotation = FRotator::ZeroRotator, const FVector& SizeCm = FVector(100.f));

	/** Everything visual pitches/rolls under this (the collision box stays level). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> HullRoot;

	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Hull;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Bow;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Deck;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Canopy;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> CanopyPostL;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> CanopyPostR;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Motor;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Paddle;
	/** Yellow star on the red roof: the tourist boats on the Nho Que fly the national flag on top. */
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> RoofStar;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Windows;

	// ---- Tuning (set from BoatType) ----
	UPROPERTY(EditAnywhere, Category = "Tuning") float MaxForwardSpeed = 1600.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float MaxReverseSpeed = 300.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float Acceleration = 500.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float WaterDrag = 0.35f;        // 1/s
	UPROPERTY(EditAnywhere, Category = "Tuning") float TurnRateDeg = 55.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float BoostMultiplier = 1.35f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float Draft = 25.f;              // collision centre below surface
	UPROPERTY(EditAnywhere, Category = "Tuning") float MinDepth = 70.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float BowLiftDeg = 7.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float TurnRollDeg = 9.f;

private:
	bool HasWaterDepthAt(const FVector& Location, float SurfaceZ) const;
	void ComputeCruiseInput(float& OutThrottle, float& OutSteer);
	int32 CruiseIndex = 0;
	int32 CruiseDir = 1;

	float Speed = 0.f;
	float HeadingYaw = 0.f;
	float VerticalVelocity = 0.f;
	float VisualPitch = 0.f;
	float VisualRoll = 0.f;
	float PaddlePhase = 0.f;
	bool bFloating = false;
	float LastBlockLogTime = -10.f;
};
