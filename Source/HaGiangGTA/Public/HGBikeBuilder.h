#pragma once

#include "CoreMinimal.h"
#include "HGBikeBuilder.generated.h"

class UStaticMesh;

/** Bike types of the Ha Giang loop (AHGBikePawn::SetBikeModel). */
UENUM(BlueprintType)
enum class EHGBikeModel : uint8
{
	/** Honda Win 100/110: the classic loop rental - tank, long flat seat, big rear rack, chrome fender. */
	Win,
	/** Honda Wave step-through scooter: plastic leg shield, handlebar cowl, step-through floor. */
	Wave,
	/** Honda Dream: the boxy older step-through (maroon / brown, chrome grab rail). */
	Dream,
	/** Yamaha Sirius-style semi-automatic underbone: sporty fairings and stripes. */
	Sirius,
	/** Easy Rider tour Win: side luggage racks with dry bags, backpacks strapped on the rack, flag. */
	EasyRiderWin,
	/** Local working bike (Win frame) loaded with grass, firewood or a pig basket (by seed). */
	LocalLoaded
};

/**
 * Bikes from merged primitives (FHGMeshKit), in the bike's LeanRoot frame (ground at Z = 0, X forward,
 * wheels at X = +-65 cm, axles 30 cm up). Painted panels use the MID parameter "Tint" (body colour).
 * One draw for the body (+ load), one per wheel.
 */
namespace HGBikes
{
	/** Wheel around its axle (origin), axis = local Y: spin it with a pitch rotation. */
	HAGIANGGTA_API UStaticMesh* Wheel(bool bFront, bool bScooter = false);
	/** Body of Model; LoadSeed picks the load of LocalLoaded / the bags of EasyRiderWin. */
	HAGIANGGTA_API UStaticMesh* Body(EHGBikeModel Model, int32 LoadSeed = 0);
	/** Vietnamese flag cloth, pivot on the pole (cloth along +X... flies backwards along -X). */
	HAGIANGGTA_API UStaticMesh* Flag();

	HAGIANGGTA_API bool IsStepThrough(EHGBikeModel Model);
	HAGIANGGTA_API FVector HeadLampLens(EHGBikeModel Model);
	HAGIANGGTA_API FVector HeadLampSize(EHGBikeModel Model);
	HAGIANGGTA_API FVector TailLampLens(EHGBikeModel Model);
	HAGIANGGTA_API FVector PlateLocation(EHGBikeModel Model);
	/** Top of the flag pole (EasyRiderWin always, others by seed). */
	HAGIANGGTA_API FVector FlagMount(EHGBikeModel Model);
	/** Typical paint for Model (index by seed). */
	HAGIANGGTA_API FLinearColor Paint(EHGBikeModel Model, int32 Seed);
	/** Traffic mix: weighted random model for a seed (Win 30 %, Wave 20 %, Dream 10 %, Sirius 15 %, EasyRiderWin 15 %, LocalLoaded 10 %). */
	HAGIANGGTA_API EHGBikeModel RandomModel(int32 Seed);

	inline FVector FrontAxle() { return FVector(65.f, 0.f, 30.f); }
	inline FVector RearAxle() { return FVector(-65.f, 0.f, 30.f); }
}
