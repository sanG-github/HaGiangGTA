#include "HGPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "HaGiangGTA.h"
#include "HGInputFactory.h"
#include "HGVehicleBase.h"
#include "HGViewpointActor.h"
#include "HGWaterBody.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AHGPlayerCharacter::AHGPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(38.f, 92.f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->bOrientRotationToMovement = true;
	MoveComp->RotationRate = FRotator(0.f, 540.f, 0.f);
	MoveComp->MaxWalkSpeed = WalkSpeed;
	MoveComp->JumpZVelocity = 520.f;
	MoveComp->AirControl = 0.35f;
	MoveComp->BrakingDecelerationWalking = 2000.f;
	MoveComp->BrakingDecelerationFlying = 700.f;
	MoveComp->MaxFlySpeed = SwimSpeed;
	MoveComp->MaxStepHeight = 50.f;
	MoveComp->SetWalkableFloorAngle(50.f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 380.f;
	CameraBoom->SocketOffset = FVector(0.f, 45.f, 70.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 12.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Greybox body, built from engine shapes so no character assets are needed yet.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));

	BodyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	BodyRoot->SetupAttachment(RootComponent);
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -92.f)); // feet

	auto MakePart = [this](FName Name, UStaticMesh* Mesh, const FVector& Loc, const FVector& SizeCm)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(BodyRoot);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
		C->SetRelativeLocation(Loc);
		C->SetRelativeScale3D(SizeCm / 100.f);
		if (Mesh)
		{
			C->SetStaticMesh(Mesh);
		}
		return C;
	};
	Legs = MakePart(TEXT("Legs"), CylMesh.Object, FVector(0.f, 0.f, 45.f), FVector(34.f, 30.f, 90.f));
	Torso = MakePart(TEXT("Torso"), CylMesh.Object, FVector(0.f, 0.f, 120.f), FVector(44.f, 32.f, 62.f));
	Head = MakePart(TEXT("Head"), SphMesh.Object, FVector(2.f, 0.f, 166.f), FVector(24.f, 22.f, 27.f));
	Hat = MakePart(TEXT("Hat"), ConeMesh.Object, FVector(2.f, 0.f, 182.f), FVector(52.f, 52.f, 16.f)); // non la
	GetMesh()->SetVisibility(false);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void AHGPlayerCharacter::EnsureInputObjects()
{
	if (OnFootContext)
	{
		return;
	}
	MoveAction = HGInput::MakeAction(this, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookAction = HGInput::MakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	JumpAction = HGInput::MakeAction(this, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	SprintAction = HGInput::MakeAction(this, TEXT("IA_Sprint"), EInputActionValueType::Boolean);
	InteractAction = HGInput::MakeAction(this, TEXT("IA_Interact"), EInputActionValueType::Boolean);
	FlycamAction = HGInput::MakeAction(this, TEXT("IA_Flycam"), EInputActionValueType::Boolean);

	OnFootContext = HGInput::MakeContext(this, TEXT("IMC_OnFoot"));
	// Move: X = right, Y = forward.
	HGInput::MapKey(OnFootContext, MoveAction, EKeys::W, false, /*bSwizzleYX*/ true);
	HGInput::MapKey(OnFootContext, MoveAction, EKeys::S, /*bNegate*/ true, /*bSwizzleYX*/ true);
	HGInput::MapKey(OnFootContext, MoveAction, EKeys::D);
	HGInput::MapKey(OnFootContext, MoveAction, EKeys::A, /*bNegate*/ true);
	HGInput::MapKey(OnFootContext, MoveAction, EKeys::Gamepad_Left2D);
	// Look: mouse / right stick.
	HGInput::MapKey(OnFootContext, LookAction, EKeys::Mouse2D);
	HGInput::MapKey(OnFootContext, LookAction, EKeys::Gamepad_Right2D);

	HGInput::MapKey(OnFootContext, JumpAction, EKeys::SpaceBar);
	HGInput::MapKey(OnFootContext, JumpAction, EKeys::Gamepad_FaceButton_Bottom);
	HGInput::MapKey(OnFootContext, SprintAction, EKeys::LeftShift);
	HGInput::MapKey(OnFootContext, SprintAction, EKeys::Gamepad_LeftThumbstick);
	HGInput::MapKey(OnFootContext, InteractAction, EKeys::F);
	HGInput::MapKey(OnFootContext, InteractAction, EKeys::Gamepad_FaceButton_Top);
	HGInput::MapKey(OnFootContext, FlycamAction, EKeys::V);
	HGInput::MapKey(OnFootContext, FlycamAction, EKeys::Gamepad_Special_Right);
}

void AHGPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	EnsureInputObjects();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogHaGiang, Error, TEXT("%s: EnhancedInputComponent required (check DefaultInput.ini)."), *GetName());
		return;
	}
	EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AHGPlayerCharacter::Move);
	EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AHGPlayerCharacter::Look);
	EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &AHGPlayerCharacter::StartJump);
	EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &AHGPlayerCharacter::StopJump);
	EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &AHGPlayerCharacter::SprintOn);
	EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AHGPlayerCharacter::SprintOff);
	EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &AHGPlayerCharacter::Interact);
	EIC->BindAction(FlycamAction, ETriggerEvent::Started, this, &AHGPlayerCharacter::ToggleFlycam);
}

