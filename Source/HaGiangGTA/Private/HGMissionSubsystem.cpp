#include "HGMissionSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HaGiangGTA.h"
#include "HGBikePawn.h"
#include "HGGroundVehicle.h"
#include "HGPlayerCharacter.h"
#include "HGRoadSplineActor.h"
#include "HGViewpointActor.h"
#include "HGWantedSubsystem.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "NhoQueBoatPawn.h"
#include "HGPeopleBuilder.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
	const FName TourId(TEXT("tour_dong_van"));
	const FName PhotoId(TEXT("photo_ma_pi_leng"));
	const FName ChaseId(TEXT("chase_ta_lang"));
	const FName BoatTourId(TEXT("boat_tour_tu_san"));

	FString Vnd(int32 V)
	{
		FString S = FString::FromInt(FMath::Abs(V));
		for (int32 i = S.Len() - 3; i > 0; i -= 3)
		{
			S.InsertAt(i, TEXT("."));
		}
		return (V < 0 ? TEXT("-") : TEXT("+")) + S + TEXT(" VNĐ");
	}

	float PawnKmh(const APawn* P)
	{
		if (const AHGVehicleBase* V = Cast<AHGVehicleBase>(P))
		{
			return FMath::Abs(V->GetSpeedKmh());
		}
		return P ? P->GetVelocity().Size2D() * 0.036f : 0.f;
	}
}

bool UHGMissionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UHGMissionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHGMissionSubsystem, STATGROUP_Tickables);
}

