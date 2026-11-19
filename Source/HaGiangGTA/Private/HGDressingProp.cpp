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

	/** Round 6B: dry-stone rubble wall from A to B (the coursed boxes read as bricks up close): faceted
	 *  irregular stones of mixed sizes in rough courses, small gaps showing a dark core, a ragged top line, moss
	 *  on top and grass at the foot. Height ~ Courses * 22 cm. */
	void RubbleWall(FHGMeshKit& K, const FVector& A, const FVector& B, float BaseZ, int32 Courses, uint32 Seed, float Thick)
	{
		const FVector D = (B - A).GetSafeNormal2D();
		const float Len = FVector::Dist2D(A, B);
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		const FVector Nrm(-D.Y, D.X, 0.f);
		FRandomStream R{int32(Seed)};
		const float H = Courses * 22.f;
		// Dark core: the gaps between the stones show shadow, not daylight.
		K.Paint(FColorL(0.035f, 0.033f, 0.03f), HGSurf::Matte);
		const FVector Mid = (A + B) * 0.5f;
		K.Box(FVector(Mid.X, Mid.Y, BaseZ + H * 0.36f), FVector(Len, Thick * 0.62f, H * 0.68f), FRotator(0.f, Yaw, 0.f));   // below the ragged top
		for (int32 c = 0; c < Courses; ++c)
		{
			float t = R.FRandRange(0.f, 14.f);
			while (t < Len - 6.f)
			{
				const float L = R.FRandRange(16.f, 36.f) * (c == 0 ? 1.25f : 1.f);   // bigger stones at the foot
				const float Top = H * (0.86f + 0.14f * FMath::Sin(t * 0.021f + Seed));   // ragged top line
				const float Z = BaseZ + c * 22.f + 11.f;
				if (Z > BaseZ + Top || R.FRand() < 0.06f)
				{
					t += L * 0.7f;   // gap
					continue;
				}
				const float Tone = R.FRandRange(0.07f, 0.2f);
				const bool bWarm = R.FRand() < 0.25f;
				K.Paint(FColorL(Tone * (bWarm ? 1.12f : 1.f), Tone * 0.98f, Tone * (bWarm ? 0.82f : 0.92f)), HGSurf::Matte);
				for (const float S : { -1.f, 1.f })
				{
					const FVector P = A + D * FMath::Min(t + L * 0.5f, Len - 8.f) + Nrm * S * (Thick * 0.32f + R.FRandRange(-2.f, 3.f));
					K.Ellipsoid(FVector(P.X, P.Y, Z + R.FRandRange(-3.f, 3.f)),
						FVector(L * 0.55f, Thick * R.FRandRange(0.22f, 0.32f), R.FRandRange(8.f, 13.f)),
						FRotator(R.FRandRange(-12.f, 12.f), Yaw + R.FRandRange(-10.f, 10.f), R.FRandRange(-12.f, 12.f)), 5, 3);
				}
				t += L + R.FRandRange(0.f, 3.f);
			}
		}
		// Capstones laid across the top, moss on them, grass tufts at the foot.
		for (float t = 10.f; t < Len - 10.f; t += R.FRandRange(26.f, 40.f))
		{
			const float Top = H * (0.86f + 0.14f * FMath::Sin(t * 0.021f + Seed));
			const FVector P = A + D * t;
			const float Tone = R.FRandRange(0.09f, 0.18f);
			K.Paint(FColorL(Tone, Tone * 0.97f, Tone * 0.9f), HGSurf::Matte);
			K.Ellipsoid(FVector(P.X, P.Y, BaseZ + Top + 3.f), FVector(R.FRandRange(12.f, 18.f), Thick * 0.48f, R.FRandRange(4.f, 7.f)),
				FRotator(R.FRandRange(-8.f, 8.f), Yaw + 90.f + R.FRandRange(-15.f, 15.f), R.FRandRange(-6.f, 6.f)), 5, 3);
			if (R.FRand() < 0.45f)
			{
				K.Paint(R.FRand() < 0.5f ? FColorL(0.04f, 0.075f, 0.02f) : FColorL(0.07f, 0.1f, 0.03f), HGSurf::Matte);
				K.Ellipsoid(FVector(P.X, P.Y, BaseZ + Top + 7.f), FVector(R.FRandRange(6.f, 12.f), Thick * 0.35f, 2.5f),
					FRotator(0.f, Yaw + R.FRandRange(-30.f, 30.f), 0.f), 6, 3, 0.f);
			}
			if (R.FRand() < 0.5f)
			{
				K.Paint(FColorL(0.06f, 0.09f, 0.03f), HGSurf::Matte);
				const float S = R.FRand() < 0.5f ? -1.f : 1.f;
				const FVector G = P + Nrm * S * (Thick * 0.55f);
				for (int32 b = 0; b < 3; ++b)
				{
					K.Box(FVector(G.X + R.FRandRange(-4.f, 4.f), G.Y + R.FRandRange(-4.f, 4.f), BaseZ + 7.f), FVector(1.f, 1.2f, R.FRandRange(10.f, 18.f)),
						FRotator(R.FRandRange(-20.f, 20.f), R.FRandRange(0.f, 180.f), R.FRandRange(-20.f, 20.f)));
				}
			}
		}
	}

	void StoneCourse(FHGMeshKit& K, const FVector& A, const FVector& B, float BaseZ, int32 Courses, uint32 Seed, float Thick = 34.f)
	{
		RubbleWall(K, A, B, BaseZ, Courses + 1, Seed, Thick + 10.f);
	}

	void BuildHouse(FHGMeshKit& K, int32 V)
	{
		const bool bStone = (V % 3) == 0;   // 6B: most Ha Giang Hmong houses are rammed earth (trinh tuong)
		const bool bFence = (V % 3) != 2;
		FRandomStream R(1000 + V);
		const FColorL Earth(0.3f, 0.18f, 0.1f), Stone(0.22f, 0.21f, 0.2f), Wood(0.1f, 0.055f, 0.03f);
		// 6B: old fired yin-yang tiles are dark grey with moss, not light grey (they read as corrugated tin).
		const FColorL Tile(0.06f, 0.058f, 0.055f), TileLight(0.095f, 0.09f, 0.085f);
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
			// Rammed earth (trinh tuong): each 50 cm formwork lift its own tone, a dark lift line between lifts,
			// rain-drip erosion streaks under the eaves and a darker splash zone at the foot.
			for (int32 c = 0; c < 5; ++c)
			{
				K.Paint(Earth * R.FRandRange(0.82f, 1.12f));
				K.Box(FVector(0.f, 0.f, 64.f + c * 50.f), FVector(2 * HL + 1.0f, 2 * HW + 1.0f, 46.f));
				K.Paint(Earth * 0.62f);
				K.Box(FVector(0.f, 0.f, 88.f + c * 50.f), FVector(2 * HL + 1.4f, 2 * HW + 1.4f, 2.5f));
			}
			K.Paint(Earth * 0.7f);
			for (int32 k = 0; k < 14; ++k)
			{
				const bool bFront = (k & 1) == 0;
				const float X = R.FRandRange(-HL + 20.f, HL - 20.f);
				const float L = R.FRandRange(40.f, 120.f);
				K.Box(FVector(X, (bFront ? 1.f : -1.f) * (HW + 0.9f), WallTop - 30.f - L * 0.5f), FVector(R.FRandRange(5.f, 12.f), 1.f, L));
			}
			K.Paint(Earth * 0.55f);
			K.Box(FVector(0.f, 0.f, 52.f), FVector(2 * HL + 1.6f, 2 * HW + 1.6f, 22.f));
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
				K.Paint(((i & 1) ? Tile : TileLight) * R.FRandRange(0.85f, 1.15f), HGSurf::Matte);
				const FVector Top = FVector(X, 0.f, RidgeZ) + Up * 5.f;
				K.Tube(Top, Top + Down * SlopeW, (i & 1) ? 5.f : 6.5f, (i & 1) ? 5.f : 6.5f, 6, true);
			}
			// Tile courses: a shadow line every ~28 cm down the slope (overlapping tiles), moss in patches.
			K.Paint(FColorL(0.025f, 0.024f, 0.022f), HGSurf::Matte);
			for (float D = 28.f; D < SlopeW - 10.f; D += 28.f)
			{
				K.Box(FVector(0.f, 0.f, RidgeZ) + Up * 9.5f + Down * D, FVector(2 * HL + 2 * OH, 2.f, 1.2f), SlopeRot);
			}
			for (int32 m = 0; m < 7; ++m)
			{
				K.Paint(R.FRand() < 0.5f ? FColorL(0.03f, 0.04f, 0.018f) : FColorL(0.045f, 0.055f, 0.022f), HGSurf::Matte);
				K.Ellipsoid(FVector(R.FRandRange(-HL, HL), 0.f, RidgeZ) + Up * 10.f + Down * R.FRandRange(SlopeW * 0.4f, SlopeW - 15.f),
					FVector(R.FRandRange(25.f, 60.f), R.FRandRange(15.f, 35.f), 2.5f), SlopeRot, 8, 3);
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
		// Round 6B: packed red-brown earth yard in front (swept, a little worn to darker mud by the door).
		if (bFence)
		{
			for (int32 y = 0; y < 9; ++y)
			{
				K.Paint(FColorL(0.15f, 0.085f, 0.052f) * R.FRandRange(0.85f, 1.12f), HGSurf::Matte);
				K.Ellipsoid(FVector(R.FRandRange(-HL + 60.f, HL - 60.f), HW + R.FRandRange(150.f, 650.f), -1.f),
					FVector(R.FRandRange(150.f, 260.f), R.FRandRange(110.f, 200.f), 1.6f), FRotator(0.f, R.FRandRange(0.f, 180.f), 0.f), 12, 3, 0.f);
			}
			K.Paint(FColorL(0.09f, 0.055f, 0.035f), HGSurf::Matte);
			K.Ellipsoid(FVector(0.f, HW + 120.f, 0.f), FVector(120.f, 70.f, 1.8f), FRotator::ZeroRotator, 12, 3, 0.f);
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

	/** Round 4B: terrace wall under a hillside house - coursed dry stone over a 100 cm unit height (scaled in Z),
	 *  a little wider than the house platform, battered inwards like the real retaining walls. */
	void BuildPlinth(FHGMeshKit& K, float Depth)
	{
		// Round 6B: built at its real depth (cached per 50 cm) - the round-4B unit mesh scaled in Z stretched the
		// stones into 2 m slabs and read as one pale block. Dry-stone courses of big weathered blocks, battered
		// outwards towards the foot, over a dark core so the joints read as shadow.
		FRandomStream R{ 5150 + int32(Depth) };
		const float HX = 560.f, HY = 370.f;
		K.Paint(FColorL(0.035f, 0.033f, 0.03f), HGSurf::Matte);
		K.Box(FVector(0.f, 0.f, -Depth * 0.5f), FVector(2 * HX + 20.f, 2 * HY + 20.f, Depth));
		const int32 Rows = FMath::Max(1, FMath::CeilToInt(Depth / 32.f));
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const float Z = -16.f - Row * 32.f;
			const float Out = 14.f + Row * 1.6f;   // batter
			auto Stone = [&](const FVector& C, bool bAlongX)
			{
				const float G = R.FRandRange(0.055f, 0.13f);
				const bool bWarm = R.FRand() < 0.3f;
				K.Paint(FColorL(G * (bWarm ? 1.15f : 1.f), G * 0.97f, G * (bWarm ? 0.8f : 0.9f)), HGSurf::Matte);
				const float L = R.FRandRange(38.f, 78.f);
				K.Box(C, bAlongX ? FVector(L, 16.f, R.FRandRange(25.f, 33.f)) : FVector(16.f, L, R.FRandRange(25.f, 33.f)),
					FRotator(R.FRandRange(-4.f, 4.f), R.FRandRange(-3.f, 3.f), R.FRandRange(-4.f, 4.f)));
				return L;
			};
			for (const float S : { -1.f, 1.f })
			{
				for (float X = -HX - Out + (Row % 2) * 25.f; X < HX + Out;)
				{
					X += Stone(FVector(X + 30.f, S * (HY + Out + R.FRandRange(-2.f, 2.f)), Z), true) + R.FRandRange(1.f, 4.f);
				}
				for (float Y = -HY - Out + (Row % 2) * 25.f; Y < HY + Out;)
				{
					Y += Stone(FVector(S * (HX + Out + R.FRandRange(-2.f, 2.f)), Y + 30.f, Z), false) + R.FRandRange(1.f, 4.f);
				}
			}
			if (Row % 3 == 1)
			{
				// Ferns / grass in the joints.
				K.Paint(FColorL(0.05f, 0.085f, 0.025f), HGSurf::Matte);
				for (int32 t = 0; t < 4; ++t)
				{
					const float S = (t & 1) ? 1.f : -1.f;
					K.Ellipsoid(FVector(R.FRandRange(-HX, HX), S * (HY + Out + 8.f), Z + 14.f), FVector(10.f, 6.f, 7.f), FRotator::ZeroRotator, 6, 3);
				}
			}
		}
	}

	/** Roadside dry-stone fence segment, 250 cm along +X, ~85 cm high. */
	void BuildStoneFence(FHGMeshKit& K)
	{
		// Round 6B: rubble stones with gaps, ragged top, moss and grass (was coursed boxes).
		RubbleWall(K, FVector(-125.f, 0.f, 0.f), FVector(125.f, 0.f, 0.f), -4.f, 4, 911, 52.f);
	}

	/** Round 4B: H'Mong corn drying rack - two posts, three rails hung with bundles of yellow-orange cobs. */
	void BuildCornRack(FHGMeshKit& K)
	{
		FRandomStream R{ 2024 };
		K.Paint(FColorL(0.16f, 0.1f, 0.055f), HGSurf::Matte);
		for (const float X : { -110.f, 110.f })
		{
			K.Tube(FVector(X, 0.f, -10.f), FVector(X + R.FRandRange(-4.f, 4.f), 0.f, 250.f), 5.f, 4.f, 6, true);
		}
		for (const float Z : { 110.f, 170.f, 230.f })
		{
			K.Paint(FColorL(0.2f, 0.13f, 0.07f), HGSurf::Matte);
			K.Tube(FVector(-125.f, 0.f, Z), FVector(125.f, 0.f, Z + R.FRandRange(-4.f, 4.f)), 2.5f, 2.5f, 5, true);
			for (float X = -95.f; X <= 95.f; X += 19.f)
			{
				if (R.FRand() < 0.15f) { continue; }
				const float Hue = R.FRand();
				K.Paint(FColorL(0.36f + 0.12f * Hue, 0.17f + 0.08f * Hue, 0.015f), HGSurf::Matte);
				// Two cobs per tied bundle, hanging either side of the rail, with pale husks on top.
				for (const float S : { -1.f, 1.f })
				{
					K.Ellipsoid(FVector(X, S * 7.f, Z - 20.f), FVector(4.5f, 4.5f, 15.f), FRotator(0.f, 0.f, S * 10.f), 6, 4);
				}
				K.Paint(FColorL(0.5f, 0.44f, 0.3f), HGSurf::Matte);
				K.Box(FVector(X, 0.f, Z + 3.f), FVector(6.f, 18.f, 6.f));
			}
		}
		// Small tin cap against the rain.
		K.Paint(FColorL(0.3f, 0.3f, 0.28f), HGSurf::Satin);
		K.Box(FVector(0.f, 0.f, 256.f), FVector(270.f, 50.f, 2.f));
	}

	/** Round 4B: firewood stack under a board, ~1.6 x 0.7 x 1.1 m (every H'Mong yard and roadside has one). */
	void BuildWoodStack(FHGMeshKit& K)
	{
		FRandomStream R{ 77 };
		for (int32 Row = 0; Row < 5; ++Row)
		{
			for (int32 Col = 0; Col < 7; ++Col)
			{
				const float Y = -60.f + Col * 20.f + (Row % 2) * 10.f;
				if (Y > 62.f) { continue; }
				const float Z = 10.f + Row * 19.f;
				const float T = R.FRandRange(0.f, 1.f);
				K.Paint(FColorL(0.2f + 0.08f * T, 0.13f + 0.05f * T, 0.07f + 0.03f * T), HGSurf::Matte);
				K.Tube(FVector(-80.f + R.FRandRange(-6.f, 6.f), Y, Z), FVector(80.f + R.FRandRange(-6.f, 6.f), Y, Z), 9.f, 9.f, 6, true);
			}
		}
		K.Paint(FColorL(0.12f, 0.08f, 0.05f), HGSurf::Matte);
		K.Box(FVector(0.f, 0.f, 112.f), FVector(190.f, 90.f, 4.f), FRotator(0.f, 0.f, 6.f));
		K.Box(FVector(-88.f, 0.f, 50.f), FVector(6.f, 6.f, 110.f));
		K.Box(FVector(88.f, 0.f, 50.f), FVector(6.f, 6.f, 110.f));
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
	case EHGDressingKind::RoadsideLife:
		BuildRoadside();
		return;
	}
	if (UStaticMeshComponent* C = FHGMeshKit::AddComponent(this, Root, Mesh, TEXT("Kit"), FVector::ZeroVector))
	{
		C->SetCullDistance(Cull);
	}
	if (Kind == EHGDressingKind::HmongHouse && PlinthDepth > 1.f)
	{
		// Steep plot: the platform stands on a terrace wall down to the low side of the slope.
		const float Depth = FMath::CeilToFloat(PlinthDepth / 50.f) * 50.f;
		UStaticMesh* PlinthMesh = FHGMeshKit::Cached(*FString::Printf(TEXT("SM_HGHousePlinth_%d"), int32(Depth)), [Depth](FHGMeshKit& K) { BuildPlinth(K, Depth); });
		if (UStaticMeshComponent* P = FHGMeshKit::AddComponent(this, Root, PlinthMesh, TEXT("Plinth"), FVector(0.f, 0.f, -250.f)))
		{
			P->SetCullDistance(Cull);
		}
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
	Corn->SetCullDistances(0, 8000);
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
	const float Step = 130.f;   // budget: <= 2500 plants per field (the first version had 15-33k each)
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
	while (Xfs.Num() > 2500)
	{
		Xfs.RemoveAtSwap(R.RandRange(0, Xfs.Num() - 1), EAllowShrinking::No);
	}
	Corn->AddInstances(Xfs, false, true);
	UE_LOG(LogTemp, Log, TEXT("%s: %d corn plants"), *GetName(), Xfs.Num());
}

UHierarchicalInstancedStaticMeshComponent* AHGDressingProp::MakeHism(FName Name, UStaticMesh* Mesh, int32 Cull)
{
	UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, Name, RF_Transient);
	H->SetupAttachment(Root);
	H->SetStaticMesh(Mesh);
	H->SetMaterial(0, FHGMeshKit::VertexColorMaterial());
	H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	H->SetCastShadow(false);
	H->SetCullDistances(0, Cull);
	H->SetUsingAbsoluteLocation(true);
	H->SetUsingAbsoluteRotation(true);
	H->RegisterComponent();
	AddInstanceComponent(H);
	Roadside.Add(H);
	return H;
}

