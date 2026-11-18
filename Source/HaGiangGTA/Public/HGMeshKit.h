#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class AActor;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Surface class of a part, stored in UV0.x and read by M_HG_VertexColor (materials.py):
 * roughness lerp(0.85, 0.22, x), metallic above 0.7. UV0.y = 1 tints the part with the MID "Tint".
 */
namespace HGSurf
{
	constexpr float Matte = 0.f;     // cloth, skin, tyres
	constexpr float Satin = 0.35f;   // leather, plastic, worn paint
	constexpr float Gloss = 0.6f;    // fresh paint, helmet shells
	constexpr float Chrome = 1.f;    // chrome / polished alloy
}

/**
 * Low-poly mesh kit: boxes, tubes, ellipsoids, pleated frusta and tori merged into ONE vertex-coloured
 * static mesh, built at runtime (UStaticMesh::BuildFromMeshDescriptions) and cached per archetype.
 *
 * The greybox people and bikes were 10-30 engine-shape components each (one draw per part, x4 with the
 * shadow cascades); a kit mesh is one draw however detailed it is, and every bike / person of the same
 * archetype shares the same mesh. Used by HGPeopleBuilder and HGBikeBuilder. No assets needed: the only
 * content is the generated material M_HG_VertexColor (falls back to the engine's VertexColorMaterial).
 */
struct HAGIANGGTA_API FHGMeshKit
{
	/** Paint for the following primitives. */
	FHGMeshKit& Paint(const FLinearColor& InColor, float InSurf = HGSurf::Matte, bool bInTint = false)
	{
		Color = InColor;
		Surf = InSurf;
		bTint = bInTint;
		return *this;
	}

	/** Box of Size (cm) centred at Center. */
	void Box(const FVector& Center, const FVector& Size, const FRotator& Rot = FRotator::ZeroRotator);
	/** Round tube from A to B (radii RA -> RB). */
	void Tube(const FVector& A, const FVector& B, float RA, float RB, int32 Sides = 8, bool bCaps = true);
	/** Ellipsoid; ZCut in [-1, 1] keeps only the part above that normalised height (helmet shells, caps). */
	void Ellipsoid(const FVector& Center, const FVector& Radii, const FRotator& Rot = FRotator::ZeroRotator,
		int32 Seg = 10, int32 Rings = 7, float ZCut = -1.f);
	/**
	 * Elliptic frustum along the local +Z of Xf from the base (radii R0) to H (radii R1). Pleat > 0 alternates
	 * the radius of every other side by that fraction with flat shading: a pleated skirt.
	 */
	void Frustum(const FTransform& Xf, const FVector2D& R0, const FVector2D& R1, float H, int32 Sides, bool bCaps = true,
		float Pleat = 0.f);
	/** Ring in the local XZ plane of Xf (axis = local Y): tyres, rims. */
	void Torus(const FTransform& Xf, float MajorR, float MinorR, int32 Seg = 16, int32 Sides = 6);

	int32 NumTriangles() const { return Indices.Num() / 3; }

	/** One static mesh (one material slot) from everything added so far. */
	UStaticMesh* Build(FName Name, UMaterialInterface* Material) const;

	/** Kit mesh for Key, built once by Fill and shared (rooted, transient). */
	static UStaticMesh* Cached(FName Key, TFunctionRef<void(FHGMeshKit&)> Fill);

	static UMaterialInterface* VertexColorMaterial();
	static UMaterialInterface* LampMaterial();

	/** Runtime component (transient, no collision) showing Mesh under Parent. */
	static UStaticMeshComponent* AddComponent(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, FName Name,
		const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator);

	/**
	 * Small emissive lamp (unlit M_HG_Lamp, MID parameters "Color" / "Intensity") built from an engine shape.
	 * Does not light anything: no shadow, no distance-field or Lumen contribution.
	 */
	static UStaticMeshComponent* AddLamp(AActor* Owner, USceneComponent* Parent, FName Name, const FVector& Location,
		const FVector& SizeCm, const FLinearColor& LampColor, float Intensity, bool bSphere = true);
	static void SetLampIntensity(UStaticMeshComponent* Lamp, float Intensity);

private:
	struct FVert
	{
		FVector3f P;
		FVector3f N;
		FVector4f C;
		FVector2f UV;
	};

	int32 AddVert(const FVector& P, const FVector& N);
	/** Triangle facing N (winding fixed up for UE's front face). */
	void Tri(int32 A, int32 B, int32 C, const FVector& N);
	void Quad(int32 A, int32 B, int32 C, int32 D, const FVector& N) { Tri(A, B, C, N); Tri(A, C, D, N); }

	TArray<FVert> Verts;
	TArray<uint32> Indices;
	FLinearColor Color = FLinearColor::White;
	float Surf = HGSurf::Matte;
	bool bTint = false;
};