void UHGMissionSubsystem::SetupMissions()
{
	bSetup = true;
	AHGRoadSplineActor* Best = nullptr;
	for (TActorIterator<AHGRoadSplineActor> It(GetWorld()); It; ++It)
	{
		if (!Best || It->GetRoadLength() > Best->GetRoadLength())
		{
			Best = *It;
		}
	}
	TArray<TPair<float, AHGViewpointActor*>> Vps;
	for (TActorIterator<AHGViewpointActor> It(GetWorld()); It; ++It)
	{
		Vps.Add({ Best ? Best->GetDistanceAlongRoad(It->GetActorLocation()) : 0.f, *It });
	}
	Vps.Sort([](const TPair<float, AHGViewpointActor*>& A, const TPair<float, AHGViewpointActor*>& B) { return A.Key < B.Key; });
	if (!Best || Vps.Num() < 3)
	{
		UE_LOG(LogHaGiang, Warning, TEXT("Missions: need the road and the three viewpoints"));
		return;
	}
	Road = Best;
	Viewpoints.Reset();
	for (const TPair<float, AHGViewpointActor*>& V : Vps)
	{
		Viewpoints.Add(V.Value);
	}
	// Profile (money, tyres, intro seen) before deciding on the intro card. The benchmark never loads or saves.
	if (!bProfileLoaded && !FParse::Param(FCommandLine::Get(), TEXT("hgperf")))
	{
		bProfileLoaded = true;
		LoadProfile();
	}
	// Intro / tutorial card on the first spawn (not once the profile says it was seen; not over -hgperf frames).
	if (!bIntroShown && !FParse::Param(FCommandLine::Get(), TEXT("hgperf")))
	{
		bIntroShown = true;
		IntroTime = 25.f;
	}
	const USplineComponent* Sp = Best->GetSpline();
	auto RoadAt = [Sp](float D, float Lane)
	{
		const FVector T = Sp->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).GetSafeNormal2D();
		const FVector R = FVector::CrossProduct(FVector::UpVector, T).GetSafeNormal();
		return Sp->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World) + R * Lane + FVector(0.f, 0.f, 60.f);
	};
	const float MplD = Vps[1].Key;
	const AHGViewpointActor* Mpl = Vps[1].Value;
	const AHGViewpointActor* Dock = Vps[2].Value;

	// The route reaches QL4C from the Dong Van side 1.4 km before the Ma Pi Leng viewpoint (the link road stands
	// in for the road down from Dong Van town, which is off the 5.9 km map): the backpackers wait there.
	FHGMission Tour;
	Tour.Id = TourId;
	Tour.Title = TEXT("Xe ôm Đồng Văn → Mã Pí Lèng");
	Tour.Start = RoadAt(FMath::Max(MplD - 140000.f, 2000.f), 150.f);
	Tour.Target = RoadAt(MplD, 150.f);
	Tour.RadiusCm = 1000.f;
	Tour.Reward = 150000;
	Missions.Add(Tour);

	FHGMission Photo;
	Photo.Id = PhotoId;
	Photo.Title = TEXT("Ảnh Mã Pí Lèng");
	Photo.Start = RoadAt(MplD, 150.f);
	Photo.Target = Mpl->OrbitCenter.IsNearlyZero() ? Mpl->GetActorLocation() : Mpl->OrbitCenter;
	Photo.RadiusCm = 2500.f;
	Photo.Reward = 50000;
	Missions.Add(Photo);
	PhotoAim = Photo.Target;

	FHGMission Chase;
	Chase.Id = ChaseId;
	Chase.Title = TEXT("Trốn CSGT xuống bến Tà Làng");
	const float TopD = Best->SecondaryFromDistance >= 0.f ? Best->SecondaryFromDistance + 15000.f : Best->GetRoadLength() * 0.7f;
	Chase.Start = RoadAt(TopD, 120.f);
	Chase.Target = Dock->GetActorLocation();
	Chase.RadiusCm = 1000.f;
	Chase.Reward = 300000;
	Missions.Add(Chase);

	// Tu San boat tour: from the dock (the dock viewpoint is just off the slipway, on the water) down the gorge to
	// the Ma Pi Leng bend (the cruise-line point nearest the Ma Pi Leng viewpoint, set when the job starts) and back.
	FHGMission Boat;
	Boat.Id = BoatTourId;
	Boat.Title = TEXT("Thuyền Tu Sản: chở 2 khách");
	// The ring is where the hired cano lies at the dock (the dock viewpoint is ~55 m off it, over the slipway).
	Boat.Start = Dock->GetActorLocation();
	Boat.Target = Mpl->GetActorLocation();
	Boat.RadiusCm = 3000.f;
	Boat.Reward = 250000;
	Missions.Add(Boat);

	// The boatman waits at the top of the slipway (the dock viewpoint itself is 3 m out in the water).
	BoatHirePoint = RoadAt(FMath::Max(Best->GetRoadLength() - 1000.f, 0.f), 0.f);
	if (const ANhoQueBoatPawn* HireBoat = FindHireBoat())
	{
		Missions.Last().Start = HireBoat->GetActorLocation();
	}
	(void)Dock;
	UE_LOG(LogHaGiang, Log, TEXT("Missions: %d jobs, boat hire at %s"), Missions.Num(), *BoatHirePoint.ToString());
}

void UHGMissionSubsystem::Complete(FHGMission& M, int32 Pay, const FString& Line)
{
	ClearBoatGuests();
	M.State = EHGMissionState::Done;
	ActiveIndex = INDEX_NONE;
	if (UHGWantedSubsystem* W = GetWorld()->GetSubsystem<UHGWantedSubsystem>())
	{
		W->AddMoney(Pay);
	}
	Banner = FString::Printf(TEXT("%s  %s"), *Line, *Vnd(Pay));
	BannerTime = 4.f;
	Objective.Reset();
	UE_LOG(LogHaGiang, Display, TEXT("[HG_MISSION] %s done: %s (%.0f s)"), *M.Id.ToString(), *Vnd(Pay), M.Elapsed);
}

void UHGMissionSubsystem::Fail(FHGMission& M, const FString& Why)
{
	ClearBoatGuests();
	M.State = EHGMissionState::Available;   // retry from the start ring
	ActiveIndex = INDEX_NONE;
	Banner = FString::Printf(TEXT("NHIỆM VỤ THẤT BẠI - %s"), *Why);
	BannerTime = 4.f;
	Objective.Reset();
	UE_LOG(LogHaGiang, Display, TEXT("[HG_MISSION] %s failed: %s"), *M.Id.ToString(), *Why);
}

