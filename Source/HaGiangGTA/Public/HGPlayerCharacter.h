#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HGPeopleBuilder.h"
#include "HGPlayerCharacter.generated.h"

class AHGVehicleBase;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

UENUM(BlueprintType)
enum class EHGOnFootState : uint8
{
	Walking,
	Swimming,
	InVehicle
};

/**
 * Third-person on-foot player: walk / sprint / jump, swim in the Nho Que
 * river (custom buoyancy on top of flying movement, no physics volume
 * needed), and [F] to get on the nearest bike / boat.
 *
 * Controls: WASD move, mouse look, Space jump, Shift sprint, F enter vehicle.
 */
UCLASS()
class HAGIANGGTA_API AHGPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AHGPlayerCharacter();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void UnPossessed() override;

	UFUNCTION(BlueprintPure, Category = "HaGiang|Player")
	EHGOnFootState GetOnFootState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "HaGiang|Player")
	AHGVehicleBase* GetCurrentVehicle() const;

	/** Vehicle that [F] would enter right now (null if none in reach). */
	UFUNCTION(BlueprintPure, Category = "HaGiang|Player")
	AHGVehicleBase* GetFocusedVehicle() const;

	UFUNCTION(BlueprintPure, Category = "HaGiang|Player")
	float GetSpeedKmh() const;

	/** Called by AHGVehicleBase. */
	void OnEnteredVehicle(AHGVehicleBase* Vehicle);
	void OnExitedVehicle(AHGVehicleBase* Vehicle, const FVector& ExitLocation, const FRotator& ExitRotation);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** Greybox body (until a skeletal mesh is set on GetMesh()). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> BodyRoot;

	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Torso;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Legs;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Head;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Hat;

	UPROPERTY(EditAnywhere, Category = "Movement") float WalkSpeed = 260.f;
	float LookIgnoreUntil = 0.f;
	UPROPERTY(EditAnywhere, Category = "Movement") float SprintSpeed = 620.f;
	UPROPERTY(EditAnywhere, Category = "Movement") float SwimSpeed = 300.f;
	UPROPERTY(EditAnywhere, Category = "Movement") float SwimSprintSpeed = 480.f;
	/** Capsule centre sits this far below the surface while swimming. */
	UPROPERTY(EditAnywhere, Category = "Movement") float SwimFloatDepth = 60.f;
	UPROPERTY(EditAnywhere, Category = "Interaction") float InteractRadius = 450.f;

private:
	void EnsureInputObjects();
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartJump(const FInputActionValue& Value);
	void StopJump(const FInputActionValue& Value);
	void SprintOn(const FInputActionValue& Value);
	void SprintOff(const FInputActionValue& Value);
	void Interact(const FInputActionValue& Value);
	void ToggleFlycam(const FInputActionValue& Value);

	void UpdateSwimming(float DeltaSeconds);
	void StartSwimming();
	void StopSwimming();
	void UpdateFocusedVehicle();
	void ApplyMaxSpeed();
	void SetSeatedPose(bool bSeated);

	/** Low-poly body (HGPeopleBuilder): standing (non la) and seated (helmet) rigs, procedurally animated. */
	FHGPersonRig StandRig;
	FHGPersonRig SeatRig;
	bool bSeatedVisual = false;
	void AnimateBody(float DeltaSeconds);

	EHGOnFootState State = EHGOnFootState::Walking;
	bool bSprinting = false;

	UPROPERTY(Transient) TObjectPtr<AHGVehicleBase> CurrentVehicle;
	UPROPERTY(Transient) TObjectPtr<AHGVehicleBase> FocusedVehicle;

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> OnFootContext;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SprintAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> InteractAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> FlycamAction;
};