void AHGDressingProp::BuildRoadside()
{
	using namespace HGDressingKit;
	if (FParse::Param(FCommandLine::Get(), TEXT("hgnokit")))
	{
		return;
	}
	UStaticMesh* FenceMesh = FHGMeshKit::Cached(TEXT("SM_HGStoneFence"), [](FHGMeshKit& K) { BuildStoneFence(K); });
	UStaticMesh* RackMesh = FHGMeshKit::Cached(TEXT("SM_HGCornRack"), [](FHGMeshKit& K) { BuildCornRack(K); });
	UStaticMesh* WoodMesh = FHGMeshKit::Cached(TEXT("SM_HGWoodStack"), [](FHGMeshKit& K) { BuildWoodStack(K); });
	if (!FenceMesh || !RackMesh || !WoodMesh)
	{
		return;
	}
	TArray<FVector> Houses;
	for (TActorIterator<AHGDressingProp> It(GetWorld()); It; ++It)
	{
		if (It->Kind == EHGDressingKind::HmongHouse)
		{
			Houses.Add(It->GetActorLocation());
		}
	}
	const FVector C = GetActorLocation();
	struct FRoadSample { FVector P; FVector T; int32 Road; float D; float Bend; };
	TArray<FRoadSample> Samples;
	int32 RoadIdx = 0;
	for (TActorIterator<AHGRoadSplineActor> It(GetWorld()); It; ++It, ++RoadIdx)
	{
		const USplineComponent* Sp = It->GetSpline();
		if (!Sp)
		{
			continue;
		}
		const float Len = Sp->GetSplineLength();
		for (float D = 0.f; D < Len; D += 250.f)
		{
			const FVector P = Sp->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
			if (FVector::DistSquared2D(P, C) < FMath::Square(Radius + 1500.f))
			{
				const FVector Ta = Sp->GetTangentAtDistanceAlongSpline(FMath::Max(D - 500.f, 0.f), ESplineCoordinateSpace::World).GetSafeNormal2D();
				const FVector Tb = Sp->GetTangentAtDistanceAlongSpline(FMath::Min(D + 500.f, Len), ESplineCoordinateSpace::World).GetSafeNormal2D();
				const float Bend = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Ta, Tb), -1.f, 1.f)));
				Samples.Add({ P, Sp->GetTangentAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).GetSafeNormal2D(), RoadIdx, D, Bend });
			}
		}
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGRoadside), false, this);
	auto Ground = [&](const FVector& P, FVector& Out) -> bool
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 3000.f), P - FVector(0, 0, 6000.f), ECC_Visibility, Params)
			|| !Hit.GetActor() || !Hit.GetActor()->GetClass()->GetName().Contains(TEXT("Landscape"))
			|| Hit.ImpactNormal.Z < 0.78f || Hit.ImpactPoint.Z < MinZ)
		{
			return false;
		}
		Out = Hit.ImpactPoint;
		return true;
	};
	// Round 6B: keep the plazas / viewpoints free (a corn rack stood among the tourists at the check-in ring).
	TArray<FVector> Plazas;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetClass()->GetName().Contains(TEXT("ViewpointActor")))
		{
			Plazas.Add(It->GetActorLocation());
		}
	}
	auto Clear = [&](const FVector& P, float RoadCm, float HouseCm) -> bool
	{
		for (const FVector& V : Plazas)
		{
			if (FVector::DistSquared2D(V, P) < FMath::Square(3000.f)) { return false; }
		}
		for (const FRoadSample& S : Samples)
		{
			if (FVector::DistSquared2D(S.P, P) < RoadCm * RoadCm) { return false; }
		}
		for (const FVector& H : Houses)
		{
			if (FVector::DistSquared2D(H, P) < HouseCm * HouseCm) { return false; }
		}
		return true;
	};
	TArray<FTransform> Fence, Racks, Wood;
	for (const FRoadSample& S : Samples)
	{
		if (FVector::DistSquared2D(S.P, C) > Radius * Radius)
		{
			continue;
		}
		const FVector N(-S.T.Y, S.T.X, 0.f);
		for (const float Side : { -1.f, 1.f })
		{
			// One choice per 10 m block and side, so fences run continuously and racks / stacks stand alone.
			const int32 Block = FMath::FloorToInt(S.D / 1000.f);
			FRandomStream R(HashCombine(HashCombine(GetTypeHash(S.Road), GetTypeHash(Block)), GetTypeHash(Side > 0.f)));
			const float Pick = R.FRand();
			// Fence line: one offset per road side (per-block offsets stacked runs at two distances).
			const float Off = 590.f + 50.f * float(GetTypeHash(S.Road * 2 + (Side > 0.f)) % 3) * 0.5f;
			const FVector Base = S.P + N * Side * Off;
			if (Pick < 0.42f && S.Bend < 22.f)      // no fences around hairpins (they read as loose bricks)
			{
				FVector A, B;
				const FVector Pa = Base - S.T * 125.f, Pb = Base + S.T * 125.f;
				if (Ground(Pa, A) && Ground(Pb, B) && Clear(Base, Off - 60.f, 650.f) && FMath::Abs(A.Z - B.Z) < 90.f)
				{
					const FVector Mid = (A + B) * 0.5f - FVector(0, 0, 12.f);
					const FRotator Rot = (B - A).Rotation();
					Fence.Add(FTransform(Rot, Mid, FVector(1.22f, 1.f, R.FRandRange(0.85f, 1.15f))));   // overlap: no gaps on the outside of bends
				}
			}
			else if (FMath::Fmod(S.D, 1000.f) < 250.f)       // one rack / stack per block
			{
				FVector G;
				const FVector P = Base + N * Side * 120.f;
				if (Pick < 0.6f && Ground(P, G) && Clear(P, Off + 60.f, 600.f))
				{
					Racks.Add(FTransform(FRotator(0.f, S.T.Rotation().Yaw + R.FRandRange(-8.f, 8.f), 0.f), G - FVector(0, 0, 5.f)));
				}
				else if (Pick >= 0.6f && Pick < 0.78f && Ground(P, G) && Clear(P, Off + 60.f, 450.f))
				{
					Wood.Add(FTransform(FRotator(0.f, S.T.Rotation().Yaw + R.FRandRange(-10.f, 10.f), 0.f), G - FVector(0, 0, 4.f)));
				}
			}
		}
	}
	MakeHism(TEXT("StoneFence"), FenceMesh, 14000)->AddInstances(Fence, false, true);
	MakeHism(TEXT("CornRacks"), RackMesh, 9000)->AddInstances(Racks, false, true);
	MakeHism(TEXT("WoodStacks"), WoodMesh, 9000)->AddInstances(Wood, false, true);
	UE_LOG(LogTemp, Log, TEXT("%s: roadside %d fence segments, %d corn racks, %d wood stacks"), *GetName(), Fence.Num(), Racks.Num(), Wood.Num());
}
