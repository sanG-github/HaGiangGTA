#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HGVehicleBase.generated.h"

class AHGPlayerCharacter;
class UAudioComponent;
class UBoxComponent;
class USoundBase;
class UCameraComponent;
class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
struct FInputActionValue;

/**
 * Base class for every drivable thing (box car, Honda Win, cano, kayak).
 * Owns the collision root, driver seat, chase camera, the shared driving
 * input (built at runtime) and the [F] enter / exit flow.
 *
 * Driving controls: W/S throttle, A/D steer, Space handbrake, Shift boost,
 * R reset, F exit.
 */
UCLASS(Abstract)
class HAGIANGGTA_API AHGVehicleBase : public APawn
{
	GENERATED_BODY()

public:
	AHGVehicleBase();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void UnPossessed() override;

	UFUNCTION(BlueprintPure, Category = "HaGiang|Vehicle")
	virtual float GetSpeedKmh() const { return 0.f; }

	UFUNCTION(BlueprintPure, Category = "HaGiang|Vehicle")
	FText GetVehicleDisplayName() const { return DisplayName; }

	UFUNCTION(BlueprintPure, Category = "HaGiang|Vehicle")
	virtual bool IsWaterVehicle() const { return false; }

	UFUNCTION(BlueprintPure, Category = "HaGiang|Vehicle")
	AHGPlayerCharacter* GetDriver() const;

	bool CanEnter(const AHGPlayerCharacter* Who) const;

	/** Seat the character and give its controller to this vehicle. */
	bool EnterVehicle(AHGPlayerCharacter* Who);

	/** Put the driver back on foot next to the vehicle. */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Vehicle")
	void ExitVehicle();

	USceneComponent* GetDriverSeat() const { return DriverSeat; }
	bool ShouldShowDriver() const { return bShowDriver; }

	UFUNCTION(BlueprintCallable, Category = "HaGiang|Vehicle")
	virtual void ResetToSpawn();

protected:
	virtual void BeginPlay() override;

	/** [R] pressed: default puts the vehicle back where it spawned (ground vehicles recover onto the road). */
	virtual void HandleResetInput() { ResetToSpawn(); }

	/** Where the driver is placed on exit (world space). */
	virtual FVector GetExitLocation() const;

	/**
	 * AddActorWorldOffset with sweep that first resolves a starting overlap: if the collision box starts inside
	 * something (terrain bank, a moored hull, the pontoon) it is pushed out along the minimum translation and
	 * the move is retried. Without this a starting overlap blocks the sweep at t = 0 every frame for good.
	 */
	void SweepMove(const FVector& Delta, FHitResult& OutHit);
	void ResolvePenetration(const FHitResult& Hit);

	float ReadAxis(const UInputAction* Action) const;
	bool ReadButton(const UInputAction* Action) const;

	// ---- Components ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> DriverSeat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle")
	FText DisplayName;

	/** Driver mesh stays visible on the seat (bike, boats) or is hidden (car). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle")
	bool bShowDriver = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle")
	bool bEnterable = true;

	// ---- Engine sound (looping, pitch follows speed) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TSoftObjectPtr<USoundBase> EngineSound;

	UPROPERTY(EditAnywhere, Category = "Audio") float EngineTopKmh = 90.f;
	UPROPERTY(EditAnywhere, Category = "Audio") float EngineIdlePitch = 0.8f;
	UPROPERTY(EditAnywhere, Category = "Audio") float EngineMaxPitch = 2.3f;
	UPROPERTY(EditAnywhere, Category = "Audio") float EngineVolume = 0.7f;
	UPROPERTY(EditAnywhere, Category = "Audio") float EngineFalloff = 5000.f;

	UPROPERTY(Transient) TObjectPtr<UAudioComponent> EngineAudio;
	float EnginePitch = 1.f;

	// ---- Shared driving input ----
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> DrivingContext;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SteerAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HandbrakeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> BoostAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ResetAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> InteractAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> FlycamAction;

	FTransform SpawnTransform;

private:
	void EnsureInputObjects();
	void OnInteract(const FInputActionValue& Value);
	void OnReset(const FInputActionValue& Value);
	void OnFlycam(const FInputActionValue& Value);

	UPROPERTY(Transient)
	TObjectPtr<AHGPlayerCharacter> Driver;

	double EnteredAtSeconds = -1.0;
};
