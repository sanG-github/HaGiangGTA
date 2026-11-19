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

	void BuildWheel(FHGMeshKit& K, bool bFront, bool bKnobby)
	{
		const FTransform Id = FTransform::Identity;
		// Tyre (2.50-17 / 2.75-17: ~61 cm outside); the Win runs semi-knobbies, scooters street tyres.
		K.Paint(Rubber);
		const float Minor = bKnobby ? (bFront ? 4.0f : 4.6f) : 3.6f;
		K.Torus(Id, bKnobby ? 25.8f : 26.4f, Minor, 28, 8);
		if (bKnobby)
		{
			const int32 Knobs = 30;
			for (int32 i = 0; i < Knobs; ++i)
			{
				// Short blocks standing 1 cm proud of the carcass, staggered left / right.
				const float A = 2.f * PI * i / Knobs;
				const FVector Dir(FMath::Cos(A), 0.f, FMath::Sin(A));
				const float Y = (i & 1) ? 1.7f : -1.7f;
				K.Box(Dir * (25.8f + Minor + 0.1f) + FVector(0.f, Y, 0.f), FVector(1.8f, 3.2f, 3.4f),
					FRotator(FMath::RadiansToDegrees(A), 0.f, 0.f));
			}
		}
		// Round 6B: dried mud and dust on the tyre sidewalls.
		{
			FRandomStream Rm{ bFront ? 71 : 72 };
			for (int32 i = 0; i < 14; ++i)
			{
				const float A = Rm.FRandRange(0.f, 2.f * PI);
				const float Y = (i & 1) ? Minor * 0.85f : -Minor * 0.85f;
				K.Paint(i % 3 ? FLC(0.11f, 0.075f, 0.045f) : FLC(0.2f, 0.15f, 0.09f), HGSurf::Matte);
				K.Ellipsoid(FVector(FMath::Cos(A), 0.f, FMath::Sin(A)) * Rm.FRandRange(24.5f, 27.f) + FVector(0.f, Y, 0.f),
					FVector(Rm.FRandRange(2.f, 4.5f), 0.5f, Rm.FRandRange(1.2f, 2.6f)), FRotator(FMath::RadiansToDegrees(-A) + 90.f, 0.f, 0.f), 6, 3);
			}
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

	/** Round 6B: close-up detail for every bike - control cables, chain + sprockets, plate lettering, mud on the
	 *  lower body and a little wear. Plate: centre + tilt of the plate board of that body. */
	void AddDetail(FHGMeshKit& K, bool bStep, const FVector& PlateC, float PlatePitch, int32 Seed)
	{
		const FVector F = HGBikes::FrontAxle();
		const FVector R = HGBikes::RearAxle();
		FRandomStream Rs{ 3100 + Seed };
		// Cables from the levers / throttle looping down to the steering head, front brake cable to the drum.
		K.Paint(FLC(0.012f, 0.012f, 0.012f), HGSurf::Satin);
		for (const float S : { -1.f, 1.f })
		{
			const FVector A(42.f, 21.f * S, 104.5f), B(52.f, 10.f * S, 99.f), C(48.f, 3.f * S, bStep ? 92.f : 84.f);
			K.Tube(A, B, 0.38f, 0.38f, 4, false);
			K.Tube(B, C, 0.38f, 0.38f, 4, false);
		}
		K.Tube(FVector(42.f, 18.f, 104.f), FVector(53.f, 6.f, 96.f), 0.38f, 0.38f, 4, false);       // throttle (2nd)
		K.Tube(FVector(50.f, -7.f, 90.f), F + FVector(4.f, -8.f, 13.f), 0.38f, 0.38f, 4, false);   // front brake
		// Chain + sprockets on the left.
		const FVector Fs = bStep ? FVector(-4.f, -11.8f, 34.f) : FVector(4.f, -11.8f, 37.f);
		const FVector Rs3 = R + FVector(0.f, -11.8f, 0.f);
		const FLC ChainC(0.05f, 0.042f, 0.035f);
		K.Paint(FLC(0.09f, 0.085f, 0.08f), HGSurf::Chrome);
		K.Torus(FTransform(Rs3), 8.2f, 0.9f, 20, 4);                                                  // rear sprocket
		K.Tube(Rs3 - FVector(0.f, 0.4f, 0.f), Rs3 + FVector(0.f, 0.4f, 0.f), 7.4f, 7.4f, 16, true);
		for (int32 i = 0; i < 18; ++i)
		{
			const float A = 2.f * PI * i / 18.f;
			K.Box(Rs3 + FVector(FMath::Cos(A), 0.f, FMath::Sin(A)) * 9.2f, FVector(1.1f, 0.7f, 1.4f), FRotator(FMath::RadiansToDegrees(A), 0.f, 0.f));
		}
		K.Tube(Fs - FVector(0.f, 0.4f, 0.f), Fs + FVector(0.f, 0.4f, 0.f), 3.4f, 3.4f, 10, true);     // front sprocket
		K.Paint(ChainC, HGSurf::Satin);
		for (const float Up : { 1.f, -1.f })
		{
			const FVector A = Fs + FVector(0.f, 0.f, 3.8f * Up), B = Rs3 + FVector(0.f, 0.f, 9.4f * Up);
			K.Tube(A, B, 0.45f, 0.45f, 4, false);
			const int32 Links = int32((B - A).Size() / 2.6f);
			const FRotator Dir = (B - A).Rotation();
			for (int32 l = 0; l <= Links; ++l)
			{
				K.Paint(l % 2 ? ChainC : ChainC * 1.6f, HGSurf::Satin);
				K.Box(FMath::Lerp(A, B, float(l) / FMath::Max(Links, 1)), FVector(2.2f, 1.4f, 1.3f), Dir);
			}
		}
		// Plate lettering: "23-B1 / 123.45" style two rows of seven-segment-ish glyphs.
		const FQuat PQ = FRotator(PlatePitch, 0.f, 0.f).Quaternion();
		K.Paint(FLC(0.015f, 0.015f, 0.018f), HGSurf::Satin);
		for (int32 Row = 0; Row < 2; ++Row)
		{
			const int32 N = Row ? 6 : 5;
			for (int32 g = 0; g < N; ++g)
			{
				const float Y = (g - (N - 1) * 0.5f) * 2.8f;
				const float Z = Row ? -3.1f : 3.1f;
				const uint32 Mask = (Rs.GetUnsignedInt() & 0x7f) | 0x09;   // always top + bottom bars
				if ((Row == 0 && g == 2) || (Row == 1 && g == 3))
				{
					K.Box(PlateC + PQ.RotateVector(FVector(-0.6f, Y, Z + (Row ? -1.4f : 0.f))), FVector(0.4f, (Row ? 0.6f : 1.5f), (Row ? 0.6f : 0.4f)), FRotator(PlatePitch, 0.f, 0.f));
					continue;   // dash / dot
				}
				const FVector Seg[7][2] = {
					{ FVector(0.f, 0.f, 1.5f), FVector(0.f, 1.4f, 0.35f) }, { FVector(0.f, 0.f, 0.f), FVector(0.f, 1.4f, 0.35f) },
					{ FVector(0.f, 0.f, -1.5f), FVector(0.f, 1.4f, 0.35f) }, { FVector(0.f, -0.7f, 0.75f), FVector(0.f, 0.35f, 1.5f) },
					{ FVector(0.f, 0.7f, 0.75f), FVector(0.f, 0.35f, 1.5f) }, { FVector(0.f, -0.7f, -0.75f), FVector(0.f, 0.35f, 1.5f) },
					{ FVector(0.f, 0.7f, -0.75f), FVector(0.f, 0.35f, 1.5f) } };
				for (int32 k = 0; k < 7; ++k)
				{
					if (Mask & (1u << ((k == 2) ? 3 : (k == 3 ? 2 : k))))
					{
						K.Box(PlateC + PQ.RotateVector(FVector(-0.6f, Y, Z) + Seg[k][0]), Seg[k][1] + FVector(0.4f, 0.f, 0.f), FRotator(PlatePitch, 0.f, 0.f));
					}
				}
			}
		}
		// Mud: splashes on the fenders, the lower frame, engine bottom, exhaust and stand (red-brown laterite dust).
		const FLC Mud(0.13f, 0.085f, 0.05f), MudDry(0.24f, 0.17f, 0.1f);
		const FVector Spots[14] = {
			F + FVector(-8.f, 0.f, 26.f), F + FVector(-14.f, 0.f, 22.f), F + FVector(8.f, 6.f, 8.f), F + FVector(8.f, -6.f, 8.f),
			R + FVector(14.f, 0.f, 30.f), R + FVector(24.f, 0.f, 24.f), R + FVector(-18.f, 0.f, 30.f), R + FVector(6.f, 10.f, 10.f),
			FVector(10.f, -8.f, 26.f), FVector(10.f, 8.f, 26.f), FVector(-6.f, 0.f, 24.f), FVector(-20.f, 13.f, 24.f),
			FVector(-26.f, -13.f, 26.f), FVector(22.f, 12.f, 30.f) };
		for (int32 i = 0; i < 14; ++i)
		{
			K.Paint(i % 3 ? Mud : MudDry, HGSurf::Matte);
			K.Ellipsoid(Spots[i] + FVector(Rs.FRandRange(-2.f, 2.f), 0.f, Rs.FRandRange(-2.f, 2.f)),
				FVector(Rs.FRandRange(3.f, 6.f), Rs.FRandRange(2.f, 4.f), Rs.FRandRange(1.f, 2.2f)),
				FRotator(Rs.FRandRange(-30.f, 30.f), Rs.FRandRange(0.f, 180.f), 0.f), 6, 3);
		}
	}

	void BuildWin(FHGMeshKit& K)
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
		AddDetail(K, false, FVector(-95.2f, 0.f, 63.f), 8.f, 1);
	}

	// ---------------------------------------------------------------------------------------------------
	// Step-through / underbone bikes (Wave, Dream, Sirius)
	// ---------------------------------------------------------------------------------------------------
	struct FStepStyle
	{
		float CowlLen = 12.f;      // handlebar cowl
		float ShieldPitch = -22.f; // leg shield rake
		float RearRise = 0.f;      // up-sweep of the rear body (deg)
		bool bBoxy = false;        // Dream
		bool bSport = false;       // Sirius: stripes, tank cover, slimmer
		FLC Trim = FLC(0.7f, 0.7f, 0.68f);
	};

	void BuildStepThrough(FHGMeshKit& K, const FStepStyle& St)
	{
		const FVector F = HGBikes::FrontAxle();
		const FVector R = HGBikes::RearAxle();
		// Fork under a body-colour front fender.
		for (const float Y : { -6.f, 6.f })
		{
			K.Paint(Chrome, HGSurf::Chrome);
			K.Tube(FVector(47.f, Y, 92.f), FVector(F.X + 0.5f, Y, F.Z + 3.f), 1.8f, 1.8f, 8, true);
			K.Paint(Black, HGSurf::Satin);
			K.Tube(FVector(55.f, Y, 62.f), FVector(F.X + 0.5f, Y, F.Z + 3.f), 2.6f, 2.4f, 8, true);
		}
		K.Paint(FLC::White, HGSurf::Gloss, true);
		Arc(K, F, 33.f, 15.f, 125.f, 12.f, 1.2f, 9);
		// Leg shield + inner panel, floorboard, underbone.
		K.Box(FVector(43.f, 0.f, 64.f), FVector(5.f, St.bBoxy ? 36.f : 32.f, 46.f), FRotator(St.ShieldPitch, 0.f, 0.f));
		// Curved shell of the leg shield (reads as one moulded panel wrapping the column).
		K.Ellipsoid(FVector(46.f, 0.f, 66.f), FVector(9.f, St.bBoxy ? 21.f : 19.f, 30.f), FRotator(St.ShieldPitch, 0.f, 0.f), 14, 7);
		K.Paint(FLC(0.05f, 0.05f, 0.055f), HGSurf::Satin);
		K.Box(FVector(39.f, 0.f, 62.f), FVector(2.f, 28.f, 40.f), FRotator(St.ShieldPitch, 0.f, 0.f));
		K.Paint(Rubber);
		K.Box(FVector(18.f, 0.f, 33.f), FVector(32.f, 27.f, 3.f));
		K.Paint(Black, HGSurf::Satin);
		K.Tube(FVector(42.f, 0.f, 55.f), FVector(4.f, 0.f, 30.f), 2.4f, 2.4f, 8, true);
		// Steering-column cover from the shield up to the cowl, and the lower panel closing shield -> floor
		// (the first version showed a free-standing board with daylight round it).
		K.Paint(FLC::White, HGSurf::Gloss, true);
		K.Tube(FVector(45.f, 0.f, 70.f), FVector(46.f, 0.f, 97.f), 7.f, 6.f, 12, true);
		K.Box(FVector(33.f, 0.f, 40.f), FVector(22.f, 26.f, 14.f), FRotator(-30.f, 0.f, 0.f));
		K.Ellipsoid(FVector(30.f, 0.f, 38.f), FVector(14.f, 13.5f, 9.f), FRotator(-25.f, 0.f, 0.f), 10, 5);
		// Handlebar cowl with the headlamp; bars and grips at the Win's grip points.
		K.Paint(FLC::White, HGSurf::Gloss, true);
		if (St.bBoxy)
		{
			K.Box(FVector(48.f, 0.f, 100.f), FVector(20.f, 22.f, 12.f), FRotator(-8.f, 0.f, 0.f));
		}
		else
		{
			K.Ellipsoid(FVector(47.f, 0.f, 100.f), FVector(St.CowlLen, St.bSport ? 19.f : 22.f, 7.5f), FRotator(-10.f, 0.f, 0.f), 14, 6);
		}
		K.Paint(Black, HGSurf::Satin);
		K.Ellipsoid(FVector(44.f, 0.f, 106.f), FVector(6.f, 9.f, 2.5f), FRotator(-15.f, 0.f, 0.f), 10, 4, 0.f);
		for (const float S : { -1.f, 1.f })
		{
			K.Paint(Chrome, HGSurf::Chrome);
			K.Tube(FVector(43.f, 18.f * S, 103.f), FVector(42.f, 26.f * S, 103.f), 1.3f, 1.3f, 6, false);
			K.Tube(FVector(43.f, 24.f * S, 104.f), FVector(49.f, 31.f * S, 103.f), 0.55f, 0.45f, 4, false);
			K.Tube(FVector(44.f, 15.f * S, 104.f), FVector(41.f, 22.f * S, 121.f), 0.55f, 0.55f, 4, false);
			K.Paint(Black, HGSurf::Satin);
			K.Tube(FVector(42.f, 25.f * S, 103.f), FVector(41.f, 35.f * S, 103.f), 1.9f, 1.9f, 8, true);
			K.Ellipsoid(FVector(40.5f, 22.f * S, 123.5f), FVector(1.6f, 4.5f, 3.4f), FRotator::ZeroRotator, 8, 4);
			K.Paint(Amber, HGSurf::Gloss);
			K.Ellipsoid(FVector(52.f, 17.f * S, 99.f), FVector(3.f, 2.5f, 2.f), FRotator::ZeroRotator, 8, 4);
		}
		// Rear body under the seat (tinted), side stripes.
		const FRotator Rise(St.RearRise, 0.f, 0.f);
		K.Paint(FLC::White, HGSurf::Gloss, true);
		if (St.bBoxy)
		{
			K.Box(FVector(-38.f, 0.f, 64.f), FVector(70.f, 27.f, 24.f), Rise);
		}
		K.Ellipsoid(FVector(-38.f, 0.f, 66.f), FVector(St.bBoxy ? 38.f : 40.f, St.bSport ? 12.5f : 14.f, St.bBoxy ? 13.f : 15.f), Rise, 16, 8);
		K.Ellipsoid(FVector(-8.f, 0.f, 50.f), FVector(22.f, 12.f, 16.f), FRotator(-20.f, 0.f, 0.f), 12, 6);
		if (St.bSport)
		{
			K.Ellipsoid(FVector(14.f, 0.f, 62.f), FVector(16.f, 10.f, 9.f), FRotator(-12.f, 0.f, 0.f), 12, 6);
		}
		K.Paint(St.Trim, HGSurf::Gloss);
		for (const float S : { -1.f, 1.f })
		{
			K.Box(FVector(-40.f, (St.bSport ? 12.6f : 14.f) * S, 68.f), FVector(48.f, 0.6f, St.bSport ? 2.4f : 1.6f), Rise + FRotator(St.bSport ? 6.f : 0.f, 0.f, 0.f));
		}
		// Seat (top ~85 cm like the Win: the rider / passenger anchors don't move).
		K.Paint(FLC(0.02f, 0.018f, 0.016f), HGSurf::Satin);
		K.Box(FVector(-30.f, 0.f, 80.f), FVector(66.f, 23.f, 5.f), Rise);
		K.Ellipsoid(FVector(-30.f, 0.f, 82.f), FVector(34.f, 11.8f, 3.2f), Rise, 14, 5, 0.f);
		// Grab rail / small rack, tail housing, plate, indicators, fender.
		K.Paint(Chrome, HGSurf::Chrome);
		for (const float S : { -1.f, 1.f })
		{
			K.Tube(FVector(-58.f, 12.f * S, 84.f), FVector(-82.f, 10.f * S, 84.f), 1.f, 1.f, 6, false);
		}
		K.Tube(FVector(-82.f, -10.f, 84.f), FVector(-82.f, 10.f, 84.f), 1.f, 1.f, 6, false);
		if (St.bBoxy)
		{
			K.Paint(Black, HGSurf::Satin);
			for (const float X : { -66.f, -74.f })
			{
				K.Tube(FVector(X, -11.f, 84.f), FVector(X, 11.f, 84.f), 0.8f, 0.8f, 5, false);
			}
		}
		K.Paint(FLC::White, HGSurf::Gloss, true);
		K.Box(FVector(-80.f, 0.f, 73.f), FVector(12.f, 18.f, 10.f), Rise);
		K.Paint(Black, HGSurf::Satin);
		Arc(K, R, 33.f, 40.f, 165.f, 11.f, 0.9f, 9);
		K.Paint(FLC(0.8f, 0.8f, 0.77f), HGSurf::Satin);
		K.Box(FVector(-88.f, 0.f, 58.f), FVector(1.f, 19.f, 13.f), FRotator(8.f, 0.f, 0.f));
		for (const float S : { -1.f, 1.f })
		{
			K.Paint(Amber, HGSurf::Gloss);
			K.Ellipsoid(FVector(-85.f, 11.f * S, 72.f), FVector(2.8f, 2.2f, 2.2f), FRotator::ZeroRotator, 8, 4);
			K.Paint(Black, HGSurf::Satin);
			K.Tube(FVector(-4.f, 9.f * S, 35.f), FVector(R.X, 9.f * S, R.Z), 2.f, 1.7f, 8, true);
			K.Paint(St.bSport ? FLC(0.6f, 0.05f, 0.03f) : Chrome, St.bSport ? HGSurf::Gloss : HGSurf::Chrome);
			K.Tube(FVector(-56.f, 11.f * S, 33.f), FVector(-52.f, 11.f * S, 66.f), 2.6f, 2.6f, 8, true);
			K.Paint(Black, HGSurf::Matte);
			K.Tube(FVector(20.f, 9.f * S, 33.f), FVector(20.f, 20.f * S, 33.f), 1.4f, 1.4f, 6, true);
			K.Tube(FVector(-30.f, 10.f * S, 38.f), FVector(-30.f, 19.f * S, 38.f), 1.2f, 1.2f, 6, true);
		}
		// Horizontal Cub engine + exhaust.
		K.Paint(Alloy, HGSurf::Satin);
		K.Box(FVector(0.f, 0.f, 34.f), FVector(28.f, 16.f, 20.f));
		K.Ellipsoid(FVector(-2.f, -8.5f, 34.f), FVector(12.f, 2.5f, 9.f), FRotator::ZeroRotator, 12, 5);
		K.Paint(DarkAlloy, HGSurf::Satin);
		K.Box(FVector(18.f, 0.f, 38.f), FVector(16.f, 11.f, 11.f), FRotator(8.f, 0.f, 0.f));
		K.Paint(Black, HGSurf::Satin);
		K.Box(FVector(-30.f, -12.f, 34.f), FVector(52.f, 1.4f, 7.f));
		K.Paint(Chrome, HGSurf::Chrome);
		K.Tube(FVector(24.f, 4.f, 34.f), FVector(10.f, 12.f, 22.f), 2.f, 2.f, 8, false);
		K.Tube(FVector(10.f, 12.f, 22.f), FVector(-18.f, 13.f, 26.f), 2.f, 2.f, 8, false);
		K.Paint(FLC(0.03f, 0.03f, 0.03f), HGSurf::Satin);
		K.Tube(FVector(-18.f, 13.f, 27.f), FVector(-70.f, 13.f, 38.f), 4.f, 5.f, 12, true);
		K.Paint(Chrome, HGSurf::Chrome);
		K.Box(FVector(-40.f, 18.f, 32.f), FVector(28.f, 1.f, 7.f), FRotator(-12.f, 0.f, 0.f));
		K.Tube(FVector(-4.f, -11.f, 29.f), FVector(-26.f, -13.f, 27.f), 1.f, 1.f, 5, true);
		AddDetail(K, true, FVector(-88.f, 0.f, 58.f), 8.f, St.bBoxy ? 2 : (St.bSport ? 3 : 4));
	}

	// ---------------------------------------------------------------------------------------------------
	// Luggage and loads
	// ---------------------------------------------------------------------------------------------------
	const FLC Brocade[5] = { FLC(0.6f, 0.03f, 0.05f), FLC(0.75f, 0.48f, 0.02f), FLC(0.04f, 0.3f, 0.1f), FLC(0.7f, 0.12f, 0.35f), FLC(0.03f, 0.08f, 0.38f) };

	void Straps(FHGMeshKit& K, float X0, float X1, float Z, float HalfW)
	{
		K.Paint(FLC(0.02f, 0.02f, 0.02f));
		for (const float X : { X0, X1 })
		{
			K.Box(FVector(X, 0.f, Z), FVector(2.f, HalfW * 2.f + 1.f, 1.f));
			for (const float S : { -1.f, 1.f })
			{
				K.Box(FVector(X, (HalfW + 0.5f) * S, Z - 8.f), FVector(2.f, 1.f, 16.f));
			}
		}
	}

	void BuildEasyRiderLuggage(FHGMeshKit& K, int32 Seed)
	{
		// Side frames off the rack with rolled dry bags; two backpacks and a tho cam bag strapped on top.
		K.Paint(Black, HGSurf::Satin);
		for (const float S : { -1.f, 1.f })
		{
			K.Tube(FVector(-62.f, 11.f * S, 86.f), FVector(-64.f, 22.f * S, 60.f), 1.f, 1.f, 5, false);
			K.Tube(FVector(-92.f, 11.f * S, 86.f), FVector(-90.f, 22.f * S, 60.f), 1.f, 1.f, 5, false);
			K.Tube(FVector(-64.f, 22.f * S, 60.f), FVector(-90.f, 22.f * S, 60.f), 1.f, 1.f, 5, false);
		}
		for (const float S : { -1.f, 1.f })
		{
			const FLC Bag = ((S < 0.f) == bool(Seed & 1)) ? FLC(0.75f, 0.3f, 0.03f) : FLC(0.04f, 0.12f, 0.4f);
			K.Paint(Bag, HGSurf::Satin);
			K.Tube(FVector(-60.f, 25.f * S, 70.f), FVector(-94.f, 25.f * S, 70.f), 9.f, 9.f, 10, true);
			K.Paint(Bag * 0.55f, HGSurf::Satin);
			K.Tube(FVector(-94.f, 25.f * S, 70.f), FVector(-97.f, 25.f * S, 72.f), 6.f, 4.f, 8, true);
		}
		const FLC Packs[3] = { FLC(0.06f, 0.1f, 0.2f), FLC(0.12f, 0.15f, 0.07f), FLC(0.32f, 0.08f, 0.03f) };
		K.Paint(Packs[Seed % 3], HGSurf::Satin);
		K.Box(FVector(-79.f, 0.f, 101.f), FVector(30.f, 32.f, 28.f));
		K.Ellipsoid(FVector(-79.f, 0.f, 115.f), FVector(15.f, 16.f, 5.f), FRotator::ZeroRotator, 10, 4, 0.f);
		K.Paint(Packs[(Seed + 1) % 3], HGSurf::Satin);
		K.Box(FVector(-80.f, 0.f, 124.f), FVector(26.f, 26.f, 18.f), FRotator(0.f, 10.f, 0.f));
		for (int32 i = 0; i < 4; ++i)
		{
			K.Paint(Brocade[(Seed + i) % 5]);
			K.Box(FVector(-80.f, 0.f, 134.f + i * 2.2f), FVector(16.f, 20.f, 2.2f));
		}
		// Round 6B: strapped, not glued - criss-crossed bungee cords over the packs hooked onto the rack rails,
		// cam-strap webbing round each dry bag and its frame.
		const FLC Bungee[2] = { FLC(0.5f, 0.03f, 0.03f), FLC(0.02f, 0.02f, 0.025f) };
		for (int32 c = 0; c < 2; ++c)
		{
			K.Paint(Bungee[(Seed + c) % 2], HGSurf::Satin);
			const float Y0 = c ? -15.f : 15.f;
			const FVector Hook0(-64.f, Y0, 87.f), Hook1(-94.f, -Y0, 87.f);
			const FVector Over0(-70.f, Y0 * 0.7f, 116.f), Over1(-88.f, -Y0 * 0.7f, 116.f), Top(-79.f, 0.f, 134.f);
			K.Tube(Hook0, Over0, 0.6f, 0.6f, 5, false);
			K.Tube(Over0, Top, 0.6f, 0.6f, 5, false);
			K.Tube(Top, Over1, 0.6f, 0.6f, 5, false);
			K.Tube(Over1, Hook1, 0.6f, 0.6f, 5, false);
			K.Paint(FLC(0.5f, 0.5f, 0.48f), HGSurf::Chrome);
			K.Tube(Hook0, Hook0 + FVector(0.f, 0.f, -2.5f), 0.35f, 0.35f, 4, false);
			K.Tube(Hook1, Hook1 + FVector(0.f, 0.f, -2.5f), 0.35f, 0.35f, 4, false);
		}
		K.Paint(FLC(0.02f, 0.02f, 0.02f), HGSurf::Satin);
		for (const float S : { -1.f, 1.f })
		{
			for (const float X : { -68.f, -86.f })
			{
				K.Torus(FTransform(FRotator(0.f, 90.f, 0.f), FVector(X, 25.f * S, 70.f)), 9.3f, 0.55f, 14, 4);
				K.Box(FVector(X, 25.f * S, 79.8f), FVector(2.f, 3.f, 1.f));                                   // cam buckle
			}
		}
		Straps(K, -70.f, -90.f, 116.f, 16.f);
	}

	void BuildLocalLoad(FHGMeshKit& K, int32 Seed)
	{
		switch (Seed % 3)
		{
		case 0:
		{
			// Cut grass for the buffalo, bundled higher than the rider's shoulders.
			K.Paint(FLC(0.09f, 0.2f, 0.035f));
			K.Ellipsoid(FVector(-80.f, 0.f, 108.f), FVector(32.f, 36.f, 24.f), FRotator::ZeroRotator, 12, 6);
			K.Paint(FLC(0.16f, 0.26f, 0.06f));
			K.Ellipsoid(FVector(-78.f, -4.f, 126.f), FVector(26.f, 30.f, 16.f), FRotator(0.f, 20.f, 0.f), 12, 6);
			K.Paint(FLC(0.3f, 0.26f, 0.1f));
			for (int32 i = 0; i < 10; ++i)
			{
				const float A = i * 0.63f;
				K.Box(FVector(-80.f + 26.f * FMath::Cos(A), 32.f * FMath::Sin(A), 104.f + (i % 3) * 6.f), FVector(16.f, 1.5f, 1.f),
					FRotator(10.f * (i % 3), FMath::RadiansToDegrees(A) + 90.f, 0.f));
			}
			Straps(K, -70.f, -92.f, 132.f, 26.f);
			break;
		}
		case 1:
		{
			// Firewood: split logs along the rack, tied with rope.
			for (int32 i = 0; i < 14; ++i)
			{
				const int32 Row = i / 5, Col = i % 5;
				K.Paint(FLC(0.2f + 0.05f * (i % 3), 0.13f + 0.03f * (i % 2), 0.07f));
				const float Y = -20.f + Col * 10.f, Z = 92.f + Row * 8.f;
				K.Tube(FVector(-40.f - (i % 2) * 4.f, Y, Z), FVector(-124.f + (i % 3) * 5.f, Y + (i % 2) * 2.f, Z), 4.f, 3.6f, 6, true);
			}
			K.Paint(FLC(0.45f, 0.38f, 0.2f));
			for (const float X : { -60.f, -105.f })
			{
				K.Torus(FTransform(FRotator(0.f, 90.f, 0.f), FVector(X, 0.f, 100.f)), 24.f, 0.8f, 12, 4);
			}
			break;
		}
		default:
		{
			// Pig in a bamboo basket lying across the rack, off to market.
			K.Paint(FLC(0.4f, 0.29f, 0.14f), HGSurf::Satin);
			K.Frustum(FTransform(FRotator(0.f, 0.f, -90.f), FVector(-80.f, -30.f, 108.f)), FVector2D(19.f), FVector2D(19.f), 60.f, 18, false, 0.05f);
			K.Paint(FLC(0.3f, 0.2f, 0.1f));
			for (const float Y : { -24.f, 0.f, 24.f })
			{
				K.Torus(FTransform(FVector(-80.f, Y, 108.f)), 20.f, 1.f, 16, 4);
			}
			K.Paint(FLC(0.72f, 0.46f, 0.42f));
			K.Ellipsoid(FVector(-80.f, 31.f, 106.f), FVector(10.f, 7.f, 9.f), FRotator::ZeroRotator, 10, 6);
			K.Paint(FLC(0.6f, 0.36f, 0.33f));
			K.Tube(FVector(-80.f, 37.f, 105.f), FVector(-80.f, 40.f, 105.f), 4.f, 3.6f, 8, true);
			K.Paint(FLC(0.1f, 0.06f, 0.06f));
			K.Ellipsoid(FVector(-78.5f, 40.2f, 106.f), FVector(0.7f, 0.3f, 0.7f), FRotator::ZeroRotator, 4, 3);
			K.Ellipsoid(FVector(-81.5f, 40.2f, 106.f), FVector(0.7f, 0.3f, 0.7f), FRotator::ZeroRotator, 4, 3);
			Straps(K, -66.f, -94.f, 128.f, 20.f);
			break;
		}
		}
	}
}

