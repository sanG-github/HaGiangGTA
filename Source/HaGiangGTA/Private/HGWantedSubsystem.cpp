#include "HGWantedSubsystem.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HaGiangGTA.h"
#include "HGCowActor.h"
#include "HGGroundVehicle.h"
#include "HGPoliceBikePawn.h"
#include "HGPoliceHelicopter.h"
#include "HGRoadSplineActor.h"
#include "HGTouristNPC.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// Heat needed for 1..5 stars.
	constexpr float StarHeat[5] = { 0.5f, 2.f, 4.f, 7.f, 11.f };
	constexpr int32 BikesPerStar[6] = { 0, 1, 2, 2, 3, 4 };
	constexpr float SightRange = 15000.f;   // police keep you "spotted" within 150 m
	constexpr float MinSpawnSightCm = 15000.f;   // police bikes spawn at least this far away in a straight line
	constexpr int32 BustFine = 200000;
}

bool UHGWantedSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UHGWantedSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHGWantedSubsystem, STATGROUP_Tickables);
}

void UHGWantedSubsystem::ReportImpact(AActor* Victim, UPrimitiveComponent* /*VictimComponent*/, float ImpactSpeed)
{
	if (!Victim || ImpactSpeed < 250.f)
	{
		return;
	}
	const float SpeedFactor = FMath::Clamp(ImpactSpeed / 1500.f, 0.4f, 1.5f);
	if (Victim->ActorHasTag(TEXT("RoadPosts")))
	{
		AddHeat(0.6f * SpeedFactor, TEXT("Tong coc tieu"));
	}
	else if (Victim->IsA<AHGPoliceBikePawn>())
	{
		AddHeat(2.5f * SpeedFactor, TEXT("Tan cong CSGT"));
	}
	else if (const AHGGroundVehicle* Vehicle = Cast<AHGGroundVehicle>(Victim))
	{
		if (!Vehicle->IsPlayerControlled())
		{
			AddHeat(1.2f * SpeedFactor, TEXT("Va cham doan xe"));
		}
	}
	else if (Victim->IsA<AHGCowActor>())
	{
		AddHeat(1.f * SpeedFactor, TEXT("Tong bo"));
	}
	else if (Victim->IsA<AHGTouristNPC>())
	{
		AddHeat(2.5f * SpeedFactor, TEXT("Tong nguoi di bo"));
	}
}

void UHGWantedSubsystem::AddHeat(float Amount, const FString& Reason)
{
	Heat = FMath::Min(Heat + Amount, 16.f);
	UnseenTime = 0.f;
	LastReason = Reason;
	ReasonTime = 3.f;
	UpdateStars();
	UE_LOG(LogHaGiang, Log, TEXT("Wanted +%.1f (%s) -> heat %.1f, %d stars"), Amount, *Reason, Heat, Stars);
}

void UHGWantedSubsystem::UpdateStars()
{
	int32 NewStars = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		if (Heat >= StarHeat[i])
		{
			NewStars = i + 1;
		}
	}
	Stars = NewStars;
}

void UHGWantedSubsystem::ClearWanted()
{
	Heat = 0.f;
	Stars = 0;
	ClearTimer = 4.f;
}

AHGRoadSplineActor* UHGWantedSubsystem::FindRoad()
{
	if (!CachedRoad)
	{
		// Main route = the longest road (QL4C stubs to the map edge are scenery).
		for (TActorIterator<AHGRoadSplineActor> It(GetWorld()); It; ++It)
		{
			if (!CachedRoad || It->GetRoadLength() > CachedRoad->GetRoadLength())
			{
				CachedRoad = *It;
			}
		}
	}
	return CachedRoad;
}

