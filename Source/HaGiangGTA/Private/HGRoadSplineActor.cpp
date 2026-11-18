#include "HGRoadSplineActor.h"

#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
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
			Seg->SetMaterial(0, Mat);
		}
		Seg->SetForwardAxis(ESplineMeshAxis::X, false);
		Seg->SetSplineUpDir(FVector::UpVector, false);
		Seg->SetStartAndEnd(P0, T0, P1, T1, false);
		Seg->SetStartScale(FVector2D(GetWidthAtDistance(D0) / 100.f, RoadThickness / 100.f), false);
		Seg->SetEndScale(FVector2D(GetWidthAtDistance(D1) / 100.f, RoadThickness / 100.f), false);
		Seg->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Seg->SetCanEverAffectNavigation(false);
		Seg->SetCastShadow(false);
		// Round 3B: road distance for the materials (markings and concrete slabs per real metre, a tone per pour):
		// [0] distance along the road at the segment start (m), [1] segment length (m).
		Seg->SetCustomPrimitiveDataFloat(0, D0 / 100.f);
		Seg->SetCustomPrimitiveDataFloat(1, Step / 100.f);
		Seg->RegisterComponent();
		AddInstanceComponent(Seg);
		Seg->UpdateMesh();
		Segments.Add(Seg);
	}
	UE_LOG(LogHaGiang, Log, TEXT("Road meshes rebuilt: %d segments"), Count);
}
