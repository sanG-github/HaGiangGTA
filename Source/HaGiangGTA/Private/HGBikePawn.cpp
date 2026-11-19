#include "HGBikePawn.h"
#include "HGPaint.h"
#include "HGMeshKit.h"

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
#include "HGTouristNPC.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float BikeBoxHalfZ = 40.f;
	constexpr float BikeBodyClearance = 34.f;

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
	GearTopsKmh = { 22.f, 38.f, 55.f, 85.f };   // Honda Win 110: 4-speed
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


	// Ground is (box half height + clearance) below the actor origin.
	const float GroundZ = -(BikeBoxHalfZ + BikeBodyClearance);

	LeanRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LeanRoot"));
	LeanRoot->SetupAttachment(Collision);
	LeanRoot->SetRelativeLocation(FVector(0.f, 0.f, GroundZ)); // lean pivots at the contact patch

	// Wheels: anchors for the kit wheels (HGBikeBuilder), assigned with the rest of the visuals on the first tick.
	FrontWheel = AddPart(TEXT("FrontWheel"), LeanRoot, nullptr, HGBikes::FrontAxle(), FRotator::ZeroRotator, FVector(100.f));
	RearWheel = AddPart(TEXT("RearWheel"), LeanRoot, nullptr, HGBikes::RearAxle(), FRotator::ZeroRotator, FVector(100.f));

	HeadLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadLight"));
	// Small gameplay light: no Lumen GI (Lumen lit every one of ~30 bike / pickup lights with offscreen
	// shadow traces, ~2.8 ms of the 17 ms GPU frame on the M1) and culled past 60 m.
	HeadLight->bAffectGlobalIllumination = false;
	HeadLight->MaxDrawDistance = 6000.f;
	HeadLight->SetupAttachment(LeanRoot);
	HeadLight->SetRelativeLocation(FVector(75.f, 0.f, 90.f));
	HeadLight->SetRelativeRotation(FRotator(-6.f, 0.f, 0.f));
	// The sun is 10 lux (non-physical, world.build_golden_hour) so lights in real candelas were ~100x too
	// strong: 1500 cd lit the rider's arms and hat as a white halo. Daytime: a faint pool on the road only;
	// the lamp itself reads through the emissive lens.
	HeadLight->SetIntensityUnits(ELightUnits::Candelas);
	HeadLight->SetIntensity(4.f);
	HeadLight->SetAttenuationRadius(1500.f);
	HeadLight->SetOuterConeAngle(24.f);
	HeadLight->SetInnerConeAngle(10.f);
	HeadLight->SetSpecularScale(0.3f);
	HeadLight->SetVolumetricScatteringIntensity(0.f);
	HeadLight->SetLightColor(FLinearColor(1.f, 0.93f, 0.8f));
	HeadLight->SetCastShadows(false);

	TailLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("TailLight"));
	TailLight->bAffectGlobalIllumination = false;
	TailLight->MaxDrawDistance = 4000.f;
	TailLight->SetupAttachment(LeanRoot);
	TailLight->SetRelativeLocation(FVector(-92.f, 0.f, 76.f));
	TailLight->SetIntensityUnits(ELightUnits::Candelas);
	// 15 cd (60 braking) against a 10 lux sun painted a red smear metres wide on the road and the rider:
	// now a faint tint on the fender; the lamp reads through the emissive lens (brighter when braking).
	TailLight->SetIntensity(0.25f);
	TailLight->SetAttenuationRadius(90.f);
	TailLight->SetSpecularScale(0.f);
	TailLight->SetVolumetricScatteringIntensity(0.f);
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
	SetNpcRiderVisible(IsAutoDriving() && !IsPlayerControlled());
	LoadedScream = ScreamSound.LoadSynchronous();
}

void AHGBikePawn::SetBodyColor(const FLinearColor& Color)
{
	BodyColor = Color;
	bPaintChosen = true;   // an explicit paint survives the automatic model pick
	ApplyColors();
}

void AHGBikePawn::SetBikeModel(EHGBikeModel InModel, int32 InSeed)
{
	BikeModel = InModel;
	BikeSeed = InSeed;
	BodyColor = HGBikes::Paint(InModel, InSeed);
	bModelChosen = true;
	bPaintChosen = false;
	if (bVisualsBuilt)
	{
		ClearVisuals();
		BuildVisuals();
	}
}

void AHGBikePawn::SetTourGroup(int32 Group)
{
	TourGroup = Group;
	if (bVisualsBuilt)
	{
		ClearVisuals();
		BuildVisuals();
	}
}

void AHGBikePawn::SetNpcRiderVisible(bool bVisible)
{
	bRiderVisible = bVisible;
	RiderRig.SetVisible(bVisible);
	UpdatePassengerShown();
}

