#include "HGBikePawn.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName ColorParam(TEXT("Color"));

	enum class EPartColor : uint8 { Body, Black, Chrome, Shirt, Skin, Backpack, Nylon };

	FLinearColor PartColor(EPartColor C, const FLinearColor& Body)
	{
		switch (C)
		{
		case EPartColor::Body: return Body;
		case EPartColor::Black: return FLinearColor(0.02f, 0.02f, 0.02f);
		case EPartColor::Chrome: return FLinearColor(0.6f, 0.6f, 0.62f);
		case EPartColor::Shirt: return FLinearColor(0.75f, 0.02f, 0.02f);
		case EPartColor::Skin: return FLinearColor(0.85f, 0.62f, 0.5f);
		case EPartColor::Backpack: return FLinearColor(0.1f, 0.25f, 0.6f);
		case EPartColor::Nylon: return FLinearColor(0.85f, 0.88f, 0.9f);
		}
		return FLinearColor::White;
	}

	const FName PartColorTag[] = { TEXT("C_Body"), TEXT("C_Black"), TEXT("C_Chrome"), TEXT("C_Shirt"), TEXT("C_Skin"), TEXT("C_Backpack"), TEXT("C_Nylon") };
}

AHGBikePawn::AHGBikePawn()
{
	DisplayName = NSLOCTEXT("HaGiang", "HondaWin", "Honda Win 110");
	bShowDriver = true;

	// Bike-ish tuning: lighter, twitchier, lower top speed than the car.
	MaxForwardSpeed = 2500.f;   // 90 km/h
	MaxReverseSpeed = 250.f;
	Acceleration = 950.f;
	BrakeDeceleration = 2200.f;
	CoastDeceleration = 250.f;
	BoostMultiplier = 1.4f;
	SteerRateDeg = 115.f;
	HighSpeedSteerScale = 0.42f;
	GroundClearance = 10.f;

	Collision->SetBoxExtent(FVector(95.f, 38.f, 52.f));

	SpringArm->TargetArmLength = 480.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 150.f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cube = CubeMesh.Object;
	UStaticMesh* Cyl = CylinderMesh.Object;
	UStaticMesh* Sph = SphereMesh.Object;

	// Ground is (52 + 10) cm below the actor origin.
	const float GroundZ = -62.f;

	LeanRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LeanRoot"));
	LeanRoot->SetupAttachment(Collision);
	LeanRoot->SetRelativeLocation(FVector(0.f, 0.f, GroundZ)); // lean pivots at the contact patch

	auto Tag = [](UStaticMeshComponent* C, EPartColor Color) { C->ComponentTags.Add(PartColorTag[(uint8)Color]); return C; };
	const FRotator WheelRot(0.f, 0.f, 90.f); // cylinder axis -> bike's Y axis

	FrontWheel = Tag(AddPart(TEXT("FrontWheel"), LeanRoot, Cyl, FVector(65.f, 0.f, WheelRadius), WheelRot, FVector(60.f, 60.f, 9.f)), EPartColor::Black);
	RearWheel = Tag(AddPart(TEXT("RearWheel"), LeanRoot, Cyl, FVector(-65.f, 0.f, WheelRadius), WheelRot, FVector(60.f, 60.f, 11.f)), EPartColor::Black);
	Tag(AddPart(TEXT("Frame"), LeanRoot, Cube, FVector(0.f, 0.f, 48.f), FRotator(8.f, 0.f, 0.f), FVector(120.f, 14.f, 14.f)), EPartColor::Body);
	Tag(AddPart(TEXT("Tank"), LeanRoot, Cube, FVector(20.f, 0.f, 72.f), FRotator(-6.f, 0.f, 0.f), FVector(42.f, 26.f, 18.f)), EPartColor::Body);
	Tag(AddPart(TEXT("Seat"), LeanRoot, Cube, FVector(-30.f, 0.f, 78.f), FRotator::ZeroRotator, FVector(80.f, 24.f, 9.f)), EPartColor::Black);
	Tag(AddPart(TEXT("Engine"), LeanRoot, Cube, FVector(5.f, 0.f, 35.f), FRotator::ZeroRotator, FVector(34.f, 22.f, 26.f)), EPartColor::Chrome);
	Tag(AddPart(TEXT("Fork"), LeanRoot, Cyl, FVector(55.f, 0.f, 62.f), FRotator(25.f, 0.f, 0.f), FVector(6.f, 6.f, 70.f)), EPartColor::Chrome);
	Tag(AddPart(TEXT("Handlebar"), LeanRoot, Cyl, FVector(42.f, 0.f, 98.f), WheelRot, FVector(4.f, 4.f, 70.f)), EPartColor::Black);
	Tag(AddPart(TEXT("Exhaust"), LeanRoot, Cyl, FVector(-35.f, 16.f, 30.f), FRotator(-80.f, 0.f, 0.f), FVector(8.f, 8.f, 75.f)), EPartColor::Chrome);
	Tag(AddPart(TEXT("RearRack"), LeanRoot, Cube, FVector(-75.f, 0.f, 82.f), FRotator::ZeroRotator, FVector(30.f, 26.f, 3.f)), EPartColor::Black);
	Tag(AddPart(TEXT("HeadLamp"), LeanRoot, Sph, FVector(66.f, 0.f, 90.f), FRotator::ZeroRotator, FVector(16.f, 16.f, 16.f)), EPartColor::Chrome);

	HeadLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadLight"));
	HeadLight->SetupAttachment(LeanRoot);
	HeadLight->SetRelativeLocation(FVector(75.f, 0.f, 90.f));
	HeadLight->SetRelativeRotation(FRotator(-6.f, 0.f, 0.f));
	HeadLight->SetIntensityUnits(ELightUnits::Candelas);
	HeadLight->SetIntensity(1500.f);
	HeadLight->SetAttenuationRadius(4000.f);
	HeadLight->SetOuterConeAngle(28.f);
	HeadLight->SetInnerConeAngle(12.f);
	HeadLight->SetLightColor(FLinearColor(1.f, 0.93f, 0.8f));
	HeadLight->SetCastShadows(false);

	TailLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("TailLight"));
	TailLight->SetupAttachment(LeanRoot);
	TailLight->SetRelativeLocation(FVector(-92.f, 0.f, 76.f));
	TailLight->SetIntensityUnits(ELightUnits::Candelas);
	TailLight->SetIntensity(15.f);
	TailLight->SetAttenuationRadius(250.f);
	TailLight->SetLightColor(FLinearColor(1.f, 0.05f, 0.02f));
	TailLight->SetCastShadows(false);

	Plate = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Plate"));
	Plate->SetupAttachment(LeanRoot);
	Plate->SetRelativeLocation(FVector(-96.f, 0.f, 62.f));
	Plate->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	Plate->SetHorizontalAlignment(EHTA_Center);
	Plate->SetVerticalAlignment(EVRTA_TextCenter);
	Plate->SetWorldSize(5.f);
	Plate->SetTextRenderColor(FColor(20, 20, 20));
	Plate->SetText(NSLOCTEXT("HaGiang", "Plate", "23-H1 234.56\nHA GIANG"));
	Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Driver sits on the front half of the seat and leans with the bike.
	DriverSeat->SetupAttachment(LeanRoot);
	DriverSeat->SetRelativeLocation(FVector(-10.f, 0.f, 84.f));

	// Backpacker passenger: hips on the rear seat, sways on a spring.
	PassengerRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PassengerRoot"));
	PassengerRoot->SetupAttachment(LeanRoot);
	PassengerRoot->SetRelativeLocation(FVector(-52.f, 0.f, 84.f));

	auto AddPassengerPart = [&](FName Name, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Size, EPartColor Color)
	{
		UStaticMeshComponent* C = Tag(AddPart(Name, PassengerRoot, Mesh, Loc, Rot, Size), Color);
		PassengerParts.Add(C);
	};
	AddPassengerPart(TEXT("P_Torso"), Cyl, FVector(0.f, 0.f, 32.f), FRotator(-8.f, 0.f, 0.f), FVector(38.f, 30.f, 62.f), EPartColor::Shirt);
	AddPassengerPart(TEXT("P_Head"), Sph, FVector(4.f, 0.f, 76.f), FRotator::ZeroRotator, FVector(24.f, 22.f, 26.f), EPartColor::Skin);
	AddPassengerPart(TEXT("P_Helmet"), Sph, FVector(4.f, 0.f, 84.f), FRotator::ZeroRotator, FVector(28.f, 26.f, 18.f), EPartColor::Nylon);
	AddPassengerPart(TEXT("P_Thighs"), Cube, FVector(16.f, 0.f, 4.f), FRotator::ZeroRotator, FVector(36.f, 34.f, 14.f), EPartColor::Black);
	AddPassengerPart(TEXT("P_Backpack"), Cube, FVector(-24.f, 0.f, 36.f), FRotator(-6.f, 0.f, 0.f), FVector(20.f, 34.f, 48.f), EPartColor::Backpack);
	AddPassengerPart(TEXT("P_RainCover"), Cube, FVector(-27.f, 0.f, 37.f), FRotator(-6.f, 0.f, 0.f), FVector(16.f, 37.f, 52.f), EPartColor::Nylon);
}

