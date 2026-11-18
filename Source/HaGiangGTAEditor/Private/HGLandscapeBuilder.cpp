#include "HGLandscapeBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogHGLandscapeBuilder, Log, All);

ALandscape* UHGLandscapeBuilder::ImportLandscapeFromRaw16(const FString& RawFilePath, int32 Size, FVector Location, FVector Scale,
	int32 SectionSizeQuads, int32 SectionsPerComponent, UMaterialInterface* Material, FName Tag)
{
	if (!GEditor)
	{
		return nullptr;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogHGLandscapeBuilder, Error, TEXT("No editor world."));
		return nullptr;
	}

	const int32 ComponentQuads = SectionSizeQuads * SectionsPerComponent;
	if (Size < 2 || ComponentQuads <= 0 || (Size - 1) % ComponentQuads != 0)
	{
		UE_LOG(LogHGLandscapeBuilder, Error, TEXT("Size %d is not a valid landscape size for %d quads/section x %d sections."),
			Size, SectionSizeQuads, SectionsPerComponent);
		return nullptr;
	}

	FString FullPath = RawFilePath;
	if (FPaths::IsRelative(FullPath))
	{
		FullPath = FPaths::Combine(FPaths::ProjectDir(), RawFilePath);
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FullPath))
	{
		UE_LOG(LogHGLandscapeBuilder, Error, TEXT("Cannot read %s"), *FullPath);
		return nullptr;
	}
	const int64 Expected = int64(Size) * int64(Size) * 2;
	if (Bytes.Num() != Expected)
	{
		UE_LOG(LogHGLandscapeBuilder, Error, TEXT("%s has %d bytes, expected %lld (%d x %d x 2)."), *FullPath, Bytes.Num(), Expected, Size, Size);
		return nullptr;
	}

	TArray<uint16> Heights;
	Heights.SetNumUninitialized(Size * Size);
	for (int32 i = 0; i < Heights.Num(); ++i)
	{
		Heights[i] = uint16(Bytes[2 * i]) | (uint16(Bytes[2 * i + 1]) << 8); // little-endian on every platform
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transactional;
	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator, Params);
	if (!Landscape)
	{
		UE_LOG(LogHGLandscapeBuilder, Error, TEXT("Failed to spawn ALandscape."));
		return nullptr;
	}

	// UE 5.8: edit layers are always on; Import() creates the default edit layer when given none.
	Landscape->LandscapeMaterial = Material;
	Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((Size * Size) / (2048 * 2048) + 1), (uint32)2);
	Landscape->SetActorRelativeScale3D(Scale);

	TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
	HeightDataPerLayer.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
	MaterialLayerDataPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	Landscape->Import(FGuid::NewGuid(), 0, 0, Size - 1, Size - 1, SectionsPerComponent, SectionSizeQuads,
		HeightDataPerLayer, *FullPath, MaterialLayerDataPerLayer, ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
	{
		Info->UpdateLayerInfoMap(Landscape);
	}
	Landscape->RegisterAllComponents();

	Landscape->SetActorLabel(TEXT("Landscape_HaGiang"));
	if (!Tag.IsNone())
	{
		Landscape->Tags.AddUnique(Tag);
	}

	FProperty* MaterialProp = FindFProperty<FProperty>(ALandscapeProxy::StaticClass(), TEXT("LandscapeMaterial"));
	if (MaterialProp)
	{
		FPropertyChangedEvent Event(MaterialProp);
		Landscape->PostEditChangeProperty(Event);
	}
	Landscape->PostEditChange();

	UE_LOG(LogHGLandscapeBuilder, Log, TEXT("Landscape %dx%d imported from %s"), Size, Size, *FullPath);
	return Landscape;
}

void UHGLandscapeBuilder::SetLandscapeMaterial(ALandscape* Landscape, UMaterialInterface* Material)
{
	if (!Landscape)
	{
		return;
	}
	Landscape->Modify();
	Landscape->LandscapeMaterial = Material;
	if (FProperty* MaterialProp = FindFProperty<FProperty>(ALandscapeProxy::StaticClass(), TEXT("LandscapeMaterial")))
	{
		FPropertyChangedEvent Event(MaterialProp);
		Landscape->PostEditChangeProperty(Event);
	}
}