void UHGMissionSubsystem::StartMission(int32 Index, APawn* Player)
{
	FHGMission& M = Missions[Index];
	M.State = EHGMissionState::Active;
	M.Elapsed = 0.f;
	ActiveIndex = Index;
	bPassengerAboard = false;
	if (M.Id == ChaseId)
	{
		if (UHGWantedSubsystem* W = GetWorld()->GetSubsystem<UHGWantedSubsystem>())
		{
			BustCountAtStart = W->GetBustCount();
			W->AddHeat(4.5f, TEXT("Bi to giac: tron CSGT"));   // 3 stars: bikes + the helicopter
		}
	}
	if (M.Id == TourId)
	{
		if (AHGBikePawn* Bike = Cast<AHGBikePawn>(Player))
		{
			Bike->SetHasPassenger(true);   // the backpacker climbs on
			bPassengerAboard = true;
		}
	}
	if (M.Id == BoatTourId)
	{
		bBoatLegBack = false;
		if (BoatBend.IsNearlyZero() && Viewpoints.Num() >= 2 && Viewpoints[1].IsValid())
		{
			if (const TArray<FVector>* Line = ANhoQueBoatPawn::FindCruiseLine(GetWorld()))
			{
				const FVector MplLoc = Viewpoints[1]->GetActorLocation();
				for (const FVector& P : *Line)
				{
					if (BoatBend.IsNearlyZero() || FVector::DistSquared2D(P, MplLoc) < FVector::DistSquared2D(BoatBend, MplLoc))
					{
						BoatBend = P;
					}
				}
			}
		}
		if (!BoatBend.IsNearlyZero())
		{
			M.Target = BoatBend;
		}
		SpawnBoatGuests(Cast<ANhoQueBoatPawn>(Player));
	}
	Banner = M.Title;
	BannerTime = 3.f;
	UE_LOG(LogHaGiang, Display, TEXT("[HG_MISSION] %s started"), *M.Id.ToString());
}