namespace HGBikes
{
	bool IsStepThrough(EHGBikeModel Model)
	{
		return Model == EHGBikeModel::Wave || Model == EHGBikeModel::Dream || Model == EHGBikeModel::Sirius;
	}

	UStaticMesh* Wheel(bool bFront, bool bScooter)
	{
		const FName Key = *FString::Printf(TEXT("SM_HGBikeWheel_%d_%d"), bFront ? 1 : 0, bScooter ? 1 : 0);
		return FHGMeshKit::Cached(Key, [bFront, bScooter](FHGMeshKit& K) { BuildWheel(K, bFront, !bScooter); });
	}

	UStaticMesh* Body(EHGBikeModel Model, int32 LoadSeed)
	{
		LoadSeed = FMath::Abs(LoadSeed);
		const int32 KeySeed = Model == EHGBikeModel::LocalLoaded ? LoadSeed % 3 : (Model == EHGBikeModel::EasyRiderWin ? LoadSeed % 6 : 0);
		const FName Key = *FString::Printf(TEXT("SM_HGBike_%d_%d"), int32(Model), KeySeed);
		return FHGMeshKit::Cached(Key, [Model, KeySeed](FHGMeshKit& K)
		{
			FStepStyle St;
			switch (Model)
			{
			case EHGBikeModel::Wave:
				BuildStepThrough(K, St);
				break;
			case EHGBikeModel::Dream:
				St.bBoxy = true;
				St.ShieldPitch = -14.f;
				St.Trim = FLC(0.75f, 0.72f, 0.6f);
				BuildStepThrough(K, St);
				break;
			case EHGBikeModel::Sirius:
				St.bSport = true;
				St.CowlLen = 14.f;
				St.ShieldPitch = -28.f;
				St.RearRise = -6.f;
				St.Trim = FLC(0.85f, 0.85f, 0.85f);
				BuildStepThrough(K, St);
				break;
			case EHGBikeModel::EasyRiderWin:
				BuildWin(K);
				BuildEasyRiderLuggage(K, KeySeed);
				break;
			case EHGBikeModel::LocalLoaded:
				BuildWin(K);
				BuildLocalLoad(K, KeySeed);
				break;
			default:
				BuildWin(K);
				break;
			}
		});
	}

