#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "HGDrunkCameraModifier.generated.h"

/** Happy Water camera sway: rolling horizon, breathing FOV, drifting pitch. */
UCLASS()
class HAGIANGGTA_API UHGDrunkCameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

private:
	float Time = 0.f;
};