void AHGBikePawn::ClearVisuals()
{
	for (UStaticMeshComponent* C : KitParts)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	KitParts.Reset();
	RiderRig = FHGPersonRig();
	PassengerRig = FHGPersonRig();
	KitBody = nullptr;
	HeadLamp = nullptr;
	TailLamp = nullptr;
	FlagCloth = nullptr;
	bVisualsBuilt = false;
}

void AHGBikePawn::BuildVisuals()
{
	bVisualsBuilt = true;
	const int32 NameSeed = int32(GetTypeHash(GetName()) & 0x7fffffff);
	if (!bModelChosen)
	{
		// No explicit model: tour bikes (a passenger) are Wins, every third one an Easy Rider tour Win; the rest is
		// the local mix. The player's own bike and police bikes stay Wins.
		BikeSeed = NameSeed;
		if (NpcRiderLook == EHGPersonLook::Police || IsPlayerControlled())
		{
			BikeModel = EHGBikeModel::Win;
		}
		else if (bHasPassenger)
		{
			BikeModel = (NameSeed % 3 == 0) ? EHGBikeModel::EasyRiderWin : EHGBikeModel::Win;
		}
		else
		{
			BikeModel = HGBikes::RandomModel(NameSeed);
		}
		if (!bPaintChosen && NpcRiderLook != EHGPersonLook::Police)
		{
			BodyColor = HGBikes::Paint(BikeModel, BikeSeed);
		}
	}
	const int32 Seed = BikeSeed & 0x7fffffff;
	const bool bStep = HGBikes::IsStepThrough(BikeModel);
	const bool bTour = bHasPassenger || BikeModel == EHGBikeModel::EasyRiderWin;
	const int32 Group = TourGroup >= 0 ? TourGroup : (Seed / 7) % HGPeople::NumVariants;
	auto Keep = [this](UStaticMeshComponent* C, float Cull)
	{
		if (C)
		{
			C->SetCullDistance(Cull);
			KitParts.Add(C);
		}
		return C;
	};

	KitBody = Keep(FHGMeshKit::AddComponent(this, LeanRoot, HGBikes::Body(BikeModel, Seed), TEXT("KitBody"), FVector::ZeroVector), 30000.f);
	FrontWheel->SetStaticMesh(HGBikes::Wheel(true, bStep));
	RearWheel->SetStaticMesh(HGBikes::Wheel(false, bStep));
	for (UStaticMeshComponent* W : { FrontWheel.Get(), RearWheel.Get() })
	{
		W->SetRelativeScale3D(FVector::OneVector);
		W->SetCullDistance(30000.f);
		W->SetCastShadow(true);
	}
	Plate->SetRelativeLocation(HGBikes::PlateLocation(BikeModel));

	const FVector Head = HGBikes::HeadLampLens(BikeModel);
	HeadLamp = Keep(FHGMeshKit::AddLamp(this, LeanRoot, TEXT("HeadLamp"), Head, HGBikes::HeadLampSize(BikeModel),
		FLinearColor(1.f, 0.92f, 0.75f), 3.f, BikeModel != EHGBikeModel::Dream), 6000.f);
	TailLamp = Keep(FHGMeshKit::AddLamp(this, LeanRoot, TEXT("TailLamp"), HGBikes::TailLampLens(BikeModel), FVector(1.6f, 9.f, 4.5f),
		FLinearColor(1.f, 0.03f, 0.01f), 2.f, false), 6000.f);
	HeadLight->SetRelativeLocation(Head + FVector(3.f, 0.f, 0.f));
	TailLight->SetRelativeLocation(HGBikes::TailLampLens(BikeModel) + FVector(-6.f, 0.f, 0.f));

	// Vietnamese flag: every Easy Rider tour bike and a few others.
	const bool bFlag = BikeModel == EHGBikeModel::EasyRiderWin
		|| (NpcRiderLook != EHGPersonLook::Police && BikeModel != EHGBikeModel::LocalLoaded && (Seed % 7) == 3);
	if (bFlag)
	{
		Keep(FHGMeshKit::AddComponent(this, LeanRoot, HGBikes::FlagPole(BikeModel), TEXT("FlagPole"), FVector::ZeroVector), 12000.f);
		FlagCloth = Keep(FHGMeshKit::AddComponent(this, LeanRoot, HGBikes::Flag(), TEXT("FlagCloth"), HGBikes::FlagMount(BikeModel)), 12000.f);
	}

	// NPC rider: Easy Rider guides on tour bikes (group colours), locals on the rest, CSGT on police bikes.
	EHGPersonLook Rider = NpcRiderLook;
	int32 RiderVariant = Group;
	if (Rider == EHGPersonLook::EasyRider && !bTour)
	{
		Rider = EHGPersonLook::LocalMan;
		RiderVariant = Seed % HGPeople::NumVariants;
	}
	RiderRig = HGPeople::Spawn(this, DriverSeat, Rider, EHGPersonPose::Rider, RiderVariant, TEXT("NpcRider"), FVector::ZeroVector,
		Rider == EHGPersonLook::EasyRider);
	for (UStaticMeshComponent* C : { RiderRig.Body, RiderRig.Head, RiderRig.Arm[0], RiderRig.Arm[1], RiderRig.ScarfTail })
	{
		Keep(C, 15000.f);
	}
	RiderRig.SetVisible(bRiderVisible);

	// Backpacker passenger: duck-bill helmet, a scarf on every other one, hands on the rider or on the thighs.
	PassengerRig = HGPeople::Spawn(this, PassengerRoot, EHGPersonLook::Backpacker, EHGPersonPose::Passenger,
		TourGroup >= 0 ? TourGroup : (Seed / 3) % HGPeople::NumVariants, TEXT("Passenger"), FVector::ZeroVector, (Seed & 1) != 0);
	PassengerRig.bHoldOn = (Seed % 3) != 0;
	PassengerRig.SetVisible(false);   // shown by UpdatePassengerShown only behind a rider
	bPassengerShown = false;
	for (UStaticMeshComponent* C : { PassengerRig.Body, PassengerRig.Head, PassengerRig.Arm[0], PassengerRig.Arm[1], PassengerRig.ScarfTail })
	{
		Keep(C, 15000.f);
	}
	SetHasPassenger(bHasPassenger);
	WaveTimer = 4.f + float(Seed % 9);
	ApplyColors();
}

