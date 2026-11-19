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
	enum class EHead : uint8 { Bare, NonLa, HalfHelmet, DuckBill, FullHelmet, OpenHelmet, Cap, Wrap, DaoRed };

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
		bool bBaby = false;         // baby carried on the back in a sling (Hmong mothers)
		bool bCollar = false;       // Hmong embroidered collar flap
		bool bEmbSleeve = false;    // Round 6B: bright embroidered bands on the forearms (Hmong / Dao women)
		bool bSash = false;         // Round 6B: cloth sash knotted at the waist (Hmong men)
		FLC SashColor = FLC(0.05f, 0.25f, 0.12f);
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
			O.bBasket = V == 0 || V == 3;
			O.bBaby = V == 2;
			O.bBag = V == 1 || V == 3;
			O.bCollar = true;
			O.bEmbSleeve = true;
			O.Top = (V == 1) ? FLC(0.012f, 0.012f, 0.016f) : FLC(0.02f, 0.025f, 0.075f);   // black or indigo
			O.Sleeve = O.Top;
			if (V == 3)
			{
				// Red Dao: black clothes, red embroidered collar band, the big red headdress with pom-poms.
				O.Top = FLC(0.012f, 0.012f, 0.015f);
				O.Sleeve = O.Top;
				O.Cuff = FLC(0.55f, 0.03f, 0.03f);
				O.Head = EHead::DaoRed;
				O.HeadGear = FLC(0.55f, 0.025f, 0.025f);
			}
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
			if (V & 1)
			{
				// Round 6B: Hmong man - black / indigo tunic and wide trousers, coloured sash.
				O.Top = V == 1 ? FLC(0.012f, 0.012f, 0.016f) : FLC(0.02f, 0.03f, 0.08f);
				O.Sleeve = O.Top;
				O.Cuff = O.Top;
				O.Lower = FLC(0.015f, 0.015f, 0.02f);
				O.bSash = true;
				O.SashColor = V == 1 ? FLC(0.05f, 0.28f, 0.12f) : FLC(0.5f, 0.05f, 0.08f);
			}
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
		case EHGPersonLook::HmongKid:
		{
			const FLC Tops[4] = { FLC(0.65f, 0.08f, 0.3f), FLC(0.05f, 0.35f, 0.15f), FLC(0.7f, 0.25f, 0.03f), FLC(0.06f, 0.15f, 0.5f) };
			O.Scale = 0.62f;
			O.Width = 0.72f;
			O.Skin = FLC(0.5f, 0.32f, 0.21f);
			O.Top = Tops[V];
			O.Sleeve = O.Top;
			O.Cuff = FLC(0.7f, 0.6f, 0.1f);
			O.Lower = FLC(0.03f, 0.035f, 0.1f);
			O.Shoes = FLC(0.1f, 0.12f, 0.15f);
			O.Legs = (V & 1) ? ELower::Trousers : ELower::Skirt;
			O.Head = (V == 2) ? EHead::Wrap : EHead::Bare;
			O.HeadGear = FLC(0.7f, 0.12f, 0.3f);
			break;
		}
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
		case EHGPersonPose::Squat:
			// Sitting on her heels: hips low, knees up, forearms resting on the knees.
			J.Pelvis = FVector(-8, 0, 40 * s);
			Set(J.Hip, FVector(-6, 10 * w, 40 * s));
			Set(J.Knee, FVector(22, 14 * w, 52 * s));
			Set(J.Ankle, FVector(6, 12 * w, 9));
			J.TorsoBase = FVector(-5, 0, 43 * s);
			J.TorsoPitch = 24.f;
			J.TorsoH = 44 * s;
			Set(J.Shoulder, FVector(12, 18.5f * w, 83 * s));
			Set(J.Wrist, FVector(30, 12 * w, 58 * s));
			J.NeckA = FVector(12, 0, 85 * s);
			J.NeckB = FVector(16, 0, 97 * s);
			J.HeadC = FVector(19, 0, 106 * s);
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
		if (O.bEmbSleeve)
		{
			// Hmong / Dao sleeves: stacked bright embroidered bands from the elbow to the cuff.
			const FLC Bands[5] = { FLC(0.6f, 0.03f, 0.05f), FLC(0.7f, 0.15f, 0.35f), FLC(0.7f, 0.5f, 0.03f), FLC(0.04f, 0.3f, 0.12f), FLC(0.05f, 0.1f, 0.45f) };
			for (int32 b = 0; b < 5; ++b)
			{
				const float T0 = 0.18f + b * 0.15f;
				K.Paint(Bands[(b + O.ScarfSeed) % 5]);
				K.Tube(FMath::Lerp(El, Wr, T0), FMath::Lerp(El, Wr, T0 + 0.1f), FMath::Lerp(4.4f, 3.6f, T0), FMath::Lerp(4.4f, 3.6f, T0 + 0.1f), 8, false);
			}
			K.Paint(FLC(0.6f, 0.03f, 0.05f));
			K.Tube(FMath::Lerp(Sh, El, 0.15f), FMath::Lerp(Sh, El, 0.25f), 5.3f, 5.1f, 8, false);   // shoulder band
		}
		if (!O.bShortSleeves)
		{
			K.Paint(O.Cuff);
			K.Tube(Wr - D * 5.f, Wr, 4.f, 3.8f, 8, false);
		}
		K.Paint(O.Skin);
		K.Ellipsoid(Wr + D * 3.8f, FVector(1.9f, 3.4f, 3.9f), AlongZ(D), 8, 5);                 // palm
		const FVector Side = FVector::CrossProduct(D, FVector::UpVector).GetSafeNormal();
		const FVector Palm = FVector::CrossProduct(Side, D).GetSafeNormal();
		K.Tube(Wr + D * 1.5f + Side * 2.4f, Wr + D * 5.5f + Side * 3.6f + Palm * 1.2f, 1.15f, 0.85f, 5, true);   // thumb
		// Round 6B: four slightly curled fingers (the mitten read as a mannequin hand).
		const float Len[4] = { 3.6f, 4.2f, 3.9f, 3.1f };
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector Base = Wr + D * 7.0f + Side * (1.7f - i * 1.15f);
			const FVector Mid = Base + D * Len[i] * 0.6f + Palm * 0.5f;
			K.Tube(Base, Mid, 0.72f, 0.66f, 5, false);
			K.Tube(Mid, Mid + D * Len[i] * 0.45f + Palm * 1.1f, 0.66f, 0.55f, 5, true);
		}
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
				// Hmong leg wraps (Round 6B): overlapping indigo turns spiralling down the shin, tied at the top and
				// the ankle with pale cords.
				for (int32 k = 0; k < 7; ++k)
				{
					const float T0 = 0.06f + k * 0.125f;
					K.Paint(k % 2 ? FLC(0.02f, 0.025f, 0.07f) : FLC(0.035f, 0.04f, 0.11f));
					const FVector A = FMath::Lerp(Knee, Ankle, T0), B = FMath::Lerp(Knee, Ankle, T0 + 0.11f);
					K.Tube(A + FVector(0.f, 0.f, 1.2f), B - FVector(0.f, 0.f, 1.2f), FMath::Lerp(6.4f, 5.f, T0), FMath::Lerp(6.3f, 4.9f, T0 + 0.1f), 8, false);
				}
				K.Paint(FLC(0.45f, 0.42f, 0.38f));
				K.Tube(FMath::Lerp(Knee, Ankle, 0.08f), FMath::Lerp(Knee, Ankle, 0.11f), 6.6f, 6.5f, 8, false);
				K.Tube(FMath::Lerp(Knee, Ankle, 0.9f), FMath::Lerp(Knee, Ankle, 0.93f), 5.1f, 5.f, 8, false);
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
		// Round 6B face (the round-2 egg + black dots read as a mannequin at 2-3 m): cranium + jaw, cheekbones,
		// shaded eye sockets with whites, irises and lid lines, a modelled nose with nostrils, two lips with a
		// mouth line, shaped ears with a dark inner bowl, arched two-part brows.
		const FLC Socket = O.Skin * 0.72f;
		const FLC Cheek = O.Skin * FLC(1.02f, 0.9f, 0.86f, 1.f);
		const FLC Lip = FLC(O.Skin.R * 0.62f, O.Skin.G * 0.33f, O.Skin.B * 0.31f, 1.f);
		K.Paint(O.Skin);
		K.Tube(FVector(0.f, 0.f, -2.f), NB, 5.f, 4.6f, 8, false);
		K.Ellipsoid(H + FVector(-0.6f, 0.f, 0.8f), FVector(9.3f, 8.2f, 10.6f), FRotator::ZeroRotator, 14, 10);   // cranium
		K.Ellipsoid(H + FVector(3.0f, 0.f, -5.2f), FVector(6.6f, 6.8f, 5.8f), FRotator(-8.f, 0.f, 0.f), 12, 6);  // jaw + chin
		K.Paint(Cheek);
		for (const float S : { -1.f, 1.f })
		{
			K.Ellipsoid(H + FVector(5.2f, S * 4.4f, -1.8f), FVector(2.0f, 2.2f, 1.6f), FRotator::ZeroRotator, 8, 5);
		}
		K.Paint(O.Skin);
		K.Ellipsoid(H + FVector(9.2f, 0.f, -0.9f), FVector(1.8f, 1.4f, 3.1f), FRotator(-10.f, 0.f, 0.f), 8, 5);   // nose bridge
		K.Ellipsoid(H + FVector(10.1f, 0.f, -3.3f), FVector(1.4f, 2.1f, 1.2f), FRotator::ZeroRotator, 8, 5);    // tip + wings
		K.Paint(O.Skin * 0.32f);
		for (const float S : { -1.f, 1.f })
		{
			K.Ellipsoid(H + FVector(10.0f, S * 0.9f, -4.15f), FVector(0.5f, 0.55f, 0.32f), FRotator::ZeroRotator, 5, 3);   // nostrils
		}
		for (const float S : { -1.f, 1.f })
		{
			K.Paint(Socket);
			K.Ellipsoid(H + FVector(7.1f, S * 3.3f, 1.5f), FVector(1.9f, 2.3f, 1.6f), FRotator::ZeroRotator, 8, 5);
			K.Paint(FLC(0.5f, 0.47f, 0.43f));
			K.Ellipsoid(H + FVector(7.9f, S * 3.3f, 1.5f), FVector(1.0f, 1.55f, 0.82f), FRotator::ZeroRotator, 8, 5);   // eye white
			K.Paint(FLC(0.028f, 0.016f, 0.01f));
			K.Ellipsoid(H + FVector(8.65f, S * 3.2f, 1.5f), FVector(0.42f, 0.72f, 0.72f), FRotator::ZeroRotator, 6, 4);  // iris
			K.Paint(FLC(0.012f, 0.01f, 0.009f));
			K.Box(H + FVector(8.45f, S * 3.3f, 2.25f), FVector(0.45f, 2.5f, 0.3f), FRotator(0.f, 0.f, S * 5.f));        // upper lid
			K.Paint(FLC(O.Hair.R * 1.2f + 0.008f, O.Hair.G * 1.2f + 0.006f, O.Hair.B * 1.2f + 0.005f, 1.f));
			K.Box(H + FVector(8.3f, S * 2.4f, 4.2f), FVector(1.0f, 1.9f, 0.85f), FRotator(0.f, 0.f, S * 4.f));       // brow (inner)
			K.Box(H + FVector(7.9f, S * 4.3f, 4.35f), FVector(1.0f, 2.2f, 0.65f), FRotator(0.f, 0.f, -S * 14.f));    // brow (outer, arched)
			K.Paint(O.Skin);
			K.Ellipsoid(H + FVector(-0.6f, S * 8.3f, -0.4f), FVector(2.3f, 1.1f, 3.4f), FRotator(0.f, -S * 14.f, 0.f), 8, 5);   // ear
			K.Paint(O.Skin * 0.55f);
			K.Ellipsoid(H + FVector(-0.2f, S * 9.0f, -0.4f), FVector(1.25f, 0.5f, 2.1f), FRotator(0.f, -S * 14.f, 0.f), 6, 4);
		}
		K.Paint(Lip);
		K.Ellipsoid(H + FVector(8.7f, 0.f, -5.5f), FVector(0.9f, 2.6f, 0.6f), FRotator::ZeroRotator, 8, 4);    // upper lip
		K.Ellipsoid(H + FVector(8.5f, 0.f, -6.6f), FVector(1.0f, 2.3f, 0.75f), FRotator::ZeroRotator, 8, 4);   // lower lip
		K.Paint(FLC(0.07f, 0.025f, 0.02f));
		K.Box(H + FVector(9.2f, 0.f, -6.05f), FVector(0.3f, 4.4f, 0.25f));                                     // mouth line
		const bool bHelmet = O.Head == EHead::FullHelmet || O.Head == EHead::OpenHelmet || O.Head == EHead::HalfHelmet || O.Head == EHead::DuckBill;
		if (!bHelmet)
		{
			K.Paint(O.Hair);
			K.Ellipsoid(H + FVector(-1.6f, 0.f, 1.6f), FVector(9.8f, 8.9f, 10.8f), FRotator::ZeroRotator, 12, 6, -0.05f);
			// Round 6B: hairline / fringe over the forehead and short sideburns (no bald egg at 2 m).
			K.Ellipsoid(H + FVector(5.2f, 0.f, 6.6f), FVector(4.6f, 7.6f, 2.6f), FRotator(-24.f, 0.f, 0.f), 10, 4);
			if (O.Head != EHead::Wrap && O.Head != EHead::DaoRed)
			{
				for (const float S : { -1.f, 1.f })
				{
					K.Box(H + FVector(3.6f, S * 7.7f, 1.6f), FVector(1.6f, 0.7f, 3.0f));   // short sideburns
				}
			}
			if (O.Head == EHead::Wrap || O.Head == EHead::DaoRed)
			{
				K.Ellipsoid(H + FVector(-9.2f, 0.f, -1.5f), FVector(4.0f, 5.2f, 4.4f), FRotator::ZeroRotator, 8, 5);   // bun under the wrap
			}
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
		{
			// Round 6B: the big wound head cloth of the Dong Van / Meo Vac Hmong women - several folded turns
			// (tilted rings), a checked band, the cloth end tucked at the side with tassels.
			K.Paint(O.HeadGear);
			K.Frustum(FTransform(H + FVector(-1.f, 0.f, 2.f)), FVector2D(10.6f, 9.8f), FVector2D(12.4f, 11.4f), 7.f, 16, true);
			for (int32 t = 0; t < 4; ++t)
			{
				K.Paint(t % 2 ? O.HeadGear * 0.82f : O.HeadGear);
				K.Torus(FTransform(FRotator((t % 2 ? 3.f : -2.5f), 0.f, 90.f), H + FVector(-1.f, 0.f, 3.f + t * 1.7f)),
					11.4f + (t == 1 ? 0.4f : 0.f), 1.9f, 16, 5);
			}
			K.Paint(FLC(0.6f, 0.58f, 0.52f));
			for (int32 c = 0; c < 10; ++c)
			{
				const float A = FMath::DegreesToRadians(c * 36.f + 18.f);
				K.Box(H + FVector(-1.f + FMath::Cos(A) * 12.6f, FMath::Sin(A) * 11.6f, 6.4f), FVector(1.f, 2.2f, 1.6f),
					FRotator(0.f, c * 36.f + 18.f, 0.f));                                                   // check squares
			}
			K.Paint(O.HeadGear * 0.8f);
			K.Box(H + FVector(-2.f, 11.5f, 4.f), FVector(6.f, 2.f, 9.f), FRotator(0.f, 0.f, 12.f));         // tucked end
			K.Paint(FLC(0.65f, 0.5f, 0.08f));
			for (int32 q = 0; q < 4; ++q)
			{
				K.Tube(H + FVector(-4.f + q * 1.8f, 12.6f, 0.f), H + FVector(-4.f + q * 1.8f, 13.2f, -6.f - (q % 2) * 2.f), 0.45f, 0.35f, 4, false);
			}
			break;
		}
		case EHead::DaoRed:
		{
			// Red Dao headdress: tall wound red cloth, a crown of red pom-poms and tassels down the back.
			K.Paint(O.HeadGear);
			K.Frustum(FTransform(H + FVector(-1.f, 0.f, 2.f)), FVector2D(10.8f, 10.f), FVector2D(13.5f, 12.5f), 12.f, 16, true);
			K.Paint(O.HeadGear * 1.2f);
			for (int32 q = 0; q < 10; ++q)
			{
				const float A = q * 2.f * PI / 10.f;
				K.Ellipsoid(H + FVector(-1.f + FMath::Cos(A) * 11.f, FMath::Sin(A) * 10.f, 14.5f), FVector(3.4f), FRotator::ZeroRotator, 6, 4);
			}
			K.Ellipsoid(H + FVector(-1.f, 0.f, 16.5f), FVector(4.f), FRotator::ZeroRotator, 6, 4);
			K.Paint(FLC(0.45f, 0.42f, 0.38f), HGSurf::Chrome);   // silver coins on the band
			for (int32 q = 0; q < 8; ++q)
			{
				const float A = (q - 3.5f) * 0.32f;
				K.Tube(H + FVector(-1.f + FMath::Cos(A) * 11.4f, FMath::Sin(A) * 10.6f, 4.f), H + FVector(-1.f + FMath::Cos(A) * 11.8f, FMath::Sin(A) * 11.f, 4.f), 1.2f, 1.2f, 6, true);
			}
			K.Paint(O.HeadGear);
			for (int32 q = 0; q < 5; ++q)
			{
				K.Tube(H + FVector(-11.f, -4.f + q * 2.f, 4.f), H + FVector(-13.f, -4.f + q * 2.f, -14.f), 0.6f, 0.5f, 4, false);
			}
			break;
		}
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
		// Round 6B: rows of small embroidered lozenges on the batik band and a cross-stitch row on the hem band.
		for (int32 Row = 0; Row < 2; ++Row)
		{
			const float Zr = (Row ? 55.f : 78.f) * s;
			const FVector2D Rr = R(Zr) * 1.085f;
			for (int32 i = 0; i < 20; ++i)
			{
				const float A = (i + 0.5f * Row) * 2.f * PI / 20.f;
				K.Paint((i + Row) % 3 == 0 ? FLC(0.7f, 0.6f, 0.08f) : (Row ? FLC(0.75f, 0.72f, 0.65f) : FLC(0.55f, 0.05f, 0.12f)));
				K.Box(FVector(FMath::Cos(A) * Rr.X, FMath::Sin(A) * Rr.Y, Zr), FVector(0.8f, 2.4f, 2.4f),
					FRotator(0.f, FMath::RadiansToDegrees(A), 45.f));
			}
		}
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
		if (Pose == EHGPersonPose::Squat)
		{
			// Flat bamboo tray of goods on the ground in front: greens, chillies, corn.
			K.Paint(FLC(0.4f, 0.29f, 0.14f), HGSurf::Satin);
			K.Frustum(FTransform(FVector(52.f, 0.f, 0.f)), FVector2D(24.f), FVector2D(30.f), 9.f, 16, true, 0.03f);
			K.Paint(FLC(0.08f, 0.22f, 0.04f));
			K.Ellipsoid(FVector(46.f, -8.f, 12.f), FVector(12.f, 10.f, 6.f), FRotator::ZeroRotator, 8, 4, 0.f);
			K.Paint(FLC(0.6f, 0.04f, 0.02f));
			K.Ellipsoid(FVector(58.f, 10.f, 11.f), FVector(8.f, 7.f, 4.f), FRotator::ZeroRotator, 8, 4, 0.f);
			K.Paint(FLC(0.75f, 0.5f, 0.06f));
			K.Ellipsoid(FVector(62.f, -12.f, 11.f), FVector(4.f, 10.f, 4.f), FRotator(0.f, 30.f, 0.f), 6, 4);
		}
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
		if (Pose == EHGPersonPose::Squat && O.Legs == ELower::Skirt)
		{
			// Skirt draped over the knees.
			K.Paint(O.Skirt[1]);
			K.Ellipsoid(FVector(6.f, 0.f, 44.f * O.Scale), FVector(26.f, 24.f * O.Width, 16.f), FRotator(-10.f, 0.f, 0.f), 14, 6);
			K.Paint(O.Skirt[2]);
			K.Frustum(FTransform(FVector(4.f, 0.f, 8.f)), FVector2D(26.f, 24.f), FVector2D(25.f, 23.f), 26.f * O.Scale, 18, false, 0.06f);
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
		// Round 6B cloth detail: side seams, a turned hem, soft folds at the waist, shoulder seams.
		{
			const FLC Seam = O.Top * 0.55f, Fold = O.Top * 0.75f;
			K.Paint(Seam);
			for (const float S : { -1.f, 1.f })
			{
				const float Ym = (R0.Y + R1.Y) * 0.5f + 0.15f;
				K.Box(At(0.f, S * Ym, J.TorsoH * 0.5f), FVector(0.9f, 0.5f, J.TorsoH * 0.92f), Lean);
				K.Box(At(0.f, S * 13.f * w, J.TorsoH + 2.4f), FVector(9.f, 0.5f, 0.5f), Lean + FRotator(0.f, 0.f, -S * 18.f));
			}
			K.Paint(Fold);
			K.Frustum(FTransform(Lean, At(0.f, 0.f, 0.5f)), R0 + FVector2D(0.5f), R0 + FVector2D(0.6f), 2.5f, 14, false);   // hem
			for (int32 k = 0; k < 3; ++k)
			{
				K.Box(At(R0.X + 0.15f, -6.f + k * 6.f, 7.f + (k % 2) * 2.5f), FVector(0.6f, 7.f, 0.7f), Lean + FRotator(0.f, 0.f, (k - 1) * 14.f));
			}
			if (!O.bCollar && Look != EHGPersonLook::Backpacker)
			{
				// Jacket placket with buttons and two chest pockets with flaps.
				K.Paint(O.Top * 0.6f);
				K.Box(At(R0.X + 0.5f, 0.6f, J.TorsoH * 0.5f), FVector(0.6f, 1.6f, J.TorsoH * 0.95f), Lean);
				K.Paint(FLC(0.12f, 0.1f, 0.08f), HGSurf::Satin);
				for (int32 b = 0; b < 4; ++b)
				{
					K.Ellipsoid(At(R0.X + 0.9f, 0.6f, 8.f + b * 9.f), FVector(0.5f, 0.8f, 0.8f), Lean, 5, 3);
				}
				if (Look == EHGPersonLook::Player || Look == EHGPersonLook::Police)
				{
					for (const float S : { -1.f, 1.f })
					{
						K.Paint(O.Top * 0.82f);
						K.Box(At(R1.X * 0.95f + 0.4f, S * 7.f, J.TorsoH * 0.72f), FVector(0.8f, 8.f, 8.f), Lean);
						K.Paint(O.Top * 0.6f);
						K.Box(At(R1.X * 0.95f + 0.9f, S * 7.f, J.TorsoH * 0.72f + 3.6f), FVector(0.6f, 8.4f, 2.2f), Lean);
					}
				}
			}
			if (O.bSash)
			{
				K.Paint(O.SashColor);
				K.Frustum(FTransform(Lean, At(0.f, 0.f, -4.f)), R0 + FVector2D(1.2f), R0 + FVector2D(1.f), 7.f, 14, false);
				K.Tube(At(R0.X * 0.6f, R0.Y * 0.55f, -2.f), At(R0.X * 0.75f, R0.Y * 0.6f, -26.f), 2.6f, 2.f, 6, true);   // hanging ends
				K.Tube(At(R0.X * 0.55f, R0.Y * 0.7f, -2.f), At(R0.X * 0.6f, R0.Y * 0.8f, -22.f), 2.4f, 1.8f, 6, true);
			}
			else if (O.Legs != ELower::Skirt)
			{
				// Belt with a buckle.
				K.Paint(FLC(0.03f, 0.022f, 0.016f), HGSurf::Satin);
				K.Frustum(FTransform(Lean, At(0.f, 0.f, -2.5f)), R0 + FVector2D(0.7f), R0 + FVector2D(0.7f), 3.f, 14, false);
				K.Paint(FLC(0.45f, 0.42f, 0.35f), HGSurf::Chrome);
				K.Box(At(R0.X + 1.f, 0.f, -1.f), FVector(0.6f, 3.4f, 2.6f), Lean);
			}
		}
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
			// Round 6B: real straps - over the shoulders and down the chest to the hip belt, a sternum strap with
			// a buckle, compression straps and a bottle in the side pocket.
			K.Paint(O.Pack * 0.45f, HGSurf::Satin);
			for (const float Y : { -9.5f, 9.5f })
			{
				const FVector Top = At(-R1.X - 1.f, Y, J.TorsoH - 1.f), Over = At(1.f, Y * 1.05f, J.TorsoH + 3.f);
				const FVector Chest = At(R1.X + 0.8f, Y * 0.95f, J.TorsoH * 0.62f), Hip = At(R0.X + 0.8f, Y * 1.2f, 4.f);
				K.Tube(Top, Over, 1.1f, 1.1f, 5, false);
				K.Tube(Over, Chest, 1.1f, 1.f, 5, false);
				K.Tube(Chest, Hip, 0.5f, 0.5f, 4, false);
				K.Box(C + Xf.TransformVectorNoScale(FVector(-7.8f, Y * 0.8f, 2.f)), FVector(1.f, 2.f, 30.f), Lean);   // compression strap
			}
			K.Box(At(R1.X + 1.f, 0.f, J.TorsoH * 0.66f), FVector(1.f, 19.f, 1.4f), Lean);                              // sternum strap
			K.Paint(FLC(0.02f, 0.02f, 0.02f), HGSurf::Satin);
			K.Box(At(R1.X + 1.6f, 0.f, J.TorsoH * 0.66f), FVector(1.2f, 3.2f, 2.f), Lean);
			K.Paint(FLC(0.08f, 0.22f, 0.45f), HGSurf::Gloss);
			K.Tube(C + Xf.TransformVectorNoScale(FVector(0.f, 15.5f, -14.f)), C + Xf.TransformVectorNoScale(FVector(0.f, 15.5f, 6.f)), 3.4f, 3.f, 8, true);  // bottle
		}
		if (O.bBaby && bStanding)
		{
			// Baby on the back in an embroidered carrier (sling knotted over the chest), head peeking out.
			const FTransform BXf(Lean, At(-R1.X - 9.f, 0.f, J.TorsoH * 0.45f));
			K.Paint(O.Skirt[2]);
			K.Ellipsoid(BXf.GetLocation(), FVector(11.f, 15.f, 18.f), Lean, 10, 6);
			K.Paint(O.Skirt[3]);
			K.Box(BXf.TransformPosition(FVector(-9.f, 0.f, 0.f)), FVector(2.f, 22.f, 26.f), Lean);
			K.Paint(O.Skin);
			K.Ellipsoid(BXf.TransformPosition(FVector(2.f, -5.f, 22.f)), FVector(6.5f, 6.f, 7.f), Lean, 8, 5);
			K.Paint(FLC(0.6f, 0.08f, 0.2f));
			K.Ellipsoid(BXf.TransformPosition(FVector(1.f, -5.f, 26.f)), FVector(7.f, 6.5f, 5.f), Lean, 8, 4, 0.f);   // baby's cap
			K.Paint(O.Skirt[2]);
			for (const float Y : { -9.f, 9.f })
			{
				K.Box(At(R0.X * 0.7f, Y, J.TorsoH - 10.f), FVector(18.f, 4.f, 1.5f), Lean + FRotator(-35.f, 0.f, 0.f));
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
			// Idle: weight shift from foot to foot, look around now and then, hands drift, breathing.
			BodyRoll = 1.6f * FMath::Sin(T * 0.45f);
			BodyZ = 0.35f * FMath::Sin(T * 1.7f);
			HeadYawNow += 32.f * FMath::Sin(T * 0.21f) * FMath::Clamp(FMath::Sin(T * 0.07f + 1.f) * 2.f, -1.f, 1.f);
			HeadPitch = 4.f * FMath::Sin(T * 0.33f);
			D[0] = (Rest[0] + FVector(0.05f * FMath::Sin(T * 0.8f), 0.f, 0.f)).GetSafeNormal();
			D[1] = (Rest[1] + FVector(0.05f * FMath::Sin(T * 0.7f + 2.f), 0.f, 0.f)).GetSafeNormal();
			// Round 6B: each person keeps one of a few natural rest poses (everybody standing with arms hanging
			// read as a mannequin crowd): hands behind the back, a hand on the chest / bag strap, arms folded,
			// or loose; plus a cocked-hip stance (weight on one leg).
			const int32 Style = int32(IdleSeed * 7.f) % 5;
			const float Drift = 0.03f * FMath::Sin(T * 0.6f);
			switch (Style)
			{
			case 1:   // hands behind the back
				D[0] = FVector(-0.5f + Drift, 0.22f, -1.f).GetSafeNormal();
				D[1] = FVector(-0.5f - Drift, -0.22f, -1.f).GetSafeNormal();
				break;
			case 2:   // right hand on the chest / strap
				D[1] = FVector(0.62f + Drift, -0.42f, -0.55f).GetSafeNormal();
				break;
			case 3:   // arms folded
				D[0] = FVector(0.66f, 0.5f, -0.42f + Drift).GetSafeNormal();
				D[1] = FVector(0.6f, -0.52f, -0.36f - Drift).GetSafeNormal();
				HeadPitch -= 3.f;
				break;
			default:
				break;
			}
			if (Style != 0)
			{
				BodyRoll += (int32(IdleSeed) & 1) ? 2.2f : -2.2f;   // weight on one leg
			}
		}
		// Relaxed knee on the free leg while standing (Round 6B).
		const float Relax = (WalkSpeed <= 5.f && (int32(IdleSeed * 7.f) % 5) != 0) ? 5.f : 0.f;
		const int32 Free = (int32(IdleSeed) & 1) ? 0 : 1;
		for (int32 s = 0; s < 2; ++s)
		{
			if (Leg[s])
			{
				Leg[s]->SetRelativeRotation(FRotator((s ? -LegPitch : LegPitch) + (s == Free ? Relax : 0.f), 0.f, s == Free ? (s ? 3.f : -3.f) * (Relax > 0.f) : 0.f));
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