void UHGMissionSubsystem::TickActive(FHGMission& M, APawn* Player, float DeltaTime)
{
	M.Elapsed += DeltaTime;
	const FVector Loc = Player->GetActorLocation();
	const float Dist = FVector::Dist2D(Loc, M.Target);
	ActiveDistance = Dist;
	if (M.Id == TourId)
	{
		AHGBikePawn* Bike = Cast<AHGBikePawn>(Player);
		if (!Bike)
		{
			Objective = TEXT("Lên lại xe máy - khách đang đợi");
			return;
		}
		Objective = FString::Printf(TEXT("Chở khách tới Mã Pí Lèng  ·  %.1f km"), Dist / 100000.f);
		if (Dist < M.RadiusCm && PawnKmh(Player) < 6.f)
		{
			Bike->SetHasPassenger(false);   // the backpacker gets off at the viewpoint
			// Tip: full 50,000 under 4 min from the Dong Van turn, nothing after 8.
			const int32 Tip = FMath::RoundToInt(50000.f * FMath::Clamp((480.f - M.Elapsed) / 240.f, 0.f, 1.f) / 1000.f) * 1000;
			Complete(M, M.Reward + Tip, TEXT("Khách đã tới Mã Pí Lèng"));
		}
		return;
	}
	if (M.Id == PhotoId)
	{
		Objective = TEXT("Hướng máy ảnh về hẻm Tu Sản và nhấn  P");
		APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
		if (FVector::Dist2D(Loc, M.Start) > 6000.f)
		{
			Fail(M, TEXT("rời điểm ngắm"));
			return;
		}
		if (PC && PC->WasInputKeyJustPressed(EKeys::P) && PC->PlayerCameraManager)
		{
			FlashTime = 0.f;
			const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
			const FVector Fwd = PC->PlayerCameraManager->GetCameraRotation().Vector();
			const float Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Fwd, (PhotoAim - CamLoc).GetSafeNormal()), -1.f, 1.f)));
			if (Off > 25.f)
			{
				Banner = FString::Printf(TEXT("Lệch %.0f° - hướng về hẻm Tu Sản"), Off);
				BannerTime = 2.f;
				UE_LOG(LogHaGiang, Display, TEXT("[HG_MISSION] photo off target by %.0f deg"), Off);
				return;
			}
			const int32 Bonus = FMath::RoundToInt(50000.f * (1.f - Off / 25.f) / 1000.f) * 1000;
			Complete(M, M.Reward + Bonus, FString::Printf(TEXT("Ảnh đẹp! (lệch %.0f°)"), Off));
		}
		return;
	}
	if (M.Id == BoatTourId)
	{
		if (!Player->IsA<ANhoQueBoatPawn>())
		{
			Objective = TEXT("Quay lại cano - khách đang đợi");
			return;
		}
		if (!bBoatLegBack)
		{
			Objective = FString::Printf(TEXT("Chở khách xuống khúc cua Mã Pí Lèng  ·  %.1f km"), Dist / 100000.f);
			if (Dist < 4000.f)
			{
				bBoatLegBack = true;
				M.Target = M.Start;
				Banner = TEXT("Khách chụp ảnh khúc cua Mã Pí Lèng - quay về bến");
				BannerTime = 3.f;
				UE_LOG(LogHaGiang, Display, TEXT("[HG_MISSION] %s: Ma Pi Leng bend reached (%.0f s), back to the dock"), *M.Id.ToString(), M.Elapsed);
			}
			return;
		}
		Objective = FString::Printf(TEXT("Đưa khách về bến Tà Làng  ·  %.1f km"), Dist / 100000.f);
		if (Dist < M.RadiusCm && PawnKmh(Player) < 10.f)
		{
			// Tip: 50,000 for the round trip under 8 min, nothing after 14.
			const int32 Tip = FMath::RoundToInt(50000.f * FMath::Clamp((840.f - M.Elapsed) / 360.f, 0.f, 1.f) / 1000.f) * 1000;
			M.Target = BoatBend;
			Complete(M, M.Reward + Tip, TEXT("Tour thuyền Tu Sản xong"));
		}
		return;
	}
	if (M.Id == ChaseId)
	{
		UHGWantedSubsystem* W = GetWorld()->GetSubsystem<UHGWantedSubsystem>();
		if (W && W->GetBustCount() > BustCountAtStart)
		{
			Fail(M, TEXT("bị bắt"));
			return;
		}
		Objective = FString::Printf(TEXT("Cắt đuôi CSGT - xuống bến Tà Làng  ·  %.1f km"), Dist / 100000.f);
		if (Dist < M.RadiusCm * 2.5f)
		{
			if (W)
			{
				W->ClearWanted();
			}
			Complete(M, M.Reward, TEXT("Thoát! Xuống tới bến Tà Làng"));
		}
	}
}

ANhoQueBoatPawn* UHGMissionSubsystem::FindHireBoat() const
{
	ANhoQueBoatPawn* Best = nullptr;
	for (TActorIterator<ANhoQueBoatPawn> It(GetWorld()); It; ++It)
	{
		if (It->bAutoCruise || It->GetBoatType() != EHGBoatType::Cano)
		{
			continue;
		}
		if (It->GetActorNameOrLabel().StartsWith(TEXT("Boat_Cano")))
		{
			return *It;
		}
		if (!Best || FVector::DistSquared(It->GetActorLocation(), BoatHirePoint) < FVector::DistSquared(Best->GetActorLocation(), BoatHirePoint))
		{
			Best = *It;
		}
	}
	return Best;
}

bool UHGMissionSubsystem::TryBoatHire(AHGPlayerCharacter* Player)
{
	if (!Player || BoatHirePoint.IsNearlyZero() || FVector::Dist2D(Player->GetActorLocation(), BoatHirePoint) > 2500.f)
	{
		return false;
	}
	ANhoQueBoatPawn* Boat = FindHireBoat();
	UHGWantedSubsystem* W = GetWorld()->GetSubsystem<UHGWantedSubsystem>();
	if (!Boat || !W || !Boat->CanEnter(Player))
	{
		return false;
	}
	if (W->GetMoney() < BoatHirePrice)
	{
		Banner = TEXT("Không đủ tiền thuê cano");
		BannerTime = 2.f;
		return true;
	}
	W->AddMoney(-BoatHirePrice);
	Boat->EnterVehicle(Player);   // the boatman brings it alongside: no 40 m swim
	Banner = FString::Printf(TEXT("Thuê cano  %s"), *Vnd(-BoatHirePrice));
	BannerTime = 2.5f;
	UE_LOG(LogHaGiang, Display, TEXT("[HG_MISSION] boat hired: %s"), *Boat->GetName());
	return true;
}

