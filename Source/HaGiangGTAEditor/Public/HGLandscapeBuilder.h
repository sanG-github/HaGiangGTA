#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HGLandscapeBuilder.generated.h"

class ALandscape;
class UMaterialInterface;

/**
 * Editor-only helpers exposed to Python (unreal.HGLandscapeBuilder.*):
 * stock UE 5.5 Python cannot create a landscape from a heightmap, so the
 * checkpoint scripts call this instead.
 */
UCLASS()
class HAGIANGGTAEDITOR_API UHGLandscapeBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a landscape in the editor world from a raw little-endian uint16
	 * heightmap (.r16), e.g. 4033 x 4033 with 63-quad sections, 2x2 sections
	 * per component (32 x 32 components).
	 */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Editor")
	static ALandscape* ImportLandscapeFromRaw16(const FString& RawFilePath, int32 Size, FVector Location, FVector Scale,
		int32 SectionSizeQuads, int32 SectionsPerComponent, UMaterialInterface* Material, FName Tag);

	/** Set (or clear) the material on an existing landscape and refresh it. */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Editor")
	static void SetLandscapeMaterial(ALandscape* Landscape, UMaterialInterface* Material);
};
