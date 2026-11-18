#pragma once

#include "CoreMinimal.h"
#include "HGVehicleBase.h"
#include "HGGroundVehicle.generated.h"

/**
 * Arcade, trace-based ground vehicle (no Chaos setup): follows slopes,
 * gravity along grades, launches off sharp crests, lands, slides along walls.
 * Shared by the CP01 box car and the Honda Win.
 */
UCLASS(Abstract)
class HAGIANGGTA_API AHGGroundVehicle : public AHGVehicleBase
{
	GENERATED_BODY()

public:
	AHGGroundVehicle();

	virtual void Tick(float DeltaSeconds) override;
	virtual float GetSpeedKmh() const override { return Speed * 0.036f; }
	virtual void ResetToSpawn() override;

	UFUNCTION(BlueprintPure, Category = "HaGiang|Vehicle")
	bool IsGrounded() const { return bGrounded; }

	/** Signed forward speed, cm/s. */
	float GetSpeed() const { return Speed; }

	/** Heading change rate, deg/s (for lean / passenger sway). */
	float GetYawRate() const { return YawRateDeg; }

	/** Multiplier on top speed and acceleration (Happy Water nitro, CP04). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float SpeedMultiplier = 1.f;

protected:
	virtual void BeginPlay() override;

	/** Hook for cosmetic animation after movement (lean, wheels...). */
	virtual void UpdateVisuals(float DeltaSeconds, float Steer) {}

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
	/** Fall this far below the spawn point and the vehicle respawns. */
	UPROPERTY(EditAnywhere, Category = "Tuning") float KillDepth = 150000.f;

	/** Last throttle input actually applied (for subclasses). */
	float LastThrottle = 0.f;

private:
	bool TraceGround(const FVector& From, FHitResult& OutHit) const;
	void UpdateSpeed(float DeltaSeconds, float Throttle, bool bHandbrake, bool bBoost, const FVector& MoveDir);
	void MoveWithCollision(const FVector& Delta, const FVector& MoveDir);
	void SnapToGround(float DeltaSeconds, const FVector& MoveDir);
	void UpdateOrientation(float DeltaSeconds);

	float Speed = 0.f;              // signed, along heading
	float VerticalVelocity = 0.f;   // only used while airborne
	float HeadingYaw = 0.f;
	float YawRateDeg = 0.f;
	bool bGrounded = false;
	FVector GroundNormal = FVector::UpVector;
};
