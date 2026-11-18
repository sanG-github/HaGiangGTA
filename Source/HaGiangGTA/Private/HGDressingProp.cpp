#include "HGDressingProp.h"
#include "HGBikeBuilder.h"
#include "HGMeshKit.h"
#include "HGRoadSplineActor.h"
#include "Components/SplineComponent.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Materials/MaterialInstanceDynamic.h"

// Named namespace (not anonymous): unity builds merge this file with the bike / people builders.
namespace HGDressingKit
{
	using FColorL = FLinearColor;

	/** Triangular prism along +X from X0 to X1: base half-width HalfW at BaseZ, apex Height above (gable ends). */
	void Gable(FHGMeshKit& K, float X0, float X1, float BaseZ, float HalfW, float Height)
	{
		const float Rx = Height / 1.5f;
		const float Ry = HalfW / 0.866f;
		const FMatrix M = FRotationMatrix::MakeFromZX(FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f));
		const FTransform Xf(M.Rotator(), FVector(X0, 0.f, BaseZ + Rx * 0.5f));
		K.Frustum(Xf, FVector2D(Rx, Ry), FVector2D(Rx, Ry), X1 - X0, 3, true);
	}

	void StoneCourse(FHGMeshKit& K, const FVector& A, const FVector& B, float BaseZ, int32 Courses, uint32 Seed, float Thick = 34.f)
	{
		const FVector D = (B - A).GetSafeNormal2D();
		const float Len = FVector::Dist2D(A, B);
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		FRandomStream R{int32(Seed)};
		for (int32 c = 0; c < Courses; ++c)
		{
			float t = (c & 1) ? 18.f : 0.f;
			while (t < Len)
			{
				const float L = R.FRandRange(28.f, 46.f);
				const float Tone = R.FRandRange(0.15f, 0.27f);
				K.Paint(FColorL(Tone, Tone * 0.98f, Tone * 0.93f), HGSurf::Matte);
				const FVector P = A + D * FMath::Min(t + L * 0.5f, Len - 10.f);
				K.Box(FVector(P.X, P.Y, BaseZ + c * 24.f + 12.f), FVector(L - 3.f, Thick + R.FRandRange(-5.f, 5.f), R.FRandRange(19.f, 26.f)),
					FRotator(R.FRandRange(-3.f, 3.f), Yaw + R.FRandRange(-4.f, 4.f), R.FRandRange(-3.f, 3.f)));
				t += L;
			}
		}
	}

	void BuildHouse(FHGMeshKit& K, int32 V)
	{
		const bool bStone = (V % 2) == 0;
		const bool bFence = (V % 3) != 2;
		FRandomStream R(1000 + V);
		const FColorL Earth(0.27f, 0.17f, 0.1f), Stone(0.22f, 0.21f, 0.2f), Wood(0.1f, 0.055f, 0.03f);
		const FColorL Tile(0.11f, 0.11f, 0.115f), TileLight(0.17f, 0.17f, 0.175f);
		const float HL = 500.f, HW = 310.f, WallTop = 300.f;
		// Stone platform (hillside plots: it reaches 2.6 m down so the downhill side never floats).
		K.Paint(FColorL(0.17f, 0.165f, 0.155f));
		K.Box(FVector(0.f, 0.f, -110.f), FVector(2 * HL + 60.f, 2 * HW + 60.f, 300.f));
		// Walls.
		K.Paint(bStone ? Stone : Earth);
		K.Box(FVector(0.f, 0.f, 40.f + (WallTop - 40.f) * 0.5f), FVector(2 * HL, 2 * HW, WallTop - 40.f));
		Gable(K, -HL, HL, WallTop, HW, 170.f);
		if (bStone)
		{
			// Coursed stone: slightly darker / lighter bands with mortar lines.
			for (int32 c = 0; c < 9; ++c)
			{
				const float T = R.FRandRange(0.18f, 0.26f);
				K.Paint(FColorL(T, T * 0.98f, T * 0.94f));
				K.Box(FVector(0.f, 0.f, 52.f + c * 28.f), FVector(2 * HL + 1.5f, 2 * HW + 1.5f, 22.f));
			}
		}
		else
		{
			// Rammed earth: horizontal formwork layers.
			for (int32 c = 0; c < 5; ++c)
			{
				K.Paint(Earth * R.FRandRange(0.85f, 1.1f));
				K.Box(FVector(0.f, 0.f, 70.f + c * 50.f), FVector(2 * HL + 1.2f, 2 * HW + 1.2f, 4.f));
			}
		}
		// Door, windows, lintel (front = +Y).
		K.Paint(Wood, HGSurf::Satin);
		K.Box(FVector(0.f, HW + 1.f, 140.f), FVector(150.f, 4.f, 200.f));
		K.Box(FVector(0.f, HW + 2.f, 250.f), FVector(210.f, 6.f, 16.f));
		K.Paint(FColorL(0.02f, 0.02f, 0.02f));
		K.Box(FVector(-300.f, HW + 1.f, 180.f), FVector(60.f, 4.f, 50.f));
		K.Box(FVector(300.f, HW + 1.f, 180.f), FVector(60.f, 4.f, 50.f));
		K.Paint(FColorL(0.55f, 0.08f, 0.06f));
		K.Box(FVector(-40.f, HW + 3.f, 200.f), FVector(30.f, 1.f, 60.f));     // red paper couplets by the door
		K.Box(FVector(40.f, HW + 3.f, 200.f), FVector(30.f, 1.f, 60.f));
		// Yin-yang tile roof: two slopes, channel tiles every 24 cm, ridge cap.
		const float OH = 70.f, RidgeZ = WallTop + 170.f;
		const float SlopeW = FMath::Sqrt(FMath::Square(HW + OH) + FMath::Square(RidgeZ - (WallTop - 30.f)));
		for (const float S : { -1.f, 1.f })
		{
			const FVector Mid(0.f, S * (HW + OH) * 0.5f, (RidgeZ + WallTop - 30.f) * 0.5f);
			const FVector Down = FVector(0.f, S * (HW + OH), WallTop - 30.f - RidgeZ).GetSafeNormal();
			FVector Up = FVector::CrossProduct(FVector::ForwardVector, Down);
			if (Up.Z < 0.f)
			{
				Up = -Up;
			}
			// Roof plane: local X along the ridge, local Y down the slope (built from axes, no roll-sign guessing).
			const FRotator SlopeRot = FRotationMatrix::MakeFromXY(FVector::ForwardVector, Down).Rotator();
			K.Paint(Tile, HGSurf::Satin);
			K.Box(Mid, FVector(2 * HL + 2 * OH, SlopeW, 8.f), SlopeRot);
			int32 i = 0;
			for (float X = -HL - OH + 12.f; X < HL + OH; X += 24.f, ++i)
			{
				K.Paint((i & 1) ? Tile : TileLight, HGSurf::Satin);
				const FVector Top = FVector(X, 0.f, RidgeZ) + Up * 5.f;
				K.Tube(Top, Top + Down * SlopeW, (i & 1) ? 5.f : 6.5f, (i & 1) ? 5.f : 6.5f, 6, true);
			}
			// Eave line of tile ends.
			K.Paint(TileLight, HGSurf::Satin);
			K.Box(FVector(0.f, S * (HW + OH), WallTop - 33.f), FVector(2 * HL + 2 * OH, 8.f, 8.f));
		}
		K.Paint(TileLight, HGSurf::Satin);
		K.Tube(FVector(-HL - OH, 0.f, RidgeZ + 8.f), FVector(HL + OH, 0.f, RidgeZ + 8.f), 11.f, 11.f, 8, true);
		// Porch posts and corn cobs drying under the front eave.
		K.Paint(Wood, HGSurf::Satin);
		K.Tube(FVector(-250.f, HW + 55.f, 40.f), FVector(-250.f, HW + 55.f, WallTop - 30.f), 7.f, 7.f, 8, true);
		K.Tube(FVector(250.f, HW + 55.f, 40.f), FVector(250.f, HW + 55.f, WallTop - 30.f), 7.f, 7.f, 8, true);
		if (V % 4 != 3)
		{
			for (int32 c = 0; c < 6; ++c)
			{
				const float X = -420.f + c * 26.f + R.FRandRange(-5.f, 5.f);
				K.Paint(FColorL(0.75f, 0.5f, 0.06f) * R.FRandRange(0.85f, 1.1f));
				for (int32 k = 0; k < 4; ++k)
				{
					K.Ellipsoid(FVector(X, HW + 30.f, 240.f - k * 22.f), FVector(5.f, 5.f, 11.f), FRotator::ZeroRotator, 6, 4);
				}
			}
		}
		// Firewood stack against the side wall.
		K.Paint(FColorL(0.24f, 0.15f, 0.08f));
		for (int32 r = 0; r < 4; ++r)
		{
			for (int32 c = 0; c < 6; ++c)
			{
				K.Tube(FVector(HL + 25.f, -200.f + c * 16.f, 20.f + r * 15.f), FVector(HL + 95.f, -200.f + c * 16.f, 20.f + r * 15.f), 7.f, 7.f, 6, true);
			}
		}
		// Dry-stone yard wall in front (gate gap at the door).
		if (bFence)
		{
			const float YF = HW + 820.f, XF = HL + 300.f;
			StoneCourse(K, FVector(-XF, HW + 40.f, 0), FVector(-XF, YF, 0), -30.f, 5, 11 + V);
			StoneCourse(K, FVector(XF, HW + 40.f, 0), FVector(XF, YF, 0), -30.f, 5, 23 + V);
			StoneCourse(K, FVector(-XF, YF, 0), FVector(-110.f, YF, 0), -30.f, 5, 37 + V);
			StoneCourse(K, FVector(110.f, YF, 0), FVector(XF, YF, 0), -30.f, 5, 53 + V);
		}
	}

	void BuildStone(FHGMeshKit& K)
	{
		K.Paint(FColorL(0.24f, 0.235f, 0.22f), HGSurf::Matte);
		K.Ellipsoid(FVector(0.f, 0.f, 70.f), FVector(150.f, 55.f, 100.f), FRotator(0.f, 0.f, 4.f), 10, 6);
		K.Paint(FColorL(0.2f, 0.195f, 0.18f));
		K.Ellipsoid(FVector(-110.f, 10.f, 30.f), FVector(60.f, 45.f, 45.f), FRotator(10.f, 20.f, 0.f), 8, 5);
		K.Ellipsoid(FVector(120.f, -5.f, 20.f), FVector(55.f, 40.f, 35.f), FRotator(-8.f, 10.f, 0.f), 8, 5);
		K.Paint(FColorL(0.3f, 0.29f, 0.27f));
		K.Box(FVector(0.f, 50.f, 95.f), FVector(220.f, 14.f, 90.f), FRotator(0.f, 0.f, -6.f));   // dressed face
	}

	void BuildWoodSign(FHGMeshKit& K)
	{
		K.Paint(FColorL(0.15f, 0.09f, 0.05f), HGSurf::Satin);
		K.Box(FVector(-110.f, 0.f, 90.f), FVector(10.f, 10.f, 180.f));
		K.Box(FVector(110.f, 0.f, 90.f), FVector(10.f, 10.f, 180.f));
		K.Paint(FColorL(0.32f, 0.2f, 0.1f), HGSurf::Satin);
		K.Box(FVector(0.f, 0.f, 150.f), FVector(260.f, 6.f, 70.f));
		K.Paint(FColorL(0.2f, 0.12f, 0.06f), HGSurf::Satin);
		K.Box(FVector(0.f, 0.f, 188.f), FVector(270.f, 10.f, 8.f));
	}

	void BuildFlagPole(FHGMeshKit& K)
	{
		K.Paint(FColorL(0.5f, 0.5f, 0.48f));
		K.Box(FVector(0.f, 0.f, 15.f), FVector(220.f, 220.f, 30.f));
		K.Box(FVector(0.f, 0.f, 45.f), FVector(150.f, 150.f, 30.f));
		K.Paint(FColorL(0.8f, 0.8f, 0.78f), HGSurf::Gloss);
		K.Tube(FVector(0.f, 0.f, 60.f), FVector(0.f, 0.f, 1260.f), 7.f, 4.f, 10, true);
		K.Paint(FColorL(0.85f, 0.65f, 0.1f), HGSurf::Chrome);
		K.Ellipsoid(FVector(0.f, 0.f, 1266.f), FVector(8.f), FRotator::ZeroRotator, 8, 5);
	}

	void BuildSprout(FHGMeshKit& K)
	{
		// Young corn (late March: 2-4 leaves, ~30 cm) on a tilled mound.
		K.Paint(FColorL(0.2f, 0.13f, 0.08f));
		K.Ellipsoid(FVector(0.f, 0.f, 0.f), FVector(16.f, 16.f, 5.f), FRotator::ZeroRotator, 6, 3, 0.f);
		K.Paint(FColorL(0.12f, 0.28f, 0.05f), HGSurf::Satin);
		for (int32 i = 0; i < 4; ++i)
		{
			const float Yaw = i * 97.f;
			K.Box(FRotator(0.f, Yaw, 0.f).RotateVector(FVector(8.f, 0.f, 14.f)), FVector(22.f, 4.f, 1.f), FRotator(35.f + i * 6.f, Yaw, 0.f));
		}
		K.Tube(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 26.f), 1.2f, 0.8f, 5, false);
	}
}

