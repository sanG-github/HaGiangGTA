#include "HGTrafficSpawner.h"

#include "Engine/World.h"
#include "HaGiangGTA.h"
#include "HGBikePawn.h"
#include "HGRoadSplineActor.h"
#include "HGTruckPawn.h"
#include "Components/SplineComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

AHGTrafficSpawner::AHGTrafficSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AHGTrafficSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (!Road)
	{
		UE_LOG(LogHaGiang, Warning, TEXT("%s: no Road set, no traffic."), *GetName());
		return;
	}
	const float Length = Road->GetRoadLength();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	static const FLinearColor BikeColors[] = {
		FLinearColor(0.55f, 0.02f, 0.02f), FLinearColor(0.05f, 0.08f, 0.35f), FLinearColor(0.08f, 0.08f, 0.08f),
		FLinearColor(0.6f, 0.6f, 0.62f), FLinearColor(0.02f, 0.25f, 0.08f) };

	for (const FHGConvoySpec& Spec : Convoys)
	{
		AHGGroundVehicle* Leader = nullptr;
		const int32 Num = FMath::Clamp(Spec.NumBikes, 1, 16);
		for (int32 i = 0; i < Num; ++i)
		{
			const float D = Spec.StartMeters * 100.f - float(Spec.Direction >= 0 ? 1 : -1) * i * BikeSpacing;
			if (D < 0.f || D > Length)
			{
				continue;
			}
			AHGBikePawn* Bike = GetWorld()->SpawnActor<AHGBikePawn>(AHGBikePawn::StaticClass(), FTransform::Identity, Params);
			if (!Bike)
			{
				continue;
			}
			Bike->SetHasPassenger(FMath::FRand() < PassengerChance);
			Bike->SetBodyColor(BikeColors[FMath::RandRange(0, int32(UE_ARRAY_COUNT(BikeColors)) - 1)]);
			// Stagger the column a little inside the lane so it reads as a group of riders.
			const float Lane = LaneOffset + ((i % 2) ? 60.f : -40.f);
			Bike->StartAutoDrive(Road, D, Spec.Direction, Spec.CruiseKmh * FMath::FRandRange(0.95f, 1.05f), Lane);
			Bike->SetAutoDriveLeader(Leader);
			Leader = Bike;
			Spawned.Add(Bike);
		}
	}

	for (const FHGTruckSpec& Spec : Trucks)
	{
		AHGTruckPawn* Truck = GetWorld()->SpawnActor<AHGTruckPawn>(AHGTruckPawn::StaticClass(), FTransform::Identity, Params);
		if (!Truck)
		{
			continue;
		}
		const float MinCm = Spec.MinMeters >= 0.f ? Spec.MinMeters * 100.f : -1.f;
		const float MaxCm = Spec.MaxMeters >= 0.f ? Spec.MaxMeters * 100.f : -1.f;
		Truck->SetAutoDriveRange(MinCm, MaxCm);
		const float StartCm = FMath::Clamp(Spec.StartMeters * 100.f, FMath::Max(MinCm, 0.f), MaxCm >= 0.f ? FMath::Min(MaxCm, Length) : Length);
		Truck->StartAutoDrive(Road, StartCm, Spec.Direction, Spec.CruiseKmh, LaneOffset + 40.f);
		Spawned.Add(Truck);
	}
	UE_LOG(LogHaGiang, Log, TEXT("Traffic: %d vehicles (%d convoys, %d trucks)"), Spawned.Num(), Convoys.Num(), Trucks.Num());

	// Ambient pool (placed around the player from the first Tick).
	if (Trucks.Num() > 0)
	{
		TruckMinCm = Trucks[0].MinMeters >= 0.f ? Trucks[0].MinMeters * 100.f : -1.f;
		TruckMaxCm = Trucks[0].MaxMeters >= 0.f ? Trucks[0].MaxMeters * 100.f : -1.f;
	}
	const FTransform Hidden(FVector(0.f, 0.f, -100000.f));
	for (int32 i = 0; i < AmbientBikes + AmbientTrucks; ++i)
	{
		const bool bTruck = i >= AmbientBikes;
		AHGGroundVehicle* V = bTruck
			? static_cast<AHGGroundVehicle*>(GetWorld()->SpawnActor<AHGTruckPawn>(AHGTruckPawn::StaticClass(), Hidden, Params))
			: static_cast<AHGGroundVehicle*>(GetWorld()->SpawnActor<AHGBikePawn>(AHGBikePawn::StaticClass(), Hidden, Params));
		if (!V)
		{
			continue;
		}
		if (AHGBikePawn* Bike = Cast<AHGBikePawn>(V))
		{
			Bike->SetHasPassenger(FMath::FRand() < 0.5f);
			Bike->SetBodyColor(BikeColors[FMath::RandRange(0, int32(UE_ARRAY_COUNT(BikeColors)) - 1)]);
		}
		V->SetActorHiddenInGame(true);
		V->SetActorTickEnabled(false);   // parked out of the world until placed
		Ambient.Add(V);
		AmbientPlaced.Add(false);
	}
}

