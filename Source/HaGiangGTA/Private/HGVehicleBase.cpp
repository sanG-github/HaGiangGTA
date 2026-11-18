#include "HGVehicleBase.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "HaGiangGTA.h"
#include "HGInputFactory.h"
#include "HGPlayerCharacter.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"

namespace
{
	/** Ignore [F] for this long after entering, so the same key press can't exit. */
	constexpr double EnterExitCooldown = 0.4;
}

AHGVehicleBase::AHGVehicleBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetBoxExtent(FVector(200.f, 90.f, 55.f));
	Collision->SetCollisionProfileName(TEXT("Pawn"));
	Collision->SetSimulatePhysics(false);
	Collision->SetCanEverAffectNavigation(false);
	RootComponent = Collision;

	DriverSeat = CreateDefaultSubobject<USceneComponent>(TEXT("DriverSeat"));
	DriverSeat->SetupAttachment(Collision);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Collision);
	SpringArm->TargetArmLength = 700.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 200.f);
	SpringArm->SetRelativeRotation(FRotator(-10.f, 0.f, 0.f));
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 8.f;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 5.f;
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->FieldOfView = 90.f;

	AutoPossessAI = EAutoPossessAI::Disabled;
	DisplayName = NSLOCTEXT("HaGiang", "Vehicle", "Vehicle");
}

void AHGVehicleBase::BeginPlay()
{
	Super::BeginPlay();
	SpawnTransform = GetActorTransform();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void AHGVehicleBase::EnsureInputObjects()
{
	if (DrivingContext)
	{
		return;
	}

	ThrottleAction = HGInput::MakeAction(this, TEXT("IA_Throttle"), EInputActionValueType::Axis1D);
	SteerAction = HGInput::MakeAction(this, TEXT("IA_Steer"), EInputActionValueType::Axis1D);
	HandbrakeAction = HGInput::MakeAction(this, TEXT("IA_Handbrake"), EInputActionValueType::Boolean);
	BoostAction = HGInput::MakeAction(this, TEXT("IA_Boost"), EInputActionValueType::Boolean);
	ResetAction = HGInput::MakeAction(this, TEXT("IA_Reset"), EInputActionValueType::Boolean);
	InteractAction = HGInput::MakeAction(this, TEXT("IA_VehicleInteract"), EInputActionValueType::Boolean);

	DrivingContext = HGInput::MakeContext(this, TEXT("IMC_Driving"));

	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::W);
	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::Up);
	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::S, /*bNegate*/ true);
	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::Down, /*bNegate*/ true);
	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::Gamepad_RightTriggerAxis);
	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::Gamepad_LeftTriggerAxis, /*bNegate*/ true);

	HGInput::MapKey(DrivingContext, SteerAction, EKeys::D);
	HGInput::MapKey(DrivingContext, SteerAction, EKeys::Right);
	HGInput::MapKey(DrivingContext, SteerAction, EKeys::A, /*bNegate*/ true);
	HGInput::MapKey(DrivingContext, SteerAction, EKeys::Left, /*bNegate*/ true);
	HGInput::MapKey(DrivingContext, SteerAction, EKeys::Gamepad_LeftX);

	HGInput::MapKey(DrivingContext, HandbrakeAction, EKeys::SpaceBar);
	HGInput::MapKey(DrivingContext, HandbrakeAction, EKeys::Gamepad_FaceButton_Right);

	HGInput::MapKey(DrivingContext, BoostAction, EKeys::LeftShift);
	HGInput::MapKey(DrivingContext, BoostAction, EKeys::Gamepad_FaceButton_Bottom);

	HGInput::MapKey(DrivingContext, ResetAction, EKeys::R);
	HGInput::MapKey(DrivingContext, ResetAction, EKeys::Gamepad_Special_Left);

	HGInput::MapKey(DrivingContext, InteractAction, EKeys::F);
	HGInput::MapKey(DrivingContext, InteractAction, EKeys::Gamepad_FaceButton_Top);
}

void AHGVehicleBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	EnsureInputObjects();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogHaGiang, Error, TEXT("%s: EnhancedInputComponent required (check DefaultInput.ini)."), *GetName());
		return;
	}

	// Continuous values are polled every tick by the driving models.
	EIC->BindActionValue(ThrottleAction);
	EIC->BindActionValue(SteerAction);
	EIC->BindActionValue(HandbrakeAction);
	EIC->BindActionValue(BoostAction);

	EIC->BindAction(ResetAction, ETriggerEvent::Started, this, &AHGVehicleBase::OnReset);
	EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &AHGVehicleBase::OnInteract);
}

void AHGVehicleBase::PawnClientRestart()
{
	EnsureInputObjects();
	Super::PawnClientRestart();
	HGInput::AddContext(this, DrivingContext, 0);
}

void AHGVehicleBase::UnPossessed()
{
	HGInput::RemoveContext(this, DrivingContext);
	Super::UnPossessed();
}

float AHGVehicleBase::ReadAxis(const UInputAction* Action) const
{
	if (!Controller)
	{
		return 0.f;
	}
	const UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	return (EIC && Action) ? FMath::Clamp(EIC->GetBoundActionValue(Action).Get<float>(), -1.f, 1.f) : 0.f;
}

bool AHGVehicleBase::ReadButton(const UInputAction* Action) const
{
	if (!Controller)
	{
		return false;
	}
	const UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	return (EIC && Action) ? EIC->GetBoundActionValue(Action).Get<bool>() : false;
}

void AHGVehicleBase::OnReset(const FInputActionValue& /*Value*/)
{
	ResetToSpawn();
}

void AHGVehicleBase::OnInteract(const FInputActionValue& /*Value*/)
{
	if (GetWorld()->GetTimeSeconds() - EnteredAtSeconds < EnterExitCooldown)
	{
		return;
	}
	ExitVehicle();
}

void AHGVehicleBase::ResetToSpawn()
{
	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

// ---------------------------------------------------------------------------
// Enter / exit
// ---------------------------------------------------------------------------

AHGPlayerCharacter* AHGVehicleBase::GetDriver() const
{
	return Driver;
}

bool AHGVehicleBase::CanEnter(const AHGPlayerCharacter* Who) const
{
	return bEnterable && Who != nullptr && Driver == nullptr && Controller == nullptr;
}

bool AHGVehicleBase::EnterVehicle(AHGPlayerCharacter* Who)
{
	if (!CanEnter(Who))
	{
		return false;
	}
	AController* PC = Who->GetController();
	if (!PC)
	{
		return false;
	}

	Driver = Who;
	EnteredAtSeconds = GetWorld()->GetTimeSeconds();
	Who->OnEnteredVehicle(this);
	PC->Possess(this);
	UE_LOG(LogHaGiang, Log, TEXT("%s entered %s"), *Who->GetName(), *GetName());
	return true;
}

FVector AHGVehicleBase::GetExitLocation() const
{
	const FVector Extent = Collision->GetScaledBoxExtent();
	const FVector Side = GetActorRightVector().GetSafeNormal2D();
	return GetActorLocation() - Side * (Extent.Y + 90.f) + FVector(0.f, 0.f, 60.f);
}

void AHGVehicleBase::ExitVehicle()
{
	AController* PC = Controller;
	if (!Driver || !PC)
	{
		return;
	}
	if (GetWorld()->GetTimeSeconds() - EnteredAtSeconds < EnterExitCooldown)
	{
		return;
	}

	AHGPlayerCharacter* Leaving = Driver;
	Driver = nullptr;

	FVector ExitLoc = GetExitLocation();
	const FRotator ExitRot(0.f, GetActorRotation().Yaw, 0.f);
	Leaving->OnExitedVehicle(this, ExitLoc, ExitRot);

	PC->Possess(Leaving);
	PC->SetControlRotation(FRotator(-12.f, ExitRot.Yaw, 0.f));
	UE_LOG(LogHaGiang, Log, TEXT("%s exited %s"), *Leaving->GetName(), *GetName());
}
