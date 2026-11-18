#include "HGBikeBuilder.h"
#include "HGMeshKit.h"

namespace
{
	using FLC = FLinearColor;
	const FLC Black(0.012f, 0.012f, 0.012f);
	const FLC Rubber(0.018f, 0.018f, 0.018f);
	const FLC Chrome(0.75f, 0.75f, 0.77f);
	const FLC Alloy(0.4f, 0.4f, 0.41f);
	const FLC DarkAlloy(0.1f, 0.1f, 0.105f);
	const FLC Amber(0.85f, 0.38f, 0.02f);

	/** Thin curved strip (fender) around Center in the XZ plane, from angle A0 to A1 (deg, 0 = +X, 90 = up). */
	void Arc(FHGMeshKit& K, const FVector& Center, float Radius, float A0, float A1, float Width, float Thick, int32 Segs)
	{
		for (int32 i = 0; i < Segs; ++i)
		{
			const float T0 = FMath::DegreesToRadians(FMath::Lerp(A0, A1, float(i) / Segs));
			const float T1 = FMath::DegreesToRadians(FMath::Lerp(A0, A1, float(i + 1) / Segs));
			const FVector P0 = Center + FVector(FMath::Cos(T0), 0.f, FMath::Sin(T0)) * Radius;
			const FVector P1 = Center + FVector(FMath::Cos(T1), 0.f, FMath::Sin(T1)) * Radius;
			const FVector D = P1 - P0;
			const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(D.Z, D.X));
			K.Box((P0 + P1) * 0.5f, FVector(D.Size() + 0.6f, Width, Thick), FRotator(Pitch, 0.f, 0.f));
		}
	}

	void BuildWheel(FHGMeshKit& K, bool bFront)
	{
		const FTransform Id = FTransform::Identity;
		// Tyre (2.50-17 / 2.75-17: ~61 cm outside) with knobby tread blocks.
		K.Paint(Rubber);
		K.Torus(Id, 25.8f, bFront ? 4.0f : 4.6f, 28, 8);
		const int32 Knobs = 26;
		for (int32 i = 0; i < Knobs; ++i)
		{
			const float A = 2.f * PI * (i + (i & 1) * 0.25f) / Knobs;
			const FVector Dir(FMath::Cos(A), 0.f, FMath::Sin(A));
			const float Y = (i & 1) ? 1.8f : -1.8f;
			K.Box(Dir * (bFront ? 29.6f : 30.2f) + FVector(0.f, Y, 0.f), FVector(1.6f, 3.4f, 3.6f),
				FRotator(FMath::RadiansToDegrees(-A), 0.f, 0.f));
		}
		// Rim, hub with brake drum, 2 x 9 spokes.
		K.Paint(Chrome, HGSurf::Chrome);
		K.Torus(Id, 21.6f, 1.3f, 28, 5);
		K.Paint(Alloy, HGSurf::Satin);
		K.Tube(FVector(0.f, -6.f, 0.f), FVector(0.f, 6.f, 0.f), 5.5f, 5.5f, 12, true);
		K.Tube(FVector(0.f, bFront ? -6.f : 6.f, 0.f), FVector(0.f, bFront ? -8.f : 8.f, 0.f), 8.f, 8.f, 14, true);
		K.Paint(Chrome, HGSurf::Chrome);
		for (int32 i = 0; i < 18; ++i)
		{
			const float A = 2.f * PI * i / 18.f;
			const float A2 = A + ((i & 1) ? 0.35f : -0.35f);
			const float Side = (i & 1) ? 4.5f : -4.5f;
			K.Tube(FVector(FMath::Cos(A) * 4.5f, Side, FMath::Sin(A) * 4.5f),
				FVector(FMath::Cos(A2) * 20.8f, Side * 0.2f, FMath::Sin(A2) * 20.8f), 0.32f, 0.32f, 3, false);
		}
		K.Paint(DarkAlloy, HGSurf::Satin);
		K.Tube(FVector(0.f, -9.5f, 0.f), FVector(0.f, 9.5f, 0.f), 1.2f, 1.2f, 6, true);   // axle
	}

	void BuildBody(FHGMeshKit& K)
	{
		const FVector F = HGBikes::FrontAxle();
		const FVector R = HGBikes::RearAxle();

		// --- Fork, clamps, front fender ---
		for (const float Y : { -7.5f, 7.5f })
		{
			const FVector Top(43.f, Y, 99.f), Bot(F.X + 1.f, Y, F.Z + 3.f);
			K.Paint(Chrome, HGSurf::Chrome);
			K.Tube(Top, FMath::Lerp(Top, Bot, 0.6f), 1.9f, 1.9f, 8, true);
			K.Paint(Alloy, HGSurf::Satin);
			K.Tube(FMath::Lerp(Top, Bot, 0.5f), Bot, 2.7f, 2.5f, 8, true);
			K.Paint(Black, HGSurf::Satin);
			K.Tube(FMath::Lerp(Top, Bot, 0.42f), FMath::Lerp(Top, Bot, 0.52f), 2.9f, 2.9f, 8, false);   // dust boots
		}
		K.Paint(Black, HGSurf::Satin);
		K.Box(FVector(43.5f, 0.f, 99.f), FVector(8.f, 21.f, 3.f), FRotator(-19.f, 0.f, 0.f));
		K.Box(FVector(46.5f, 0.f, 90.f), FVector(8.f, 21.f, 3.5f), FRotator(-19.f, 0.f, 0.f));
		K.Paint(Chrome, HGSurf::Chrome);
		Arc(K, F, 34.f, 10.f, 145.f, 11.f, 0.9f, 10);
		K.Tube(F + FVector(-2.f, -7.5f, 8.f), F + FVector(10.f, -7.f, 32.f), 0.5f, 0.5f, 4, false);       // fender stays
		K.Tube(F + FVector(-2.f, 7.5f, 8.f), F + FVector(10.f, 7.f, 32.f), 0.5f, 0.5f, 4, false);

		// --- Headlamp shell (lens = emissive part), indicators, speedo ---
		K.Paint(Chrome, HGSurf::Chrome);
		K.Ellipsoid(FVector(71.5f, 0.f, 88.f), FVector(8.5f, 10.f, 10.f), FRotator(0.f, 0.f, 0.f), 14, 8);
		K.Paint(Black, HGSurf::Satin);
		K.Tube(FVector(78.f, 0.f, 88.f) + FVector(0.f, -9.6f, 0.f), FVector(78.f, 0.f, 88.f) + FVector(0.f, 9.6f, 0.f), 0.f, 0.f, 3, false);
		K.Tube(FVector(64.f, -10.f, 88.f), FVector(48.f, -8.f, 90.f), 0.8f, 0.8f, 5, false);    // headlamp ears
		K.Tube(FVector(64.f, 10.f, 88.f), FVector(48.f, 8.f, 90.f), 0.8f, 0.8f, 5, false);
		for (const float Y : { -15.f, 15.f })
		{
			K.Paint(Black, HGSurf::Satin);
			K.Tube(FVector(60.f, Y * 0.55f, 84.f), FVector(62.f, Y, 84.f), 0.7f, 0.7f, 5, false);
			K.Paint(Amber, HGSurf::Gloss);
			K.Ellipsoid(FVector(64.f, Y, 84.f), FVector(3.2f, 2.4f, 2.4f), FRotator::ZeroRotator, 8, 4);
		}
		K.Paint(Black, HGSurf::Satin);
		K.Ellipsoid(FVector(47.f, 0.f, 106.f), FVector(4.5f, 6.f, 5.f), FRotator(-20.f, 0.f, 0.f), 10, 5);
		K.Paint(FLC(0.7f, 0.7f, 0.68f), HGSurf::Gloss);
		K.Ellipsoid(FVector(46.f, 0.f, 109.2f), FVector(4.f, 5.f, 1.2f), FRotator(-20.f, 0.f, 0.f), 10, 3);

		// --- Handlebar, grips, levers, mirrors ---
		K.Paint(Chrome, HGSurf::Chrome);
		K.Tube(FVector(42.f, -26.f, 103.f), FVector(42.f, 26.f, 103.f), 1.3f, 1.3f, 6, false);
		for (const float S : { -1.f, 1.f })
		{
			K.Paint(Chrome, HGSurf::Chrome);
			K.Tube(FVector(42.f, 8.f * S, 103.f), FVector(44.f, 4.f * S, 99.f), 1.2f, 1.2f, 6, false);   // risers
			K.Tube(FVector(43.f, 24.f * S, 104.f), FVector(49.f, 31.f * S, 103.f), 0.55f, 0.45f, 4, false);   // levers
			K.Tube(FVector(42.f, 21.f * S, 104.f), FVector(39.f, 25.f * S, 121.f), 0.55f, 0.55f, 4, false);   // mirror stalks
			K.Paint(Black, HGSurf::Satin);
			K.Tube(FVector(42.f, 25.f * S, 103.f), FVector(41.f, 35.f * S, 103.f), 1.9f, 1.9f, 8, true);     // grips
			K.Box(FVector(42.f, 23.f * S, 103.f), FVector(4.f, 3.f, 4.f));                                    // switch pods
			K.Ellipsoid(FVector(38.5f, 26.f * S, 123.5f), FVector(1.6f, 5.f, 3.6f), FRotator::ZeroRotator, 8, 4);
			K.Paint(Chrome, HGSurf::Chrome);
			K.Ellipsoid(FVector(37.f, 26.f * S, 123.5f), FVector(0.4f, 4.4f, 3.f), FRotator::ZeroRotator, 8, 4);
		}

		// --- Frame ---
		K.Paint(Black, HGSurf::Satin);
		K.Tube(FVector(43.f, 0.f, 98.f), FVector(49.f, 0.f, 80.f), 2.6f, 2.6f, 8, true);     // steering head
		K.Tube(FVector(45.f, 0.f, 92.f), FVector(6.f, 0.f, 76.f), 2.4f, 2.4f, 8, true);      // backbone
		K.Tube(FVector(48.f, 0.f, 82.f), FVector(26.f, 0.f, 42.f), 2.2f, 2.2f, 8, true);     // down tube
		for (const float S : { -1.f, 1.f })
		{
			K.Tube(FVector(8.f, 7.f * S, 76.f), FVector(-84.f, 9.f * S, 78.f), 1.5f, 1.5f, 6, true);     // seat rails
			K.Tube(FVector(-6.f, 8.f * S, 46.f), FVector(-50.f, 9.f * S, 77.f), 1.3f, 1.3f, 6, true);   // rear subframe
			// Swingarm, shocks.
			K.Tube(FVector(-4.f, 10.f * S, 35.f), FVector(R.X, 10.f * S, R.Z), 2.2f, 1.8f, 8, true);
			K.Paint(Chrome, HGSurf::Chrome);
			K.Tube(FVector(-55.f, 12.f * S, 33.f), FVector(-50.f, 11.5f * S, 62.f), 2.9f, 2.9f, 8, true);
			K.Paint(Black, HGSurf::Satin);
			K.Tube(FVector(-50.f, 11.5f * S, 62.f), FVector(-48.f, 11.f * S, 79.f), 2.1f, 2.1f, 8, true);
		}
		// Chain guard (left) and sprocket cover.
		K.Box(FVector(-32.f, -12.5f, 35.f), FVector(58.f, 1.4f, 7.f), FRotator(-3.f, 0.f, 0.f));
		K.Paint(DarkAlloy, HGSurf::Satin);
		K.Tube(FVector(R.X, -11.f, R.Z), FVector(R.X, -12.f, R.Z), 10.f, 10.f, 14, true);

		// --- Engine: crankcase, finned near-horizontal cylinder, carb ---
		K.Paint(Alloy, HGSurf::Satin);
		K.Box(FVector(6.f, 0.f, 37.f), FVector(30.f, 17.f, 24.f));
		K.Ellipsoid(FVector(4.f, -9.f, 37.f), FVector(13.f, 2.6f, 11.f), FRotator::ZeroRotator, 12, 5);
		K.Ellipsoid(FVector(4.f, 9.f, 37.f), FVector(13.f, 2.6f, 11.f), FRotator::ZeroRotator, 12, 5);
		K.Paint(DarkAlloy, HGSurf::Satin);
		K.Box(FVector(27.f, 0.f, 45.f), FVector(20.f, 12.f, 12.f), FRotator(15.f, 0.f, 0.f));
		K.Paint(FLC(0.25f, 0.25f, 0.26f), HGSurf::Satin);
		for (int32 i = 0; i < 6; ++i)
		{
			K.Box(FVector(20.f + i * 3.2f, 0.f, 43.f + i * 0.86f), FVector(1.3f, 17.f, 17.f), FRotator(15.f, 0.f, 0.f));
		}
		K.Box(FVector(39.f, 0.f, 48.5f), FVector(6.f, 15.f, 15.f), FRotator(15.f, 0.f, 0.f));
		K.Paint(Alloy, HGSurf::Satin);
		K.Box(FVector(12.f, 5.f, 53.f), FVector(8.f, 6.f, 6.f));
		K.Paint(Black, HGSurf::Satin);
		K.Tube(FVector(12.f, 5.f, 53.f), FVector(-6.f, 6.f, 60.f), 3.5f, 4.5f, 8, true);      // air box hose

		// --- Exhaust with heat shield (right side) ---
		K.Paint(Chrome, HGSurf::Chrome);
		const FVector Ex[] = { FVector(41.f, 3.f, 44.f), FVector(40.f, 9.f, 30.f), FVector(26.f, 13.f, 22.f), FVector(-14.f, 14.f, 25.f) };
		for (int32 i = 0; i + 1 < UE_ARRAY_COUNT(Ex); ++i)
		{
			K.Tube(Ex[i], Ex[i + 1], 2.2f, 2.2f, 8, false);
			K.Ellipsoid(Ex[i + 1], FVector(2.2f), FRotator::ZeroRotator, 6, 4);
		}
		K.Paint(FLC(0.03f, 0.03f, 0.03f), HGSurf::Satin);
		K.Tube(FVector(-14.f, 14.f, 26.f), FVector(-74.f, 14.f, 40.f), 4.2f, 5.4f, 12, true);
		K.Paint(Chrome, HGSurf::Chrome);
		K.Tube(FVector(-74.f, 14.f, 40.f), FVector(-77.f, 14.f, 40.7f), 3.f, 2.6f, 10, true);
		K.Box(FVector(-40.f, 19.f, 33.f), FVector(32.f, 1.f, 8.f), FRotator(-13.5f, 0.f, 0.f));

		// --- Tank (tinted), knee pads, badge, side covers ---
		K.Paint(FLC::White, HGSurf::Gloss, true);
		K.Ellipsoid(FVector(23.f, 0.f, 84.f), FVector(21.f, 12.5f, 10.f), FRotator(-6.f, 0.f, 0.f), 16, 9);
		K.Box(FVector(-12.f, -10.f, 66.f), FVector(22.f, 2.5f, 13.f));
		K.Box(FVector(-12.f, 10.f, 66.f), FVector(22.f, 2.5f, 13.f));
		K.Paint(Chrome, HGSurf::Chrome);
		K.Tube(FVector(24.f, 0.f, 93.5f), FVector(24.f, 0.f, 94.8f), 3.f, 3.f, 10, true);
		K.Paint(FLC(0.8f, 0.8f, 0.78f), HGSurf::Gloss);
		K.Box(FVector(25.f, -12.2f, 84.f), FVector(10.f, 0.5f, 2.6f));
		K.Box(FVector(25.f, 12.2f, 84.f), FVector(10.f, 0.5f, 2.6f));
		K.Paint(Black, HGSurf::Satin);
		K.Box(FVector(12.f, -11.5f, 80.f), FVector(9.f, 1.2f, 6.f));
		K.Box(FVector(12.f, 11.5f, 80.f), FVector(9.f, 1.2f, 6.f));

		// --- Long flat seat (top ~85 cm: driver seat point 84, passenger 84) ---
		K.Paint(FLC(0.02f, 0.018f, 0.016f), HGSurf::Satin);
		K.Box(FVector(-31.f, 0.f, 80.f), FVector(76.f, 23.f, 6.f));
		K.Ellipsoid(FVector(-31.f, 0.f, 82.f), FVector(38.f, 12.f, 3.4f), FRotator::ZeroRotator, 14, 5, 0.f);
		K.Ellipsoid(FVector(6.f, 0.f, 81.f), FVector(5.f, 11.f, 3.5f), FRotator::ZeroRotator, 8, 4);
		K.Paint(Chrome, HGSurf::Chrome);
		K.Tube(FVector(-64.f, -12.f, 81.f), FVector(-64.f, 12.f, 81.f), 0.8f, 0.8f, 5, false);   // grab strap bar

		// --- Big rear cargo rack ---
		K.Paint(Black, HGSurf::Satin);
		for (const float S : { -1.f, 1.f })
		{
			K.Tube(FVector(-62.f, 11.f * S, 86.f), FVector(-99.f, 11.f * S, 86.f), 1.f, 1.f, 5, true);
			K.Tube(FVector(-66.f, 11.f * S, 86.f), FVector(-56.f, 9.f * S, 78.f), 0.9f, 0.9f, 5, false);
			K.Tube(FVector(-92.f, 11.f * S, 86.f), FVector(-82.f, 9.f * S, 78.f), 0.9f, 0.9f, 5, false);
		}
		for (const float X : { -62.f, -72.f, -82.f, -92.f, -99.f })
		{
			K.Tube(FVector(X, -11.f, 86.f), FVector(X, 11.f, 86.f), 0.9f, 0.9f, 5, false);
		}

		// --- Rear fender, tail lamp housing, plate holder, indicators ---
		K.Paint(Black, HGSurf::Satin);
		Arc(K, R, 34.f, 35.f, 175.f, 12.f, 0.9f, 10);
		K.Box(FVector(-94.f, 0.f, 79.f), FVector(6.f, 13.f, 8.f));
		K.Paint(FLC(0.8f, 0.8f, 0.77f), HGSurf::Satin);
		K.Box(FVector(-95.2f, 0.f, 63.f), FVector(1.f, 19.f, 13.f), FRotator(8.f, 0.f, 0.f));
		for (const float Y : { -13.f, 13.f })
		{
			K.Paint(Black, HGSurf::Satin);
			K.Tube(FVector(-90.f, Y * 0.5f, 78.f), FVector(-91.f, Y, 78.f), 0.7f, 0.7f, 5, false);
			K.Paint(Amber, HGSurf::Gloss);
			K.Ellipsoid(FVector(-93.f, Y, 78.f), FVector(3.f, 2.3f, 2.3f), FRotator::ZeroRotator, 8, 4);
		}

		// --- Footpegs, kickstand, pedals ---
		K.Paint(Black, HGSurf::Matte);
		for (const float S : { -1.f, 1.f })
		{
			K.Tube(FVector(20.f, 9.f * S, 33.f), FVector(20.f, 21.f * S, 33.f), 1.5f, 1.5f, 6, true);
			K.Tube(FVector(-30.f, 10.f * S, 38.f), FVector(-30.f, 20.f * S, 38.f), 1.3f, 1.3f, 6, true);
		}
		K.Tube(FVector(18.f, 12.f, 34.f), FVector(30.f, 13.f, 30.f), 0.8f, 0.8f, 5, true);         // brake pedal
		K.Tube(FVector(-4.f, -11.f, 29.f), FVector(-28.f, -14.f, 27.f), 1.1f, 1.1f, 5, true);      // kickstand (up)
	}
}

namespace HGBikes
{
	UStaticMesh* Wheel(bool bFront)
	{
		return FHGMeshKit::Cached(bFront ? FName(TEXT("SM_HGWinWheelFront")) : FName(TEXT("SM_HGWinWheelRear")),
			[bFront](FHGMeshKit& K) { BuildWheel(K, bFront); });
	}

	UStaticMesh* Body()
	{
		return FHGMeshKit::Cached(TEXT("SM_HGWinBody"), [](FHGMeshKit& K) { BuildBody(K); });
	}
}
