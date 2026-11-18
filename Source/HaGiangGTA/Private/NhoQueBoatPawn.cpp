#include "NhoQueBoatPawn.h"
#include "HGPaint.h"
#include "HGMeshKit.h"
#include "HGBikeBuilder.h"
#include "HaGiangGTA.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
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
		// Low and well behind (round 3): the gorge is the subject, the boat ~1/5 of the frame. When the arm is pulled in
		// against a pontoon the canopy is hidden (UpdateBoatCamera) instead of filling the view.
		// Round 4: 12 m behind, 3 m up - at 19 m the cano read as just its roof.
		SpringArm->TargetArmLength = 1200.f;
		SpringArm->SocketOffset = FVector(0.f, 0.f, 300.f);
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
	if (bCano)
	{
		BuildCanoKit();
	}
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
	UpdateOrbitCamera(DeltaSeconds, -7.f, -55.f, -3.f);
	// Arm pulled in close (pontoon, cliff): hide the roof slab so it doesn't fill the lower third of the frame.
	const bool bClose = Camera && FVector::Dist(Camera->GetComponentLocation(), GetActorLocation()) < 900.f;
	if (Canopy && Canopy->IsVisible() == bClose && BoatType == EHGBoatType::Cano)
	{
		Canopy->SetVisibility(!bClose);
		RoofStar->SetVisibility(!bClose);
	}
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
	float YawError = FRotator::NormalizeAxis(To.Rotation().Yaw - HeadingYaw);
	float Slow = 1.f;
	// Give the player's boat room: if it is within 60 m ahead, steer to pass it 15 m off and ease off.
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (const ANhoQueBoatPawn* Player = Cast<ANhoQueBoatPawn>(PC->GetPawn()))
		{
			const FVector Fwd = FRotator(0.f, HeadingYaw, 0.f).Vector();
			const FVector D = Player->GetActorLocation() - GetActorLocation();
			const float Ahead = FVector::DotProduct(D, Fwd);
			const float Side = FVector::DotProduct(D, FVector::CrossProduct(FVector::UpVector, Fwd));
			if (Ahead > 0.f && Ahead < 6000.f && FMath::Abs(Side) < 1500.f)
			{
				const float Away = Side >= 0.f ? -1.f : 1.f;     // pass on the other side
				YawError = FMath::Clamp(YawError + Away * 35.f * (1.f - Ahead / 6000.f), -90.f, 90.f);
				Slow = FMath::GetMappedRangeValueClamped(FVector2D(1500.f, 6000.f), FVector2D(0.2f, 0.8f), Ahead);
			}
		}
	}
	OutSteer = FMath::Clamp(YawError / 25.f, -1.f, 1.f);
	OutThrottle = CruiseThrottle * Slow * (FMath::Abs(YawError) > 60.f ? 0.4f : 1.f);
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
	if (KitFlag)
	{
		const float T = GetWorld()->GetTimeSeconds();
		const float W = FMath::Clamp(FMath::Abs(Speed) / 1500.f, 0.f, 1.f);
		KitFlag->SetRelativeRotation(FRotator(-(1.f - W) * 50.f, FMath::Sin(T * (5.f + 9.f * W)) * (8.f + 14.f * W), 0.f));
	}
	if (KitWake)
	{
		// Foam only under way, longer and wider with speed, with a slight shimmer so it does not look painted on.
		const float W = FMath::Clamp((Speed - 150.f) / 1300.f, 0.f, 1.f);
		KitWake->SetVisibility(W > 0.f && bFloating);
		if (W > 0.f)
		{
			const float T = GetWorld()->GetTimeSeconds();
			KitWake->SetRelativeScale3D(FVector(0.35f + 0.65f * W + 0.03f * FMath::Sin(T * 3.1f), 0.6f + 0.4f * W, 1.f));
		}
	}
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
			if ((h % 5) < 2)   // ~60 % of the seats taken
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
				C->SetCullDistance(7000.f);   // a few pixels under the roof past 70 m (boat stage was 5-15 FPS down)
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------
// Round 3B: the Nho Que tourist canoe from the mesh kit (long narrow steel hull, low roof on thin posts,
// benches, long-tail outboard, flag). HullRoot frame of the round-2 parts: flush deck at z ~40, X forward,
// collision box unchanged (+-640 x +-140).
// ---------------------------------------------------------------------------------------------------------
namespace HGCanoKit
{
	using FC = FLinearColor;

