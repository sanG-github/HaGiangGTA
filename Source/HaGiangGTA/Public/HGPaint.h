#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

/**
 * Greybox colouring for parts built from /Engine/BasicShapes.
 *
 * In UE 5.8 those meshes default to WorldGridMaterial (the grey checker, which has no "Color" parameter), so
 * CreateAndSetMaterialInstanceDynamic(0) on the mesh's own material left every bike, rider, cow, truck and
 * tourist rendering as a checker. Always start the MID from BasicShapeMaterial, which exposes "Color".
 */
namespace HGPaint
{
	inline UMaterialInterface* BaseMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		}
		return Cached.Get();
	}

	/** A tintable MID (parameter "Color") on material slot Index of C. */
	inline UMaterialInstanceDynamic* MakeMID(UPrimitiveComponent* C, int32 Index = 0)
	{
		if (!C)
		{
			return nullptr;
		}
		UMaterialInterface* Base = BaseMaterial();
		return Base ? C->CreateDynamicMaterialInstance(Index, Base) : C->CreateAndSetMaterialInstanceDynamic(Index);
	}

	inline void Paint(UPrimitiveComponent* C, const FLinearColor& Color)
	{
		if (UMaterialInstanceDynamic* MID = MakeMID(C))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}

	/**
	 * Greybox actors are 10-30 engine-shape parts each (a bike with rider and passenger is ~30 draws, x4 with
	 * the shadow cascades): with convoys, parked bikes, cows and tourists along 19 km the render thread was the
	 * frame limit. Parts under SmallRadiusCm stop drawing (and casting shadows) past SmallCm, the rest past FarCm.
	 */
	inline void LimitDrawDistance(AActor* Actor, float SmallCm = 6000.f, float FarCm = 30000.f, float SmallRadiusCm = 30.f)
	{
		if (!Actor)
		{
			return;
		}
		TInlineComponentArray<UPrimitiveComponent*> Prims(Actor);
		for (UPrimitiveComponent* P : Prims)
		{
			if (!P)
			{
				continue;
			}
			const bool bSmall = P->Bounds.SphereRadius < SmallRadiusCm;
			P->SetCullDistance(bSmall ? SmallCm : FarCm);
			if (bSmall)
			{
				P->SetCastShadow(false);
			}
		}
	}
}
