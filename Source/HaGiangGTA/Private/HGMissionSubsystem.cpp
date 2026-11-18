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

namespace
{
	const FName TourId(TEXT("tour_dong_van"));
	const FName PhotoId(TEXT("photo_ma_pi_leng"));
	const FName ChaseId(TEXT("chase_ta_lang"));

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

	// The boatman waits at the top of the slipway (the dock viewpoint itself is 3 m out in the water).
	BoatHirePoint = RoadAt(FMath::Max(Best->GetRoadLength() - 1000.f, 0.f), 0.f);
	(void)Dock;
	UE_LOG(LogHaGiang, Log, TEXT("Missions: %d jobs, boat hire at %s"), Missions.Num(), *BoatHirePoint.ToString());
}

void UHGMissionSubsystem::Complete(FHGMission& M, int32 Pay, const FString& Line)
{
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
	Banner = M.Title;
	BannerTime = 3.f;
	UE_LOG(LogHaGiang, Display, TEXT("[HG_MISSION] %s started"), *M.Id.ToString());
}

void UHGMissionSubsystem::TickActive(FHGMission& M, APawn* Player, float DeltaTime)
{
	M.Elapsed += DeltaTime;
	const FVector Loc = Player->GetActorLocation();
	const float Dist = FVector::Dist2D(Loc, M.Target);
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
		for (int32 i = 0; i < Missions.Num(); ++i)
		{
			const FHGMission& M = Missions[i];
			if (M.State != EHGMissionState::Available || FVector::Dist2D(Player->GetActorLocation(), M.Start) > M.RadiusCm)
			{
				continue;
			}
			const bool bNeedsBike = M.Id != PhotoId;
			if (bNeedsBike && !bOnBike)
			{
				Prompt = FString::Printf(TEXT("%s - cần xe máy"), *M.Title);
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
