#include "HGAudio.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/Package.h"

namespace HGAudio
{
	USoundAttenuation* GetAttenuation(float FalloffRadiusCm)
	{
		static TMap<int32, USoundAttenuation*> Cache;
		const int32 Key = FMath::RoundToInt(FalloffRadiusCm / 100.f);
		if (USoundAttenuation** Found = Cache.Find(Key))
		{
			return *Found;
		}
		USoundAttenuation* Att = NewObject<USoundAttenuation>(GetTransientPackage());
		Att->AddToRoot(); // lives for the session, shared by every sound with this radius
		FSoundAttenuationSettings& S = Att->Attenuation;
		S.bAttenuate = true;
		S.bSpatialize = true;
		S.AttenuationShape = EAttenuationShape::Sphere;
		S.AttenuationShapeExtents = FVector(FalloffRadiusCm * 0.08f, 0.f, 0.f);
		S.FalloffDistance = FalloffRadiusCm;
		S.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Cache.Add(Key, Att);
		return Att;
	}

	void PlayAt(const UObject* WorldContext, USoundBase* Sound, const FVector& Location, float Volume, float Pitch, float FalloffRadiusCm)
	{
		if (!Sound || !WorldContext)
		{
			return;
		}
		UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.f,
			GetAttenuation(FalloffRadiusCm));
	}

	UAudioComponent* CreateLoop(USceneComponent* Parent, USoundBase* Sound, float FalloffRadiusCm)
	{
		if (!Parent || !Sound)
		{
			return nullptr;
		}
		UAudioComponent* Audio = NewObject<UAudioComponent>(Parent->GetOwner());
		Audio->bAutoActivate = false;
		Audio->bAutoDestroy = false;
		Audio->SetupAttachment(Parent);
		Audio->SetSound(Sound);
		Audio->bOverrideAttenuation = false;
		Audio->AttenuationSettings = GetAttenuation(FalloffRadiusCm);
		Audio->RegisterComponent();
		return Audio;
	}
}