AHGDressingProp::AHGDressingProp()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AHGDressingProp::BeginPlay()
{
	Super::BeginPlay();
	using namespace HGDressingKit;
	UStaticMesh* Mesh = nullptr;
	float Cull = 40000.f;
	switch (Kind)
	{
	case EHGDressingKind::HmongHouse:
	{
		// No dry-stone yard wall on a slope steeper than ~12 deg (it would float / bury on the downhill side).
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HGHouseGround), false, this);
		const FVector L = GetActorLocation();
		if (GetWorld()->LineTraceSingleByChannel(Hit, L + FVector(0, 0, 500.f), L - FVector(0, 0, 1500.f), ECC_Visibility, Params)
			&& Hit.ImpactNormal.Z < 0.978f && (Variant % 3) != 2)
		{
			Variant = (Variant % 6) < 3 ? 2 : 5;
		}
		Mesh = FHGMeshKit::Cached(*FString::Printf(TEXT("SM_HGHmongHouse_%d"), Variant % 6), [this](FHGMeshKit& K) { BuildHouse(K, Variant % 6); });
		Cull = 60000.f;
		break;
	}
	case EHGDressingKind::FlagPole:
		Mesh = FHGMeshKit::Cached(TEXT("SM_HGFlagPoleTall"), [](FHGMeshKit& K) { BuildFlagPole(K); });
		break;
	case EHGDressingKind::StoneSign:
		Mesh = FHGMeshKit::Cached(TEXT("SM_HGStoneSign"), [](FHGMeshKit& K) { BuildStone(K); });
		break;
	case EHGDressingKind::WoodSign:
		Mesh = FHGMeshKit::Cached(TEXT("SM_HGWoodSign"), [](FHGMeshKit& K) { BuildWoodSign(K); });
		break;
	case EHGDressingKind::CornField:
		BuildCornField();
		return;
	}
	if (UStaticMeshComponent* C = FHGMeshKit::AddComponent(this, Root, Mesh, TEXT("Kit"), FVector::ZeroVector))
	{
		C->SetCullDistance(Cull);
	}
	if (Kind == EHGDressingKind::FlagPole)
	{
		Flag = FHGMeshKit::AddComponent(this, Root, HGBikes::Flag(), TEXT("Flag"), FVector(0.f, 0.f, 1250.f));
		if (Flag)
		{
			Flag->SetRelativeScale3D(FVector(4.5f));
			Flag->SetCullDistance(40000.f);
			SetActorTickEnabled(true);
		}
	}
	if ((Kind == EHGDressingKind::StoneSign || Kind == EHGDressingKind::WoodSign) && !SignText.IsEmpty())
	{
		// Lettering on a thin plane just in front of the face (texture baked by gen_signs.py).
		const bool bStone = Kind == EHGDressingKind::StoneSign;
		UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		if (SignMaterial && PlaneMesh)
		{
			if (UStaticMeshComponent* P = FHGMeshKit::AddComponent(this, Root, PlaneMesh, TEXT("Lettering"),
				bStone ? FVector(0.f, 57.8f, 98.f) : FVector(0.f, 3.6f, 150.f), FRotator(0.f, 0.f, 90.f)))
			{
				// Plane is 100 x 100 in XY facing +Z; roll 90 stands it up facing +-Y (two-sided material).
				P->SetRelativeScale3D(bStone ? FVector(2.05f, 0.4f, 1.f) : FVector(2.45f, 0.46f, 1.f));
				P->SetMaterial(0, SignMaterial);
				P->SetCastShadow(false);
				P->SetCullDistance(8000.f);
			}
		}
	}
}

