#include "HGPeopleBuilder.h"
#include "HGMeshKit.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	using FLC = FLinearColor;

	enum class ELower : uint8 { Trousers, Shorts, Skirt };
	enum class EHead : uint8 { Bare, NonLa, HalfHelmet, DuckBill, FullHelmet, OpenHelmet, Cap, Wrap };

	// Tho cam (brocade) palette: the woven bags and scarves sold all along the loop.
	const FLC ThoCam[6] = { FLC(0.6f, 0.03f, 0.05f), FLC(0.75f, 0.48f, 0.02f), FLC(0.04f, 0.3f, 0.1f),
		FLC(0.7f, 0.12f, 0.35f), FLC(0.03f, 0.08f, 0.38f), FLC(0.012f, 0.012f, 0.015f) };
	// Tour-group colours (matching vests / scarves / helmets of one Easy Rider group).
	const FLC TourColor[4] = { FLC(0.7f, 0.9f, 0.04f), FLC(0.85f, 0.25f, 0.02f), FLC(0.04f, 0.2f, 0.65f), FLC(0.65f, 0.02f, 0.04f) };

	struct FOutfit
	{
		float Scale = 1.f;          // height (1 = 168 cm)
		float Width = 1.f;
		FLC Skin = FLC(0.5f, 0.33f, 0.22f);
		FLC Hair = FLC(0.012f, 0.01f, 0.009f);
		FLC Top = FLC(0.04f, 0.04f, 0.05f);
		FLC Sleeve = FLC(0.04f, 0.04f, 0.05f);
		FLC Cuff = FLC(0.04f, 0.04f, 0.05f);
		FLC Lower = FLC(0.05f, 0.05f, 0.06f);
		FLC Shoes = FLC(0.02f, 0.02f, 0.02f);
		FLC HeadGear = FLC(0.7f, 0.6f, 0.36f);
		FLC Pack = FLC(0.06f, 0.1f, 0.2f);
		FLC Vest = FLC(0.75f, 0.95f, 0.05f);
		ELower Legs = ELower::Trousers;
		EHead Head = EHead::Bare;
		bool bShortSleeves = false;
		bool bVest = false;
		bool bBackpack = false;
		bool bBasket = false;
		bool bBag = false;          // tho cam shoulder bag
		bool bStar = false;
		bool bCollar = false;       // Hmong embroidered collar flap
		int32 ScarfSeed = 0;
		FLC Skirt[4] = { FLC(0.02f, 0.025f, 0.08f), FLC(0.1f, 0.13f, 0.32f), FLC(0.55f, 0.04f, 0.12f), FLC(0.6f, 0.45f, 0.05f) };
	};

	int32 Wrap(int32 V) { return ((V % HGPeople::NumVariants) + HGPeople::NumVariants) % HGPeople::NumVariants; }

	FOutfit OutfitFor(EHGPersonLook Look, EHGPersonPose Pose, int32 V)
	{
		V = Wrap(V);
		FOutfit O;
		O.ScarfSeed = V;
		switch (Look)
		{
		case EHGPersonLook::HmongWoman:
		{
			O.Scale = 0.92f;
			O.Width = 0.9f;
			O.Skin = FLC(0.48f, 0.31f, 0.2f);
			O.Top = FLC(0.025f, 0.03f, 0.09f);                      // hand-dyed indigo
			O.Sleeve = O.Top;
			O.Cuff = (V & 1) ? FLC(0.05f, 0.35f, 0.12f) : FLC(0.6f, 0.05f, 0.18f);
			O.Lower = FLC(0.03f, 0.035f, 0.1f);                     // leg wraps
			O.Shoes = FLC(0.015f, 0.015f, 0.015f);
			O.Legs = ELower::Skirt;
			O.Head = EHead::Wrap;
			const FLC Wraps[4] = { FLC(0.7f, 0.12f, 0.3f), FLC(0.1f, 0.4f, 0.25f), FLC(0.08f, 0.08f, 0.25f), FLC(0.75f, 0.35f, 0.05f) };
			O.HeadGear = Wraps[V];
			O.bBasket = V != 1;
			O.bBag = V == 1 || V == 3;
			O.bCollar = true;
			const FLC Mid[4] = { FLC(0.1f, 0.13f, 0.32f), FLC(0.5f, 0.05f, 0.1f), FLC(0.3f, 0.06f, 0.3f), FLC(0.05f, 0.06f, 0.18f) };
			const FLC Band[4] = { FLC(0.6f, 0.05f, 0.14f), FLC(0.7f, 0.4f, 0.05f), FLC(0.08f, 0.35f, 0.4f), FLC(0.65f, 0.1f, 0.3f) };
			O.Skirt[1] = Mid[V];
			O.Skirt[2] = Band[V];
			O.Skirt[3] = Band[(V + 1) % 4];
			break;
		}
		case EHGPersonLook::LocalMan:
		{
			const FLC Jackets[4] = { FLC(0.025f, 0.025f, 0.03f), FLC(0.09f, 0.1f, 0.05f), FLC(0.03f, 0.045f, 0.09f), FLC(0.12f, 0.08f, 0.05f) };
			O.Skin = FLC(0.46f, 0.3f, 0.19f);
			O.Top = Jackets[V];
			O.Sleeve = O.Top;
			O.Cuff = O.Top;
			O.Lower = V == 1 ? FLC(0.03f, 0.03f, 0.035f) : FLC(0.06f, 0.06f, 0.065f);
			O.Shoes = V == 2 ? FLC(0.05f, 0.12f, 0.05f) : FLC(0.02f, 0.02f, 0.02f);   // green army canvas shoes
			O.Head = V == 0 ? EHead::Cap : ((V & 1) ? EHead::NonLa : EHead::Bare);
			O.HeadGear = V == 0 ? FLC(0.02f, 0.02f, 0.025f) : FLC(0.7f, 0.6f, 0.36f);
			O.bBag = V == 2;
			break;
		}
		case EHGPersonLook::Backpacker:
		{
			const FLC Shirts[4] = { FLC(0.6f, 0.02f, 0.02f), FLC(0.6f, 0.02f, 0.02f), FLC(0.75f, 0.73f, 0.68f), FLC(0.05f, 0.18f, 0.25f) };
			const FLC Packs[4] = { FLC(0.05f, 0.09f, 0.2f), FLC(0.12f, 0.16f, 0.07f), FLC(0.3f, 0.08f, 0.03f), FLC(0.15f, 0.15f, 0.16f) };
			O.Scale = 1.06f;
			O.Width = 1.05f;
			O.Skin = (V == 3) ? FLC(0.62f, 0.42f, 0.3f) : FLC(0.78f, 0.55f, 0.43f);
			O.Hair = V == 2 ? FLC(0.35f, 0.25f, 0.1f) : FLC(0.12f, 0.07f, 0.03f);
			O.Top = Shirts[V];
			O.Sleeve = O.Top;
			O.Cuff = O.Top;
			O.Lower = FLC(0.3f, 0.27f, 0.17f);                      // khaki cargo shorts
			O.Shoes = FLC(0.35f, 0.35f, 0.33f);
			O.Legs = ELower::Shorts;
			// Yellow duck-bill rental helmets ("non vit") on nearly every backpacker of the loop.
			O.Head = V == 2 ? EHead::HalfHelmet : EHead::DuckBill;
			O.HeadGear = V == 2 ? FLC(0.75f, 0.75f, 0.72f) : FLC(0.85f, 0.62f, 0.02f);
			O.Pack = Packs[V];
			O.bShortSleeves = true;
			O.bBackpack = Pose == EHGPersonPose::Standing || V != 3;
			O.bBag = V == 3;
			O.bStar = V < 2;
			break;
		}
		case EHGPersonLook::EasyRider:
			O.Top = FLC(0.03f, 0.03f, 0.035f);
			O.Sleeve = O.Top;
			O.Cuff = O.Top;
			O.Lower = FLC(0.04f, 0.045f, 0.06f);
			O.Head = EHead::FullHelmet;
			O.HeadGear = (V & 1) ? FLC(0.6f, 0.6f, 0.58f) : FLC(0.02f, 0.02f, 0.02f);
			O.Vest = TourColor[V];
			O.bVest = true;
			break;
		case EHGPersonLook::Player:
			O.Top = FLC(0.09f, 0.11f, 0.05f);                       // army olive field jacket
			O.Sleeve = O.Top;
			O.Cuff = O.Top;
			O.Lower = FLC(0.04f, 0.06f, 0.12f);                     // jeans
			O.Shoes = FLC(0.03f, 0.025f, 0.02f);
			O.Head = Pose == EHGPersonPose::Standing ? EHead::NonLa : EHead::OpenHelmet;
			O.HeadGear = Pose == EHGPersonPose::Standing ? FLC(0.7f, 0.6f, 0.36f) : FLC(0.08f, 0.12f, 0.06f);
			O.bBag = true;
			break;
		case EHGPersonLook::Police:
			O.Top = FLC(0.55f, 0.45f, 0.12f);                       // CSGT khaki-yellow
			O.Sleeve = O.Top;
			O.Cuff = O.Top;
			O.Lower = FLC(0.04f, 0.07f, 0.04f);
			O.Head = EHead::FullHelmet;
			O.HeadGear = FLC(0.8f, 0.8f, 0.78f);
			break;
		}
		return O;
	}

	/** Joint positions of a pose (cm). */
	struct FPoseJoints
	{
		FVector Pelvis, Hip[2], Knee[2], Ankle[2], Shoulder[2], Wrist[2], NeckA, NeckB, HeadC, TorsoBase;
		float TorsoPitch = 0.f;   // forward lean (deg)
		float TorsoH = 48.f;
	};

	FPoseJoints JointsFor(EHGPersonPose Pose, const FOutfit& O)
	{
		FPoseJoints J;
		const float s = O.Scale, w = O.Width;
		auto Set = [](FVector* Pair, const FVector& P) { Pair[0] = FVector(P.X, -FMath::Abs(P.Y), P.Z); Pair[1] = FVector(P.X, FMath::Abs(P.Y), P.Z); };
		switch (Pose)
		{
		case EHGPersonPose::Standing:
			J.Pelvis = FVector(0, 0, 90 * s);
			Set(J.Hip, FVector(0, 9 * w, 88 * s));
			Set(J.Knee, FVector(1.5f, 9.5f * w, 48 * s));
			Set(J.Ankle, FVector(0, 9.5f * w, 9 * s));
			J.TorsoBase = FVector(0, 0, 92 * s);
			J.TorsoH = 47 * s;
			Set(J.Shoulder, FVector(0, 18.5f * w, 135 * s));
			Set(J.Wrist, FVector(3, 21.5f * w, 86 * s));
			J.NeckA = FVector(0, 0, 137 * s);
			J.NeckB = FVector(1, 0, 150 * s);
			J.HeadC = FVector(2, 0, 160 * s);
			break;
		case EHGPersonPose::Rider:
			J.Pelvis = FVector(0, 0, 11);
			Set(J.Hip, FVector(2, 10, 12));
			Set(J.Knee, FVector(38, 16, 4));
			Set(J.Ankle, FVector(31, 18, -43));
			J.TorsoBase = FVector(0, 0, 13);
			J.TorsoPitch = 16.f;
			J.TorsoH = 46 * s;
			Set(J.Shoulder, FVector(11, 18.5f, 53));
			Set(J.Wrist, FVector(49, 30, 17));
			J.NeckA = FVector(12, 0, 54);
			J.NeckB = FVector(15, 0, 66);
			J.HeadC = FVector(17, 0, 76);
			break;
		case EHGPersonPose::Passenger:
			J.Pelvis = FVector(0, 0, 11);
			Set(J.Hip, FVector(2, 10, 12));
			Set(J.Knee, FVector(32, 17, 5));
			Set(J.Ankle, FVector(25, 19, -42));
			J.TorsoBase = FVector(0, 0, 13);
			J.TorsoPitch = 6.f;
			J.TorsoH = 46 * s;
			Set(J.Shoulder, FVector(4, 18.5f * w, 55));
			Set(J.Wrist, FVector(26, 19, 22));
			J.NeckA = FVector(5, 0, 56);
			J.NeckB = FVector(6, 0, 68);
			J.HeadC = FVector(7, 0, 78);
			break;
		}
		return J;
	}

	FRotator AlongZ(const FVector& Dir) { return FRotationMatrix::MakeFromZ(Dir.GetSafeNormal()).Rotator(); }

	// Arm mesh joints (shoulder pivot at the origin, hanging along -Z with a slight elbow bend).
	const FVector ArmElbow(1.5f, 0.f, -26.f);
	const FVector ArmWrist(4.f, 0.f, -49.f);
	const FVector ArmAxis = ArmWrist.GetSafeNormal();

	void AddThoCamBag(FHGMeshKit& K, const FVector& C, const FRotator& Rot, const FVector& StrapTo)
	{
		const FQuat Q = Rot.Quaternion();
		K.Paint(ThoCam[5], HGSurf::Satin);
		K.Box(C, FVector(5.f, 20.f, 22.f), Rot);
		for (int32 i = 0; i < 5; ++i)
		{
			K.Paint(ThoCam[i]);
			K.Box(C + Q.RotateVector(FVector(0.f, 0.f, -8.f + i * 4.f)), FVector(5.6f, 20.6f, 2.2f), Rot);
		}
		K.Paint(ThoCam[0]);
		K.Tube(C + Q.RotateVector(FVector(0.f, 0.f, 11.f)), StrapTo, 1.1f, 1.1f, 5, false);
	}

	void AddArm(FHGMeshKit& K, const FOutfit& O, const FVector& Sh, const FVector& El, const FVector& Wr)
	{
		K.Paint(O.Sleeve);
		K.Ellipsoid(Sh, FVector(5.6f), FRotator::ZeroRotator, 8, 5);
		K.Tube(Sh, El, 5.f, 4.3f, 8, false);
		if (O.bShortSleeves)
		{
			const FVector Mid = FMath::Lerp(Sh, El, 0.45f);
			K.Tube(Mid, Mid + (El - Sh).GetSafeNormal() * 1.5f, 5.4f, 5.2f, 8, true);
			K.Paint(O.Skin);
			K.Tube(Mid, El, 4.4f, 4.f, 8, false);
		}
		K.Ellipsoid(El, FVector(4.3f), FRotator::ZeroRotator, 6, 4);
		K.Tube(El, Wr, 4.2f, 3.4f, 8, false);
		const FVector D = (Wr - El).GetSafeNormal();
		if (!O.bShortSleeves)
		{
			K.Paint(O.Cuff);
			K.Tube(Wr - D * 5.f, Wr, 4.f, 3.8f, 8, false);
		}
		K.Paint(O.Skin);
		K.Ellipsoid(Wr + D * 4.5f, FVector(2.6f, 3.4f, 5.f), AlongZ(D), 6, 4);
	}

	void AddLeg(FHGMeshKit& K, const FOutfit& O, const FVector& Hip, const FVector& Knee, const FVector& Ankle, bool bStanding)
	{
		const bool bSkirt = O.Legs == ELower::Skirt && bStanding;
		if (!bSkirt)
		{
			K.Paint(O.Lower);
			K.Tube(Hip, Knee, 8.4f * O.Width, 6.4f, 8, false);
			K.Ellipsoid(Knee, FVector(6.2f), FRotator::ZeroRotator, 6, 4);
		}
		if (O.Legs == ELower::Shorts)
		{
			K.Paint(O.Lower);
			K.Tube(Hip, FMath::Lerp(Hip, Knee, 0.9f), 9.2f * O.Width, 7.6f, 8, true);
			K.Paint(O.Skin);
			K.Tube(Knee, Ankle, 5.2f, 3.8f, 8, false);
			K.Ellipsoid(Knee, FVector(5.4f), FRotator::ZeroRotator, 6, 4);
			K.Paint(FLC(0.6f, 0.6f, 0.58f));
			K.Tube(FMath::Lerp(Knee, Ankle, 0.82f), Ankle, 4.3f, 4.2f, 8, false);
		}
		else
		{
			K.Paint(O.Lower);
			K.Tube(bSkirt ? FMath::Lerp(Hip, Knee, 0.6f) : Knee, Ankle, 6.f, 4.4f, 8, false);
			if (bSkirt)
			{
				// Hmong leg wraps: pale bands round the indigo.
				K.Paint(FLC(0.45f, 0.42f, 0.38f));
				K.Tube(FMath::Lerp(Knee, Ankle, 0.15f), FMath::Lerp(Knee, Ankle, 0.2f), 6.3f, 6.1f, 8, false);
				K.Tube(FMath::Lerp(Knee, Ankle, 0.85f), FMath::Lerp(Knee, Ankle, 0.9f), 5.f, 4.9f, 8, false);
			}
		}
		K.Paint(O.Shoes);
		K.Box(Ankle + FVector(4.f, 0.f, -4.5f), FVector(21.f, 9.f, 7.f));                                   // shoe
		K.Ellipsoid(Ankle + FVector(13.f, 0.f, -4.5f), FVector(3.5f, 4.5f, 3.5f), FRotator::ZeroRotator, 6, 4);
	}

	/** Head part: origin at the neck base. */
	void AddHead(FHGMeshKit& K, const FOutfit& O, const FPoseJoints& J)
	{
		const FVector NB = J.NeckB - J.NeckA;
		const FVector H = J.HeadC - J.NeckA;
		K.Paint(O.Skin);
		K.Tube(FVector(0.f, 0.f, -2.f), NB, 5.f, 4.6f, 8, false);
		K.Ellipsoid(H, FVector(9.6f, 8.3f, 11.2f), FRotator::ZeroRotator, 12, 8);
		K.Box(H + FVector(9.3f, 0.f, -1.5f), FVector(2.6f, 2.4f, 4.f), FRotator(-10.f, 0.f, 0.f));
		K.Ellipsoid(H + FVector(0.f, -8.f, 0.f), FVector(2.f, 1.f, 3.f), FRotator::ZeroRotator, 5, 3);
		K.Ellipsoid(H + FVector(0.f, 8.f, 0.f), FVector(2.f, 1.f, 3.f), FRotator::ZeroRotator, 5, 3);
		K.Paint(FLC(0.01f, 0.01f, 0.01f));
		K.Ellipsoid(H + FVector(8.2f, -3.3f, 1.8f), FVector(1.2f, 1.4f, 0.9f), FRotator::ZeroRotator, 5, 3);
		K.Ellipsoid(H + FVector(8.2f, 3.3f, 1.8f), FVector(1.2f, 1.4f, 0.9f), FRotator::ZeroRotator, 5, 3);
		K.Paint(FLC(0.25f, 0.1f, 0.08f));
		K.Box(H + FVector(8.6f, 0.f, -5.2f), FVector(1.f, 4.f, 0.8f));                                         // mouth
		const bool bHelmet = O.Head == EHead::FullHelmet || O.Head == EHead::OpenHelmet || O.Head == EHead::HalfHelmet || O.Head == EHead::DuckBill;
		if (!bHelmet)
		{
			K.Paint(O.Hair);
			K.Ellipsoid(H + FVector(-1.6f, 0.f, 1.6f), FVector(9.8f, 8.9f, 10.8f), FRotator::ZeroRotator, 12, 6, -0.05f);
		}
		auto Straps = [&]()
		{
			K.Tube(H + FVector(1.f, -9.f, 2.f), H + FVector(4.f, -5.f, -11.f), 0.5f, 0.5f, 4, false);
			K.Tube(H + FVector(1.f, 9.f, 2.f), H + FVector(4.f, 5.f, -11.f), 0.5f, 0.5f, 4, false);
		};
		switch (O.Head)
		{
		case EHead::NonLa:
			K.Paint(O.HeadGear, HGSurf::Satin);
			K.Frustum(FTransform(H + FVector(0.f, 0.f, 7.f)), FVector2D(24.f), FVector2D(0.6f), 15.f, 18, true);
			K.Paint(FLC(0.4f, 0.32f, 0.2f));
			Straps();
			break;
		case EHead::Cap:
			K.Paint(O.HeadGear);
			K.Ellipsoid(H + FVector(-0.6f, 0.f, 3.f), FVector(10.4f, 9.4f, 8.6f), FRotator::ZeroRotator, 12, 5, 0.f);
			K.Box(H + FVector(10.f, 0.f, 3.4f), FVector(7.f, 14.f, 1.f), FRotator(-8.f, 0.f, 0.f));
			break;
		case EHead::Wrap:
			K.Paint(O.HeadGear);
			K.Frustum(FTransform(H + FVector(-0.8f, 0.f, 2.f)), FVector2D(10.4f, 9.6f), FVector2D(11.6f, 10.6f), 5.f, 14, false);
			K.Paint(O.HeadGear * 0.6f);
			K.Frustum(FTransform(H + FVector(-0.8f, 0.f, 7.f)), FVector2D(11.6f, 10.6f), FVector2D(10.8f, 9.8f), 4.f, 14, true);
			break;
		case EHead::HalfHelmet:
		case EHead::DuckBill:
			K.Paint(O.HeadGear, HGSurf::Gloss);
			K.Ellipsoid(H + FVector(-0.5f, 0.f, 2.5f), FVector(11.6f, 10.3f, 11.f), FRotator::ZeroRotator, 14, 6, -0.05f);
			if (O.Head == EHead::DuckBill)
			{
				// "Non vit": a long flat bill like a cap's, the classic yellow rental helmet.
				K.Box(H + FVector(14.f, 0.f, 3.f), FVector(10.f, 17.f, 1.2f), FRotator(-6.f, 0.f, 0.f));
				K.Ellipsoid(H + FVector(18.5f, 0.f, 2.5f), FVector(2.5f, 8.5f, 0.6f), FRotator(-6.f, 0.f, 0.f), 10, 3);
			}
			else
			{
				K.Paint(FLC(0.02f, 0.02f, 0.02f), HGSurf::Satin);
				K.Box(H + FVector(11.f, 0.f, 3.f), FVector(5.f, 15.f, 1.2f), FRotator(-12.f, 0.f, 0.f));
			}
			K.Paint(FLC(0.02f, 0.02f, 0.02f), HGSurf::Satin);
			Straps();
			break;
		case EHead::OpenHelmet:
			K.Paint(O.HeadGear, HGSurf::Gloss);
			K.Ellipsoid(H + FVector(-1.f, 0.f, 1.5f), FVector(12.f, 10.8f, 12.f), FRotator::ZeroRotator, 14, 7, -0.3f);
			K.Paint(FLC(0.02f, 0.02f, 0.02f), HGSurf::Satin);
			K.Box(H + FVector(11.f, 0.f, 6.5f), FVector(5.f, 16.f, 1.2f), FRotator(-15.f, 0.f, 0.f));
			Straps();
			break;
		case EHead::FullHelmet:
			K.Paint(O.HeadGear, HGSurf::Gloss);
			K.Ellipsoid(H + FVector(-0.5f, 0.f, 1.f), FVector(13.f, 11.6f, 13.f), FRotator::ZeroRotator, 14, 8, -0.75f);
			K.Paint(FLC(0.015f, 0.02f, 0.025f), HGSurf::Chrome);
			K.Ellipsoid(H + FVector(5.5f, 0.f, 1.f), FVector(8.f, 9.8f, 5.f), FRotator::ZeroRotator, 12, 5);
			break;
		default:
			break;
		}
	}

	void AddSkirt(FHGMeshKit& K, const FOutfit& O)
	{
		const float s = O.Scale;
		const float Z[5] = { 99.f * s, 88.f * s, 68.f * s, 59.f * s, 50.f * s };
		auto R = [&](float z) { const float t = (Z[0] - z) / (Z[0] - Z[4]); return FVector2D(FMath::Lerp(13.f, 26.f, t), FMath::Lerp(15.f, 28.f, t)) * O.Width; };
		for (int32 b = 0; b < 4; ++b)
		{
			K.Paint(O.Skirt[b]);
			K.Frustum(FTransform(FVector(0, 0, Z[b + 1])), R(Z[b + 1]), R(Z[b]), Z[b] - Z[b + 1], 24, b == 3, 0.07f);
		}
		K.Paint(FLC(0.85f, 0.8f, 0.6f));
		K.Frustum(FTransform(FVector(0, 0, 63.f * s)), R(63.f * s) * 1.075f, R(64.f * s) * 1.075f, 1.f * s, 24, false);
		K.Paint(FLC(0.015f, 0.015f, 0.04f));
		const float Flare = FMath::RadiansToDegrees(FMath::Atan2(13.f, Z[0] - Z[4]));
		K.Box(FVector(R(74.f * s).X * 1.08f + 1.f, 0.f, 74.f * s), FVector(1.2f, 20.f, 44.f * s), FRotator(Flare, 0.f, 0.f));
		K.Paint(FLC(0.55f, 0.05f, 0.12f));
		K.Box(FVector(R(53.f * s).X * 1.08f + 1.8f, 0.f, 53.f * s), FVector(1.4f, 20.f, 3.f), FRotator(Flare, 0.f, 0.f));
	}

	/** Core: hips, torso and outfit (+ legs when seated). */
	void BuildCore(FHGMeshKit& K, EHGPersonLook Look, EHGPersonPose Pose, int32 V, bool bScarf)
	{
		const FOutfit O = OutfitFor(Look, Pose, V);
		const FPoseJoints J = JointsFor(Pose, O);
		const bool bStanding = Pose == EHGPersonPose::Standing;
		if (!bStanding)
		{
			for (int32 s = 0; s < 2; ++s)
			{
				AddLeg(K, O, J.Hip[s], J.Knee[s], J.Ankle[s], false);
			}
		}
		else if (O.Legs == ELower::Skirt)
		{
			AddSkirt(K, O);
		}
		const FRotator Lean(-J.TorsoPitch, 0.f, 0.f);
		const FTransform Xf(Lean, J.TorsoBase);
		const float w = O.Width;
		const FVector2D R0(10.5f * w, 14.f * w), R1(11.f * w, 17.5f * w);
		auto At = [&](float Fwd, float Side, float Up) { return Xf.TransformPosition(FVector(Fwd, Side, Up)); };

		K.Paint(O.Legs == ELower::Skirt && bStanding ? O.Skirt[0] : O.Lower);
		K.Ellipsoid(J.Pelvis, FVector(11.f * w, 14.5f * w, 10.f), Lean, 10, 6);
		K.Paint(O.Top);
		K.Frustum(Xf, R0, R1, J.TorsoH, 14, false);
		K.Ellipsoid(At(0.f, 0.f, J.TorsoH - 2.f), FVector(10.5f * w, 19.f * w, 6.5f), Lean, 12, 6);
		if (O.bCollar)
		{
			K.Paint(O.Cuff);
			K.Box(At(R0.X + 0.6f, 3.f, J.TorsoH * 0.55f), FVector(1.2f, 2.2f, J.TorsoH * 0.9f), Lean);
			K.Box(At(-R1.X - 0.5f, 0.f, J.TorsoH - 7.f), FVector(1.4f, 24.f, 13.f), Lean + FRotator(-8.f, 0.f, 0.f));
			K.Paint(FLC(0.65f, 0.55f, 0.1f));
			K.Box(At(-R1.X - 0.9f, 0.f, J.TorsoH - 7.f), FVector(1.f, 18.f, 2.f), Lean + FRotator(-8.f, 0.f, 0.f));
		}
		if (O.bVest)
		{
			K.Paint(O.Vest);
			K.Frustum(FTransform(Lean, At(0.f, 0.f, 6.f)), R0 + FVector2D(1.2f), R1 + FVector2D(0.8f), J.TorsoH - 10.f, 14, false);
			K.Paint(FLC(0.7f, 0.7f, 0.68f), HGSurf::Gloss);
			for (const float Z : { 14.f, 26.f })
			{
				const FVector2D Rz = FMath::Lerp(R0, R1, Z / J.TorsoH) + FVector2D(1.5f);
				K.Frustum(FTransform(Lean, At(0.f, 0.f, Z)), Rz, Rz, 2.5f, 14, false);
			}
		}
		if (O.bStar)
		{
			K.Paint(FLC(1.f, 0.78f, 0.f));
			const FVector C = At(R0.X + 0.7f, 0.f, J.TorsoH * 0.62f);
			const FVector Fw = Xf.TransformVectorNoScale(FVector::ForwardVector);
			K.Tube(C - Fw * 0.4f, C + Fw * 0.4f, 6.f, 6.f, 5, true);
		}
		if (O.bBackpack && !bStanding)
		{
			// On the bike the big pack is strapped to the rack: a slim daypack only (the round-1 passenger's
			// 50 cm pack hid the whole rider in the chase view).
			K.Paint(O.Pack, HGSurf::Satin);
			const FVector C = At(-R1.X - 5.f, 0.f, J.TorsoH * 0.55f);
			K.Box(C, FVector(9.f, 24.f, 28.f), Lean);
			K.Ellipsoid(C + Xf.TransformVectorNoScale(FVector(0.f, 0.f, 14.f)), FVector(4.5f, 12.f, 4.f), Lean, 10, 4, 0.f);
			K.Paint(O.Pack * 0.5f, HGSurf::Satin);
			for (const float Y : { -9.f, 9.f })
			{
				K.Box(At(R0.X * 0.6f, Y, J.TorsoH - 6.f), FVector(16.f, 3.f, 1.5f), Lean + FRotator(-30.f, 0.f, 0.f));
			}
		}
		else if (O.bBackpack)
		{
			K.Paint(O.Pack, HGSurf::Satin);
			const FVector C = At(-R1.X - 8.f, 0.f, J.TorsoH * 0.5f);
			K.Box(C, FVector(15.f, 29.f, 38.f), Lean);
			K.Ellipsoid(C + Xf.TransformVectorNoScale(FVector(0.f, 0.f, 19.f)), FVector(7.5f, 14.5f, 6.f), Lean, 10, 4, 0.f);
			K.Paint(O.Pack * 0.5f, HGSurf::Satin);
			K.Box(C + Xf.TransformVectorNoScale(FVector(-8.f, 0.f, -6.f)), FVector(4.f, 22.f, 16.f), Lean);
			K.Paint(FLC(0.35f, 0.3f, 0.12f), HGSurf::Satin);
			K.Box(C + Xf.TransformVectorNoScale(FVector(0.f, 0.f, -21.f)), FVector(13.f, 31.f, 7.f), Lean);   // rolled mat
			K.Paint(O.Pack * 0.5f, HGSurf::Satin);
			for (const float Y : { -9.f, 9.f })
			{
				K.Box(At(R0.X * 0.6f, Y, J.TorsoH - 6.f), FVector(16.f, 3.f, 1.5f), Lean + FRotator(-30.f, 0.f, 0.f));
			}
		}
		if (O.bBasket && bStanding)
		{
			// Bamboo carrying basket (gui) on shoulder straps.
			K.Paint(FLC(0.38f, 0.27f, 0.13f), HGSurf::Satin);
			const FTransform BXf(Lean, At(-R1.X - 13.f, 0.f, 2.f));
			K.Frustum(BXf, FVector2D(9.f, 11.f), FVector2D(15.f, 17.f), 44.f, 16, true, 0.04f);
			K.Paint(FLC(0.25f, 0.17f, 0.08f));
			K.Frustum(FTransform(Lean, BXf.TransformPosition(FVector(0, 0, 41.f))), FVector2D(15.4f, 17.4f), FVector2D(15.6f, 17.6f), 3.f, 16, false);
			K.Paint(FLC(0.12f, 0.22f, 0.05f));
			K.Ellipsoid(BXf.TransformPosition(FVector(0, 0, 44.f)), FVector(13.f, 15.f, 6.f), Lean, 10, 4, 0.f);   // greens / firewood on top
			K.Paint(FLC(0.2f, 0.12f, 0.06f));
			for (const float Y : { -8.f, 8.f })
			{
				K.Box(At(0.f, Y, J.TorsoH - 1.f), FVector(24.f, 3.f, 1.2f), Lean);
			}
		}
		if (O.bBag)
		{
			// Tho cam bag on the left hip, strap across to the right shoulder.
			AddThoCamBag(K, At(5.f, -R0.Y - 2.6f, 2.f), Lean + FRotator(0.f, 0.f, 6.f), At(0.f, R1.Y - 3.f, J.TorsoH));
		}
		if (bScarf)
		{
			// Tho cam scarf knotted round the neck (the tail is a separate part that flaps).
			const FLC A = ThoCam[O.ScarfSeed % 5], B = ThoCam[(O.ScarfSeed + 2) % 5];
			const FVector N = J.NeckA + FVector(0.f, 0.f, 2.5f);
			K.Paint(A);
			K.Torus(FTransform(FRotator(0.f, 0.f, 90.f), N), 6.6f, 2.6f, 14, 6);
			K.Paint(B);
			K.Torus(FTransform(FRotator(0.f, 0.f, 90.f), N + FVector(0.f, 0.f, 3.f)), 6.f, 1.6f, 14, 5);
		}
	}

	void BuildScarfTail(FHGMeshKit& K, int32 Seed)
	{
		for (int32 Strip = 0; Strip < 2; ++Strip)
		{
			const float Y = Strip ? 2.8f : -2.8f;
			const float Len = Strip ? 30.f : 24.f;
			for (int32 i = 0; i < 6; ++i)
			{
				K.Paint(ThoCam[(Seed + i + Strip) % 6]);
				const float Z = -Len * (i + 0.5f) / 6.f;
				K.Box(FVector(0.f, Y, Z), FVector(0.8f, 6.f, Len / 6.f + 0.2f));
			}
			K.Paint(ThoCam[(Seed + 1) % 5]);
			K.Box(FVector(0.f, Y, -Len - 1.f), FVector(0.9f, 6.4f, 2.f));   // fringe
		}
	}
}

