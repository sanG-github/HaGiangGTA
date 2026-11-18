#include "HGBikePawn.h"
#include "HGPaint.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "HGAudio.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName ColorParam(TEXT("Color"));
	constexpr float BikeBoxHalfZ = 40.f;
	constexpr float BikeBodyClearance = 34.f;

	enum class EPartColor : uint8 { Body, Black, Chrome, Shirt, Skin, Backpack, Nylon, Star, HiVis, RainCover };

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
		case EPartColor::Star: return FLinearColor(1.f, 0.82f, 0.f);
		case EPartColor::HiVis: return FLinearColor(0.75f, 0.95f, 0.05f);
		// Backpack rain cover: faded orange (a white one filled the chase view as a glaring block).
		case EPartColor::RainCover: return FLinearColor(0.55f, 0.2f, 0.04f);
		}
		return FLinearColor::White;
	}

	const FName PartColorTag[] = { TEXT("C_Body"), TEXT("C_Black"), TEXT("C_Chrome"), TEXT("C_Shirt"), TEXT("C_Skin"), TEXT("C_Backpack"), TEXT("C_Nylon"), TEXT("C_Star"), TEXT("C_HiVis"), TEXT("C_RainCover") };
}

AHGBikePawn::AHGBikePawn()
{
	DisplayName = NSLOCTEXT("HaGiang", "HondaWin", "Honda Win 110");
	bShowDriver = true;

	// Honda Win 110 (2-stroke-era single, ~8 hp with two up): ~68 km/h flat out on asphalt, ~45-50 on the
	// Ta Lang concrete (ConcreteSpeedScale), crawls off-road. 0-60 km/h in ~5 s with the engine curve.
	MaxForwardSpeed = 1900.f;   // 68 km/h
	MaxReverseSpeed = 150.f;    // pushing it back with your feet
	Acceleration = 520.f;
	BrakeDeceleration = 1100.f; // ~1.1 g with both drums: arcade but not a wall
	CoastDeceleration = 180.f;  // engine braking
	BoostMultiplier = 1.12f;    // [Shift]: tuck in and hold it at the redline
	SteerRateDeg = 115.f;
	HighSpeedSteerScale = 0.42f;
	ConcreteSpeedScale = 0.7f;
	OffRoadSpeedScale = 0.4f;
	ChaseCamPitch = -12.f;
	// Bikes hold their line tighter than trucks (the rim stretch drifted 0.5 m+ towards the cliff edge).
	LaneKeepGain = 2.2f;

	// The box only has to catch walls, posts, cows and people: its bottom sits BikeBodyClearance above the
	// ground (the probes carry the bike over the terrain). With the bottom 10 cm up, every bump in the carved
	// landscape and every corner of the box on a cambered bend started the sweep inside the terrain and the
	// bike stopped dead (14 m in 6 s with W held).
	GroundClearance = BikeBodyClearance;
	Collision->SetBoxExtent(FVector(95.f, 38.f, BikeBoxHalfZ));

	SpringArm->TargetArmLength = 480.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 150.f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cube = CubeMesh.Object;
	UStaticMesh* Cyl = CylinderMesh.Object;
	UStaticMesh* Sph = SphereMesh.Object;

	// Ground is (box half height + clearance) below the actor origin.
	const float GroundZ = -(BikeBoxHalfZ + BikeBodyClearance);

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
	// Small gameplay light: no Lumen GI (Lumen lit every one of ~30 bike / pickup lights with offscreen
	// shadow traces, ~2.8 ms of the 17 ms GPU frame on the M1) and culled past 60 m.
	HeadLight->bAffectGlobalIllumination = false;
	HeadLight->MaxDrawDistance = 6000.f;
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
	TailLight->bAffectGlobalIllumination = false;
	TailLight->MaxDrawDistance = 4000.f;
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
	AddPassengerPart(TEXT("P_RainCover"), Cube, FVector(-27.f, 0.f, 37.f), FRotator(-6.f, 0.f, 0.f), FVector(16.f, 37.f, 52.f), EPartColor::RainCover);
	// Red "co do sao vang" T-shirt: the yellow star shows on the chest (the backpack hides the back).
	AddPassengerPart(TEXT("P_Star"), Cyl, FVector(17.f, 0.f, 42.f), FRotator(90.f, 0.f, 0.f), FVector(14.f, 14.f, 1.f), EPartColor::Star);
	PassengerArmL = Tag(AddPart(TEXT("P_ArmL"), PassengerRoot, Cyl, FVector(8.f, -22.f, 50.f), FRotator(-60.f, 0.f, 0.f), FVector(9.f, 9.f, 55.f)), EPartColor::Skin);
	PassengerArmR = Tag(AddPart(TEXT("P_ArmR"), PassengerRoot, Cyl, FVector(8.f, 22.f, 50.f), FRotator(-60.f, 0.f, 0.f), FVector(9.f, 9.f, 55.f)), EPartColor::Skin);
	PassengerParts.Add(PassengerArmL);
	PassengerParts.Add(PassengerArmR);

	// NPC Easy Rider (tour guide) in a reflective vest; hidden while the player rides.
	auto AddRiderPart = [&](FName Name, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Size, EPartColor Color)
	{
		UStaticMeshComponent* C = Tag(AddPart(Name, DriverSeat, Mesh, Loc, Rot, Size), Color);
		C->SetVisibility(false);
		RiderParts.Add(C);
	};
	AddRiderPart(TEXT("R_Torso"), Cyl, FVector(0.f, 0.f, 30.f), FRotator(12.f, 0.f, 0.f), FVector(42.f, 34.f, 60.f), EPartColor::HiVis);
	AddRiderPart(TEXT("R_Head"), Sph, FVector(6.f, 0.f, 74.f), FRotator::ZeroRotator, FVector(24.f, 22.f, 26.f), EPartColor::Skin);
	AddRiderPart(TEXT("R_Helmet"), Sph, FVector(6.f, 0.f, 82.f), FRotator::ZeroRotator, FVector(29.f, 27.f, 20.f), EPartColor::Black);
	AddRiderPart(TEXT("R_Legs"), Cube, FVector(18.f, 0.f, 2.f), FRotator::ZeroRotator, FVector(40.f, 36.f, 14.f), EPartColor::Black);
	AddRiderPart(TEXT("R_Arms"), Cyl, FVector(30.f, 0.f, 44.f), FRotator(70.f, 0.f, 90.f), FVector(9.f, 9.f, 62.f), EPartColor::HiVis);

	EngineSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/HaGiang/Generated/Audio/SW_HG_BikeEngine_Loop.SW_HG_BikeEngine_Loop")));
	EngineTopKmh = 70.f;
	ScreamSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/HaGiang/Generated/Audio/SW_HG_Scream.SW_HG_Scream")));
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
	HGPaint::LimitDrawDistance(this);
	ApplyColors();
	SetHasPassenger(bHasPassenger);
	SetNpcRiderVisible(IsAutoDriving());
	LoadedScream = ScreamSound.LoadSynchronous();
}

