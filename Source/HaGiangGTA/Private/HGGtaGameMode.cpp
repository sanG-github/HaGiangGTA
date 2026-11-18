#include "HGGtaGameMode.h"

#include "Algo/Reverse.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HaGiangGTA.h"
#include "HGGtaHUD.h"
#include "HGGroundVehicle.h"
#include "NhoQueBoatPawn.h"
#include "HGRoadSplineActor.h"
#include "Components/SplineComponent.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HGPlayerCharacter.h"
#include "HGViewpointActor.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "RenderCore.h"
#include "RHI.h"
#include "RHIStats.h"

AHGGtaGameMode::AHGGtaGameMode()
{
	DefaultPawnClass = AHGPlayerCharacter::StaticClass();
	HUDClass = AHGGtaHUD::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

AActor* AHGGtaGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	TActorIterator<APlayerStart> It(GetWorld());
	if (It)
	{
		return *It;
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

APawn* AHGGtaGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	FTransform T = SpawnTransform;
	FVector Loc = T.GetLocation();
	const ACharacter* CDO = Cast<ACharacter>(GetDefaultPawnClassForController(NewPlayer)->GetDefaultObject());
	const float Half = CDO ? CDO->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 92.f;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGSpawnGround), false);
	// Probe what is around the start (diagnostics), then put the capsule on the first ground below it.
	TArray<FHitResult> Hits;
	GetWorld()->LineTraceMultiByChannel(Hits, Loc + FVector(0, 0, 5000.f), Loc - FVector(0, 0, 5000.f), ECC_Visibility, Params);
	for (const FHitResult& H : Hits)
	{
		const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(H.GetComponent());
		UE_LOG(LogHaGiang, Log, TEXT("Spawn probe: %s (%s, mesh %s, bounds %s) at Z=%.0f%s"), *GetNameSafe(H.GetActor()),
			H.GetActor() ? *H.GetActor()->GetActorNameOrLabel() : TEXT("-"), SMC ? *GetNameSafe(SMC->GetStaticMesh()) : TEXT("-"),
			H.GetComponent() ? *H.GetComponent()->Bounds.BoxExtent.ToString() : TEXT("-"),
			H.ImpactPoint.Z, H.bStartPenetrating ? TEXT(" (start inside)") : TEXT(""));
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, Loc + FVector(0, 0, 300.f), Loc - FVector(0, 0, 5000.f), ECC_Visibility, Params))
	{
		Loc.Z = Hit.ImpactPoint.Z + Half + 10.f;
	}
	T.SetLocation(Loc);
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* Pawn = GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), T, SpawnInfo);
	UE_LOG(LogHaGiang, Log, TEXT("Player spawned at %s"), Pawn ? *Pawn->GetActorLocation().ToString() : TEXT("<failed>"));
	return Pawn;
}

void AHGGtaGameMode::FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation)
{
	Super::FinishRestartPlayer(NewPlayer, StartRotation);
	if (NewPlayer && NewPlayer->GetPawn())
	{
		NewPlayer->SetControlRotation(FRotator(-15.f, StartRotation.Yaw, 0.f));
	}
}

void AHGGtaGameMode::BeginPlay()
{
	Super::BeginPlay();
	bPerfRun = FParse::Param(FCommandLine::Get(), TEXT("hgperf"));
	bPlayTest = FParse::Param(FCommandLine::Get(), TEXT("hgplaytest"));
	if (bPlayTest)
	{
		for (TActorIterator<AHGRoadSplineActor> It(GetWorld()); It; ++It)
		{
			if (!PlayRoad.IsValid() || It->GetRoadLength() > PlayRoad->GetRoadLength())
			{
				PlayRoad = *It;
			}
		}
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] playtest start"));
		return;
	}
	if (!bPerfRun)
	{
		return;
	}
	// Spot 0: wherever the player spawned (bottom of Doc Tham Ma); then each viewpoint, looking at its subject.
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		PerfSpots.Add(Pawn->GetActorTransform());
		PerfNames.Add(TEXT("Spawn_ThamMa"));
	}
	for (TActorIterator<AHGViewpointActor> It(GetWorld()); It; ++It)
	{
		const FVector Loc = It->GetActorLocation() + FVector(0.f, 0.f, 150.f);
		const FRotator Look = (It->OrbitCenter - Loc).Rotation();
		PerfSpots.Add(FTransform(FRotator(0.f, Look.Yaw, 0.f), Loc));
		PerfNames.Add(It->GetName());
	}
	PerfStage = 0;
	PerfTime = -5.f; // warm-up (streaming, shader cache)
	UE_LOG(LogHaGiang, Display, TEXT("[HG_PERF] benchmark start, %d spots"), PerfSpots.Num());
}

