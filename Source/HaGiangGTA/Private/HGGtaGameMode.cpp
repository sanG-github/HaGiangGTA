#include "HGGtaGameMode.h"

#include "Algo/Reverse.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "HGWaterBody.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
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
	StartAmbientAudio();
	if (bPlayTest)
	{
		for (TActorIterator<AHGRoadSplineActor> It(GetWorld()); It; ++It)
		{
			if (!PlayRoad.IsValid() || It->GetRoadLength() > PlayRoad->GetRoadLength())
			{
				PlayRoad = *It;
			}
		}
		AHGVehicleBase::bOrbitInputEnabled = false;   // only the scripted mouse test drives the orbit
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] playtest start"));
		return;
	}
	if (!bPerfRun)
	{
		return;
	}
	// Spot 0: wherever the player spawned (bottom of Doc Tham Ma), through the player's own camera; then the
	// review cameras (same framing as the editor screenshots) through a camera actor.
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		PerfSpots.Add(Pawn->GetActorTransform());
		PerfNames.Add(TEXT("Spawn_ThamMa"));
		PerfFov.Add(0.f);
	}
	FString Json;
	const FString CamFile = FPaths::ProjectDir() / TEXT("Generated/Heightmaps/HaGiang_review_cameras.json");
	TSharedPtr<FJsonObject> Root;
	if (FFileHelper::LoadFileToString(Json, *CamFile) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid())
	{
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("cameras")))
		{
			const TSharedPtr<FJsonObject> C = V->AsObject();
			const TArray<TSharedPtr<FJsonValue>>& Cam = C->GetArrayField(TEXT("cam_cm"));
			const TArray<TSharedPtr<FJsonValue>>& Look = C->GetArrayField(TEXT("look_cm"));
			const FVector P(Cam[0]->AsNumber(), Cam[1]->AsNumber(), Cam[2]->AsNumber());
			const FVector L(Look[0]->AsNumber(), Look[1]->AsNumber(), Look[2]->AsNumber());
			PerfSpots.Add(FTransform((L - P).Rotation(), P));
			PerfNames.Add(C->GetStringField(TEXT("name")));
			PerfFov.Add(float(C->GetNumberField(TEXT("fov"))));
		}
	}
	for (TActorIterator<AHGViewpointActor> It(GetWorld()); It && PerfSpots.Num() <= 1; ++It)
	{
		const FVector Loc = It->GetActorLocation() + FVector(0.f, 0.f, 150.f);
		const FRotator Look = (It->OrbitCenter - Loc).Rotation();
		PerfSpots.Add(FTransform(FRotator(0.f, Look.Yaw, 0.f), Loc));
		PerfNames.Add(It->GetName());
		PerfFov.Add(0.f);
	}
	PerfStage = 0;
	PerfTime = -5.f; // warm-up (streaming, shader cache)
	UE_LOG(LogHaGiang, Display, TEXT("[HG_PERF] benchmark start, %d spots"), PerfSpots.Num());
}

namespace
{
	USoundBase* LoadLoop(const TCHAR* Name)
	{
		return LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/HaGiang/Generated/Audio/%s.%s"), Name, Name));
	}
}

void AHGGtaGameMode::StartAmbientAudio()
{
	auto Make = [this](const TCHAR* Name) -> UAudioComponent*
	{
		USoundBase* S = LoadLoop(Name);
		if (!S)
		{
			UE_LOG(LogHaGiang, Warning, TEXT("Ambient audio: %s missing (run the CP05 build to import it)"), Name);
			return nullptr;
		}
		UAudioComponent* A = UGameplayStatics::CreateSound2D(this, S, 0.f, 1.f, 0.f, nullptr, true, false);
		if (A)
		{
			A->Play(FMath::FRand() * 2.f);
		}
		return A;
	};
	WindAudio = Make(TEXT("SW_HG_Wind_Loop"));
	BirdsAudio = Make(TEXT("SW_HG_Birds_Loop"));
	TyreAudio = Make(TEXT("SW_HG_Tyre_Loop"));
}