void AHGBikePawn::SetBodyColor(const FLinearColor& Color)
{
	BodyColor = Color;
	if (HasActorBegunPlay())
	{
		ApplyColors();
	}
}

void AHGBikePawn::SetNpcRiderVisible(bool bVisible)
{
	for (UStaticMeshComponent* Part : RiderParts)
	{
		if (Part)
		{
			Part->SetVisibility(bVisible);
		}
	}
}

void AHGBikePawn::OnAutoDriveChanged(bool bNowAutoDriving)
{
	SetNpcRiderVisible(bNowAutoDriving);
}

void AHGBikePawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	SetNpcRiderVisible(false); // the player is riding now
}

void AHGBikePawn::Scream()
{
	if (!bHasPassenger || ScreamCooldown > 0.f)
	{
		return;
	}
	ScreamCooldown = 7.f;
	ScreamTime = 1.6f;
	if (LoadedScream)
	{
		HGAudio::PlayAt(this, LoadedScream, PassengerRoot->GetComponentLocation(),
			FMath::FRandRange(0.85f, 1.f), FMath::FRandRange(0.92f, 1.1f), 5000.f);
	}
}

void AHGBikePawn::UpdateScreaming(float DeltaSeconds)
{
	ScreamCooldown = FMath::Max(0.f, ScreamCooldown - DeltaSeconds);
	ScreamTime = FMath::Max(0.f, ScreamTime - DeltaSeconds);
	if (!bHasPassenger)
	{
		return;
	}

	// Airborne for a moment -> scream.
	AirTime = IsGrounded() ? 0.f : AirTime + DeltaSeconds;
	if (AirTime > 0.45f)
	{
		Scream();
	}

	// Fast along a drop-off -> scream. Checked a few times a second, player bikes only.
	CliffCheckTimer -= DeltaSeconds;
	if (CliffCheckTimer <= 0.f && IsPlayerControlled() && FMath::Abs(GetSpeed()) > 1100.f)
	{
		CliffCheckTimer = 0.3f;
		const FVector Loc = GetActorLocation();
		const FVector Right = GetActorRightVector().GetSafeNormal2D();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HGCliffCheck), false, this);
		for (const float Side : { -1.f, 1.f })
		{
			const FVector Probe = Loc + Right * Side * 650.f;
			FHitResult Hit;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Probe + FVector(0, 0, 200.f), Probe - FVector(0, 0, 6000.f), ECC_Visibility, Params);
			if (!bHit || Hit.ImpactPoint.Z < Loc.Z - 2500.f)
			{
				Scream();
				break;
			}
		}
	}

	// Arms up while screaming.
	const float Up = ScreamTime > 0.f ? 1.f : 0.f;
	const float ArmPitch = FMath::Lerp(-60.f, 160.f, Up);
	const float Shake = Up * FMath::Sin(GetWorld()->GetTimeSeconds() * 22.f) * 8.f;
	PassengerArmL->SetRelativeRotation(FMath::RInterpTo(PassengerArmL->GetRelativeRotation(), FRotator(ArmPitch, 0.f, -15.f * Up + Shake), DeltaSeconds, 10.f));
	PassengerArmR->SetRelativeRotation(FMath::RInterpTo(PassengerArmR->GetRelativeRotation(), FRotator(ArmPitch, 0.f, 15.f * Up - Shake), DeltaSeconds, 10.f));
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
				if (UMaterialInstanceDynamic* MID = HGPaint::MakeMID(Part))
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
	UpdateScreaming(DeltaSeconds);
}