void AHGDressingProp::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	if (Flag)
	{
		// Steady valley wind from the west: the flag flies east (+X) and ripples.
		Flag->SetRelativeRotation(FRotator(-8.f + 4.f * FMath::Sin(Clock * 2.3f), 180.f + 14.f * FMath::Sin(Clock * 3.1f) + 6.f * FMath::Sin(Clock * 7.7f), 0.f));
	}
}

void AHGDressingProp::BuildCornField()
{
	using namespace HGDressingKit;
	UStaticMesh* Sprout = FHGMeshKit::Cached(TEXT("SM_HGCornSprout"), [](FHGMeshKit& K) { BuildSprout(K); });
	if (!Sprout || FParse::Param(FCommandLine::Get(), TEXT("hgnokit")))
	{
		return;
	}
	Corn = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, TEXT("Corn"), RF_Transient);
	Corn->SetupAttachment(Root);
	Corn->SetStaticMesh(Sprout);
	Corn->SetMaterial(0, FHGMeshKit::VertexColorMaterial());
	Corn->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Corn->SetCastShadow(false);
	Corn->SetCullDistances(0, 15000);
	Corn->SetUsingAbsoluteLocation(true);
	Corn->SetUsingAbsoluteRotation(true);
	Corn->RegisterComponent();
	AddInstanceComponent(Corn);

	// Houses to keep clear of (kit houses have no collision).
	TArray<FVector> Houses;
	for (TActorIterator<AHGDressingProp> It(GetWorld()); It; ++It)
	{
		if (It->Kind == EHGDressingKind::HmongHouse)
		{
			Houses.Add(It->GetActorLocation());
		}
	}
	const FVector C = GetActorLocation();
	// Road samples near the field (the road meshes have no collision, so the traces below hit the landscape
	// under the carriageway: the first version grew corn on the Tham Ma road).
	TArray<FVector> RoadPts;
	for (TActorIterator<AHGRoadSplineActor> It(GetWorld()); It; ++It)
	{
		const USplineComponent* Sp = It->GetSpline();
		if (!Sp)
		{
			continue;
		}
		const float Len = Sp->GetSplineLength();
		for (float D = 0.f; D < Len; D += 300.f)
		{
			const FVector P = Sp->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
			if (FVector::DistSquared2D(P, C) < FMath::Square(Radius + 1500.f))
			{
				RoadPts.Add(P);
			}
		}
	}
	FRandomStream R(GetTypeHash(GetName()));
	const float Step = 75.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGCornField), false, this);
	TArray<FTransform> Xfs;
	for (float X = -Radius; X <= Radius; X += Step)
	{
		for (float Y = -Radius; Y <= Radius; Y += Step)
		{
			if (X * X + Y * Y > Radius * Radius || R.FRand() < 0.15f)
			{
				continue;
			}
			const FVector P = C + FVector(X + R.FRandRange(-15.f, 15.f), Y + R.FRandRange(-15.f, 15.f), 0.f);
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 5000.f), P - FVector(0, 0, 8000.f), ECC_Visibility, Params))
			{
				continue;
			}
			// Only bare landscape (not roads, walls, water), gentle ground, on the terrace treads (2.6 m contour
			// bands, as in the terrain material's ploughed stripes), clear of the houses.
			if (!Hit.GetActor() || !Hit.GetActor()->GetClass()->GetName().Contains(TEXT("Landscape")) || Hit.ImpactNormal.Z < 0.8f
				|| Hit.ImpactPoint.Z < MinZ)
			{
				continue;
			}
			const float Band = FMath::Fmod(Hit.ImpactPoint.Z / 260.f, 1.f);
			if (Band < 0.f || Band > 0.6f)
			{
				continue;
			}
			bool bNearRoad = false;
			for (const FVector& Q : RoadPts)
			{
				if (FVector::DistSquared2D(Q, Hit.ImpactPoint) < FMath::Square(650.f))
				{
					bNearRoad = true;
					break;
				}
			}
			if (bNearRoad)
			{
				continue;
			}
			bool bNearHouse = false;
			for (const FVector& H : Houses)
			{
				bNearHouse |= FVector::DistSquared2D(H, Hit.ImpactPoint) < FMath::Square(1300.f);
			}
			if (bNearHouse)
			{
				continue;
			}
			Xfs.Add(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f), Hit.ImpactPoint, FVector(R.FRandRange(0.8f, 1.25f))));
		}
	}
	Corn->AddInstances(Xfs, false, true);
	UE_LOG(LogTemp, Log, TEXT("%s: %d corn plants"), *GetName(), Xfs.Num());
}