	UStaticMesh* FlagPole(EHGBikeModel Model)
	{
		const FVector Top = FlagMount(Model);
		return FHGMeshKit::Cached(*FString::Printf(TEXT("SM_HGFlagPole_%d"), IsStepThrough(Model) ? 1 : 0), [Top](FHGMeshKit& K)
		{
			K.Paint(Black, HGSurf::Satin);
			K.Tube(FVector(Top.X, Top.Y, 84.f), Top + FVector(0.f, 0.f, 2.f), 0.6f, 0.45f, 5, true);
		});
	}

	UStaticMesh* Flag()
	{
		return FHGMeshKit::Cached(TEXT("SM_HGFlagVN"), [](FHGMeshKit& K)
		{
			// Red flag with the yellow star, 36 x 24 cm, flying back along -X from the pole.
			K.Paint(FLC(0.7f, 0.02f, 0.02f), HGSurf::Satin);
			K.Box(FVector(-18.f, 0.f, -12.f), FVector(36.f, 0.6f, 24.f));
			K.Paint(FLC(1.f, 0.8f, 0.f), HGSurf::Satin);
			for (int32 i = 0; i < 5; ++i)
			{
				const float A = FMath::DegreesToRadians(90.f + 72.f * i);
				const FVector Dir(FMath::Cos(A), 0.f, FMath::Sin(A));
				K.Box(FVector(-18.f, 0.f, -12.f) + Dir * 2.8f, FVector(5.6f, 0.8f, 2.2f), FRotator(FMath::RadiansToDegrees(A), 0.f, 0.f));
			}
			K.Box(FVector(-18.f, 0.f, -12.f), FVector(3.6f, 0.8f, 3.6f));
		});
	}

