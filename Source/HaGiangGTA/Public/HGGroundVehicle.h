#pragma once

#include "CoreMinimal.h"
#include "HGVehicleBase.h"
#include "HGGroundVehicle.generated.h"

class AHGRoadSplineActor;

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

	/** Multiplier on top speed and acceleration (Happy Water nitro is applied on top for the player). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	float SpeedMultiplier = 1.f;

	virtual void PossessedBy(AController* NewController) override;

	// ---- AI auto-drive along the road spline (convoys, trucks) ----

	/**
	 * Place the vehicle on the road at DistanceCm and drive it along the spline.
	 * Direction +1 = increasing distance (up the pass), -1 = decreasing. LaneOffsetCm > 0 = right of travel.
	 */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|AI")
	void StartAutoDrive(AHGRoadSplineActor* Road, float DistanceCm, int32 Direction, float CruiseKmh, float LaneOffsetCm);

	UFUNCTION(BlueprintCallable, Category = "HaGiang|AI")
	void StopAutoDrive();

	/** Follow this vehicle at a safe gap (convoy). */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|AI")
	void SetAutoDriveLeader(AHGGroundVehicle* Leader) { AutoDriveLeader = Leader; }

	UFUNCTION(BlueprintPure, Category = "HaGiang|AI")
	bool IsAutoDriving() const { return bAutoDrive; }

	UFUNCTION(BlueprintPure, Category = "HaGiang|AI")
	float GetRoadDistance() const { return RoadDistance; }

	int32 GetAutoDriveDirection() const { return AutoDriveDirection; }

	/** When the AI reaches a road end it teleports here (cm along the spline); <0 = turn around. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float LoopRestartDistance = -1.f;

protected:
	virtual void BeginPlay() override;

	/** Hook for cosmetic animation after movement (lean, wheels...). */
	virtual void UpdateVisuals(float DeltaSeconds, float Steer) {}

	/** Called when auto-drive starts / stops (show or hide the NPC rider). */
	virtual void OnAutoDriveChanged(bool bNowAutoDriving) {}

	/** AI: throttle / steer / brake toward the road ahead. */
	virtual void ComputeAutoDriveInput(float DeltaSeconds, float& OutThrottle, float& OutSteer, bool& OutHandbrake);

	/** AI saw something blocking the lane (trucks honk). */
	virtual void OnAutoDriveObstacle(AActor* Obstacle) {}

	/** Tightest radius (cm) of the road in the next LookCm, for speed limits and blind-curve horns. */
	float GetMinRadiusAhead(float LookCm) const;

	UPROPERTY(EditAnywhere, Category = "AI") float AutoDriveLateralAccel = 380.f;   // cm/s^2 allowed in curves
	UPROPERTY(EditAnywhere, Category = "AI") float AutoDriveLookahead = 1300.f;
	UPROPERTY(EditAnywhere, Category = "AI") float ConvoyGap = 1000.f;

	UPROPERTY(Transient) bool bAutoDrive = false;
	UPROPERTY(Transient) TObjectPtr<AHGRoadSplineActor> AutoDriveRoad;
	UPROPERTY(Transient) TWeakObjectPtr<AHGGroundVehicle> AutoDriveLeader;
	float AutoDriveCruise = 1200.f;   // cm/s
	float AutoDriveLane = 180.f;
	int32 AutoDriveDirection = 1;
	float RoadDistance = 0.f;
	float RoadResyncTimer = 0.f;

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
