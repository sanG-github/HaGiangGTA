#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HGGreyboxCarPawn.generated.h"

class UBoxComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

/**
 * Checkpoint 01 greybox vehicle: a box "car" with arcade, trace-based
 * kinematic driving (no Chaos Vehicle setup needed). Follows slopes, keeps
 * its momentum off ramp crests, slides along walls and respawns with [R].
 *
 * Controls: W/S throttle-brake-reverse, A/D steer, Space handbrake,
 *           Left Shift boost, R reset. Gamepad: RT/LT, left stick.
 */
UCLASS()
class HAGIANGGTA_API AHGGreyboxCarPawn : public APawn
{
	GENERATED_BODY()

public:
	AHGGreyboxCarPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void UnPossessed() override;

	UFUNCTION(BlueprintPure, Category = "HaGiang|Vehicle")
	float GetSpeedKmh() const { return Speed * 0.036f; }

	UFUNCTION(BlueprintPure, Category = "HaGiang|Vehicle")
	bool IsGrounded() const { return bGrounded; }

	UFUNCTION(BlueprintCallable, Category = "HaGiang|Vehicle")
	void ResetToSpawn();

protected:
	virtual void BeginPlay() override;

	// ---- Components ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CabinMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	// ---- Tuning (cm, seconds, degrees) ----
	UPROPERTY(EditAnywhere, Category = "Tuning") float MaxForwardSpeed = 2800.f;      // ~100 km/h
	UPROPERTY(EditAnywhere, Category = "Tuning") float MaxReverseSpeed = 700.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float Acceleration = 1100.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float BrakeDeceleration = 2600.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float CoastDeceleration = 350.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float BoostMultiplier = 1.6f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float SteerRateDeg = 85.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float HighSpeedSteerScale = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float SlopeGravityScale = 0.6f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float GroundClearance = 12.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float GroundSnapDistance = 45.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float MaxStepUp = 60.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float MaxWalkableSlopeDeg = 40.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float AlignInterpSpeed = 10.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float BodyLeanDeg = 4.f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float KillZ = -20000.f;

private:
	void EnsureInputObjects();
	void OnReset(const FInputActionValue& Value);

	float ReadAxis(const UInputAction* Action) const;
	bool ReadButton(const UInputAction* Action) const;

	bool TraceGround(const FVector& From, FHitResult& OutHit) const;
	void UpdateSpeed(float DeltaSeconds, float Throttle, bool bHandbrake, bool bBoost, const FVector& MoveDir);
	void MoveWithCollision(const FVector& Delta, const FVector& MoveDir);
	void SnapToGround(const FVector& MoveDir);
	void UpdateOrientation(float DeltaSeconds, float Steer);

	float Speed = 0.f;              // signed, along heading
	float VerticalVelocity = 0.f;   // only used while airborne
	float HeadingYaw = 0.f;
	bool bGrounded = false;
	FVector GroundNormal = FVector::UpVector;
	FTransform SpawnTransform;

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> DrivingContext;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SteerAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HandbrakeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> BoostAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ResetAction;
};
