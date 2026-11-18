#pragma once

#include "CoreMinimal.h"
#include "HGVehicleBase.h"
#include "HGGroundVehicle.generated.h"

class AHGRoadSplineActor;
class UPrimitiveComponent;

/** What the tyres are on (player vehicles only; AI keeps its cruise speeds). */
UENUM(BlueprintType)
enum class EHGSurface : uint8
{
	Asphalt,
	Concrete,   // the 4 m poured-concrete Ta Lang road: slab joints, gravel, no overtaking room
	OffRoad     // grass / dirt / rock beside the road
};

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

	UFUNCTION(BlueprintPure, Category = "HaGiang|Vehicle")
	EHGSurface GetSurface() const { return Surface; }

	/**
	 * Put the vehicle back on the nearest road (lane on the right, facing the road direction closest to its
	 * heading), stopped. Falls back to the spawn point when no road is within 300 m. Bound to [R].
	 */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Vehicle")
	void RecoverToRoad();

	/** Teleport onto Road at DistanceCm, Direction +1/-1 along it, LaneOffsetCm right of travel, standing still. */
	void PlaceOnRoad(AHGRoadSplineActor* Road, float DistanceCm, int32 Direction, float LaneOffsetCm);

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

	/** Scripted playtest (-hgplaytest): the auto-drive steers even while the player rides. */
	bool bAutoDriveWithPlayer = false;

	/**
	 * Stretch of road (cm along the spline) the AI keeps to: it turns around at these ends instead of the
	 * road ends. < 0 = no limit. Trucks use it to stay on QL4C (they never take the narrow Ta Lang road).
	 */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|AI")
	void SetAutoDriveRange(float MinCm, float MaxCm) { AutoDriveRangeMin = MinCm; AutoDriveRangeMax = MaxCm; }

protected:
	virtual void BeginPlay() override;

	virtual void HandleResetInput() override { RecoverToRoad(); }

	/** Hook for cosmetic animation after movement (lean, wheels...). */
	virtual void UpdateVisuals(float DeltaSeconds, float Steer) {}

	/** Called when auto-drive starts / stops (show or hide the NPC rider). */
	virtual void OnAutoDriveChanged(bool bNowAutoDriving) {}

	/** AI: throttle / steer / brake toward the road ahead. */
	virtual void ComputeAutoDriveInput(float DeltaSeconds, float& OutThrottle, float& OutSteer, bool& OutHandbrake);

	/** AI saw something blocking the lane (trucks honk). */
	virtual void OnAutoDriveObstacle(AActor* Obstacle) {}

	/** Tightest radius (cm) of the road in the next LookCm (sampled every StepCm), for speed limits and blind-curve horns. */
	float GetMinRadiusAhead(float LookCm, float StepCm = 600.f) const;

	/** Lane offset (cm, signed like AutoDriveLane) at a distance: shrunk on narrow roads so the body stays
	 *  LaneEdgeMargin inside the edge (the 4 m Ta Lang concrete road has 2 m per direction, not 1.7 m + 0.6 m). */
	float GetLaneOffsetAt(float DistanceCm) const;

	UPROPERTY(EditAnywhere, Category = "AI") float AutoDriveLateralAccel = 380.f;   // cm/s^2 allowed in curves
	UPROPERTY(EditAnywhere, Category = "AI") float ConvoyGap = 1000.f;
	/** Lane keeping: cross-track gain (Stanley k) and how far ahead (seconds of travel) the road heading is read. */
	UPROPERTY(EditAnywhere, Category = "AI") float LaneKeepGain = 1.5f;
	UPROPERTY(EditAnywhere, Category = "AI") float LanePreviewSeconds = 0.3f;
	/** Minimum gap (cm) between the side of the vehicle and the road edge when it drives its lane. */
	UPROPERTY(EditAnywhere, Category = "AI") float LaneEdgeMargin = 30.f;
	UPROPERTY(EditAnywhere, Category = "AI") float AutoDriveRangeMin = -1.f;
	UPROPERTY(EditAnywhere, Category = "AI") float AutoDriveRangeMax = -1.f;

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
	/** Top speed multipliers by surface (player only) and the extra rolling drag off the road (cm/s^2). */
	UPROPERTY(EditAnywhere, Category = "Tuning") float ConcreteSpeedScale = 0.7f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float OffRoadSpeedScale = 0.45f;
	UPROPERTY(EditAnywhere, Category = "Tuning") float OffRoadDrag = 150.f;
	/** Deceleration (cm/s^2) back to top speed when above it (rolling onto grass, steep descents). */
	UPROPERTY(EditAnywhere, Category = "Tuning") float OverSpeedDeceleration = 700.f;
	/** Player steering input smoothing (1/s): keys ramp the bars in instead of snapping them. */
	UPROPERTY(EditAnywhere, Category = "Tuning") float SteerResponse = 8.f;

	// ---- Chase camera (player) ----
	/** World pitch of the chase camera on the flat; it follows only part of the slope so Doc Tham Ma's
	 *  grade doesn't point the camera into the hill (uphill) or at the bike's seat (downhill). */
	UPROPERTY(EditAnywhere, Category = "Camera") float ChaseCamPitch = -10.f;
	UPROPERTY(EditAnywhere, Category = "Camera") float ChaseCamSlopeFollow = 0.5f;
	/** Field of view at rest / at top speed (speed sensation). */
	UPROPERTY(EditAnywhere, Category = "Camera") float ChaseFovRest = 88.f;
	UPROPERTY(EditAnywhere, Category = "Camera") float ChaseFovFast = 100.f;

	/** Fall this far below the spawn point and the vehicle respawns. */
	UPROPERTY(EditAnywhere, Category = "Tuning") float KillDepth = 150000.f;

	/** Last throttle input actually applied (for subclasses). */
	float LastThrottle = 0.f;

	/** Seconds spent under water (jumped off the Ma Pi Leng cliff into the Nho Que...). */
	float SubmergedTime = 0.f;

private:
	bool TraceGround(const FVector& From, FHitResult& OutHit) const;
	void UpdateSpeed(float DeltaSeconds, float Throttle, bool bHandbrake, bool bBoost, const FVector& MoveDir);
	void MoveWithCollision(const FVector& Delta, const FVector& MoveDir);
	void SnapToGround(float DeltaSeconds, const FVector& MoveDir);
	void UpdateOrientation(float DeltaSeconds);
	void UpdateSurface(float DeltaSeconds);
	void UpdateChaseCamera(float DeltaSeconds);
	/** Road whose centreline is closest to Location (2D), and that distance (cm). */
	AHGRoadSplineActor* FindNearestRoad(const FVector& Location, float& OutLateralCm, float& OutDistanceCm) const;

	float Speed = 0.f;              // signed, along heading
	float VerticalVelocity = 0.f;   // only used while airborne
	float HeadingYaw = 0.f;
	float YawRateDeg = 0.f;
	bool bGrounded = false;
	FVector GroundNormal = FVector::UpVector;

	/** Last move's sweep was blocked (speed is then limited to what we actually covered). */
	bool bMoveBlocked = false;
	/** Seconds the AI has pushed the throttle without moving (stuck recovery). */
	float StuckTime = 0.f;
	float SmoothedSteer = 0.f;
	double LastBlockLogTime = -10.0;
	float ChaseCamPitchCurrent = -10.f;

	EHGSurface Surface = EHGSurface::Asphalt;
	float SurfaceScale = 1.f;
	float SurfaceTimer = 0.f;
	TWeakObjectPtr<UPrimitiveComponent> GroundComponent;
};