float AHGGtaGameMode::PlayFps() const
{
	// Average over the last second (one frame's delta is noisy: a single hitch read as "34 FPS").
	return PlayFpsFrames > 0 ? float(PlayFpsFrames) / FMath::Max(PlayFpsWindow, 1e-3f) : 0.f;
}

void AHGGtaGameMode::PlayCheck(const TCHAR* Label, bool bShot)
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !PC->PlayerCameraManager)
	{
		UE_LOG(LogHaGiang, Error, TEXT("[HG_PLAY] %s: no pawn / camera"), Label);
		return;
	}
	auto GroundZ = [this](const FVector& P, float& OutZ) -> bool
	{
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(HGPlayGround), false);
		if (GetWorld()->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 100000.f), P - FVector(0, 0, 100000.f), ECC_Visibility, Q))
		{
			OutZ = Hit.ImpactPoint.Z;
			return true;
		}
		return false;
	};
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
	float PawnGround = 0.f, CamGround = 0.f;
	const bool bPG = GroundZ(PawnLoc, PawnGround);
	const bool bCG = GroundZ(CamLoc, CamGround);
	float RoadOff = -1.f;
	if (PlayRoad.IsValid())
	{
		const USplineComponent* Sp = PlayRoad->GetSpline();
		const FVector OnRoad = Sp->FindLocationClosestToWorldLocation(PawnLoc, ESplineCoordinateSpace::World);
		RoadOff = FVector::Dist2D(OnRoad, PawnLoc) / 100.f;
	}
	const AHGVehicleBase* Veh = Cast<AHGVehicleBase>(Pawn);
	const float Moved = FVector::Dist(PawnLoc, PlayLastLoc) / 100.f;
	PlayLastLoc = PawnLoc;
	const bool bCamUnder = bCG && CamLoc.Z < CamGround - 20.f;
	UE_LOG(LogHaGiang, Display,
		TEXT("[HG_PLAY] %s | moved %.1f m | pawn %s alt %.0f m, above ground %.1f m | cam pitch %.0f, cam above ground %.1f m%s | %s %.0f km/h | road offset %.1f m | %.0f FPS (game %.1f / draw %.1f / GPU %.1f ms, %d draws, %.1fM tris)"),
		Label, Moved, *Pawn->GetClass()->GetName(), PawnLoc.Z / 100.f, bPG ? (PawnLoc.Z - PawnGround) / 100.f : -999.f,
		CamRot.Pitch, bCG ? (CamLoc.Z - CamGround) / 100.f : -999.f, bCamUnder ? TEXT(" CAMERA UNDERGROUND") : TEXT(""),
		Veh ? TEXT("vehicle") : TEXT("on foot"), Veh ? Veh->GetSpeedKmh() : 0.f, RoadOff,
		PlayFps(), FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
		FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()), GNumDrawCallsRHI[0], GNumPrimitivesDrawnRHI[0] / 1.0e6f);
	if (bShot)
	{
		const FString File = FPaths::ProjectSavedDir() / TEXT("HG_CI/Play") / FString::Printf(TEXT("%02d_%s.png"), PlayShot++, Label);
		FScreenshotRequest::RequestScreenshot(File, true, false);
	}
}