	FVector HeadLampLens(EHGBikeModel Model)
	{
		return IsStepThrough(Model) ? FVector(58.5f, 0.f, 99.f) : FVector(79.f, 0.f, 88.f);
	}

	FVector HeadLampSize(EHGBikeModel Model)
	{
		return Model == EHGBikeModel::Dream ? FVector(2.f, 12.f, 8.f) : (IsStepThrough(Model) ? FVector(3.f, 14.f, 6.f) : FVector(3.f, 16.5f, 16.5f));
	}

	FVector TailLampLens(EHGBikeModel Model)
	{
		return IsStepThrough(Model) ? FVector(-86.4f, 0.f, 73.f) : FVector(-97.4f, 0.f, 79.f);
	}

	FVector PlateLocation(EHGBikeModel Model)
	{
		return IsStepThrough(Model) ? FVector(-89.f, 0.f, 57.f) : FVector(-96.f, 0.f, 62.f);
	}

	FVector FlagMount(EHGBikeModel Model)
	{
		return IsStepThrough(Model) ? FVector(-80.f, -10.f, 150.f) : FVector(-97.f, -11.f, 165.f);
	}

	FLinearColor Paint(EHGBikeModel Model, int32 Seed)
	{
		Seed = FMath::Abs(Seed);
		switch (Model)
		{
		case EHGBikeModel::Wave:
		{
			const FLC C[4] = { FLC(0.04f, 0.12f, 0.45f), FLC(0.5f, 0.02f, 0.02f), FLC(0.02f, 0.02f, 0.02f), FLC(0.55f, 0.55f, 0.55f) };
			return C[Seed % 4];
		}
		case EHGBikeModel::Dream:
		{
			const FLC C[3] = { FLC(0.22f, 0.04f, 0.04f), FLC(0.18f, 0.1f, 0.05f), FLC(0.35f, 0.3f, 0.22f) };
			return C[Seed % 3];
		}
		case EHGBikeModel::Sirius:
		{
			const FLC C[3] = { FLC(0.02f, 0.02f, 0.02f), FLC(0.5f, 0.02f, 0.02f), FLC(0.02f, 0.12f, 0.5f) };
			return C[Seed % 3];
		}
		case EHGBikeModel::LocalLoaded:
		{
			const FLC C[3] = { FLC(0.2f, 0.03f, 0.03f), FLC(0.06f, 0.06f, 0.07f), FLC(0.15f, 0.12f, 0.08f) };
			return C[Seed % 3];
		}
		default:
		{
			const FLC C[5] = { FLC(0.55f, 0.02f, 0.02f), FLC(0.02f, 0.02f, 0.02f), FLC(0.05f, 0.15f, 0.45f), FLC(0.6f, 0.4f, 0.02f), FLC(0.06f, 0.2f, 0.07f) };
			return C[Seed % 5];
		}
		}
	}

	EHGBikeModel RandomModel(int32 Seed)
	{
		const int32 R = int32((uint32(Seed) * 2654435761u) >> 8) % 100;
		if (R < 30) return EHGBikeModel::Win;
		if (R < 50) return EHGBikeModel::Wave;
		if (R < 60) return EHGBikeModel::Dream;
		if (R < 75) return EHGBikeModel::Sirius;
		if (R < 90) return EHGBikeModel::EasyRiderWin;
		return EHGBikeModel::LocalLoaded;
	}
}