void UHGWantedSubsystem::SpawnPoliceBike(APawn* Player)
{
	AHGRoadSplineActor* Road = FindRoad();
	if (!Road)
	{
		return;
	}
	const FVector PlayerLoc = Player->GetActorLocation();
	const float PlayerD = Road->GetDistanceAlongRoad(PlayerLoc);
	const float Len = Road->GetRoadLength();
	// Come from behind (or ahead near the road ends), out of sight. 250 m along the road is not enough on
	// the stacked Tham Ma / Ta Lang switchbacks (the spawn point can be 30 m away, straight up the slope):
	// walk further along the road until the spawn point is also MinSpawnSightCm away in a straight line.
	const float Jitter = FMath::FRandRange(0.f, 8000.f);
	float D = -1.f;
	for (float Off = 25000.f; Off <= 125000.f && D < 0.f; Off += 5000.f)
	{
		for (const float Sign : { -1.f, 1.f })
		{
			const float Cand = PlayerD + Sign * (Off + Jitter);
			if (Cand < 0.f || Cand > Len)
			{
				continue;
			}
			const FVector P = Road->GetSpline()->GetLocationAtDistanceAlongSpline(Cand, ESplineCoordinateSpace::World);
			if (FVector::Dist2D(P, PlayerLoc) >= MinSpawnSightCm)
			{
				D = Cand;
				break;
			}
		}
	}
	if (D < 0.f)
	{
		D = PlayerD > Len * 0.5f ? FMath::Max(PlayerD - 25000.f, 0.f) : FMath::Min(PlayerD + 25000.f, Len);
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AHGPoliceBikePawn* Bike = GetWorld()->SpawnActor<AHGPoliceBikePawn>(AHGPoliceBikePawn::StaticClass(), FTransform::Identity, Params);
	if (Bike)
	{
		Bike->StartAutoDrive(Road, D, PlayerD > D ? 1 : -1, 110.f, 160.f);
		Bike->SetChaseTarget(Player);
		PoliceBikes.Add(Bike);
	}
}

void UHGWantedSubsystem::SpawnHelicopter(APawn* Player)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FVector Start = Player->GetActorLocation() - Player->GetActorForwardVector() * 40000.f + FVector(0.f, 0.f, 25000.f);
	// 250 m above the player can still be inside a ridge with ~1,200 m of relief: start above the ground.
	FHitResult Ground;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(HGHeliSpawn), false, Player);
	if (GetWorld()->LineTraceSingleByChannel(Ground, Start + FVector(0.f, 0.f, 200000.f), Start - FVector(0.f, 0.f, 200000.f), ECC_Visibility, TraceParams))
	{
		Start.Z = FMath::Max(Start.Z, Ground.ImpactPoint.Z + 8000.f);
	}
	Helicopter = GetWorld()->SpawnActor<AHGPoliceHelicopter>(AHGPoliceHelicopter::StaticClass(), Start, FRotator::ZeroRotator, Params);
	if (Helicopter)
	{
		Helicopter->SetChaseTarget(Player);
	}
}

void UHGWantedSubsystem::Busted()
{
	Money = FMath::Max(0, Money - BustFine);
	BustedMessageTime = 5.f;
	++BustCount;
	ClearWanted();
	UE_LOG(LogHaGiang, Log, TEXT("BUSTED! -%d VND"), BustFine);
}

void UHGWantedSubsystem::ManagePolice(float DeltaTime)
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	PoliceBikes.RemoveAll([](const TObjectPtr<AHGPoliceBikePawn>& B) { return !IsValid(B); });

	if (!Player)
	{
		return;
	}

	// Spotted = any police unit close enough.
	bSpotted = false;
	float ClosestBike = BIG_NUMBER;
	for (const AHGPoliceBikePawn* Bike : PoliceBikes)
	{
		const float Dist = FVector::Dist(Bike->GetActorLocation(), Player->GetActorLocation());
		ClosestBike = FMath::Min(ClosestBike, Dist);
		bSpotted |= Dist < SightRange;
	}
	if (Helicopter && FVector::Dist(Helicopter->GetActorLocation(), Player->GetActorLocation()) < SightRange * 2.f)
	{
		bSpotted = true;
	}

	if (Stars > 0)
	{
		// Cool down while unseen.
		UnseenTime = bSpotted ? 0.f : UnseenTime + DeltaTime;
		if (UnseenTime > 6.f)
		{
			Heat = FMath::Max(0.f, Heat - DeltaTime * 0.45f);
			UpdateStars();
		}

		// Reinforcements.
		SpawnCooldown -= DeltaTime;
		if (SpawnCooldown <= 0.f && PoliceBikes.Num() < BikesPerStar[Stars])
		{
			SpawnCooldown = 6.f;
			SpawnPoliceBike(Player);
		}
		if (Stars >= 3 && !Helicopter)
		{
			SpawnHelicopter(Player);
		}

		// Busted: a police bike right next to you while you are (nearly) stopped.
		const float PlayerKmh = Player->GetVelocity().Size() * 0.036f;
		const bool bSlow = PlayerKmh < 12.f;
		BustTimer = (ClosestBike < 600.f && bSlow) ? BustTimer + DeltaTime : 0.f;
		if (BustTimer > 2.5f)
		{
			BustTimer = 0.f;
			Busted();
		}
	}

	// Stand down a few seconds after the stars are gone.
	if (Stars == 0 && (PoliceBikes.Num() > 0 || Helicopter))
	{
		ClearTimer -= DeltaTime;
		if (ClearTimer <= 0.f)
		{
			for (AHGPoliceBikePawn* Bike : PoliceBikes)
			{
				Bike->Destroy();
			}
			PoliceBikes.Reset();
			if (Helicopter)
			{
				Helicopter->FlyAway();
				Helicopter = nullptr;
			}
		}
	}
	else
	{
		ClearTimer = 4.f;
	}
}

void UHGWantedSubsystem::Tick(float DeltaTime)
{
	BustedMessageTime = FMath::Max(0.f, BustedMessageTime - DeltaTime);
	ReasonTime = FMath::Max(0.f, ReasonTime - DeltaTime);
	ManagePolice(DeltaTime);
}
