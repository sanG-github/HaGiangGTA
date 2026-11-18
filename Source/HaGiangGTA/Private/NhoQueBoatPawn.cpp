#include "NhoQueBoatPawn.h"
#include "HGPaint.h"
#include "HGMeshKit.h"
#include "HaGiangGTA.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "HGWaterBody.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

ANhoQueBoatPawn::ANhoQueBoatPawn()
{
	DisplayName = NSLOCTEXT("HaGiang", "Cano", "Cano Song Nho Que");
	bShowDriver = true;

	HullRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HullRoot"));
	HullRoot->SetupAttachment(Collision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));

	Hull = AddPart(TEXT("Hull"), CubeMesh.Object);
	Bow = AddPart(TEXT("Bow"), ConeMesh.Object);
	Deck = AddPart(TEXT("Deck"), CubeMesh.Object);
	Canopy = AddPart(TEXT("Canopy"), CubeMesh.Object);
	CanopyPostL = AddPart(TEXT("CanopyPostL"), CylMesh.Object);
	CanopyPostR = AddPart(TEXT("CanopyPostR"), CylMesh.Object);
	Motor = AddPart(TEXT("Motor"), CubeMesh.Object);
	Paddle = AddPart(TEXT("Paddle"), CylMesh.Object);
	RoofStar = AddPart(TEXT("RoofStar"), CylMesh.Object);
	Windows = AddPart(TEXT("Windows"), CubeMesh.Object);

	DriverSeat->SetupAttachment(HullRoot);

	EngineSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/HaGiang/Generated/Audio/SW_HG_BoatMotor_Loop.SW_HG_BoatMotor_Loop")));
	EngineTopKmh = 60.f;

	ApplyBoatType();
}

UStaticMeshComponent* ANhoQueBoatPawn::AddPart(FName Name, UStaticMesh* Mesh)
{
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	C->SetupAttachment(HullRoot);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	if (Mesh)
	{
		C->SetStaticMesh(Mesh);
	}
	return C;
}

void ANhoQueBoatPawn::SetPart(UStaticMeshComponent* Part, bool bVisible, const FVector& Location, const FRotator& Rotation, const FVector& SizeCm)
{
	Part->SetVisibility(bVisible);
	Part->SetHiddenInGame(!bVisible);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeRotation(Rotation);
	Part->SetRelativeScale3D(SizeCm / 100.f);
}

