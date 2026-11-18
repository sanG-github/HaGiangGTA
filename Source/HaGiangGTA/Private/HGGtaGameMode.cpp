#include "HGGtaGameMode.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HaGiangGTA.h"
#include "HGGtaHUD.h"
#include "HGGroundVehicle.h"
#include "HGRoadSplineActor.h"
#include "Components/SplineComponent.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HGPlayerCharacter.h"
#include "HGViewpointActor.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"

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
		TEXT("[HG_PLAY] %s | moved %.1f m | pawn %s alt %.0f m, above ground %.1f m | cam pitch %.0f, cam above ground %.1f m%s | %s %.0f km/h | road offset %.1f m | %.0f FPS"),
		Label, Moved, *Pawn->GetClass()->GetName(), PawnLoc.Z / 100.f, bPG ? (PawnLoc.Z - PawnGround) / 100.f : -999.f,
		CamRot.Pitch, bCG ? (CamLoc.Z - CamGround) / 100.f : -999.f, bCamUnder ? TEXT(" CAMERA UNDERGROUND") : TEXT(""),
		Veh ? TEXT("vehicle") : TEXT("on foot"), Veh ? Veh->GetSpeedKmh() : 0.f, RoadOff,
		1.f / FMath::Max(GetWorld()->GetDeltaSeconds(), 1e-3f));
	if (bShot)
	{
		const FString File = FPaths::ProjectSavedDir() / TEXT("HG_CI/Play") / FString::Printf(TEXT("%02d_%s.png"), PlayShot++, Label);
		FScreenshotRequest::RequestScreenshot(File, true, false);
	}
}

void AHGGtaGameMode::TickPlayTest(float DeltaSeconds)
{
	PlayTime += DeltaSeconds;
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	AHGPlayerCharacter* Player = Cast<AHGPlayerCharacter>(Pawn);

	// Ride stages: (road distance m, label, seconds). Distances come from the route sections.
	struct FStage { float StartM; const TCHAR* Label; float Secs; };
	static const FStage Stages[] = {
		{80.f, TEXT("ride_ThamMa"), 60.f},
		{-1.f, TEXT("ride_MaPiLeng"), 40.f},     // resolved: Panorama viewpoint
		{-2.f, TEXT("ride_TaLang"), 60.f},       // resolved: top of the Ta Lang road
	};

	if (PlayPhase == 0)                    // 0: spawn, settle, check the camera at rest
	{
		if (PlayTime > 4.f) { PlayCheck(TEXT("spawn"), true); PlayPhase = 1; }
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
	if (PlayPhase == 3 && PlayTime > 2.f)
	{
		PlayCheck(TEXT("on_bike"), true);
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.f));   // the player holds W
		PlayPhase = 4; PlayTime = 0.f;
		return;
	}
	if (PlayPhase == 4)
	{
		if (PlayTime > 6.f)
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.f));
			PlayCheck(TEXT("hold_W_6s"), true);
			PlayPhase = 10; PlayTime = 0.f;
		}
		return;
	}
	if (PlayPhase >= 10 && PlayPhase < 10 + int32(UE_ARRAY_COUNT(Stages)))
	{
		const FStage& St = Stages[PlayPhase - 10];
		if (!PlayBike.IsValid() || !PlayRoad.IsValid()) { PlayPhase = 99; return; }
		if (PlayTime == DeltaSeconds || NextShot == 0.f)   // first tick of the stage: place the bike
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
			NextShot = 3.f;
		}
		if (PlayTime >= NextShot)
		{
			PlayCheck(*FString::Printf(TEXT("%s_%02d"), St.Label, int32(PlayTime)), true);
			NextShot += 8.f;
		}
		if (PlayTime > St.Secs)
		{
			PlayPhase++; PlayTime = 0.f; NextShot = 0.f;
		}
		return;
	}
	if (PlayPhase == 10 + int32(UE_ARRAY_COUNT(Stages)) || PlayPhase == 99)
	{
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] done"));
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