void AHGGtaGameMode::PlaySetKey(const FKey& Key, bool bDown)
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	const bool bIsDown = PlayKeysDown.Contains(Key);
	if (!PC || bIsDown == bDown)
	{
		return;
	}
	PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
	if (bDown)
	{
		PlayKeysDown.Add(Key);
	}
	else
	{
		PlayKeysDown.Remove(Key);
	}
}

void AHGGtaGameMode::PlayReleaseKeys()
{
	const TArray<FKey> Keys = PlayKeysDown;
	for (const FKey& K : Keys)
	{
		PlaySetKey(K, false);
	}
}

void AHGGtaGameMode::PlayDriveKeys(float DeltaSeconds)
{
	AHGGroundVehicle* Bike = PlayBike.Get();
	AHGRoadSplineActor* Road = PlayRoad.Get();
	if (!Bike || !Road)
	{
		return;
	}
	const USplineComponent* Sp = Road->GetSpline();
	const float Len = Sp->GetSplineLength();
	const FVector Loc = Bike->GetActorLocation();
	const float D = Road->GetDistanceAlongRoad(Loc);
	const float Speed = Bike->GetSpeed();
	PlayMaxKmh = FMath::Max(PlayMaxKmh, Bike->GetSpeedKmh());
	{
		const FVector OnRoad = Sp->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
		if (FVector::Dist2D(OnRoad, Loc) > Road->GetWidthAtDistance(D) * 0.5f + 50.f)
		{
			PlayOffRoadSecs += DeltaSeconds;
		}
	}

	// Steering: aim at the right lane a short way ahead; digital A / D like a keyboard rider.
	// Short look-ahead (pure pursuit): a long one aims across the inside of the 8 m hairpins, into the wall.
	const float DA = FMath::Min(D + FMath::Clamp(FMath::Abs(Speed) * 0.35f, 250.f, 900.f), Len);
	const FVector T = Sp->GetDirectionAtDistanceAlongSpline(DA, ESplineCoordinateSpace::World).GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, T).GetSafeNormal();
	const float HalfW = Road->GetWidthAtDistance(DA) * 0.5f;
	const FVector Target = Sp->GetLocationAtDistanceAlongSpline(DA, ESplineCoordinateSpace::World) + Right * FMath::Min(100.f, HalfW * 0.4f);
	const float Err = FRotator::NormalizeAxis((Target - Loc).Rotation().Yaw - Bike->GetActorRotation().Yaw);
	PlaySetKey(EKeys::D, Err > 3.f);
	PlaySetKey(EKeys::A, Err < -3.f);

	// Throttle / brake: tightest radius in the next stretch -> v = sqrt(a R) with a ~0.45 g lean.
	float MinR = BIG_NUMBER;
	FVector Prev = Sp->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).GetSafeNormal2D();
	const float Reach = 2500.f + FMath::Abs(Speed) * 1.2f;
	for (float S = 300.f; S <= Reach; S += 300.f)
	{
		const FVector Dir = Sp->GetDirectionAtDistanceAlongSpline(FMath::Min(D + S, Len), ESplineCoordinateSpace::World).GetSafeNormal2D();
		const float Ang = FMath::Acos(FMath::Clamp(FVector::DotProduct(Prev, Dir), -1.f, 1.f));
		if (Ang > KINDA_SMALL_NUMBER)
		{
			MinR = FMath::Min(MinR, 300.f / Ang);
		}
		Prev = Dir;
	}
	const float VMax = FMath::Max(FMath::Sqrt(450.f * MinR), 450.f);
	PlaySetKey(EKeys::W, Speed < VMax * 0.95f && FMath::Abs(Err) < 60.f);
	PlaySetKey(EKeys::S, Speed > VMax * 1.15f);
}