void ANhoQueBoatPawn::ApplyBoatType()
{
	if (BoatType == EHGBoatType::Cano)
	{
		DisplayName = NSLOCTEXT("HaGiang", "Cano", "Cano Song Nho Que");
		Collision->SetBoxExtent(FVector(640.f, 140.f, 50.f));
		MaxForwardSpeed = 1700.f;  // ~61 km/h
		MaxReverseSpeed = 350.f;
		Acceleration = 520.f;
		WaterDrag = 0.3f;
		TurnRateDeg = 48.f;
		BoostMultiplier = 1.35f;
		Draft = 25.f;
		MinDepth = 70.f;
		BowLiftDeg = 7.f;
		TurnRollDeg = 9.f;

		SetPart(Hull, true, FVector(-30.f, 0.f, -5.f), FRotator::ZeroRotator, FVector(1180.f, 270.f, 90.f));
		SetPart(Bow, true, FVector(575.f, 0.f, -5.f), FRotator(-90.f, 0.f, 0.f), FVector(270.f, 90.f, 140.f));
		SetPart(Deck, true, FVector(-60.f, 0.f, 40.f), FRotator::ZeroRotator, FVector(1100.f, 250.f, 6.f));
		// Open-sided tour canoe: a low bulwark instead of the solid white "window" block, so the seated
		// tourists show (the river boats in the photos are open under the red roof).
		SetPart(Windows, true, FVector(-60.f, 0.f, 62.f), FRotator::ZeroRotator, FVector(1040.f, 262.f, 36.f));
		SetPart(Canopy, true, FVector(-60.f, 0.f, 205.f), FRotator::ZeroRotator, FVector(1120.f, 290.f, 10.f));
		SetPart(RoofStar, true, FVector(-60.f, 0.f, 212.f), FRotator::ZeroRotator, FVector(170.f, 170.f, 2.f));
		SetPart(CanopyPostL, true, FVector(-60.f, -125.f, 120.f), FRotator::ZeroRotator, FVector(8.f, 8.f, 170.f));
		SetPart(CanopyPostR, true, FVector(-60.f, 125.f, 120.f), FRotator::ZeroRotator, FVector(8.f, 8.f, 170.f));
		SetPart(Motor, true, FVector(-630.f, 0.f, 20.f), FRotator::ZeroRotator, FVector(50.f, 50.f, 90.f));
		SetPart(Paddle, false);
		DriverSeat->SetRelativeLocation(FVector(-520.f, 0.f, 135.f));
		EngineVolume = 0.8f;

		// Camera above the canopy (2.1 m) even when the arm's collision test pulls it in against a pontoon or a
		// moored hull: with 3.2 m of socket height it ended inside our own roof / hull (black frames).
		SpringArm->TargetArmLength = 1250.f;
		SpringArm->SocketOffset = FVector(0.f, 0.f, 450.f);
	}
	else
	{
		DisplayName = NSLOCTEXT("HaGiang", "Kayak", "Kayak Nho Que");
		Collision->SetBoxExtent(FVector(210.f, 40.f, 25.f));
		MaxForwardSpeed = 520.f;   // ~19 km/h
		MaxReverseSpeed = 150.f;
		Acceleration = 240.f;
		WaterDrag = 0.5f;
		TurnRateDeg = 70.f;
		BoostMultiplier = 1.2f;
		Draft = 8.f;
		MinDepth = 35.f;
		BowLiftDeg = 3.f;
		TurnRollDeg = 5.f;

		SetPart(Hull, true, FVector(0.f, 0.f, 0.f), FRotator::ZeroRotator, FVector(330.f, 70.f, 32.f));
		SetPart(Bow, true, FVector(205.f, 0.f, 0.f), FRotator(-90.f, 0.f, 0.f), FVector(64.f, 30.f, 80.f));
		SetPart(Deck, true, FVector(-20.f, 0.f, 18.f), FRotator::ZeroRotator, FVector(90.f, 50.f, 4.f));
		SetPart(Canopy, false);
		SetPart(RoofStar, false);
		SetPart(Windows, false);
		SetPart(CanopyPostL, false);
		SetPart(CanopyPostR, false);
		SetPart(Motor, false);
		SetPart(Paddle, true, FVector(10.f, 0.f, 60.f), FRotator(0.f, 0.f, 90.f), FVector(5.f, 5.f, 230.f));
		DriverSeat->SetRelativeLocation(FVector(-20.f, 0.f, 70.f));
		EngineVolume = 0.f; // paddling: no motor

		SpringArm->TargetArmLength = 650.f;
		SpringArm->SocketOffset = FVector(0.f, 0.f, 240.f);
	}
}

void ANhoQueBoatPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyBoatType();
}

void ANhoQueBoatPawn::BeginPlay()
{
	ApplyBoatType();
	Super::BeginPlay();
	HeadingYaw = GetActorRotation().Yaw;

	// Cruise boats are spawned part-way along the route: start from the nearest point, heading the way we
	// face (CruiseIndex 0 sent every boat back to the start of the route first).
	if (CruisePoints.Num() >= 2)
	{
		float Best = BIG_NUMBER;
		for (int32 i = 0; i < CruisePoints.Num(); ++i)
		{
			const float D = FVector::DistSquared2D(CruisePoints[i], GetActorLocation());
			if (D < Best)
			{
				Best = D;
				CruiseIndex = i;
			}
		}
		const int32 Next = FMath::Min(CruiseIndex + 1, CruisePoints.Num() - 1);
		const int32 Prev = FMath::Max(CruiseIndex - 1, 0);
		const FVector Fwd = GetActorForwardVector();
		CruiseDir = FVector::DotProduct(CruisePoints[Next] - CruisePoints[Prev], Fwd) >= 0.f ? 1 : -1;
		if (CruiseIndex + CruiseDir >= 0 && CruiseIndex + CruiseDir < CruisePoints.Num())
		{
			CruiseIndex += CruiseDir;
		}
	}

	const bool bCano = BoatType == EHGBoatType::Cano;
	auto Paint = [](UStaticMeshComponent* C, const FLinearColor& Color)
	{
		if (UMaterialInstanceDynamic* MID = HGPaint::MakeMID(C))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	};
	const FLinearColor HullColor = bCano ? FLinearColor(0.04f, 0.22f, 0.6f) : FLinearColor(1.f, 0.45f, 0.02f);
	Paint(Hull, HullColor);
	Paint(Bow, HullColor);
	Paint(Deck, bCano ? FLinearColor(0.35f, 0.2f, 0.1f) : FLinearColor(0.05f, 0.05f, 0.05f));
	Paint(Canopy, FLinearColor(0.62f, 0.03f, 0.02f));   // co do sao vang roof
	Paint(RoofStar, FLinearColor(1.f, 0.78f, 0.f));
	Paint(Windows, bCano ? FLinearColor(0.05f, 0.2f, 0.5f) : FLinearColor(0.75f, 0.78f, 0.8f));
	if (bCano && bAutoCruise)
	{
		BuildCrew();
	}
	Paint(CanopyPostL, FLinearColor(0.7f, 0.7f, 0.7f));
	Paint(CanopyPostR, FLinearColor(0.7f, 0.7f, 0.7f));
	Paint(Motor, FLinearColor(0.08f, 0.08f, 0.1f));
	Paint(Paddle, FLinearColor(0.9f, 0.8f, 0.1f));
}