UStaticMeshComponent* AHGBikePawn::AddPart(FName Name, USceneComponent* Parent, UStaticMesh* Mesh,
	const FVector& Location, const FRotator& Rotation, const FVector& SizeCm)
{
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	C->SetupAttachment(Parent);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	C->SetRelativeLocation(Location);
	C->SetRelativeRotation(Rotation);
	C->SetRelativeScale3D(SizeCm / 100.f); // engine basic shapes are 100 uu
	if (Mesh)
	{
		C->SetStaticMesh(Mesh);
	}
	BodyParts.Add(C);
	return C;
}

void AHGBikePawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetHasPassenger(bHasPassenger);
}

void AHGBikePawn::BeginPlay()
{
	Super::BeginPlay();
	ApplyColors();
	SetHasPassenger(bHasPassenger);
}

void AHGBikePawn::ApplyColors()
{
	for (UStaticMeshComponent* Part : BodyParts)
	{
		if (!Part)
		{
			continue;
		}
		for (uint8 i = 0; i < UE_ARRAY_COUNT(PartColorTag); ++i)
		{
			if (Part->ComponentHasTag(PartColorTag[i]))
			{
				if (UMaterialInstanceDynamic* MID = Part->CreateAndSetMaterialInstanceDynamic(0))
				{
					MID->SetVectorParameterValue(ColorParam, PartColor(static_cast<EPartColor>(i), BodyColor));
				}
				break;
			}
		}
	}
}