void AHGPlayerCharacter::PawnClientRestart()
{
	EnsureInputObjects();
	Super::PawnClientRestart();
	HGInput::AddContext(this, OnFootContext, 0);
}

void AHGPlayerCharacter::UnPossessed()
{
	HGInput::RemoveContext(this, OnFootContext);
	Super::UnPossessed();
}

void AHGPlayerCharacter::Move(const FInputActionValue& Value)
{
	if (!Controller || State == EHGOnFootState::InVehicle)
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator YawRot(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);
	AddMovementInput(Forward, Axis.Y);
	AddMovementInput(Right, Axis.X);
}

void AHGPlayerCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
}

void AHGPlayerCharacter::StartJump(const FInputActionValue& /*Value*/)
{
	if (State == EHGOnFootState::Walking)
	{
		Jump();
	}
}

void AHGPlayerCharacter::StopJump(const FInputActionValue& /*Value*/)
{
	StopJumping();
}

void AHGPlayerCharacter::SprintOn(const FInputActionValue& /*Value*/)
{
	bSprinting = true;
	ApplyMaxSpeed();
}

void AHGPlayerCharacter::SprintOff(const FInputActionValue& /*Value*/)
{
	bSprinting = false;
	ApplyMaxSpeed();
}

void AHGPlayerCharacter::ApplyMaxSpeed()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
	MoveComp->MaxFlySpeed = bSprinting ? SwimSprintSpeed : SwimSpeed;
}

void AHGPlayerCharacter::ToggleFlycam(const FInputActionValue& /*Value*/)
{
	AHGViewpointActor::ToggleFlycam(this);
}

void AHGPlayerCharacter::Interact(const FInputActionValue& /*Value*/)
{
	if (State == EHGOnFootState::InVehicle)
	{
		return;
	}
	UpdateFocusedVehicle();
	if (FocusedVehicle)
	{
		FocusedVehicle->EnterVehicle(this);
	}
}

// ---------------------------------------------------------------------------
// Tick: swimming + interaction focus
// ---------------------------------------------------------------------------

AHGVehicleBase* AHGPlayerCharacter::GetCurrentVehicle() const
{
	return CurrentVehicle;
}

AHGVehicleBase* AHGPlayerCharacter::GetFocusedVehicle() const
{
	return FocusedVehicle;
}

float AHGPlayerCharacter::GetSpeedKmh() const
{
	return GetVelocity().Size2D() * 0.036f;
}

void AHGPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (State == EHGOnFootState::InVehicle)
	{
		return;
	}
	UpdateSwimming(DeltaSeconds);
	UpdateFocusedVehicle();
}

void AHGPlayerCharacter::UpdateFocusedVehicle()
{
	FocusedVehicle = nullptr;
	float BestDistSq = FMath::Square(InteractRadius);
	const FVector Loc = GetActorLocation();
	for (TActorIterator<AHGVehicleBase> It(GetWorld()); It; ++It)
	{
		AHGVehicleBase* Vehicle = *It;
		if (!Vehicle->CanEnter(this))
		{
			continue;
		}
		// Measure to the closest point of the vehicle's bounds, so long boats are easy to reach.
		const FBox Bounds = Vehicle->GetComponentsBoundingBox(/*bNonColliding*/ false);
		const float DistSq = Bounds.IsValid ? Bounds.ComputeSquaredDistanceToPoint(Loc) : FVector::DistSquared(Loc, Vehicle->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			FocusedVehicle = Vehicle;
		}
	}
}