void ANhoQueBoatPawn::ResetToSpawn()
{
	Super::ResetToSpawn();
	HeadingYaw = SpawnTransform.Rotator().Yaw;
	Speed = 0.f;
	VerticalVelocity = 0.f;
}

const TArray<FVector>* ANhoQueBoatPawn::FindCruiseLine(const UWorld* World)
{
	const TArray<FVector>* Best = nullptr;
	for (TActorIterator<ANhoQueBoatPawn> It(World); It; ++It)
	{
		if (It->bAutoCruise && It->CruisePoints.Num() >= 2 && (!Best || It->CruisePoints.Num() > Best->Num()))
		{
			Best = &It->CruisePoints;
		}
	}
	return Best;
}

void ANhoQueBoatPawn::RecoverToRoute()
{
	const TArray<FVector>* Line = FindCruiseLine(GetWorld());
	if (!Line)
	{
		ResetToSpawn();
		return;
	}
	const FVector Loc = GetActorLocation();
	int32 Best = 0;
	for (int32 i = 1; i < Line->Num(); ++i)
	{
		if (FVector::DistSquared2D((*Line)[i], Loc) < FVector::DistSquared2D((*Line)[Best], Loc))
		{
			Best = i;
		}
	}
	const int32 Next = FMath::Min(Best + 1, Line->Num() - 1);
	const int32 Prev = FMath::Max(Best - 1, 0);
	FVector Along = ((*Line)[Next] - (*Line)[Prev]).GetSafeNormal2D();
	if (FVector::DotProduct(Along, FRotator(0.f, HeadingYaw, 0.f).Vector()) < 0.f)
	{
		Along = -Along;
	}
	FVector P = (*Line)[Best];
	float SurfaceZ = P.Z;
	AHGWaterBody::FindSurface(GetWorld(), P, SurfaceZ);
	P.Z = SurfaceZ - Draft + 20.f;
	HeadingYaw = Along.Rotation().Yaw;
	SetActorLocationAndRotation(P, FRotator(0.f, HeadingYaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	Speed = 0.f;
	VerticalVelocity = 0.f;
	SnapCamera();
}

void ANhoQueBoatPawn::UpdateBoatCamera(float DeltaSeconds)
{
	if (!IsPlayerControlled())
	{
		return;
	}
	// Never look up from below: the arm end must stay above the hull and the water.
	UpdateOrbitCamera(DeltaSeconds, -14.f, -55.f, -5.f);
	// Where the arm puts the camera without collision; lift it (TargetOffset is world space) so it stays
	// at least 1 m above the river surface whatever the orbit.
	const FRotator ArmRot = SpringArm->GetComponentRotation();
	const FVector End = SpringArm->GetComponentLocation() + ArmRot.RotateVector(FVector(-SpringArm->TargetArmLength, 0.f, 0.f) + SpringArm->SocketOffset);
	float SurfaceZ = 0.f;
	float Lift = 0.f;
	if (AHGWaterBody::FindSurface(GetWorld(), End, SurfaceZ))
	{
		Lift = FMath::Max(0.f, SurfaceZ + 100.f - End.Z);
	}
	SpringArm->TargetOffset = FVector(0.f, 0.f, FMath::FInterpTo(SpringArm->TargetOffset.Z, Lift, DeltaSeconds, 6.f));
}

FVector ANhoQueBoatPawn::GetExitLocation() const
{
	// Prefer stepping onto a nearby bank/dock; otherwise drop into the water beside the boat.
	const FVector Extent = Collision->GetScaledBoxExtent();
	const FVector Side = GetActorRightVector().GetSafeNormal2D();
	for (const float Sign : { -1.f, 1.f })
	{
		const FVector Probe = GetActorLocation() + Side * Sign * (Extent.Y + 120.f);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HGBoatExit), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Probe + FVector(0, 0, 300.f), Probe - FVector(0, 0, 400.f), ECC_Visibility, Params))
		{
			float SurfaceZ;
			if (!AHGWaterBody::FindSurface(GetWorld(), Probe, SurfaceZ) || Hit.ImpactPoint.Z > SurfaceZ)
			{
				return Hit.ImpactPoint + FVector(0.f, 0.f, 100.f);
			}
		}
	}
	return GetActorLocation() - Side * (Extent.Y + 90.f) + FVector(0.f, 0.f, 40.f);
}

