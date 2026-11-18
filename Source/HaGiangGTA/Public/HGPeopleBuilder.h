#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Who: outfit, build and headgear (ART_BIBLE section 5 - people of the Ha Giang loop). */
enum class EHGPersonLook : uint8
{
	HmongWoman,    // pleated batik skirt with embroidered bands, indigo jacket, leg wraps, head wrap, bamboo basket / tho cam bag
	LocalMan,      // dark jacket (black / army olive / navy), dark trousers, cap or non la
	Backpacker,    // co do sao vang T-shirt, cargo shorts, yellow duck-bill helmet ("non vit"), backpack, scarf
	EasyRider,     // tour guide: tour-colour vest over a dark jacket, full helmet, tho cam scarf
	Player,        // olive jacket and jeans; non la on foot, open-face helmet on a bike
	Police         // CSGT traffic police: khaki-yellow shirt, dark green trousers, white helmet
};

/** Standing: origin at the feet. Rider / Passenger: origin where the hips touch the seat, X forward. */
enum class EHGPersonPose : uint8
{
	Standing,
	Rider,         // hands on the handlebar grips (seat-relative ~52, +-32, 17)
	Passenger      // rear seat, feet on the rear pegs
};

/**
 * One animated person: a core mesh (hips, torso, outfit, seated legs) with head, arms, standing legs and
 * a scarf tail as child parts on pivots (neck, shoulders, hips, nape). Animate() drives them procedurally:
 * walk cycle with arm swing, idle weight shift and head turns, phone photos, waving, arms-up screams,
 * riders' arms following the steering and heads turning / leaning into bends, scarves flapping in the
 * wind. It costs a few rotations per tick and stops past ~60 m from the camera.
 */
struct HAGIANGGTA_API FHGPersonRig
{
	UStaticMeshComponent* Body = nullptr;
	UStaticMeshComponent* Head = nullptr;
	UStaticMeshComponent* Arm[2] = { nullptr, nullptr };   // 0 = left
	UStaticMeshComponent* Leg[2] = { nullptr, nullptr };   // standing only
	UStaticMeshComponent* ScarfTail = nullptr;

	// ---- Inputs for Animate ----
	float WalkSpeed = 0.f;     // cm/s, standing pose: walk cycle
	float HeadYaw = 0.f;       // extra look direction (deg)
	float Steer = 0.f;         // rider: -1..1 (arms turn the bar, head looks into the bend)
	float LeanDeg = 0.f;       // rider / passenger: bike lean (head stays nearer upright)
	float Wind = 0.f;          // 0..1, scarves stream back
	bool bPhoto = false;       // right arm up holding a phone
	bool bWave = false;        // right arm waving
	bool bArmsUp = false;      // both arms up (screaming passenger)
	bool bHoldOn = false;      // passenger hands on the rider's waist instead of the thighs

	bool IsValid() const { return Body != nullptr; }
	void SetVisible(bool bVisible);
	/** Advance the procedural animation. Owner is used for the camera-distance check. */
	void Animate(AActor* Owner, float DeltaSeconds);

	EHGPersonPose Pose = EHGPersonPose::Standing;
	FVector BodyBase = FVector::ZeroVector;
	float Phase = 0.f;
	float Clock = 0.f;
	float IdleSeed = 0.f;
	float NearTimer = 0.f;
	bool bNear = true;
	FVector Rest[2];           // arm rest directions (body frame)
};

/**
 * Low-poly people from merged primitives (FHGMeshKit). Meshes are shared per look / pose / variant / part.
 */
namespace HGPeople
{
	/** Colour variants per look (tour-group colours for EasyRider / Backpacker). */
	constexpr int32 NumVariants = 4;

	/** Adds a person to Owner under Parent (runtime: call from BeginPlay). */
	HAGIANGGTA_API FHGPersonRig Spawn(AActor* Owner, USceneComponent* Parent, EHGPersonLook Look, EHGPersonPose Pose,
		int32 Variant, FName Name, const FVector& Location = FVector::ZeroVector, bool bScarf = false);

	/** Stable variant from a name (e.g. the actor's), so a crowd is not all the same person. */
	HAGIANGGTA_API int32 VariantFor(const FString& Seed);
}
