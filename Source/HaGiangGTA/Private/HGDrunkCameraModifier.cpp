#include "HGDrunkCameraModifier.h"

#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "HGHappyWaterSubsystem.h"

bool UHGDrunkCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	Super::ModifyCamera(DeltaTime, InOutPOV);
	Time += DeltaTime;

	const UWorld* World = CameraOwner ? CameraOwner->GetWorld() : nullptr;
	const UHGHappyWaterSubsystem* HW = World ? World->GetSubsystem<UHGHappyWaterSubsystem>() : nullptr;
	const float I = HW ? HW->GetIntensity() : 0.f;
	if (I <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	InOutPOV.Rotation.Roll += 14.f * I * FMath::Sin(Time * 1.1f) + 4.f * I * FMath::Sin(Time * 2.9f);
	InOutPOV.Rotation.Pitch += 3.f * I * FMath::Sin(Time * 0.8f + 0.5f);
	InOutPOV.Rotation.Yaw += 2.5f * I * FMath::Sin(Time * 0.6f);
	InOutPOV.FOV = FMath::Clamp(InOutPOV.FOV + 9.f * I * FMath::Sin(Time * 0.7f), 40.f, 130.f);
	return false;
}