void UHGMissionSubsystem::Tick(float DeltaTime)
{
	HandleMenuInput(UGameplayStatics::GetPlayerController(GetWorld(), 0));
	if (GetWorld()->IsPaused())
	{
		return;
	}
	IntroTime = FMath::Max(0.f, IntroTime - DeltaTime);
	// Save the profile when the wallet changes (checked twice a second).
	ProfileTimer -= DeltaTime;
	if (bProfileLoaded && ProfileTimer <= 0.f)
	{
		ProfileTimer = 0.5f;
		const UHGWantedSubsystem* PW = GetWorld()->GetSubsystem<UHGWantedSubsystem>();
		if (PW && PW->GetMoney() != SavedMoney)
		{
			SaveProfile();
		}
	}
	BannerTime = FMath::Max(0.f, BannerTime - DeltaTime);
	FlashTime += DeltaTime;
	APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player)
	{
		return;
	}
	if (!bSetup)
	{
		SetupMissions();
	}
	Prompt.Reset();
	if (FHGMission* Active = Missions.IsValidIndex(ActiveIndex) ? &Missions[ActiveIndex] : nullptr)
	{
		TickActive(*Active, Player, DeltaTime);
	}
	else
	{
		Objective.Reset();
		const bool bOnBike = Player->IsA<AHGBikePawn>();
		const bool bOnBoat = Player->IsA<ANhoQueBoatPawn>();
		for (int32 i = 0; i < Missions.Num(); ++i)
		{
			const FHGMission& M = Missions[i];
			if (M.State != EHGMissionState::Available || FVector::Dist2D(Player->GetActorLocation(), M.Start) > M.RadiusCm)
			{
				continue;
			}
			if (M.Id == BoatTourId ? !bOnBoat : (M.Id != PhotoId && !bOnBike))
			{
				Prompt = FString::Printf(TEXT("%s - %s"), *M.Title, M.Id == BoatTourId ? TEXT("cần cano (thuê ở bến, F)") : TEXT("cần xe máy"));
				break;
			}
			if (PawnKmh(Player) < 8.f)
			{
				StartMission(i, Player);
			}
			else
			{
				Prompt = FString::Printf(TEXT("Dừng lại để nhận việc: %s"), *M.Title);
			}
			break;
		}
	}
	// Boat hire offer at the Ta Lang dock.
	if (Prompt.IsEmpty() && Player->IsA<AHGPlayerCharacter>() && !BoatHirePoint.IsNearlyZero()
		&& FVector::Dist2D(Player->GetActorLocation(), BoatHirePoint) < 2500.f)
	{
		Prompt = TEXT("Nhấn  F  thuê cano Tu Sản - 100.000 VNĐ");
	}
}

// ---------------------------------------------------------------------------
// Boat tour guests, mission list / upgrade, pause menu, intro card
// ---------------------------------------------------------------------------

