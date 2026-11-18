#pragma once

#include "CoreMinimal.h"

class UAudioComponent;
class USceneComponent;
class USoundAttenuation;
class USoundBase;

/**
 * Spatial audio helpers. The procedural WAVs are imported without attenuation
 * assets, so distance falloff is created here in code (cached per radius).
 */
namespace HGAudio
{
	HAGIANGGTA_API USoundAttenuation* GetAttenuation(float FalloffRadiusCm);

	HAGIANGGTA_API void PlayAt(const UObject* WorldContext, USoundBase* Sound, const FVector& Location,
		float Volume = 1.f, float Pitch = 1.f, float FalloffRadiusCm = 6000.f);

	/** Looping 3D sound following Parent (engines, siren, rotor). Not started. */
	HAGIANGGTA_API UAudioComponent* CreateLoop(USceneComponent* Parent, USoundBase* Sound, float FalloffRadiusCm);
}
