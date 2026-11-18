#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HGVehicleBase.generated.h"

class AHGPlayerCharacter;
class UBoxComponent;
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

	/** Where the driver is placed on exit (world space). */
	virtual FVector GetExitLocation() const;

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

	// ---- Shared driving input ----
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> DrivingContext;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SteerAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HandbrakeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> BoostAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ResetAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> InteractAction;

	FTransform SpawnTransform;

private:
	void EnsureInputObjects();
	void OnInteract(const FInputActionValue& Value);
	void OnReset(const FInputActionValue& Value);

	UPROPERTY(Transient)
	TObjectPtr<AHGPlayerCharacter> Driver;

	double EnteredAtSeconds = -1.0;
};