void AHGBikePawn::SetHasPassenger(bool bInHasPassenger)
{
	bHasPassenger = bInHasPassenger;
	for (UStaticMeshComponent* Part : PassengerParts)
	{
		if (Part)
		{
			Part->SetVisibility(bHasPassenger);
		}
	}
}

void AHGBikePawn::UpdateVisuals(float DeltaSeconds, float Steer)
{
	const float CurSpeed = GetSpeed();
	const float YawRateRad = FMath::DegreesToRadians(GetYawRate());
	const float G = FMath::Abs(GetWorld()->GetGravityZ());

	// Lean angle that balances the centripetal acceleration v * omega.
	const float LateralAccel = CurSpeed * YawRateRad;                      // cm/s^2
	const float TargetLean = IsGrounded()
		? FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(LateralAccel, G)), -MaxLeanDeg, MaxLeanDeg)
		: LeanDeg * 0.9f;
	LeanDeg = FMath::FInterpTo(LeanDeg, TargetLean, DeltaSeconds, LeanInterpSpeed);
	LeanRoot->SetRelativeRotation(FRotator(0.f, 0.f, LeanDeg));

	// Wheels.
	WheelAngle = FMath::Fmod(WheelAngle - FMath::RadiansToDegrees(CurSpeed * DeltaSeconds / WheelRadius), 360.f);
	const FRotator Spin(WheelAngle, 0.f, 90.f);
	FrontWheel->SetRelativeRotation(Spin);
	RearWheel->SetRelativeRotation(Spin);

	// Brake light.
	const bool bBraking = (CurSpeed > 50.f && LastThrottle < -0.1f);
	TailLight->SetIntensity(bBraking ? 60.f : 15.f);

	// Passenger: spring-damper toward an inertial offset (leans out of turns, nods on braking).
	if (bHasPassenger && DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		const float LongAccel = (CurSpeed - PrevSpeed) / DeltaSeconds;
		const FVector2D Target(
			FMath::Clamp(-LateralAccel / G * SwayDegPerG * 0.5f, -12.f, 12.f),
			FMath::Clamp(-LongAccel / G * SwayDegPerG, -15.f, 15.f));
		const FVector2D Accel = (Target - SwayAngle) * SwayStiffness - SwayVelocity * SwayDamping;
		SwayVelocity += Accel * DeltaSeconds;
		SwayAngle += SwayVelocity * DeltaSeconds;
		// Small idle bob from road vibration.
		const float Bob = FMath::Sin(GetWorld()->GetTimeSeconds() * 11.f) * FMath::Clamp(FMath::Abs(CurSpeed) / 2500.f, 0.f, 1.f) * 1.2f;
		PassengerRoot->SetRelativeRotation(FRotator(SwayAngle.Y + Bob, 0.f, SwayAngle.X));
	}
	PrevSpeed = CurSpeed;
}