void UHGMissionSubsystem::SpawnBoatGuests(ANhoQueBoatPawn* Boat)
{
	ClearBoatGuests();
	if (!Boat)
	{
		return;
	}
	// Two backpackers seated on the cano's benches (the player's hire cano has none of the tour boats' crew), built
	// like that crew (HGPeople, Passenger pose, on the hull frame so they pitch and roll with it).
	USceneComponent* Parent = Boat->GetRootComponent();
	TArray<USceneComponent*> Scene;
	Boat->GetComponents(Scene);
	for (USceneComponent* C : Scene)
	{
		if (C->GetFName() == TEXT("HullRoot"))
		{
			Parent = C;
		}
	}
	TArray<UActorComponent*> Before;
	Boat->GetComponents(Before);
	static int32 GuestSerial = 0;
	HGPeople::Spawn(Boat, Parent, EHGPersonLook::Backpacker, EHGPersonPose::Passenger, 1, *FString::Printf(TEXT("TourGuest%d"), GuestSerial++), FVector(-122.f, -60.f, 83.f), true);
	HGPeople::Spawn(Boat, Parent, EHGPersonLook::Backpacker, EHGPersonPose::Passenger, 2, *FString::Printf(TEXT("TourGuest%d"), GuestSerial++), FVector(18.f, 60.f, 83.f), false);
	TArray<UActorComponent*> After;
	Boat->GetComponents(After);
	for (UActorComponent* C : After)
	{
		if (!Before.Contains(C))
		{
			BoatGuests.Add(C);
		}
	}
}

void UHGMissionSubsystem::ClearBoatGuests()
{
	for (const TWeakObjectPtr<UActorComponent>& G : BoatGuests)
	{
		if (UActorComponent* C = G.Get())
		{
			C->DestroyComponent();
		}
	}
	BoatGuests.Reset();
}

bool UHGMissionSubsystem::PlayerHasGripTyres(const UWorld* World)
{
	const UHGMissionSubsystem* M = World ? World->GetSubsystem<UHGMissionSubsystem>() : nullptr;
	return M && M->HasGripTyres();
}

bool UHGMissionSubsystem::BuyGripTyres()
{
	UHGWantedSubsystem* W = GetWorld()->GetSubsystem<UHGWantedSubsystem>();
	if (bGripTyres || !W)
	{
		Banner = TEXT("Đã lắp lốp gai");
		BannerTime = 2.f;
		return false;
	}
	if (W->GetMoney() < GripTyresPrice)
	{
		Banner = FString::Printf(TEXT("Không đủ tiền - lốp gai %s"), *Vnd(GripTyresPrice).RightChop(1));
		BannerTime = 2.f;
		return false;
	}
	W->AddMoney(-GripTyresPrice);
	bGripTyres = true;
	SaveProfile();
	Banner = FString::Printf(TEXT("Lắp lốp gai: bám đường bê tông & đường đất  %s"), *Vnd(-GripTyresPrice));
	BannerTime = 3.f;
	UE_LOG(LogHaGiang, Display, TEXT("[HG_MISSION] upgrade bought: grip tyres %s"), *Vnd(-GripTyresPrice));
	return true;
}

FString UHGMissionSubsystem::GetRestartName() const
{
	const AHGViewpointActor* V = Viewpoints.IsValidIndex(RestartIndex) ? Viewpoints[RestartIndex].Get() : nullptr;
	return V ? V->ViewpointName.ToString() : FString();
}

void UHGMissionSubsystem::SetPaused(bool bPause)
{
	bPauseOpen = bPause;
	PauseItem = EHGPauseItem::Resume;
	bListOpen = false;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		UGameplayStatics::SetGamePaused(GetWorld(), bPause);
		(void)PC;
	}
	UE_LOG(LogHaGiang, Display, TEXT("[HG_MENU] pause %s"), bPause ? TEXT("open") : TEXT("closed"));
}

