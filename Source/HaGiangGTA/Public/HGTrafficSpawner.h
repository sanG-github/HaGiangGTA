#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGTrafficSpawner.generated.h"

class AHGBikePawn;
class AHGGroundVehicle;
class AHGRoadSplineActor;
class AHGTruckPawn;

USTRUCT(BlueprintType)
struct FHGConvoySpec
{
	GENERATED_BODY()

	/** Where the convoy leader starts, metres along the road. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Convoy") float StartMeters = 300.f;
	/** +1 up the pass, -1 down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Convoy") int32 Direction = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Convoy") int32 NumBikes = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Convoy") float CruiseKmh = 42.f;
};

USTRUCT(BlueprintType)
struct FHGTruckSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Truck") float StartMeters = 6000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Truck") int32 Direction = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Truck") float CruiseKmh = 38.f;
	/** Stretch of road (metres) the truck ping-pongs on; < 0 = road end. Keeps trucks off the narrow
	 *  Ta Lang concrete road (real trucks do not take its 8 m hairpins). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Truck") float MinMeters = -1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Truck") float MaxMeters = -1.f;
};

/**
 * Spawns the Ha Giang Loop traffic at BeginPlay: Easy Rider motorbike convoys
 * (8-12 bikes, guide in a reflective vest + backpacker in a red star T-shirt,
 * nylon-wrapped backpacks) nose-to-tail along the road spline, and dump trucks
 * coming the other way. Every vehicle can be hijacked with [F].
 */
UCLASS()
class HAGIANGGTA_API AHGTrafficSpawner : public AActor
{
	GENERATED_BODY()

public:
	AHGTrafficSpawner();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TObjectPtr<AHGRoadSplineActor> Road;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TArray<FHGConvoySpec> Convoys;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TArray<FHGTruckSpec> Trucks;

	/** Distance between bikes in a convoy (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float BikeSpacing = 1100.f;

	/** Lateral offset from the road centre (cm, right of travel - Vietnam drives on the right). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float LaneOffset = 140.f;   // centre of a bike in the right half of the 6 m QL4C, ~1.6 m clear of the edge

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float PassengerChance = 0.8f;

	/**
	 * Solo traffic kept around the player wherever they ride: bikes coming the other way and a few faster ones
	 * catching up from behind (they overtake), plus trucks on QL4C. Each is (re)placed out of sight - beyond
	 * its draw distance, 350-550 m ahead or 250-350 m behind along the road - once it is 900 m away.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Ambient") int32 AmbientBikes = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Ambient") int32 AmbientTrucks = 1;

	const TArray<TObjectPtr<AHGGroundVehicle>>& GetSpawnedVehicles() const { return Spawned; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient) TArray<TObjectPtr<AHGGroundVehicle>> Spawned;
	UPROPERTY(Transient) TArray<TObjectPtr<AHGGroundVehicle>> Ambient;
	TArray<bool> AmbientPlaced;
	float AmbientTimer = 0.f;
	int32 AmbientFlip = 0;
	float TruckMinCm = -1.f;
	float TruckMaxCm = -1.f;
	bool PlaceAmbient(AHGGroundVehicle* V, bool bTruck, float PlayerDist, int32 PlayerDir);
	/** Bike model / variant hook (seeded; convoys share a tour group). Wired to the bike builder after merge. */
	void ConfigureBikeVariant(AHGBikePawn* Bike, bool bConvoy, int32 ConvoyIndex);
};