// ---------------------------------------------------------------------------

namespace HGPeople
{
	FHGPersonRig Spawn(AActor* Owner, USceneComponent* Parent, EHGPersonLook Look, EHGPersonPose Pose, int32 Variant,
		FName Name, const FVector& Location, bool bScarf)
	{
		Variant = Wrap(Variant);
		const FOutfit O = OutfitFor(Look, Pose, Variant);
		const FPoseJoints J = JointsFor(Pose, O);
		const FString Base = FString::Printf(TEXT("SM_HGPerson_%d_%d_%d"), int32(Look), int32(Pose), Variant);

		FHGPersonRig Rig;
		Rig.Pose = Pose;
		Rig.BodyBase = Location;
		Rig.IdleSeed = float(GetTypeHash(Name) % 1000) * 0.37f;
		UStaticMesh* Core = FHGMeshKit::Cached(*(Base + (bScarf ? TEXT("_CoreS") : TEXT("_Core"))),
			[&](FHGMeshKit& K) { BuildCore(K, Look, Pose, Variant, bScarf); });
		Rig.Body = FHGMeshKit::AddComponent(Owner, Parent, Core, Name, Location);
		if (!Rig.Body)
		{
			return Rig;
		}
		UStaticMesh* HeadMesh = FHGMeshKit::Cached(*(Base + TEXT("_Head")), [&](FHGMeshKit& K) { AddHead(K, O, J); });
		Rig.Head = FHGMeshKit::AddComponent(Owner, Rig.Body, HeadMesh, *(Name.ToString() + TEXT("_Head")), J.NeckA);
		UStaticMesh* ArmMesh = FHGMeshKit::Cached(*FString::Printf(TEXT("SM_HGPersonArm_%d_%d"), int32(Look), Variant),
			[&](FHGMeshKit& K) { AddArm(K, O, FVector::ZeroVector, ArmElbow, ArmWrist); });
		for (int32 s = 0; s < 2; ++s)
		{
			Rig.Arm[s] = FHGMeshKit::AddComponent(Owner, Rig.Body, ArmMesh, *(Name.ToString() + (s ? TEXT("_ArmR") : TEXT("_ArmL"))), J.Shoulder[s]);
			Rig.Rest[s] = (J.Wrist[s] - J.Shoulder[s]).GetSafeNormal();
		}
		if (Pose == EHGPersonPose::Standing)
		{
			const FVector Knee = J.Knee[0] - J.Hip[0], Ankle = J.Ankle[0] - J.Hip[0];
			UStaticMesh* LegMesh = FHGMeshKit::Cached(*FString::Printf(TEXT("SM_HGPersonLeg_%d_%d"), int32(Look), Variant),
				[&](FHGMeshKit& K) { AddLeg(K, O, FVector::ZeroVector, FVector(Knee.X, 0.f, Knee.Z), FVector(Ankle.X, 0.f, Ankle.Z), true); });
			for (int32 s = 0; s < 2; ++s)
			{
				Rig.Leg[s] = FHGMeshKit::AddComponent(Owner, Rig.Body, LegMesh, *(Name.ToString() + (s ? TEXT("_LegR") : TEXT("_LegL"))), J.Hip[s]);
			}
		}
		if (bScarf)
		{
			UStaticMesh* Tail = FHGMeshKit::Cached(*FString::Printf(TEXT("SM_HGScarfTail_%d"), O.ScarfSeed), [&](FHGMeshKit& K) { BuildScarfTail(K, O.ScarfSeed); });
			const FVector Nape = J.NeckA + FRotator(-J.TorsoPitch, 0.f, 0.f).RotateVector(FVector(-7.f, 0.f, 3.f));
			Rig.ScarfTail = FHGMeshKit::AddComponent(Owner, Rig.Body, Tail, *(Name.ToString() + TEXT("_Scarf")), Nape);
		}
		// Render-thread budget: people stop drawing at 150 m; only the core and the legs cast shadows (heads,
		// arms and scarves add a shadow draw per part per cascade for a few pixels of shadow).
		for (UStaticMeshComponent* C : { Rig.Head, Rig.Arm[0], Rig.Arm[1], Rig.ScarfTail })
		{
			if (C)
			{
				C->SetCastShadow(false);
			}
		}
		for (UStaticMeshComponent* C : { Rig.Body, Rig.Head, Rig.Arm[0], Rig.Arm[1], Rig.Leg[0], Rig.Leg[1], Rig.ScarfTail })
		{
			if (C)
			{
				C->SetCullDistance(15000.f);
			}
		}
		return Rig;
	}