void AHGGtaGameMode::PlayDriveBoatKeys(float DeltaSeconds)
{
	ANhoQueBoatPawn* Boat = PlayBoat.Get();
	if (!Boat || PlayBoatPath.Num() < 2)
	{
		PlaySetKey(EKeys::W, true);
		return;
	}
	PlayMaxKmh = FMath::Max(PlayMaxKmh, Boat->GetSpeedKmh());
	// Follow the cruise line: closest point at or beyond the current one (search a short window), aim two ahead.
	const FVector BoatLoc = Boat->GetActorLocation();
	int32 Closest = PlayBoatIndex;
	for (int32 i = PlayBoatIndex; i < FMath::Min(PlayBoatIndex + 6, PlayBoatPath.Num()); ++i)
	{
		if (FVector::DistSquared2D(PlayBoatPath[i], BoatLoc) < FVector::DistSquared2D(PlayBoatPath[Closest], BoatLoc)) { Closest = i; }
	}
	PlayBoatIndex = Closest;
	const FVector To = PlayBoatPath[FMath::Min(PlayBoatIndex + 2, PlayBoatPath.Num() - 1)] - BoatLoc;
	const float Err = FRotator::NormalizeAxis(To.Rotation().Yaw - Boat->GetActorRotation().Yaw);
	// Like a player boxed in at the dock: no progress for 2 s -> back out (S) with the bow swinging to the
	// side of the target for 2.5 s, then go again.
	PlayBoatCheckTime += DeltaSeconds;
	if (PlayBoatCheckTime > 2.f)
	{
		const bool bStuck = FVector::Dist2D(Boat->GetActorLocation(), PlayBoatCheckLoc) < 80.f && PlayBoatReverse <= 0.f;
		PlayBoatCheckLoc = Boat->GetActorLocation();
		PlayBoatCheckTime = 0.f;
		if (bStuck)
		{
			PlayBoatReverse = 2.5f;
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] boat boxed in: backing out"));
		}
	}
	if (PlayBoatReverse > 0.f)
	{
		PlayBoatReverse -= DeltaSeconds;
		PlaySetKey(EKeys::W, false);
		PlaySetKey(EKeys::S, true);
		// Reversing turns the stern the way of the rudder: steer away from the target so the bow swings to it.
		PlaySetKey(EKeys::A, Err > 0.f);
		PlaySetKey(EKeys::D, Err <= 0.f);
		return;
	}
	PlaySetKey(EKeys::S, false);
	PlaySetKey(EKeys::D, Err > 5.f);
	PlaySetKey(EKeys::A, Err < -5.f);
	PlaySetKey(EKeys::W, FMath::Abs(Err) < 70.f);
}

