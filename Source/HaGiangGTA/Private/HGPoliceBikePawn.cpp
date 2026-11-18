#include "HGPoliceBikePawn.h"

#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "HGAudio.h"
#include "HGRoadSplineActor.h"
#include "Sound/SoundBase.h"

AHGPoliceBikePawn::AHGPoliceBikePawn()
{
	DisplayName = NSLOCTEXT("HaGiang", "PoliceBike", "Xe CSGT");
	bHasPassenger = false;
	BodyColor = FLinearColor(0.85f, 0.85f, 0.85f);
	MaxForwardSpeed = 3300.f;     // ~120 km/h: faster than a Honda Win
	Acceleration = 1300.f;
	AutoDriveLateralAccel = 520.f;
	ConvoyGap = 600.f;

	LightRed = CreateDefaultSubobject<UPointLightComponent>(TEXT("LightRed"));
	LightRed->SetupAttachment(LeanRoot);
	LightRed->SetRelativeLocation(FVector(-70.f, -20.f, 115.f));
	LightRed->SetLightColor(FLinearColor(1.f, 0.02f, 0.02f));
	LightRed->SetIntensityUnits(ELightUnits::Candelas);
	LightRed->SetAttenuationRadius(1500.f);
	LightRed->SetCastShadows(false);

	LightBlue = CreateDefaultSubobject<UPointLightComponent>(TEXT("LightBlue"));
	LightBlue->SetupAttachment(LeanRoot);
	LightBlue->SetRelativeLocation(FVector(-70.f, 20.f, 115.f));
	LightBlue->SetLightColor(FLinearColor(0.05f, 0.15f, 1.f));
	LightBlue->SetIntensityUnits(ELightUnits::Candelas);
	LightBlue->SetAttenuationRadius(1500.f);
	LightBlue->SetCastShadows(false);

	SirenSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/HaGiang/Generated/Audio/SW_HG_Siren_Loop.SW_HG_Siren_Loop")));
}

void AHGPoliceBikePawn::BeginPlay()
{
	Super::BeginPlay();
	if (USoundBase* Siren = SirenSound.LoadSynchronous())
	{
		SirenAudio = HGAudio::CreateLoop(RootComponent, Siren, 20000.f);
		if (SirenAudio)
		{
			SirenAudio->Play();
		}
	}
}

void AHGPoliceBikePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FlashTime += DeltaSeconds;
	const bool bRed = FMath::Fmod(FlashTime, 0.5f) < 0.25f;
	LightRed->SetIntensity(bRed ? 400.f : 0.f);
	LightBlue->SetIntensity(bRed ? 0.f : 400.f);
}

void AHGPoliceBikePawn::ComputeAutoDriveInput(float DeltaSeconds, float& OutThrottle, float& OutSteer, bool& OutHandbrake)
{
	APawn* Target = ChaseTarget.Get();
	if (!Target || !AutoDriveRoad)
	{
		Super::ComputeAutoDriveInput(DeltaSeconds, OutThrottle, OutSteer, OutHandbrake);
		return;
	}

	// Which way along the road is the player?
	RetargetTimer -= DeltaSeconds;
	if (RetargetTimer <= 0.f)
	{
		RetargetTimer = 0.3f;
		TargetRoadDistance = AutoDriveRoad->GetDistanceAlongRoad(Target->GetActorLocation());
		const float Diff = TargetRoadDistance - RoadDistance;
		if (FMath::Abs(Diff) > 2000.f)
		{
			AutoDriveDirection = Diff > 0.f ? 1 : -1;
		}
	}

	const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	const float Dist = ToTarget.Size2D();
	// "Close" also needs the player close along the road: on the switchbacks the player can be 40 m away
	// straight down the slope but 300 m away by road, and steering straight at them drives off the cliff.
	const bool bCloseOnRoad = FMath::Abs(TargetRoadDistance - RoadDistance) < 8000.f;
	if (Dist > 5000.f || !bCloseOnRoad)
	{
		// Far: race along the road (still braking for hairpins - only open throttle is floored).
		AutoDriveCruise = MaxForwardSpeed;
		Super::ComputeAutoDriveInput(DeltaSeconds, OutThrottle, OutSteer, OutHandbrake);
		if (OutThrottle > 0.f)
		{
			OutThrottle = FMath::Max(OutThrottle, 0.6f);
		}
		return;
	}

	// Close: steer straight for the player and match speed to box them in.
	RoadDistance += GetSpeed() * DeltaSeconds * AutoDriveDirection;
	const float YawError = FRotator::NormalizeAxis(ToTarget.Rotation().Yaw - GetActorRotation().Yaw);
	OutSteer = FMath::Clamp(YawError / 20.f, -1.f, 1.f);
	const float TargetSpeed = FMath::Clamp(Target->GetVelocity().Size() + (Dist - 500.f) * 1.2f, 0.f, MaxForwardSpeed);
	OutThrottle = FMath::Clamp((TargetSpeed - GetSpeed()) / 300.f, -1.f, 1.f);
	OutHandbrake = false;
}