void ANhoQueBoatPawn::ComputeCruiseInput(float& OutThrottle, float& OutSteer)
{
	if (CruisePoints.Num() < 2)
	{
		return;
	}
	CruiseIndex = FMath::Clamp(CruiseIndex, 0, CruisePoints.Num() - 1);
	const FVector Target = CruisePoints[CruiseIndex];
	const FVector To = Target - GetActorLocation();
	if (To.Size2D() < CruiseAcceptRadius)
	{
		if (CruiseIndex + CruiseDir < 0 || CruiseIndex + CruiseDir >= CruisePoints.Num())
		{
			CruiseDir = -CruiseDir; // turn around at the ends of the route
		}
		CruiseIndex += CruiseDir;
	}
	const float YawError = FRotator::NormalizeAxis(To.Rotation().Yaw - HeadingYaw);
	OutSteer = FMath::Clamp(YawError / 25.f, -1.f, 1.f);
	OutThrottle = CruiseThrottle * (FMath::Abs(YawError) > 60.f ? 0.4f : 1.f);
}

bool ANhoQueBoatPawn::HasWaterDepthAt(const FVector& Location, float SurfaceZ) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGBoatDepth), false, this);
	const FVector Start(Location.X, Location.Y, SurfaceZ + 50.f);
	const FVector End(Location.X, Location.Y, SurfaceZ - 2000.f);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return true;
	}
	return Hit.ImpactPoint.Z < SurfaceZ - MinDepth;
}

void ANhoQueBoatPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Crew.Num() > 0)
	{
		Crew[0].SetVisible(!IsPlayerControlled());
		const float Wind = FMath::Clamp(FMath::Abs(Speed) / 1500.f, 0.f, 1.f);
		for (int32 i = 0; i < Crew.Num(); ++i)
		{
			Crew[i].Wind = Wind;
			Crew[i].bWave = i > 0 && FMath::Fmod(Crew[i].Clock + Crew[i].IdleSeed, 23.f) < 1.8f;
			Crew[i].Animate(this, DeltaSeconds);
		}
	}
	const float Dt = FMath::Min(DeltaSeconds, 1.f / 20.f);
	const float Time = GetWorld()->GetTimeSeconds();

	float Throttle = ReadAxis(ThrottleAction);
	float Steer = ReadAxis(SteerAction);
	const bool bBoost = ReadButton(BoostAction);
	const bool bKayak = BoatType == EHGBoatType::Kayak;
	if (!Controller && bAutoCruise)
	{
		ComputeCruiseInput(Throttle, Steer);
	}

	FVector Loc = GetActorLocation();
	float SurfaceZ = 0.f;
	const bool bOverWater = AHGWaterBody::FindSurface(GetWorld(), Loc, SurfaceZ);
	bFloating = bOverWater && Loc.Z < SurfaceZ + 150.f;

	// ---- Thrust & drag ----
	if (bFloating)
	{
		float Thrust = Throttle * Acceleration * (bBoost ? BoostMultiplier : 1.f);
		if (bKayak && FMath::Abs(Throttle) > 0.05f)
		{
			// Paddle strokes: thrust comes in pulses.
			PaddlePhase += Dt * 5.5f;
			Thrust *= 0.55f + 0.9f * FMath::Max(0.f, FMath::Sin(PaddlePhase));
		}
		Speed += Thrust * Dt;
		Speed -= Speed * WaterDrag * Dt;
		const float Top = MaxForwardSpeed * (bBoost ? BoostMultiplier : 1.f);
		Speed = FMath::Clamp(Speed, -MaxReverseSpeed, Top);
	}

	// ---- Turning (rudder needs flow; kayaks can pivot slowly) ----
	const float Flow = FMath::Clamp(FMath::Abs(Speed) / 400.f, bKayak ? 0.35f : 0.1f, 1.f);
	const float YawRate = bFloating ? Steer * TurnRateDeg * Flow * (Speed < -10.f ? -1.f : 1.f) : 0.f;
	HeadingYaw = FRotator::NormalizeAxis(HeadingYaw + YawRate * Dt);

	// ---- Horizontal move with bank checks ----
	const FVector Fwd = FRotator(0.f, HeadingYaw, 0.f).Vector();
	const FVector Delta = Fwd * Speed * Dt;
	if (!Delta.IsNearlyZero())
	{
		const FVector BowProbe = Loc + Delta + Fwd * Collision->GetScaledBoxExtent().X * FMath::Sign(Speed);
		if (bFloating && !HasWaterDepthAt(BowProbe, SurfaceZ))
		{
			Speed *= -0.25f; // ran aground: bounce back
		}
		else
		{
			FHitResult Hit;
			const FVector Before = GetActorLocation();
			SweepMove(Delta, Hit);   // the dock cano starts overlapping the pontoon / moored line: push out first
			if (Hit.bBlockingHit)
			{
				if (IsPlayerControlled() && Time - LastBlockLogTime > 1.f)
				{
					LastBlockLogTime = Time;
					UE_LOG(LogHaGiang, Log, TEXT("[HG_VEH] %s blocked by %s / %s (start inside: %d)"), *GetName(), Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT("-"),
						*GetNameSafe(Hit.GetComponent()), Hit.bStartPenetrating);
				}
				const FVector Slide = FVector::VectorPlaneProject(Delta * (1.f - Hit.Time), Hit.ImpactNormal);
				AddActorWorldOffset(FVector(Slide.X, Slide.Y, 0.f), true);
				Speed *= 1.f - 0.8f * FMath::Abs(FVector::DotProduct(Fwd, Hit.ImpactNormal));
				// Pinned against a hull / the pontoon: the speedo shows what we actually make, not the thrust
				// (a boat nosed into the moored line read 6-7 km/h standing still).
				const float Along = FVector::DotProduct(GetActorLocation() - Before, Fwd) / Dt;
				Speed = Speed > 0.f ? FMath::Min(Speed, FMath::Max(Along, 0.f)) : FMath::Max(Speed, FMath::Min(Along, 0.f));
			}
		}
	}

	// ---- Vertical: buoyancy spring on water, gravity otherwise ----
	Loc = GetActorLocation();
	if (bOverWater)
	{
		float Wave = 0.f;
		TActorIterator<AHGWaterBody> It(GetWorld());
		if (It)
		{
			Wave = It->GetWaveOffset(Loc, Time);
		}
		const float TargetZ = SurfaceZ - Draft + Wave;
		if (Loc.Z > TargetZ + 30.f)
		{
			VerticalVelocity += GetWorld()->GetGravityZ() * Dt; // falling in (e.g. dropped from a cliff)
		}
		else
		{
			VerticalVelocity += ((TargetZ - Loc.Z) * 40.f - VerticalVelocity * 8.f) * Dt;
		}
	}
	else
	{
		VerticalVelocity += GetWorld()->GetGravityZ() * Dt;
	}
	FHitResult VHit;
	SweepMove(FVector(0.f, 0.f, VerticalVelocity * Dt), VHit);
	if (VHit.bBlockingHit)
	{
		VerticalVelocity = 0.f;
		Speed *= 0.95f; // grinding on land
	}

	// ---- Visual attitude: bow lifts under power, roll into turns, bob on waves ----
	const float SpeedAlpha = FMath::Clamp(Speed / MaxForwardSpeed, -1.f, 1.f);
	const float PlaneLift = BowLiftDeg * (0.6f * SpeedAlpha * (1.f - 0.4f * SpeedAlpha) + 0.4f * FMath::Max(Throttle, 0.f));
	const float Bob = FMath::Sin(Time * 1.9f + Loc.X * 0.001f) * (bFloating ? 1.2f : 0.f);
	VisualPitch = FMath::FInterpTo(VisualPitch, PlaneLift + Bob, Dt, 3.f);
	VisualRoll = FMath::FInterpTo(VisualRoll, -Steer * TurnRollDeg * FMath::Abs(SpeedAlpha) + Bob * 0.7f, Dt, 3.f);
	HullRoot->SetRelativeRotation(FRotator(VisualPitch, 0.f, VisualRoll));
	SetActorRotation(FRotator(0.f, HeadingYaw, 0.f));
	UpdateBoatCamera(Dt);

	if (bKayak)
	{
		const float Stroke = FMath::Abs(Throttle) > 0.05f ? FMath::Sin(PaddlePhase) * 35.f : 0.f;
		Paddle->SetRelativeRotation(FRotator(Stroke * 0.5f, 0.f, 90.f + Stroke));
	}

	if (Loc.Z < SpawnTransform.GetLocation().Z - 100000.f)
	{
		ResetToSpawn();
	}
}