	float Beam(float X)          // half-beam (cm)
	{
		if (X > 300.f)
		{
			return 138.f * FMath::Pow(FMath::Clamp((640.f - X) / 340.f, 0.f, 1.f), 0.62f);
		}
		return X < -560.f ? FMath::Lerp(118.f, 132.f, (X + 620.f) / 60.f) : 138.f;
	}
	// Round 4B: a stronger sheer and forefoot so the bow reads pointed and raised from the chase camera.
	float Gunwale(float X) { return 45.f + FMath::Square(FMath::Max(0.f, X - 260.f) / 380.f) * 70.f; }
	float Keel(float X) { return -45.f + FMath::Max(0.f, X - 220.f) * 0.2f; }

	void BuildHull(FHGMeshKit& K)
	{
		// Round 4B: dark teal topsides and a near-black bottom (the bright blue + red read as a toy from above).
		const FC Blue(0.012f, 0.085f, 0.085f), White(0.7f, 0.7f, 0.66f), Inside(0.2f, 0.21f, 0.2f), Red(0.035f, 0.028f, 0.022f);
		// Stations from the transom to the bow tip; each section: gunwale, stripe, chine, keel (left -> right).
		TArray<float> Xs;
		for (float X = -620.f; X < 300.f; X += 92.f) { Xs.Add(X); }
		for (float X = 300.f; X <= 640.f; X += 34.f) { Xs.Add(X); }
		auto Section = [](float X)
		{
			const float B = Beam(X), G = Gunwale(X), Kz = Keel(X);
			return TArray<FVector>{
				FVector(X, -B, G), FVector(X, -B * 0.99f, G - 13.f), FVector(X, -B * 0.93f, Kz + 16.f), FVector(X, -B * 0.32f, Kz + 2.f),
				FVector(X, B * 0.32f, Kz + 2.f), FVector(X, B * 0.93f, Kz + 16.f), FVector(X, B * 0.99f, G - 13.f), FVector(X, B, G) };
		};
		for (int32 i = 0; i + 1 < Xs.Num(); ++i)
		{
			const TArray<FVector> A = Section(Xs[i]), Bs = Section(Xs[i + 1]);
			const FVector AxA(Xs[i], 0.f, 0.f), AxB(Xs[i + 1], 0.f, 0.f);
			// White sheer stripe, blue topsides, dark red bottom (each band its own loft).
			for (int32 band = 0; band < 2; ++band)
			{
				const int32 o = band == 0 ? 0 : 6;   // left / right stripe
				K.Paint(White, HGSurf::Gloss);
				K.Loft({ A[o], A[o + 1] }, { Bs[o], Bs[o + 1] }, AxA, AxB, &Inside);
			}
			K.Paint(Blue, HGSurf::Gloss);
			K.Loft({ A[1], A[2] }, { Bs[1], Bs[2] }, AxA, AxB, &Inside);
			K.Loft({ A[5], A[6] }, { Bs[5], Bs[6] }, AxA, AxB, &Inside);
			K.Paint(Red, HGSurf::Satin);
			K.Loft({ A[2], A[3], A[4], A[5] }, { Bs[2], Bs[3], Bs[4], Bs[5] }, AxA, AxB, &Inside);
		}
		// Transom.
		K.Paint(Blue, HGSurf::Gloss);
		K.Fan(Section(-620.f), FVector(-620.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f));
		// Flush plank floor, gunwale rails, bow deck.
		K.Paint(FC(0.22f, 0.13f, 0.07f), HGSurf::Satin);
		for (int32 p = 0; p < 9; ++p)
		{
			const float Y = -116.f + p * 29.f;
			K.Box(FVector(-150.f, Y, 38.f), FVector(920.f, 27.f, 3.f));
		}
		K.Paint(FC(0.6f, 0.6f, 0.6f), HGSurf::Chrome);
		for (const float S : { -1.f, 1.f })
		{
			K.Tube(FVector(-610.f, S * 120.f, 62.f), FVector(330.f, S * 135.f, 62.f), 1.6f, 1.6f, 6, true);
			for (float X = -560.f; X <= 300.f; X += 172.f)
			{
				K.Tube(FVector(X, S * (Beam(X) - 4.f), 45.f), FVector(X, S * (Beam(X) - 4.f), 62.f), 1.2f, 1.2f, 5, false);
			}
		}
		// Pointed foredeck that follows the gunwale to the stem (was a rectangle overhanging the bow).
		{
			TArray<FVector> Deck;
			for (float X = 330.f; X <= 640.f; X += 31.f) { Deck.Add(FVector(X, -Beam(X) + 2.f, Gunwale(X) + 1.f)); }
			for (float X = 640.f; X >= 330.f; X -= 31.f) { Deck.Add(FVector(X, Beam(X) - 2.f, Gunwale(X) + 1.f)); }
			K.Paint(FC(0.6f, 0.6f, 0.57f), HGSurf::Gloss);
			K.Fan(Deck, FVector(470.f, 0.f, Gunwale(470.f) + 3.f), FVector::UpVector);
			K.Paint(FC(0.03f, 0.02f, 0.015f), HGSurf::Satin);
			K.Tube(FVector(600.f, 0.f, Gunwale(600.f) + 2.f), FVector(640.f, 0.f, Gunwale(640.f) + 18.f), 3.f, 2.f, 6, true);  // stem post
		}
		// Benches (6 rows), wheel console at the stern.
		for (int32 r = 0; r < 6; ++r)
		{
			const float X = -400.f + r * 140.f;
			K.Paint(FC(0.3f, 0.17f, 0.08f), HGSurf::Satin);
			K.Box(FVector(X, 0.f, 80.f), FVector(34.f, 236.f, 5.f));
			K.Box(FVector(X - 16.f, 0.f, 100.f), FVector(4.f, 236.f, 36.f));      // backrest
			K.Paint(FC(0.2f, 0.11f, 0.05f), HGSurf::Satin);
			K.Box(FVector(X, -90.f, 60.f), FVector(6.f, 6.f, 38.f));
			K.Box(FVector(X, 90.f, 60.f), FVector(6.f, 6.f, 38.f));
		}
		K.Paint(FC(0.12f, 0.12f, 0.13f), HGSurf::Satin);
		K.Box(FVector(-470.f, 0.f, 85.f), FVector(40.f, 70.f, 90.f));
		K.Paint(FC(0.02f, 0.02f, 0.02f), HGSurf::Satin);
		K.Tube(FVector(-450.f, 0.f, 135.f), FVector(-440.f, 0.f, 128.f), 18.f, 18.f, 12, true);    // wheel
		// Round 4B: slimmer, shorter roof high on its posts - the hull, benches and people show around it and
		// under it from the chase camera (the full-length wide roof read as an orange box).
		K.Paint(FC(0.62f, 0.62f, 0.6f), HGSurf::Chrome);
		for (const float X : { -290.f, 20.f, 320.f })      // open stern: helmsman and engine show from behind
		{
			for (const float S : { -1.f, 1.f })
			{
				K.Tube(FVector(X, S * 96.f, 45.f), FVector(X, S * 92.f, 222.f), 2.6f, 2.2f, 6, true);
			}
		}
		K.Paint(FC(0.13f, 0.012f, 0.01f), HGSurf::Satin);   // deep maroon: the brighter red read salmon in the sun
		K.Box(FVector(15.f, 0.f, 226.f), FVector(650.f, 196.f, 3.f));
		K.Box(FVector(15.f, 0.f, 231.f), FVector(650.f, 120.f, 6.f));      // shallow camber ridge
		K.Paint(FC(0.75f, 0.75f, 0.72f), HGSurf::Gloss);
		K.Box(FVector(15.f, -99.f, 222.f), FVector(654.f, 3.f, 10.f));
		K.Box(FVector(15.f, 99.f, 222.f), FVector(654.f, 3.f, 10.f));
		K.Box(FVector(341.f, 0.f, 222.f), FVector(3.f, 200.f, 10.f));
		K.Box(FVector(-311.f, 0.f, 222.f), FVector(3.f, 200.f, 10.f));
		K.Paint(FC(1.f, 0.78f, 0.f), HGSurf::Satin);
		const FVector StarC(15.f, 0.f, 235.f);
		for (int32 i = 0; i < 5; ++i)
		{
			const float A = FMath::DegreesToRadians(90.f + 72.f * i);
			const FVector Dir(FMath::Sin(A), FMath::Cos(A), 0.f);
			K.Box(StarC + Dir * 22.f, FVector(44.f, 16.f, 2.f), FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 0.f));
		}
		K.Box(StarC, FVector(30.f, 30.f, 2.f), FRotator(0.f, 36.f, 0.f));
		// Long-tail outboard (Round 4B: bigger, raised on its pivot so it shows over the transom from behind):
		// red-capped engine block, exhaust, long shaft angled down into the water, prop.
		K.Paint(FC(0.06f, 0.06f, 0.065f), HGSurf::Satin);
		K.Box(FVector(-600.f, 0.f, 60.f), FVector(30.f, 30.f, 30.f));                  // pivot mount
		K.Box(FVector(-668.f, 0.f, 98.f), FVector(96.f, 56.f, 50.f));
		K.Paint(FC(0.42f, 0.03f, 0.02f), HGSurf::Gloss);
		K.Box(FVector(-668.f, 0.f, 128.f), FVector(90.f, 52.f, 12.f));                  // red cowl
		K.Paint(FC(0.03f, 0.03f, 0.03f), HGSurf::Satin);
		K.Tube(FVector(-640.f, 30.f, 110.f), FVector(-650.f, 34.f, 160.f), 4.f, 4.f, 6, true);     // exhaust
		K.Paint(FC(0.55f, 0.55f, 0.56f), HGSurf::Chrome);
		K.Tube(FVector(-700.f, 0.f, 85.f), FVector(-960.f, 0.f, 0.f), 4.5f, 3.5f, 8, true);
		K.Tube(FVector(-620.f, 0.f, 105.f), FVector(-520.f, 0.f, 120.f), 1.8f, 1.8f, 5, true);     // tiller
		K.Paint(FC(0.2f, 0.2f, 0.2f), HGSurf::Satin);
		K.Tube(FVector(-965.f, -10.f, -2.f), FVector(-965.f, 10.f, -2.f), 13.f, 13.f, 3, true); // prop
		// Flag staff at the stern (the cloth is a separate flapping part).
		K.Paint(FC(0.6f, 0.6f, 0.58f), HGSurf::Chrome);
		K.Tube(FVector(-600.f, -110.f, 45.f), FVector(-600.f, -110.f, 262.f), 1.6f, 1.2f, 6, true);
	}
}