void AHGGtaGameMode::UpdateAmbientAudio(float DeltaSeconds)
{
	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	const AHGVehicleBase* Veh = Cast<AHGVehicleBase>(Pawn);
	const AHGGroundVehicle* Ground = Cast<AHGGroundVehicle>(Pawn);
	const float Kmh = Veh ? FMath::Abs(Veh->GetSpeedKmh()) : Pawn->GetVelocity().Size2D() * 0.036f;
	// Altitude above the river (450 m) to the passes (~1500 m): more wind up high, even standing still.
	const float AltM = Pawn->GetActorLocation().Z / 100.f;
	const float High = FMath::GetMappedRangeValueClamped(FVector2D(600.f, 1500.f), FVector2D(0.f, 1.f), AltM);
	const float SpeedWind = FMath::Clamp((Kmh - 8.f) / 60.f, 0.f, 1.f);
	const float WindTarget = 0.08f + 0.12f * High + 0.65f * SpeedWind * SpeedWind;
	// Birds and insects carry when on foot / slow, drowned by wind and engine at speed.
	const float BirdsTarget = 0.35f * (1.f - FMath::Clamp(Kmh / 35.f, 0.f, 1.f)) * (Veh && Veh->IsWaterVehicle() ? 0.6f : 1.f);
	// Tyres: hiss on asphalt, rumble on the concrete slabs (joints every 4 m), crunch off-road.
	float TyreTarget = 0.f, TyrePitch = 1.f;
	if (Ground && Ground->IsGrounded() && Kmh > 3.f)
	{
		const EHGSurface Surf = Ground->GetSurface();
		const float SurfGain = Surf == EHGSurface::Concrete ? 0.55f : Surf == EHGSurface::OffRoad ? 0.75f : 0.25f;
		TyreTarget = SurfGain * FMath::Clamp(Kmh / 50.f, 0.15f, 1.f);
		TyrePitch = (Surf == EHGSurface::OffRoad ? 0.7f : 0.9f) + Kmh / 140.f;
	}
	WindVol = FMath::FInterpTo(WindVol, WindTarget, DeltaSeconds, 2.f);
	BirdsVol = FMath::FInterpTo(BirdsVol, BirdsTarget, DeltaSeconds, 1.f);
	TyreVol = FMath::FInterpTo(TyreVol, TyreTarget, DeltaSeconds, 4.f);
	if (WindAudio) { WindAudio->SetVolumeMultiplier(WindVol); WindAudio->SetPitchMultiplier(0.85f + 0.4f * SpeedWind); }
	if (BirdsAudio) { BirdsAudio->SetVolumeMultiplier(BirdsVol); }
	if (TyreAudio) { TyreAudio->SetVolumeMultiplier(TyreVol); TyreAudio->SetPitchMultiplier(TyrePitch); }
}

FString AHGGtaGameMode::AudioState() const
{
	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	const AHGVehicleBase* Veh = PC ? Cast<AHGVehicleBase>(PC->GetPawn()) : nullptr;
	return FString::Printf(TEXT("audio: engine %s | wind %.2f%s | birds %.2f%s | tyres %.2f%s"),
		Veh ? *FString::Printf(TEXT("pitch %.2f vol %.2f"), Veh->GetEnginePitch(), Veh->GetEngineVolumeNow()) : TEXT("-"),
		WindVol, WindAudio && WindAudio->IsPlaying() ? TEXT("") : TEXT(" (not playing)"),
		BirdsVol, BirdsAudio && BirdsAudio->IsPlaying() ? TEXT("") : TEXT(" (not playing)"),
		TyreVol, TyreAudio && TyreAudio->IsPlaying() ? TEXT("") : TEXT(" (not playing)"));
}

void AHGGtaGameMode::PlayResetStageStats()
{
	PlayTotNear += PlayNearMiss;
	PlayTotStops += PlayStops;
	PlayTotOncoming += PlayOncoming;
	PlayTotOvertookUs += PlayOvertookUs;
	PlayNearMiss = PlayStops = PlayOncoming = PlayOvertookUs = 0;
	PlayStopTime.Reset();
	PlayLongOffset.Reset();
	PlayMet.Reset();
	PlayLaneErrSum = 0.0;
	PlayLaneErrMax = 0.f;
	PlayLaneSamples = 0;
}