void AHGPlayerCharacter::UpdateSwimming(float DeltaSeconds)
{
	const FVector Loc = GetActorLocation();
	float SurfaceZ = 0.f;
	const bool bOverWater = AHGWaterBody::FindSurface(GetWorld(), Loc, SurfaceZ);
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();

	if (State == EHGOnFootState::Walking)
	{
		// Chest under water -> swim.
		if (bOverWater && Loc.Z < SurfaceZ + HalfHeight * 0.2f)
		{
			StartSwimming();
		}
		return;
	}

	// Swimming.
	if (!bOverWater)
	{
		StopSwimming();
		return;
	}

	// Wade out when the river bed comes up near the feet (bank / dock steps).
	FHitResult Floor;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGSwimFloor), false, this);
	const bool bFloor = GetWorld()->LineTraceSingleByChannel(Floor, Loc, Loc - FVector(0.f, 0.f, HalfHeight + 40.f), ECC_Visibility, Params);
	if (bFloor && Floor.ImpactPoint.Z > SurfaceZ - HalfHeight * 1.1f)
	{
		StopSwimming();
		return;
	}

	// Buoyancy: spring the capsule toward the float depth; kill big falling speeds on entry.
	const float TargetZ = SurfaceZ - SwimFloatDepth;
	FVector Vel = MoveComp->Velocity;
	const float SpringVz = (TargetZ - Loc.Z) * 4.f;
	Vel.Z = FMath::FInterpTo(Vel.Z, SpringVz, DeltaSeconds, 6.f);
	MoveComp->Velocity = Vel;

	// Swim stroke bob for the greybox body.
	const float Stroke = FMath::Sin(GetWorld()->GetTimeSeconds() * 6.f) * FMath::Clamp(Vel.Size2D() / SwimSpeed, 0.f, 1.f);
	BodyRoot->SetRelativeRotation(FRotator(-70.f + Stroke * 8.f, 0.f, Stroke * 10.f));
}

void AHGPlayerCharacter::StartSwimming()
{
	State = EHGOnFootState::Swimming;
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->SetMovementMode(MOVE_Flying);
	MoveComp->Velocity.Z = FMath::Max(MoveComp->Velocity.Z, -300.f); // splash, don't sink to the bed
	ApplyMaxSpeed();
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -20.f));
}

void AHGPlayerCharacter::StopSwimming()
{
	State = EHGOnFootState::Walking;
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -92.f));
	BodyRoot->SetRelativeRotation(FRotator::ZeroRotator);
}

// ---------------------------------------------------------------------------
// Vehicles
// ---------------------------------------------------------------------------

void AHGPlayerCharacter::SetSeatedPose(bool bSeated)
{
	// Seated: hide the legs and drop the upper body onto the seat point.
	Legs->SetVisibility(!bSeated);
	BodyRoot->SetRelativeLocation(bSeated ? FVector(0.f, 0.f, -92.f - 90.f) : FVector(0.f, 0.f, -92.f));
	BodyRoot->SetRelativeRotation(FRotator::ZeroRotator);
}

void AHGPlayerCharacter::OnEnteredVehicle(AHGVehicleBase* Vehicle)
{
	if (State == EHGOnFootState::Swimming)
	{
		StopSwimming();
	}
	State = EHGOnFootState::InVehicle;
	CurrentVehicle = Vehicle;
	FocusedVehicle = nullptr;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->StopMovementImmediately();
	MoveComp->DisableMovement();
	SetActorEnableCollision(false);

	// Capsule centre goes 92 cm above the seat so the body's hips land on it.
	AttachToComponent(Vehicle->GetDriverSeat(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(FVector(0.f, 0.f, 92.f));
	SetActorRelativeRotation(FRotator::ZeroRotator);
	SetSeatedPose(true);
	SetActorHiddenInGame(!Vehicle->ShouldShowDriver());
}

void AHGPlayerCharacter::OnExitedVehicle(AHGVehicleBase* Vehicle, const FVector& ExitLocation, const FRotator& ExitRotation)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorHiddenInGame(false);
	SetSeatedPose(false);

	SetActorEnableCollision(true);
	FVector Placement = ExitLocation;
	GetWorld()->FindTeleportSpot(this, Placement, ExitRotation);
	SetActorLocationAndRotation(Placement, ExitRotation, false, nullptr, ETeleportType::TeleportPhysics);

	State = EHGOnFootState::Walking;
	CurrentVehicle = nullptr;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->SetMovementMode(MOVE_Falling);
	MoveComp->Velocity = FVector::ZeroVector;
	ApplyMaxSpeed();
}