namespace HGCanoKit
{
	/** Round 4B: foam wake behind the long-tail (local frame of the actor root, z = 0 at the water surface): a
	 * churned prop wash and two diverging arms of broken foam that fade into the water colour. Scaled by speed. */
	void BuildWake(FHGMeshKit& K)
	{
		FRandomStream R{ 4242 };
		// Round 4B tuning: softer foam (pale aqua, not paper white) fading into the water within ~15 m; continuous
		// thin arms instead of evenly spaced white dashes (read as fish bones in the first pass).
		for (int32 i = 0; i < 30; ++i)
		{
			const float D = i * 55.f;                                   // distance behind the prop
			const float Fade = FMath::Pow(D / 1650.f, 0.6f);
			const FC Foam = FMath::Lerp(FC(0.5f, 0.58f, 0.56f), FC(0.05f, 0.2f, 0.19f), Fade);
			K.Paint(Foam, HGSurf::Matte);
			// Prop wash: churned, widening and breaking up behind the prop.
			if (i < 8 || R.FRand() < 0.7f)
			{
				K.Ellipsoid(FVector(-990.f - D, R.FRandRange(-25.f, 25.f) * (1.f + D / 600.f), 0.f),
					FVector(45.f + D * 0.02f, 16.f + D * 0.06f, 2.f), FRotator(0.f, R.FRandRange(-12.f, 12.f), 0.f), 8, 3, 0.f);
			}
			// Diverging arms (Kelvin wake, ~19 deg each side) from the bow shoulders: overlapping thin streaks.
			for (const float S : { -1.f, 1.f })
			{
				if (R.FRand() < 0.2f)
				{
					continue;
				}
				const float X = -560.f - D * 1.05f, Y = S * (150.f + D * 0.36f);
				K.Ellipsoid(FVector(X, Y + R.FRandRange(-8.f, 8.f), 0.f),
					FVector(48.f, 5.f + D * 0.008f, 1.5f), FRotator(0.f, -S * 19.f, 0.f), 6, 3, 0.f);
			}
		}
		// Bow wave: white wings at the forefoot.
		K.Paint(FC(0.55f, 0.62f, 0.6f), HGSurf::Matte);
		for (const float S : { -1.f, 1.f })
		{
			K.Ellipsoid(FVector(420.f, S * 128.f, 0.f), FVector(110.f, 9.f, 2.f), FRotator(0.f, -S * 12.f, 0.f), 8, 3, 0.f);
		}
	}
}

