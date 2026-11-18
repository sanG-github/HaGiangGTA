#include "HGRoadSplineActor.h"
#include "Misc/CommandLine.h"

#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "HaGiangGTA.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AHGRoadSplineActor::AHGRoadSplineActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	Spline->SetMobility(EComponentMobility::Static);
	RootComponent = Spline;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		RoadMesh = CubeMesh.Object;
	}
}

void AHGRoadSplineActor::SetRoadPoints(const TArray<FVector>& WorldPoints)
{
	Modify();
	Spline->ClearSplinePoints(false);
	for (const FVector& P : WorldPoints)
	{
		Spline->AddSplinePoint(P, ESplineCoordinateSpace::World, false);
	}
	for (int32 i = 0; i < Spline->GetNumberOfSplinePoints(); ++i)
	{
		Spline->SetSplinePointType(i, ESplinePointType::Curve, false);
	}
	Spline->SetClosedLoop(false, false);
	Spline->UpdateSpline();
	UE_LOG(LogHaGiang, Log, TEXT("Road spline: %d points, %.2f km"), WorldPoints.Num(), Spline->GetSplineLength() / 100000.f);
}

float AHGRoadSplineActor::GetRoadLength() const
{
	return Spline->GetSplineLength();
}

float AHGRoadSplineActor::GetDistanceAlongRoad(const FVector& WorldLocation) const
{
	const float Key = Spline->FindInputKeyClosestToWorldLocation(WorldLocation);
	return Spline->GetDistanceAlongSplineAtSplineInputKey(Key);
}

float AHGRoadSplineActor::GetWidthAtDistance(float DistanceCm) const
{
	if (SecondaryFromDistance < 0.f)
	{
		return RoadWidth;
	}
	const float Alpha = FMath::Clamp((DistanceCm - SecondaryFromDistance + 1000.f) / 2000.f, 0.f, 1.f);
	return FMath::Lerp(RoadWidth, SecondaryWidth, Alpha);
}

void AHGRoadSplineActor::RebuildRoadMeshes()
{
	Modify();
	for (USplineMeshComponent* Old : Segments)
	{
		if (Old)
		{
			RemoveInstanceComponent(Old);
			Old->DestroyComponent();
		}
	}
	Segments.Reset();
	SharedMIDs.Reset();

	if (!RoadMesh)
	{
		UE_LOG(LogHaGiang, Warning, TEXT("%s: no RoadMesh set"), *GetName());
		return;
	}

	const float Length = Spline->GetSplineLength();
	const int32 Count = FMath::Max(1, FMath::CeilToInt(Length / FMath::Max(SegmentLength, 100.f)));
	const float Step = Length / Count;
	// Cube is 100 uu thick centred on the spline: lift by half the thickness so
	// the top of the asphalt sits exactly on the spline points.
	const FVector Lift(0.f, 0.f, RoadThickness * 0.5f);

	Segments.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const float D0 = Step * i;
		const float D1 = Step * (i + 1);
		const FVector P0 = Spline->GetLocationAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local) - Lift;
		const FVector P1 = Spline->GetLocationAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local) - Lift;
		const FVector T0 = Spline->GetDirectionAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local) * Step;
		const FVector T1 = Spline->GetDirectionAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local) * Step;

		USplineMeshComponent* Seg = NewObject<USplineMeshComponent>(this, NAME_None, RF_Transactional);
		Seg->CreationMethod = EComponentCreationMethod::Instance;
		Seg->SetMobility(EComponentMobility::Static);
		Seg->SetupAttachment(Spline);
		Seg->SetStaticMesh(RoadMesh);
		const bool bSecondary = SecondaryFromDistance >= 0.f && (D0 + D1) * 0.5f >= SecondaryFromDistance;
		UMaterialInterface* Mat = (bSecondary && SecondaryMaterial) ? SecondaryMaterial.Get() : RoadMaterial.Get();
		if (Mat)
		{
			// Round 3B: road distance for the materials (dashes and concrete pours per real metre) as MID
			// parameters - custom primitive data did not reach the spline-mesh shaders on this platform.
			// Round 4B: shared MIDs - the markings repeat every 36 m (9 m dash cycle, 4 m pours), so only the start
			// modulo 36 m matters; quantised to 0.25 m that is <= 144 phases per material instead of one MID per
			// segment (281 unique materials broke draw batching).
			const float PhaseM = FMath::Fmod(D0 / 100.f, 36.f);
			const int32 PhaseKey = FMath::RoundToInt(PhaseM * 4.f) % 144;
			const int32 LenKey = FMath::RoundToInt(Step / 100.f * 4.f);
			const FString Key = FString::Printf(TEXT("%p_%d_%d"), Mat, PhaseKey, LenKey);
			TObjectPtr<UMaterialInstanceDynamic>* Found = SharedMIDs.Find(Key);
			UMaterialInstanceDynamic* MID = Found ? Found->Get() : nullptr;
			if (!MID)
			{
				MID = UMaterialInstanceDynamic::Create(Mat, this);
				MID->SetScalarParameterValue(TEXT("RoadSegStartM"), PhaseKey * 0.25f);
				MID->SetScalarParameterValue(TEXT("RoadSegLenM"), LenKey * 0.25f);
				SharedMIDs.Add(Key, MID);
			}
			Seg->SetMaterial(0, MID);
		}
		Seg->SetForwardAxis(ESplineMeshAxis::X, false);
		Seg->SetSplineUpDir(FVector::UpVector, false);
		Seg->SetStartAndEnd(P0, T0, P1, T1, false);
		Seg->SetStartScale(FVector2D(GetWidthAtDistance(D0) / 100.f, RoadThickness / 100.f), false);
		Seg->SetEndScale(FVector2D(GetWidthAtDistance(D1) / 100.f, RoadThickness / 100.f), false);
		Seg->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Seg->SetCanEverAffectNavigation(false);
		Seg->SetCastShadow(false);
		Seg->RegisterComponent();
		AddInstanceComponent(Seg);
		Seg->UpdateMesh();
		Segments.Add(Seg);
	}
	UE_LOG(LogHaGiang, Log, TEXT("Road meshes rebuilt: %d segments"), Count);
}

void AHGRoadSplineActor::BeginPlay()
{
	Super::BeginPlay();
	float Cull = RuntimeCullDistance;
	float Meters = -1.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("hgroadcull="), Meters))
	{
		Cull = Meters * 100.f;
	}
	for (USplineMeshComponent* Seg : Segments)
	{
		if (Seg)
		{
			Seg->SetCullDistance(Cull);
		}
	}
}