void AHGBikePawn::OnAutoDriveChanged(bool bNowAutoDriving)
{
	// Never on a bike the player is sitting on (the playtest's auto stages drive the player's bike: the NPC
	// rider - an olive-jacket man in a non la - appeared on top of the player).
	SetNpcRiderVisible(bNowAutoDriving && !IsPlayerControlled());
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

}

void AHGBikePawn::ApplyColors()
{
	if (!KitBody)
	{
		return;
	}
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(KitBody->GetMaterial(0));
	if (!MID)
	{
		MID = KitBody->CreateDynamicMaterialInstance(0, FHGMeshKit::VertexColorMaterial());
	}
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Tint"), BodyColor);
	}
}

void AHGBikePawn::SetHasPassenger(bool bInHasPassenger)
{
	bHasPassenger = bInHasPassenger;
	UpdatePassengerShown();
}

void AHGBikePawn::UpdatePassengerShown()
{
	// Only behind someone (the NPC rider or the player): a backpacker alone on a parked bike, hands out to
	// a missing rider, looked broken. A loaded working bike has no room for one.
	const bool bShow = bHasPassenger && BikeModel != EHGBikeModel::LocalLoaded && (bRiderVisible || IsPlayerControlled());
	if (bShow != bPassengerShown || !bVisualsBuilt)
	{
		bPassengerShown = bShow;
		PassengerRig.SetVisible(bShow);
	}
}