void ANhoQueBoatPawn::BuildCanoKit()
{
	if (KitHull || BoatType != EHGBoatType::Cano)
	{
		return;
	}
	UStaticMesh* HullMesh = FHGMeshKit::Cached(TEXT("SM_HGCanoHull"), [](FHGMeshKit& K) { HGCanoKit::BuildHull(K); });
	KitHull = FHGMeshKit::AddComponent(this, HullRoot, HullMesh, TEXT("KitHull"), FVector::ZeroVector);
	if (!KitHull)
	{
		return;   // -hgnokit: keep the greybox
	}
	KitHull->SetCullDistance(60000.f);
	UStaticMesh* WakeMesh = FHGMeshKit::Cached(TEXT("SM_HGCanoWake"), [](FHGMeshKit& K) { HGCanoKit::BuildWake(K); });
	KitWake = FHGMeshKit::AddComponent(this, GetRootComponent(), WakeMesh, TEXT("KitWake"), FVector(0.f, 0.f, Draft + 1.f));
	if (KitWake)
	{
		KitWake->SetCastShadow(false);
		KitWake->SetCullDistance(30000.f);
		KitWake->SetVisibility(false);
	}
	KitFlag = FHGMeshKit::AddComponent(this, HullRoot, HGBikes::Flag(), TEXT("KitFlag"), FVector(-600.f, -110.f, 260.f));
	if (KitFlag)
	{
		KitFlag->SetRelativeScale3D(FVector(1.4f));
		KitFlag->SetCullDistance(15000.f);
	}
	for (UStaticMeshComponent* P : { Hull.Get(), Bow.Get(), Deck.Get(), Canopy.Get(), CanopyPostL.Get(), CanopyPostR.Get(), Motor.Get(),
		Paddle.Get(), RoofStar.Get(), Windows.Get() })
	{
		if (P)
		{
			P->SetVisibility(false);
			P->SetHiddenInGame(true);
		}
	}
}