void UHGMissionSubsystem::RestartAtViewpoint(int32 Index)
{
	AHGViewpointActor* Vp = Viewpoints.IsValidIndex(Index) ? Viewpoints[Index].Get() : nullptr;
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!Vp || !PC)
	{
		return;
	}
	SetPaused(false);
	if (Missions.IsValidIndex(ActiveIndex))
	{
		Missions[ActiveIndex].State = EHGMissionState::Available;
		if (Missions[ActiveIndex].Id == BoatTourId)
		{
			Missions[ActiveIndex].Target = BoatBend;
		}
	}
	ActiveIndex = INDEX_NONE;
	Objective.Reset();
	ClearBoatGuests();
	APawn* Pawn = PC->GetPawn();
	AHGPlayerCharacter* Char = Cast<AHGPlayerCharacter>(Pawn);
	if (AHGVehicleBase* V = Cast<AHGVehicleBase>(Pawn))
	{
		Char = V->GetDriver();
		V->ExitVehicle();
	}
	if (!Char)
	{
		return;
	}
	// The dock viewpoint stands on the water: restart at the boatman on the slipway instead.
	FVector Spot = (Index == Viewpoints.Num() - 1 && !BoatHirePoint.IsNearlyZero()) ? BoatHirePoint : Vp->GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(HGRestart), false, Char);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Spot + FVector(0.f, 0.f, 2000.f), Spot - FVector(0.f, 0.f, 5000.f), ECC_Visibility, Q))
	{
		Spot.Z = Hit.ImpactPoint.Z + 100.f;
	}
	Char->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
	const FVector Look = Vp->OrbitCenter.IsNearlyZero() ? Vp->GetActorLocation() + Vp->GetActorForwardVector() * 1000.f : Vp->OrbitCenter;
	PC->SetControlRotation(FRotator(-10.f, (Look - Spot).Rotation().Yaw, 0.f));
	if (UHGWantedSubsystem* W = GetWorld()->GetSubsystem<UHGWantedSubsystem>())
	{
		W->ClearWanted();
	}
	Banner = FString::Printf(TEXT("Về điểm ngắm %s"), *Vp->ViewpointName.ToString());
	BannerTime = 2.5f;
	UE_LOG(LogHaGiang, Display, TEXT("[HG_MENU] restart at %s (%s)"), *Vp->ViewpointName.ToString(), *Spot.ToString());
}

void UHGMissionSubsystem::HandleMenuInput(APlayerController* PC)
{
	if (!PC)
	{
		return;
	}
	// Own edge detection on the key state: WasInputKeyJustPressed re-fired every frame while a (simulated) key was
	// held at some points and toggled the mission list ten times in 0.2 s.
	auto EdgeOf = [this, PC](const FKey& K)
	{
		const bool bDown = PC->IsInputKeyDown(K);
		bool& bWas = MenuKeysDown.FindOrAdd(K);
		const bool bEdge = bDown && !bWas;
		bWas = bDown;
		return bEdge;
	};
	// Poll every menu key each tick so the edge state never goes stale.
	static const FKey Keys[] = { EKeys::Enter, EKeys::SpaceBar, EKeys::Escape, EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right,
		EKeys::W, EKeys::S, EKeys::A, EKeys::D, EKeys::F, EKeys::Tab, EKeys::M, EKeys::B };
	TSet<FKey> Edges;
	for (const FKey& K : Keys)
	{
		if (EdgeOf(K)) { Edges.Add(K); }
	}
	auto Pressed = [&Edges](const FKey& K) { return Edges.Contains(K); };
	if (IntroTime > 0.f)
	{
		// Any of the "start" keys - or simply riding / walking off - puts the card away.
		if (Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar) || Pressed(EKeys::Escape) || PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up))
		{
			UE_LOG(LogHaGiang, Display, TEXT("[HG_MENU] intro card closed (%s)"), Pressed(EKeys::Enter) ? TEXT("Enter") : Pressed(EKeys::SpaceBar) ? TEXT("Space")
				: Pressed(EKeys::Escape) ? TEXT("Esc") : TEXT("riding off"));
			IntroTime = 0.f;
			SaveProfile();   // intro seen
			return;
		}
	}
	if (bPauseOpen)
	{
		const int32 N = int32(EHGPauseItem::Count);
		int32 Item = int32(PauseItem);
		if (Pressed(EKeys::Escape))
		{
			SetPaused(false);
			return;
		}
		if (Pressed(EKeys::Up) || Pressed(EKeys::W))
		{
			Item = (Item + N - 1) % N;
		}
		if (Pressed(EKeys::Down) || Pressed(EKeys::S))
		{
			Item = (Item + 1) % N;
		}
		PauseItem = EHGPauseItem(Item);
		if (PauseItem == EHGPauseItem::Restart && Viewpoints.Num() > 0)
		{
			if (Pressed(EKeys::Left) || Pressed(EKeys::A))
			{
				RestartIndex = (RestartIndex + Viewpoints.Num() - 1) % Viewpoints.Num();
			}
			if (Pressed(EKeys::Right) || Pressed(EKeys::D))
			{
				RestartIndex = (RestartIndex + 1) % Viewpoints.Num();
			}
		}
		if (Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar) || Pressed(EKeys::F))
		{
			switch (PauseItem)
			{
			case EHGPauseItem::Resume: SetPaused(false); break;
			case EHGPauseItem::Restart: RestartAtViewpoint(RestartIndex); break;
			case EHGPauseItem::Quit:
				UE_LOG(LogHaGiang, Display, TEXT("[HG_MENU] quit"));
				UKismetSystemLibrary::QuitGame(GetWorld(), PC, EQuitPreference::Quit, false);
				break;
			default: break;
			}
		}
		return;
	}
	if (GetWorld()->IsPaused())
	{
		return;   // paused by something else (debug console)
	}
	if (Pressed(EKeys::Escape))
	{
		SetPaused(true);
		return;
	}
	if (Pressed(EKeys::Tab) || Pressed(EKeys::M))
	{
		bListOpen = !bListOpen;
		UE_LOG(LogHaGiang, Display, TEXT("[HG_MENU] mission list %s"), bListOpen ? TEXT("open") : TEXT("closed"));
	}
	if (bListOpen && Pressed(EKeys::B))
	{
		BuyGripTyres();
	}
}