void AHGBikePawn::UpdateVisuals(float DeltaSeconds, float Steer)
{
	if (!bVisualsBuilt)
	{
		BuildVisuals();
	}
	if (bRiderVisible && IsPlayerControlled())
	{
		SetNpcRiderVisible(false);   // whatever order auto-drive and possession arrived in
	}
	UpdatePassengerShown();   // follows the player getting on / off
	VisualClock += DeltaSeconds;
	const float CurSpeed = GetSpeed();
	const float YawRateRad = FMath::DegreesToRadians(GetYawRate());
	const float G = FMath::Abs(GetWorld()->GetGravityZ());

	// Lean angle that balances the centripetal acceleration v * omega.
	const float LateralAccel = CurSpeed * YawRateRad;                      // cm/s^2
	const float TargetLean = IsGrounded()
		? FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(LateralAccel, G)), -MaxLeanDeg, MaxLeanDeg)
		: LeanDeg * 0.9f;
	LeanDeg = FMath::FInterpTo(LeanDeg, TargetLean, DeltaSeconds, LeanInterpSpeed);
	// Round 6A: braking dive / acceleration squat on the forks (nose down up to 3.5 deg under hard braking).
	const float LongAccelNow = DeltaSeconds > KINDA_SMALL_NUMBER ? (CurSpeed - PrevSpeed) / DeltaSeconds : 0.f;
	const float TargetDive = IsGrounded() ? FMath::Clamp(LongAccelNow / G * 6.f, -3.5f, 1.5f) : 0.f;
	DiveDeg = FMath::FInterpTo(DiveDeg, TargetDive, DeltaSeconds, 6.f);
	LeanRoot->SetRelativeRotation(FRotator(DiveDeg, 0.f, LeanDeg));

	// Wheels.
	WheelAngle = FMath::Fmod(WheelAngle - FMath::RadiansToDegrees(CurSpeed * DeltaSeconds / WheelRadius), 360.f);
	const FRotator Spin(WheelAngle, 0.f, 0.f);   // kit wheels are built round the Y axis
	FrontWheel->SetRelativeRotation(Spin);
	RearWheel->SetRelativeRotation(Spin);

	// Brake light.
	const bool bBraking = (CurSpeed > 50.f && LastThrottle < -0.1f);
	TailLight->SetIntensity(bBraking ? 1.f : 0.25f);
	FHGMeshKit::SetLampIntensity(TailLamp, bBraking ? 12.f : 2.f);

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
		const float Bob = FMath::Sin(GetWorld()->GetTimeSeconds() * 11.f) * FMath::Clamp(FMath::Abs(CurSpeed) / 2500.f, 0.f, 1.f) * 1.2f;
		PassengerRoot->SetRelativeRotation(FRotator(SwayAngle.Y + Bob, 0.f, SwayAngle.X));
	}

	// People: the rider's arms follow the bar and the head looks into the bend, scarves stream in the
	// wind, the passenger looks around and waves now and then (FHGPersonRig, off past ~60 m).
	const float Wind = FMath::Clamp(FMath::Abs(CurSpeed) / 1800.f, 0.f, 1.f);
	RiderRig.Steer = FMath::Clamp(Steer, -1.f, 1.f);
	// Round 6A: in slow tight bends (the Tham Ma hairpins at 30-40 km/h) the rider shifts his weight further in
	// than the bike leans - up to 8 deg more.
	const float HairpinShift = FMath::Clamp(FMath::Abs(GetYawRate()) / 45.f, 0.f, 1.f) * FMath::Clamp((1300.f - FMath::Abs(CurSpeed)) / 600.f, 0.f, 1.f) * 8.f;
	// (Review 03_keys_6s: driving RiderRig.LeanDeg past the bike's lean bent the rider sideways off the seat - the
	// rig already leans with LeanRoot. Kept to a gentle 3 deg until the rig has a proper hip offset.)
	RiderRig.LeanDeg = LeanDeg + FMath::Sign(LeanDeg) * FMath::Min(HairpinShift, 3.f);
	RiderRig.Wind = Wind;
	RiderRig.Animate(this, DeltaSeconds);
	if (bHasPassenger)
	{
		WaveTimer -= DeltaSeconds;
		if (WaveTimer <= 0.f)
		{
			WaveTimer = FMath::FRandRange(9.f, 20.f);
			WaveLeft = FMath::FRandRange(1.5f, 3.f);
		}
		WaveLeft = FMath::Max(0.f, WaveLeft - DeltaSeconds);
		PassengerRig.bWave = WaveLeft > 0.f;
		PassengerRig.bArmsUp = ScreamTime > 0.f;
		PassengerRig.LeanDeg = LeanDeg;
		PassengerRig.Wind = Wind;
		PassengerRig.Animate(this, DeltaSeconds);
	}
	if (FlagCloth)
	{
		const float Flap = FMath::Sin(VisualClock * (5.f + 9.f * Wind)) * (8.f + 14.f * Wind);
		FlagCloth->SetRelativeRotation(FRotator(-(1.f - Wind) * 55.f, Flap, 0.f));
	}
	PrevSpeed = CurSpeed;
	UpdateScreaming(DeltaSeconds);
}

void AHGBikePawn::OnViewpointStop(bool bStart, const FVector& LookAt)
{
	if (!bStart)
	{
		if (AActor* R = PhotoRider.Get())
		{
			R->Destroy();
		}
		PhotoRider = nullptr;
		SetNpcRiderVisible(true);
		return;
	}
	// Riders get off and take photos of the view - only where someone can see it (within 350 m of the camera).
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager || FVector::DistSquared(PC->PlayerCameraManager->GetCameraLocation(), GetActorLocation()) > FMath::Square(35000.f))
	{
		return;
	}
	const FVector Off = GetActorRightVector() * 140.f + GetActorForwardVector() * (FMath::FRand() * 120.f - 60.f);
	const FTransform T(FRotator(0.f, (LookAt - GetActorLocation()).Rotation().Yaw, 0.f), GetActorLocation() + Off + FVector(0.f, 0.f, 60.f));
	AHGTouristNPC* Npc = GetWorld()->SpawnActorDeferred<AHGTouristNPC>(AHGTouristNPC::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Npc)
	{
		return;
	}
	Npc->Style = EHGTouristStyle::EasyRider;
	Npc->bWander = false;
	Npc->LookAtPoint = LookAt;
	Npc->FinishSpawning(T);
	Npc->SetActorEnableCollision(false);   // standing by the bike, not in anyone's way
	PhotoRider = Npc;
	SetNpcRiderVisible(false);
}