void ANhoQueBoatPawn::BuildCrew()
{
	if (bCrewBuilt)
	{
		return;
	}
	bCrewBuilt = true;
	const int32 Seed = int32(GetTypeHash(GetName()) & 0x7fffffff);
	// Benches across the deck.
	UStaticMesh* Benches = FHGMeshKit::Cached(TEXT("SM_HGBoatBenches"), [](FHGMeshKit& K)
	{
		for (int32 r = 0; r < 6; ++r)
		{
			const float X = -400.f + r * 140.f;
			K.Paint(FLinearColor(0.3f, 0.17f, 0.08f), HGSurf::Satin);
			K.Box(FVector(X, 0.f, 80.f), FVector(34.f, 236.f, 5.f));
			K.Paint(FLinearColor(0.2f, 0.11f, 0.05f), HGSurf::Satin);
			K.Box(FVector(X, -90.f, 61.f), FVector(6.f, 6.f, 38.f));
			K.Box(FVector(X, 90.f, 61.f), FVector(6.f, 6.f, 38.f));
		}
	});
	if (UStaticMeshComponent* B = FHGMeshKit::AddComponent(this, HullRoot, Benches, TEXT("Benches"), FVector::ZeroVector))
	{
		B->SetCullDistance(25000.f);
	}
	// Boatman at the stern wheel.
	FHGPersonRig Boatman = HGPeople::Spawn(this, DriverSeat, EHGPersonLook::LocalMan, EHGPersonPose::Rider, Seed % 4, TEXT("Boatman"));
	Crew.Add(Boatman);
	// Tourists: some benches full, some empty; a mix of backpackers, Easy Rider groups and locals.
	int32 n = 0;
	for (int32 r = 0; r < 6; ++r)
	{
		for (const float Y : { -60.f, 60.f })
		{
			const int32 h = (Seed >> ((n % 12) + 1)) + n * 7;
			++n;
			if ((h % 5) == 0)
			{
				continue;
			}
			const EHGPersonLook Look = (h % 4 == 1) ? EHGPersonLook::HmongWoman : ((h % 4 == 2) ? EHGPersonLook::LocalMan : EHGPersonLook::Backpacker);
			FHGPersonRig P = HGPeople::Spawn(this, HullRoot, Look, EHGPersonPose::Passenger, h % 4, *FString::Printf(TEXT("Tourist%d"), n),
				FVector(-400.f + r * 140.f - 2.f, Y, 83.f), Look == EHGPersonLook::Backpacker && (h & 1));
			P.IdleSeed += float(n) * 3.1f;
			Crew.Add(P);
		}
	}
	for (FHGPersonRig& P : Crew)
	{
		for (UStaticMeshComponent* C : { P.Body, P.Head, P.Arm[0], P.Arm[1], P.ScarfTail })
		{
			if (C)
			{
				C->SetCullDistance(25000.f);
			}
		}
	}
}