bool AHGTrafficSpawner::PlaceAmbient(AHGGroundVehicle* V, bool bTruck, float PlayerDist, int32 PlayerDir)
{
	const float Length = Road->GetRoadLength();
	for (int32 Try = 0; Try < 2; ++Try)
	{
		const bool bOncoming = bTruck || ((AmbientFlip + Try) % 3 != 0);   // 2/3 oncoming, 1/3 catching up
		const float Gap = bTruck ? FMath::FRandRange(65000.f, 80000.f) : bOncoming ? FMath::FRandRange(35000.f, 55000.f) : FMath::FRandRange(25000.f, 35000.f);
		const float D = bOncoming ? PlayerDist + PlayerDir * Gap : PlayerDist - PlayerDir * Gap;
		const int32 Dir = bOncoming ? -PlayerDir : PlayerDir;
		if (D < 2000.f || D > Length - 2000.f)
		{
			continue;
		}
		if (bTruck && ((TruckMinCm >= 0.f && D < TruckMinCm) || (TruckMaxCm >= 0.f && D > TruckMaxCm)))
		{
			continue;
		}
		bool bClear = true;
		for (TActorIterator<AHGGroundVehicle> It(GetWorld()); It && bClear; ++It)
		{
			bClear = !(It->IsAutoDriving() && FMath::Abs(It->GetRoadDistance() - D) < 4000.f);
		}
		if (!bClear)
		{
			continue;
		}
		V->SetActorHiddenInGame(false);
		V->SetActorTickEnabled(true);
		if (bTruck)
		{
			V->SetAutoDriveRange(TruckMinCm, TruckMaxCm);
		}
		const float Kmh = bTruck ? 34.f : bOncoming ? FMath::FRandRange(36.f, 46.f) : FMath::FRandRange(50.f, 56.f);
		V->StartAutoDrive(Road, D, Dir, Kmh, LaneOffset + (bTruck ? 40.f : FMath::FRandRange(-30.f, 30.f)));
		++AmbientFlip;
		return true;
	}
	return false;
}

void AHGTrafficSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Road || !Player || Ambient.Num() == 0)
	{
		return;
	}
	const USplineComponent* Sp = Road->GetSpline();
	const FVector Loc = Player->GetActorLocation();
	const float PD = Road->GetDistanceAlongRoad(Loc);
	if (FVector::Dist2D(Sp->GetLocationAtDistanceAlongSpline(PD, ESplineCoordinateSpace::World), Loc) > 20000.f)
	{
		return;   // player is off the road network (on the river, up a hill): leave the traffic where it is
	}
	int32 Dir = 1;
	if (const AHGGroundVehicle* PV = Cast<AHGGroundVehicle>(Player))
	{
		Dir = PV->GetTravelDirection();
	}
	else if (Player->GetVelocity().Size2D() > 50.f)
	{
		Dir = FVector::DotProduct(Player->GetVelocity(), Sp->GetDirectionAtDistanceAlongSpline(PD, ESplineCoordinateSpace::World)) >= 0.f ? 1 : -1;
	}
	for (int32 i = 0; i < Ambient.Num(); ++i)
	{
		AHGGroundVehicle* V = Ambient[i];
		if (!V || (AmbientPlaced[i] && !V->IsAutoDriving()))
		{
			continue;   // hijacked by the player: it's theirs now
		}
		if (!AmbientPlaced[i] || FMath::Abs(V->GetRoadDistance() - PD) > 90000.f)
		{
			AmbientPlaced[i] = PlaceAmbient(V, Cast<AHGTruckPawn>(V) != nullptr, PD, Dir) || AmbientPlaced[i];
		}
	}
}