// ---------------------------------------------------------------------------
// Player profile
// ---------------------------------------------------------------------------

FString UHGMissionSubsystem::ProfilePath() const
{
	const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("hgplaytest"));
	return FPaths::ProjectSavedDir() / (bTest ? TEXT("HGProfile_test.ini") : TEXT("HGProfile.ini"));
}

bool UHGMissionSubsystem::LoadProfile()
{
	const FString Path = ProfilePath();
	if (FParse::Param(FCommandLine::Get(), TEXT("hgplaytest")) && !FParse::Param(FCommandLine::Get(), TEXT("hgkeepprofile")))
	{
		IFileManager::Get().Delete(*Path);   // every scripted run starts from a new profile (and tests the save)
	}
	FConfigFile Ini;
	Ini.Read(Path);
	int32 Money = -1, Tyres = 0, Intro = 0;
	const bool bAny = Ini.GetInt(TEXT("Profile"), TEXT("Money"), Money);
	Ini.GetInt(TEXT("Profile"), TEXT("GripTyres"), Tyres);
	Ini.GetInt(TEXT("Profile"), TEXT("IntroSeen"), Intro);
	if (!bAny)
	{
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PROFILE] no profile at %s: new game"), *Path);
		return false;
	}
	if (UHGWantedSubsystem* W = GetWorld()->GetSubsystem<UHGWantedSubsystem>())
	{
		W->AddMoney(Money - W->GetMoney());
	}
	bGripTyres = Tyres != 0;
	bIntroShown = bIntroShown || Intro != 0;
	SavedMoney = Money;
	UE_LOG(LogHaGiang, Display, TEXT("[HG_PROFILE] loaded %s: money %d, grip tyres %d, intro seen %d"), *Path, Money, Tyres, Intro);
	return true;
}

void UHGMissionSubsystem::SaveProfile()
{
	if (!bProfileLoaded)
	{
		return;
	}
	const UHGWantedSubsystem* W = GetWorld() ? GetWorld()->GetSubsystem<UHGWantedSubsystem>() : nullptr;
	if (!W)
	{
		return;
	}
	FConfigFile Ini;
	Ini.SetInt64(TEXT("Profile"), TEXT("Money"), W->GetMoney());
	Ini.SetInt64(TEXT("Profile"), TEXT("GripTyres"), bGripTyres ? 1 : 0);
	Ini.SetInt64(TEXT("Profile"), TEXT("IntroSeen"), (bIntroShown && IntroTime <= 0.f) ? 1 : 0);
	Ini.Dirty = true;
	Ini.Write(ProfilePath());
	SavedMoney = W->GetMoney();
}

void UHGMissionSubsystem::Deinitialize()
{
	SaveProfile();
	Super::Deinitialize();
}
