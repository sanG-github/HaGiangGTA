#include "HGGreyboxGameMode.h"

#include "HGGreyboxCarPawn.h"
#include "HGGreyboxHUD.h"

AHGGreyboxGameMode::AHGGreyboxGameMode()
{
	DefaultPawnClass = AHGGreyboxCarPawn::StaticClass();
	HUDClass = AHGGreyboxHUD::StaticClass();
}