FString AHGGtaGameMode::PlayTrafficSummary() const
{
	const AHGGroundVehicle::FTrafficStats& T = AHGGroundVehicle::TrafficStats;
	return FString::Printf(TEXT("traffic: %d near misses, %d AI dead stops, %d oncoming met, %d overtook us | session AI: %d overtakes, %d yields, %d go-arounds, %d unsticks%s"),
		PlayNearMiss, PlayStops, PlayOncoming, PlayOvertookUs, T.Overtakes, T.Yields, T.GoArounds, T.Unsticks,
		PlayLaneSamples > 0 ? *FString::Printf(TEXT(" | lane error mean %.2f m, max %.2f m"), PlayLaneErrSum / PlayLaneSamples / 100.0, PlayLaneErrMax / 100.f) : TEXT(""));
}

void AHGGtaGameMode::PlayTrafficTick(float DeltaSeconds)
{
	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	const APawn* Me = PC ? PC->GetPawn() : nullptr;
	if (!Me)
	{
		return;
	}
	const AHGGroundVehicle* MeV = Cast<AHGGroundVehicle>(Me);
	if (MeV && MeV->IsAutoDriving())
	{
		const float E = FMath::Abs(MeV->GetCrossTrackError());
		PlayLaneErrSum += E;
		PlayLaneErrMax = FMath::Max(PlayLaneErrMax, E);
		++PlayLaneSamples;
	}
	const FVector MyLoc = Me->GetActorLocation();
	const FVector MyFwd = Me->GetActorForwardVector().GetSafeNormal2D();
	const FVector MyVel = MeV ? MyFwd * MeV->GetSpeed() : Me->GetVelocity();
	const double Now = GetWorld()->GetTimeSeconds();
	for (TActorIterator<AHGGroundVehicle> It(GetWorld()); It; ++It)
	{
		AHGGroundVehicle* V = *It;
		if (V == Me || !V->IsAutoDriving())
		{
			continue;
		}
		const FVector D = V->GetActorLocation() - MyLoc;
		const float Dist = D.Size2D();
		if (Dist > 30000.f)
		{
			continue;
		}
		const TWeakObjectPtr<AActor> Key(V);
		const FVector VFwd = V->GetActorForwardVector().GetSafeNormal2D();
		// Near miss: inside 2.2 m at more than 15 km/h relative speed.
		const float Rel = (VFwd * V->GetSpeed() - MyVel).Size2D();
		if (Dist < 220.f && Rel > 420.f && Now - PlayNearCooldown.FindRef(Key) > 3.0)
		{
			PlayNearCooldown.Add(Key, Now);
			++PlayNearMiss;
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY]    near miss: %s at %.1f m, %.0f km/h relative"), *V->GetName(), Dist / 100.f, Rel * 0.036f);
		}
		// Dead stop: an AI vehicle within 300 m standing still for 4 s (yielding on a hairpin is allowed).
		float& Stop = PlayStopTime.FindOrAdd(Key);
		const float Before = Stop;
		Stop = (FMath::Abs(V->GetSpeed()) < 50.f && !V->IsYielding()) ? Stop + DeltaSeconds : 0.f;
		if (Before < 4.f && Stop >= 4.f)
		{
			++PlayStops;
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY]    AI dead stop: %s at road %.0f m, %.0f m from the player: %s"), *V->GetName(), V->GetRoadDistance() / 100.f, Dist / 100.f, *V->GetSpeedReason());
		}
		// Oncoming met / overtook us: longitudinal offset along our heading changes sign close by.
		const float Long = FVector::DotProduct(D, MyFwd);
		const float Lat = FMath::Abs(FVector::DotProduct(D, FVector::CrossProduct(FVector::UpVector, MyFwd)));
		if (const float* Prev = PlayLongOffset.Find(Key))
		{
			if (Lat < 1500.f)
			{
				const bool bOpposite = FVector::DotProduct(VFwd, MyFwd) < -0.3f;
				if (bOpposite && *Prev > 0.f && Long <= 0.f && !PlayMet.Contains(Key)) { ++PlayOncoming; PlayMet.Add(Key); }
				if (!bOpposite && *Prev < -100.f && Long >= 100.f) { ++PlayOvertookUs; }
			}
		}
		PlayLongOffset.Add(Key, Long);
	}
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
	float CamWater = 0.f;
	const bool bCamWet = AHGWaterBody::FindSurface(GetWorld(), CamLoc, CamWater) && CamLoc.Z < CamWater + 10.f;
	UE_LOG(LogHaGiang, Display,
		TEXT("[HG_PLAY] %s | moved %.1f m | pawn %s alt %.0f m, above ground %.1f m | cam pitch %.0f, cam above ground %.1f m%s%s | %s %.0f km/h | road offset %.1f m | %.0f FPS (game %.1f / draw %.1f / GPU %.1f ms, %d draws, %.1fM tris)"),
		Label, Moved, *Pawn->GetClass()->GetName(), PawnLoc.Z / 100.f, bPG ? (PawnLoc.Z - PawnGround) / 100.f : -999.f,
		CamRot.Pitch, bCG ? (CamLoc.Z - CamGround) / 100.f : -999.f, bCamUnder ? TEXT(" CAMERA UNDERGROUND") : TEXT(""), bCamWet ? TEXT(" CAMERA BELOW WATER") : TEXT(""),
		Veh ? TEXT("vehicle") : TEXT("on foot"), Veh ? Veh->GetSpeedKmh() : 0.f, RoadOff,
		PlayFps(), FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
		FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()), GNumDrawCallsRHI[0], GNumPrimitivesDrawnRHI[0] / 1.0e6f);
	{
		const AHGGroundVehicle* GV = Cast<AHGGroundVehicle>(Pawn);
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY]    %s | %s%s"), *AudioState(),
			GV && GV->IsAutoDriving() ? *FString::Printf(TEXT("lane: road offset %+.2f m, off its lane line %.2f m | AI speed: %s"), GV->GetLateralOffset() / 100.f, GV->GetCrossTrackError() / 100.f, *GV->GetSpeedReason().Left(80))
			                         : TEXT(""),
			Veh && (Veh->IsLookingBack() || FMath::Abs(Veh->GetOrbitYaw()) > 1.f) ? *FString::Printf(TEXT(" | orbit yaw %.0f%s"), Veh->GetOrbitYaw(), Veh->IsLookingBack() ? TEXT(" LOOK-BACK") : TEXT("")) : TEXT(""));
	}
	FString ProfileAt;
	if (FParse::Value(FCommandLine::Get(), TEXT("hgprofile="), ProfileAt) && ProfileAt == Label)
	{
		GEngine->Exec(GetWorld(), TEXT("ProfileGPU"));   // -hgprofile=<check label>: per-pass GPU timings into the log
	}
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
	PlaySetKey(EKeys::LeftShift, FMath::Abs(Err) < 12.f);   // open it up on the straights of the gorge
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
	if (PlayPhase >= 3)
	{
		PlayTrafficTick(DeltaSeconds);
	}
	// The test window sometimes shrinks (another Unreal instance starting on the same desktop): put it back,
	// so screenshots and FPS are always measured at the same resolution.
	PlayResCheck -= DeltaSeconds;
	if (PlayResCheck <= 0.f && GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		PlayResCheck = 2.f;
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Size.X > 0 && Size.X < 900)
		{
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY]    viewport shrank to %dx%d: restoring 1280x720"), Size.X, Size.Y);
			GEngine->Exec(GetWorld(), TEXT("r.SetRes 1280x720w"));
		}
	}

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
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] %s summary | %.0f m from stage start | top %.0f km/h | off road %.1f s | %s"), Label,
			Bike ? FVector::Dist(Bike->GetActorLocation(), PlayStageStart) / 100.f : -1.f, PlayMaxKmh, PlayOffRoadSecs, *PlayTrafficSummary());
		PlayResetStageStats();
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
		if (PlayTime > 41.5f)   // never teleport in the frame a screenshot was requested
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
		if (PlayTime > 2.f && NextShot >= 0.f)
		{
			PlayCheck(TEXT("after_R"), true);
			NextShot = -1.f;
		}
		if (PlayTime > 3.5f)
		{
			NextShot = 0.f;
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
		if (PlayTime > 7.f && NextShot >= 0.f)
		{
			PlayCheck(TEXT("W_straight_6s"), true);   // W still held: the bike is moving in the shot
			LogStage(TEXT("W_straight_6s"));
			NextShot = -1.f;
		}
		if (PlayTime > 8.5f)
		{
			NextShot = 0.f;
			PlaySetKey(EKeys::W, false);
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
	if (PlayPhase == 6)                    // 6: keys on the Ta Lang hairpins (+ mouse orbit and [C] look-back)
	{
		PlayDriveKeys(DeltaSeconds);
		AHGVehicleBase::bOrbitInputEnabled = PlayTime > 17.9f && PlayTime < 19.6f;
		if (PlayTime > 18.f && PlayTime < 19.5f)
		{
			// Swing the camera to the right with the mouse (simulated mouse-X deltas through Enhanced Input).
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 6.f, 1));
		}
		if (PlayTime >= 19.6f && PlayOrbitShot == 0)
		{
			PlayOrbitShot = 1;
			PlayCheck(TEXT("orbit_mouse"), true);
		}
		if (PlayTime >= 26.f && PlayTime < 28.5f) { PlaySetKey(EKeys::C, true); }
		if (PlayTime >= 27.5f && PlayOrbitShot == 1)
		{
			PlayOrbitShot = 2;
			PlayCheck(TEXT("look_back_C"), true);
		}
		if (PlayTime >= 28.5f) { PlaySetKey(EKeys::C, false); }
		if (PlayTime >= NextShot)
		{
			PlayCheck(*FString::Printf(TEXT("keys_TaLang_%02d"), int32(PlayTime)), true);
			NextShot += 8.f;
		}
		if (PlayTime > 41.5f)
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
			PlayBike->StartAutoDrive(PlayRoad.Get(), StartCm, +1, 38.f, 140.f);
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
		if (PlayTime > St.Secs + 1.5f)
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
			// The player's cano (Boat_Cano_*, world.build_vehicles), not one of the moored tour boats.
			if (It->GetBoatType() == EHGBoatType::Cano && It->GetActorNameOrLabel().StartsWith(TEXT("Boat_Cano"))) { Dock = *It; }
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
		// Ride from the dock to the Ma Pi Leng bend: the cruise point closest to the Ma Pi Leng viewpoint
		// (viewpoints sorted along the road: [1]). Trim the line to dock..bend, oriented that way.
		auto Nearest = [this](const FVector& P)
		{
			int32 Best = 0;
			for (int32 i = 1; i < PlayBoatPath.Num(); ++i)
			{
				if (FVector::DistSquared2D(PlayBoatPath[i], P) < FVector::DistSquared2D(PlayBoatPath[Best], P)) { Best = i; }
			}
			return Best;
		};
		TArray<TPair<float, FVector>> Vps;
		for (TActorIterator<AHGViewpointActor> It(GetWorld()); It; ++It)
		{
			Vps.Add({ PlayRoad.IsValid() ? PlayRoad->GetDistanceAlongRoad(It->GetActorLocation()) : 0.f, It->GetActorLocation() });
		}
		Vps.Sort([](const TPair<float, FVector>& A, const TPair<float, FVector>& B) { return A.Key < B.Key; });
		if (PlayBoatPath.Num() > 1 && Vps.Num() >= 2)
		{
			const int32 From = Nearest(Dock->GetActorLocation());
			const int32 To = Nearest(Vps[1].Value);
			TArray<FVector> Leg;
			for (int32 i = From; i != To; i += (To > From ? 1 : -1)) { Leg.Add(PlayBoatPath[i]); }
			Leg.Add(PlayBoatPath[To]);
			PlayBoatPath = Leg;
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] boat route: cruise points %d -> %d (Ma Pi Leng bend at %s)"), From, To, *PlayBoatPath.Last().ToString());
		}
		PlayBoatIndex = 0;
		PlayPhase = 30; PlayTime = 0.f;
		PlayMaxKmh = 0.f;
		PlayStageStart = Dock->GetActorLocation();
		NextShot = 3.f;
		PlayBoatCheckLoc = Dock->GetActorLocation();
		return;
	}
	if (PlayPhase == 30)                   // boat ride on the keys
	{
		if (PlayTime > 1.f) { PlayDriveBoatKeys(DeltaSeconds); }
		if (PlayTime >= NextShot)
		{
			PlayCheck(*FString::Printf(TEXT("boat_%03d"), int32(PlayTime)), true);
			NextShot += 25.f;
		}
		const bool bArrived = PlayBoatPath.Num() > 0 && PlayBoat.IsValid()
			&& FVector::Dist2D(PlayBoat->GetActorLocation(), PlayBoatPath.Last()) < 3000.f;
		if (bArrived && NextShot >= 0.f && PlayBoatArrivedAt < 0.f)
		{
			PlayBoatArrivedAt = PlayTime;
			PlayCheck(TEXT("boat_MaPiLeng_bend"), true);
		}
		if ((PlayBoatArrivedAt >= 0.f && PlayTime > PlayBoatArrivedAt + 1.5f) || PlayTime > 300.f)
		{
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] boat %s after %.0f s"), PlayBoatArrivedAt >= 0.f ? TEXT("reached the Ma Pi Leng bend") : TEXT("did NOT reach the Ma Pi Leng bend"),
				PlayBoatArrivedAt >= 0.f ? PlayBoatArrivedAt : PlayTime);
			PlayReleaseKeys();
			UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] boat summary | %.0f m from the dock (straight line) | top %.0f km/h | cruise point %d/%d"),
				PlayBoat.IsValid() ? FVector::Dist2D(PlayBoat->GetActorLocation(), PlayStageStart) / 100.f : -1.f, PlayMaxKmh, PlayBoatIndex, PlayBoatPath.Num());
			PlayPhase = 99;
		}
		return;
	}
	if (PlayPhase == 99)
	{
		PlayResetStageStats();
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] traffic session | %d near misses, %d AI dead stops, %d oncoming met, %d overtook us | %s"),
			PlayTotNear, PlayTotStops, PlayTotOncoming, PlayTotOvertookUs, *PlayTrafficSummary());
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PLAY] done | average %.1f FPS over %d frames, worst frame %.0f ms"),
			PlayTotalFrames / FMath::Max(PlayTotalTime, 1e-3f), PlayTotalFrames, PlayWorstMs);
		PlayPhase = 100;
		UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
	}
}

void AHGGtaGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateAmbientAudio(DeltaSeconds);
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
				if (PerfFov.IsValidIndex(PerfStage) && PerfFov[PerfStage] > 0.f)
				{
					if (!PerfCamera)
					{
						PerfCamera = GetWorld()->SpawnActor<ACameraActor>();
					}
					PerfCamera->SetActorTransform(PerfSpots[PerfStage]);
					PerfCamera->GetCameraComponent()->SetFieldOfView(PerfFov[PerfStage]);
					PerfCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
					PC->SetViewTarget(PerfCamera);
				}
				else if (APawn* Pawn = PC->GetPawn())
				{
					Pawn->SetActorLocation(PerfSpots[PerfStage].GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
					PC->SetControlRotation(FRotator(-15.f, PerfSpots[PerfStage].Rotator().Yaw, 0.f));
				}
			}
		}
		return;
	}
	PerfAccum += DeltaSeconds;
	PerfGpuAccum += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
	PerfGameAccum += FPlatformTime::ToMilliseconds(GGameThreadTime);
	PerfDrawAccum += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	PerfWorst = FMath::Max(PerfWorst, (double)DeltaSeconds);
	++PerfFrames;
	if (PerfTime > 12.f)
	{
		const double AvgMs = 1000.0 * PerfAccum / FMath::Max(PerfFrames, 1);
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PERF] %s: avg %.1f ms (%.0f FPS), game %.1f / draw %.1f / GPU %.1f ms, worst %.1f ms, %d frames"),
			*PerfNames[PerfStage], AvgMs, 1000.0 / FMath::Max(AvgMs, 0.001), PerfGameAccum / FMath::Max(PerfFrames, 1), PerfDrawAccum / FMath::Max(PerfFrames, 1),
			PerfGpuAccum / FMath::Max(PerfFrames, 1), PerfWorst * 1000.0, PerfFrames);
		PerfGameAccum = PerfDrawAccum = 0.0;
		{
			const FString File = FPaths::ProjectSavedDir() / TEXT("HG_CI/Perf") / FString::Printf(TEXT("%02d_%s.png"), PerfStage, *PerfNames[PerfStage]);
			FScreenshotRequest::RequestScreenshot(File, true, false);
		}
		PerfGpuAccum = 0.0;
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