void AHGGtaGameMode::TickPlayTest(float DeltaSeconds)
{
	PlayTime += DeltaSeconds;
	PlayFpsAccum += DeltaSeconds;
	++PlayFpsCount;
	PlayWorstMs = FMath::Max(PlayWorstMs, DeltaSeconds * 1000.f);
	if (PlayFpsAccum >= 1.f)
	{
		PlayFpsWindow = PlayFpsAccum;
		PlayFpsFrames = PlayFpsCount;
		PlayFpsAccum = 0.f;
		PlayFpsCount = 0;
	}
	if (PlayTime > 2.f || PlayPhase > 0)
	{
		PlayTotalTime += DeltaSeconds;
		++PlayTotalFrames;
	}
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	AHGPlayerCharacter* Player = Cast<AHGPlayerCharacter>(Pawn);

	// Auto-drive stages: (road distance m, label, seconds). Distances come from the route sections.
	struct FStage { float StartM; const TCHAR* Label; float Secs; };
	static const FStage Stages[] = {
		{80.f, TEXT("auto_ThamMa"), 30.f},
		{-1.f, TEXT("auto_MaPiLeng"), 30.f},     // resolved: Panorama viewpoint
		{-2.f, TEXT("auto_TaLang"), 40.f},       // resolved: top of the Ta Lang road
	};
	auto LogStage = [this](const TCHAR* Label)
	{
		const AHGGroundVehicle* Bike = PlayBike.Get();
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] %s summary | %.0f m from stage start | top %.0f km/h | off road %.1f s"), Label,
			Bike ? FVector::Dist(Bike->GetActorLocation(), PlayStageStart) / 100.f : -1.f, PlayMaxKmh, PlayOffRoadSecs);
	};
	auto BeginKeyStage = [this]()
	{
		PlayMaxKmh = 0.f;
		PlayOffRoadSecs = 0.f;
		NextShot = 8.f;
		PlayStageStart = PlayBike.IsValid() ? PlayBike->GetActorLocation() : FVector::ZeroVector;
	};

	if (PlayPhase == 0)                    // 0: spawn, settle, check the camera at rest
	{
		if (PlayTime > 4.f)
		{
			PlayCheck(TEXT("spawn"), true);
			PlayPhase = 1;
			if (FParse::Param(FCommandLine::Get(), TEXT("hgplayboat")))
			{
				PlayPhase = 10 + int32(UE_ARRAY_COUNT(Stages));   // -hgplayboat: straight to the boat stage
				PlayTime = 0.f;
				return;
			}
			if (FParse::Param(FCommandLine::Get(), TEXT("hgprofilegpu")))
			{
				GEngine->Exec(GetWorld(), TEXT("ProfileGPU"));   // per-pass GPU timings into the log
			}
		}
		return;
	}
	if (PlayPhase == 1)                    // 1: walk forward 4 s
	{
		if (Player) { Player->AddMovementInput(Player->GetActorForwardVector(), 1.f); }
		if (PlayTime > 8.5f) { PlayCheck(TEXT("walk"), true); PlayPhase = 2; }
		return;
	}
	if (PlayPhase == 2)                    // 2: mount the nearest parked bike
	{
		AHGGroundVehicle* Best = nullptr;
		float BestD = 1e12f;
		for (TActorIterator<AHGGroundVehicle> It(GetWorld()); It; ++It)
		{
			if (It->IsAutoDriving() || It->IsWaterVehicle() || !Player) { continue; }
			const float D = FVector::DistSquared(It->GetActorLocation(), Player->GetActorLocation());
			if (D < BestD) { BestD = D; Best = *It; }
		}
		if (Best && Player && Best->EnterVehicle(Player))
		{
			PlayBike = Best;
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] mounted %s at %.0f m"), *Best->GetName(), FMath::Sqrt(BestD) / 100.f);
		}
		else
		{
			UE_LOG(LogHaGiang, Error, TEXT("[HG_PLAY] no bike to mount"));
		}
		PlayPhase = 3; PlayTime = 0.f;
		return;
	}
	if ((!PlayBike.IsValid() || !PlayRoad.IsValid()) && PlayPhase >= 3 && PlayPhase < 10 + int32(UE_ARRAY_COUNT(Stages)))
	{
		PlayPhase = 99;
	}
	if (PlayPhase == 3 && PlayTime > 2.f)  // 3: on the bike at rest
	{
		PlayCheck(TEXT("on_bike"), true);
		BeginKeyStage();
		PlayPhase = 4; PlayTime = 0.f;
		return;
	}
	if (PlayPhase == 4)                    // 4: ride Doc Tham Ma on the keys (W/S/A/D) from the parking spot
	{
		PlayDriveKeys(DeltaSeconds);
		if (PlayTime >= 6.f && NextShot == 8.f)
		{
			PlayCheck(TEXT("keys_6s"), true);
			NextShot = 14.f;
		}
		else if (PlayTime >= NextShot && NextShot > 8.f)
		{
			PlayCheck(*FString::Printf(TEXT("keys_ThamMa_%02d"), int32(PlayTime)), true);
			NextShot += 8.f;
		}
		if (PlayTime > 40.f)
		{
			PlayReleaseKeys();
			LogStage(TEXT("keys_ThamMa"));
			PlaySetKey(EKeys::R, true);     // [R]: back onto the road
			PlayPhase = 5; PlayTime = 0.f;
		}
		return;
	}
	if (PlayPhase == 5)                    // 5: [R] recovery check
	{
		PlaySetKey(EKeys::R, false);
		if (PlayTime > 2.f)
		{
			PlayCheck(TEXT("after_R"), true);
			// Straightest 150 m of the route: hold W only, no steering (acceleration / top speed check).
			const USplineComponent* Sp = PlayRoad->GetSpline();
			float BestD = 10000.f, BestTurn = BIG_NUMBER;
			for (float D = 10000.f; D < Sp->GetSplineLength() - 20000.f; D += 2500.f)
			{
				const float Yaw0 = Sp->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).Rotation().Yaw;
				const float Z0 = Sp->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).Z;
				float Turn = 0.f;
				for (float S = 1000.f; S <= 15000.f; S += 1000.f)
				{
					Turn = FMath::Max(Turn, FMath::Abs(FRotator::NormalizeAxis(Sp->GetDirectionAtDistanceAlongSpline(D + S, ESplineCoordinateSpace::World).Rotation().Yaw - Yaw0)));
				}
				// Prefer level road: count every 1 m of climb like 0.5 deg of turn.
				Turn += 0.005f * FMath::Abs(Sp->GetLocationAtDistanceAlongSpline(D + 15000.f, ESplineCoordinateSpace::World).Z - Z0);
				if (Turn < BestTurn) { BestTurn = Turn; BestD = D; }
			}
			PlayBike->PlaceOnRoad(PlayRoad.Get(), BestD, +1, 120.f);
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] stage W_straight from %.0f m (max turn+grade score %.1f)"), BestD / 100.f, BestTurn);
			PlayPhase = 7; PlayTime = 0.f;
			BeginKeyStage();
		}
		return;
	}
	if (PlayPhase == 7)                    // 7: W only, 6 s, on the straight
	{
		if (PlayTime > 1.f)
		{
			PlaySetKey(EKeys::W, true);
			PlayMaxKmh = FMath::Max(PlayMaxKmh, PlayBike->GetSpeedKmh());
		}
		if (PlayTime > 1.f && PlayCheckpointLoc.IsZero())
		{
			PlayCheckpointLoc = PlayBike->GetActorLocation();
			PlayLastLoc = PlayCheckpointLoc;   // "moved" in the next check = distance covered with W held
		}
		if (PlayTime > 7.f)
		{
			PlayCheck(TEXT("W_straight_6s"), true);
			PlaySetKey(EKeys::W, false);
			LogStage(TEXT("W_straight_6s"));
			PlayCheckpointLoc = FVector::ZeroVector;
			// Ta Lang concrete hairpins, ridden on the keys.
			const float From = PlayRoad->SecondaryFromDistance >= 0.f ? PlayRoad->SecondaryFromDistance + 40000.f : PlayRoad->GetRoadLength() * 0.8f;
			PlayBike->PlaceOnRoad(PlayRoad.Get(), From, +1, 80.f);
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] stage keys_TaLang from %.0f m"), From / 100.f);
			PlayPhase = 6; PlayTime = 0.f;
			BeginKeyStage();
		}
		return;
	}
	if (PlayPhase == 6)                    // 6: keys on the Ta Lang hairpins
	{
		PlayDriveKeys(DeltaSeconds);
		if (PlayTime >= NextShot)
		{
			PlayCheck(*FString::Printf(TEXT("keys_TaLang_%02d"), int32(PlayTime)), true);
			NextShot += 8.f;
		}
		if (PlayTime > 40.f)
		{
			PlayReleaseKeys();
			LogStage(TEXT("keys_TaLang"));
			PlayPhase = 10; PlayTime = 0.f; NextShot = 0.f;
		}
		return;
	}
	if (PlayPhase >= 10 && PlayPhase < 10 + int32(UE_ARRAY_COUNT(Stages)))
	{
		const FStage& St = Stages[PlayPhase - 10];
		if (NextShot == 0.f)   // first tick of the stage: place the bike
		{
			float StartCm = St.StartM * 100.f;
			if (St.StartM < 0.f)
			{
				// Viewpoints sorted along the road: [0] Tham Ma, [1] Ma Pi Leng, [2] Tu San dock.
				TArray<float> D;
				for (TActorIterator<AHGViewpointActor> It(GetWorld()); It; ++It)
				{
					D.Add(PlayRoad->GetDistanceAlongRoad(It->GetActorLocation()));
				}
				D.Sort();
				if (D.Num() >= 3)
				{
					StartCm = St.StartM < -1.5f ? FMath::Max(0.f, D[2] - 620000.f)   // top of the Ta Lang road
					                            : FMath::Max(0.f, D[1] - 25000.f);    // 250 m before the Panorama
				}
			}
			PlayBike->bAutoDriveWithPlayer = true;
			PlayBike->StartAutoDrive(PlayRoad.Get(), StartCm, +1, 38.f, 160.f);
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] stage %s from %.0f m"), St.Label, StartCm / 100.f);
			PlayMaxKmh = 0.f;
			PlayStageStart = PlayBike->GetActorLocation();
			NextShot = 3.f;
		}
		PlayMaxKmh = FMath::Max(PlayMaxKmh, PlayBike->GetSpeedKmh());
		if (PlayTime >= NextShot)
		{
			PlayCheck(*FString::Printf(TEXT("%s_%02d"), St.Label, int32(PlayTime)), true);
			NextShot += 9.f;
		}
		if (PlayTime > St.Secs)
		{
			PlayOffRoadSecs = 0.f;
			LogStage(St.Label);
			PlayPhase++; PlayTime = 0.f; NextShot = 0.f;
		}
		return;
	}
	if (PlayPhase == 10 + int32(UE_ARRAY_COUNT(Stages)))   // boats: off the bike, board the dock cano with [F]
	{
		if (PlayTime < 0.5f)
		{
			if (PlayBike.IsValid() && PlayBike->GetDriver())
			{
				PlayBike->StopAutoDrive();
				PlayBike->bAutoDriveWithPlayer = false;
				PlayBike->ExitVehicle();
			}
			return;
		}
		AHGPlayerCharacter* Walker = Cast<AHGPlayerCharacter>(PC->GetPawn());
		ANhoQueBoatPawn* Dock = nullptr;
		const ANhoQueBoatPawn* Cruiser = nullptr;
		for (TActorIterator<ANhoQueBoatPawn> It(GetWorld()); It; ++It)
		{
			if (It->bAutoCruise) { if (!Cruiser || It->GetBoatType() == EHGBoatType::Cano) { Cruiser = *It; } continue; }
			if (It->GetBoatType() == EHGBoatType::Cano && (!Dock || It->GetActorLocation().Z < Dock->GetActorLocation().Z)) { Dock = *It; }
		}
		if (!Walker || !Dock)
		{
			UE_LOG(LogHaGiang, Error, TEXT("[HG_PLAY] boat stage: %s"), !Walker ? TEXT("player still on the bike") : TEXT("no docked cano"));
			PlayPhase = 99;
			return;
		}
		// Walk up to it (teleport next to the hull on the bank side), then [F].
		const FVector Side = Dock->GetActorRightVector().GetSafeNormal2D();
		Walker->SetActorLocation(Dock->GetActorLocation() + Side * 250.f + FVector(0.f, 0.f, 150.f), false, nullptr, ETeleportType::TeleportPhysics);
		Walker->SetActorLocation(Dock->GetActorLocation() - Side * 250.f + FVector(0.f, 0.f, 150.f), false, nullptr, ETeleportType::TeleportPhysics);
		const bool bIn = Dock->EnterVehicle(Walker);
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] boarded %s: %s (cruise line %d points, first %s, last %s, boat at %s)"), *Dock->GetName(), bIn ? TEXT("yes") : TEXT("NO"),
			Cruiser ? Cruiser->CruisePoints.Num() : 0, Cruiser && Cruiser->CruisePoints.Num() ? *Cruiser->CruisePoints[0].ToString() : TEXT("-"),
			Cruiser && Cruiser->CruisePoints.Num() ? *Cruiser->CruisePoints.Last().ToString() : TEXT("-"), *Dock->GetActorLocation().ToString());
		PlayBoat = Dock;
		PlayBoatPath = Cruiser ? Cruiser->CruisePoints : TArray<FVector>();
		PlayBoatIndex = 0;
		float BestD = BIG_NUMBER;
		for (int32 i = 0; i < PlayBoatPath.Num(); ++i)
		{
			const float D = FVector::Dist2D(PlayBoatPath[i], Dock->GetActorLocation());
			if (D < BestD) { BestD = D; PlayBoatIndex = i; }
		}
		// Head for the far end of the line (into the gorge) from the closest point.
		if (PlayBoatPath.Num() > 1 && PlayBoatIndex > PlayBoatPath.Num() / 2)
		{
			Algo::Reverse(PlayBoatPath);
			PlayBoatIndex = PlayBoatPath.Num() - 1 - PlayBoatIndex;
		}
		PlayPhase = 30; PlayTime = 0.f;
		PlayMaxKmh = 0.f;
		PlayStageStart = Dock->GetActorLocation();
		NextShot = 2.f;
		return;
	}
	if (PlayPhase == 30)                   // boat ride on the keys
	{
		if (PlayTime > 1.f) { PlayDriveBoatKeys(DeltaSeconds); }
		if (PlayTime >= NextShot)
		{
			PlayCheck(*FString::Printf(TEXT("boat_%02d"), int32(PlayTime)), true);
			NextShot += 9.f;
		}
		if (PlayTime > 38.f)
		{
			PlayReleaseKeys();
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] boat summary | %.0f m from the dock | top %.0f km/h | cruise point %d/%d"),
				PlayBoat.IsValid() ? FVector::Dist2D(PlayBoat->GetActorLocation(), PlayStageStart) / 100.f : -1.f, PlayMaxKmh, PlayBoatIndex, PlayBoatPath.Num());
			PlayPhase = 99;
		}
		return;
	}
	if (PlayPhase == 99)
	{
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] done | average %.1f FPS over %d frames, worst frame %.0f ms"),
			PlayTotalFrames / FMath::Max(PlayTotalTime, 1e-3f), PlayTotalFrames, PlayWorstMs);
		PlayPhase = 100;
		UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
	}
}

void AHGGtaGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPlayTest)
	{
		TickPlayTest(DeltaSeconds);
		return;
	}
	if (!bPerfRun || PerfStage < 0 || PerfStage >= PerfSpots.Num())
	{
		return;
	}
	PerfTime += DeltaSeconds;
	if (PerfTime < 0.f)
	{
		// Warm-up at the new spot: move there first, measure once streaming has settled.
		if (!bPerfMoved)
		{
			bPerfMoved = true;
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					Pawn->SetActorLocation(PerfSpots[PerfStage].GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
					PC->SetControlRotation(FRotator(-15.f, PerfSpots[PerfStage].Rotator().Yaw, 0.f));
				}
			}
		}
		return;
	}
	PerfAccum += DeltaSeconds;
	PerfWorst = FMath::Max(PerfWorst, (double)DeltaSeconds);
	++PerfFrames;
	if (PerfTime > 12.f)
	{
		const double AvgMs = 1000.0 * PerfAccum / FMath::Max(PerfFrames, 1);
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PERF] %s: avg %.1f ms (%.0f FPS), worst %.1f ms, %d frames"),
			*PerfNames[PerfStage], AvgMs, 1000.0 / FMath::Max(AvgMs, 0.001), PerfWorst * 1000.0, PerfFrames);
		++PerfStage;
		bPerfMoved = false;
		PerfTime = -4.f;
		PerfAccum = 0.0;
		PerfWorst = 0.0;
		PerfFrames = 0;
		if (PerfStage >= PerfSpots.Num())
		{
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
	}
}