	int32 VariantFor(const FString& Seed)
	{
		return int32(GetTypeHash(Seed) % uint32(NumVariants));
	}
}

void FHGPersonRig::SetVisible(bool bVisible)
{
	if (Body)
	{
		Body->SetVisibility(bVisible, true);
	}
}

void FHGPersonRig::Animate(AActor* Owner, float DeltaSeconds)
{
	if (!Body || !Body->IsVisible())
	{
		return;
	}
	Clock += DeltaSeconds;
	NearTimer -= DeltaSeconds;
	if (NearTimer <= 0.f)
	{
		NearTimer = 0.4f + FMath::Fmod(IdleSeed, 0.3f);
		bNear = true;
		if (APlayerCameraManager* Cam = Owner ? UGameplayStatics::GetPlayerCameraManager(Owner, 0) : nullptr)
		{
			const float D2 = FVector::DistSquared(Cam->GetCameraLocation(), Body->GetComponentLocation());
			bNear = D2 < FMath::Square(6000.f);
			bClose = D2 < FMath::Square(2000.f);
		}
	}
	if (!bNear)
	{
		return;
	}
	// Past 20 m the pose updates at half rate (each update re-sends every part's transform to the renderer).
	PendingDt += DeltaSeconds;
	if (!bClose && (++FrameCounter & 1))
	{
		return;
	}
	DeltaSeconds = PendingDt;
	PendingDt = 0.f;
	const float T = Clock + IdleSeed;
	FVector D[2] = { Rest[0], Rest[1] };
	float ArmRoll[2] = { 0.f, 0.f };
	float HeadPitch = 0.f, HeadYawNow = HeadYaw, HeadRoll = 0.f, BodyRoll = 0.f, BodyZ = 0.f;

	if (Pose == EHGPersonPose::Standing)
	{
		float LegPitch = 0.f;
		if (WalkSpeed > 5.f)
		{
			Phase += DeltaSeconds * WalkSpeed / 38.f;
			const float S = FMath::Sin(Phase);
			LegPitch = 26.f * S;
			D[0] = (Rest[0] + FVector(-0.42f * S, 0.f, 0.f)).GetSafeNormal();
			D[1] = (Rest[1] + FVector(0.42f * S, 0.f, 0.f)).GetSafeNormal();
			BodyZ = 1.6f * FMath::Abs(FMath::Cos(Phase));
			BodyRoll = 1.5f * S;
			HeadYawNow += 8.f * FMath::Sin(T * 0.4f);
		}
		else
		{
			// Idle: weight shift from foot to foot, look around now and then, hands drift.
			BodyRoll = 1.6f * FMath::Sin(T * 0.45f);
			HeadYawNow += 32.f * FMath::Sin(T * 0.21f) * FMath::Clamp(FMath::Sin(T * 0.07f + 1.f) * 2.f, -1.f, 1.f);
			HeadPitch = 4.f * FMath::Sin(T * 0.33f);
			D[0] = (Rest[0] + FVector(0.05f * FMath::Sin(T * 0.8f), 0.f, 0.f)).GetSafeNormal();
			D[1] = (Rest[1] + FVector(0.05f * FMath::Sin(T * 0.7f + 2.f), 0.f, 0.f)).GetSafeNormal();
		}
		for (int32 s = 0; s < 2; ++s)
		{
			if (Leg[s])
			{
				Leg[s]->SetRelativeRotation(FRotator(s ? -LegPitch : LegPitch, 0.f, 0.f));
			}
		}
		if (bPhoto)
		{
			D[1] = FVector(0.85f, -0.15f, 0.5f).GetSafeNormal();
			HeadPitch = 8.f;
			HeadYawNow = 0.f;
		}
	}
	else if (Pose == EHGPersonPose::Rider)
	{
		// The bar turns: inside grip comes back, outside grip goes forward; head looks into the bend.
		D[0] = (Rest[0] + FVector(-0.14f * Steer, 0.f, 0.f)).GetSafeNormal();
		D[1] = (Rest[1] + FVector(0.14f * Steer, 0.f, 0.f)).GetSafeNormal();
		HeadYawNow += 22.f * Steer + 4.f * FMath::Sin(T * 0.3f);
		HeadRoll = -LeanDeg * 0.55f;
		BodyRoll = LeanDeg * 0.12f;
	}
	else
	{
		if (bHoldOn)
		{
			D[0] = FVector(0.75f, 0.1f, -0.65f).GetSafeNormal();
			D[1] = FVector(0.75f, -0.1f, -0.65f).GetSafeNormal();
		}
		HeadYawNow += 35.f * FMath::Sin(T * 0.17f) * FMath::Clamp(FMath::Sin(T * 0.05f) * 3.f, -1.f, 1.f);
		HeadRoll = -LeanDeg * 0.4f;
		BodyRoll = -LeanDeg * 0.1f;
	}
	if (bWave)
	{
		D[1] = FVector(0.25f, 0.45f, 1.f).GetSafeNormal();
		ArmRoll[1] = 28.f * FMath::Sin(T * 9.f);
	}
	if (bArmsUp)
	{
		const float Shake = 10.f * FMath::Sin(T * 22.f);
		D[0] = FVector(0.1f, -0.35f, 1.f).GetSafeNormal();
		D[1] = FVector(0.1f, 0.35f, 1.f).GetSafeNormal();
		ArmRoll[0] = Shake;
		ArmRoll[1] = -Shake;
	}
	for (int32 s = 0; s < 2; ++s)
	{
		if (Arm[s])
		{
			FQuat Q = FQuat::FindBetweenNormals(ArmAxis, D[s]);
			if (ArmRoll[s] != 0.f)
			{
				Q = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(ArmRoll[s])) * Q;
			}
			Arm[s]->SetRelativeRotation(Q);
		}
	}
	if (Head)
	{
		Head->SetRelativeRotation(FRotator(-HeadPitch, FMath::Clamp(HeadYawNow, -70.f, 70.f), HeadRoll));
	}
	Body->SetRelativeLocationAndRotation(BodyBase + FVector(0.f, 0.f, BodyZ), FRotator(0.f, 0.f, BodyRoll));
	if (ScarfTail)
	{
		const float Flap = FMath::Sin(T * (6.f + 8.f * Wind)) * (5.f + 9.f * Wind);
		ScarfTail->SetRelativeRotation(FRotator(-(12.f + 66.f * Wind) + Flap, 10.f * FMath::Sin(T * 4.3f) * (0.3f + Wind), 0.f));
	}
}
