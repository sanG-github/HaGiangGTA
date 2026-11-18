#include "HGOpenWorldGameMode.h"

#include "HGOpenWorldHUD.h"
#include "HGPlayerCharacter.h"

AHGOpenWorldGameMode::AHGOpenWorldGameMode()
{
	DefaultPawnClass = AHGPlayerCharacter::StaticClass();
	HUDClass = AHGOpenWorldHUD::StaticClass();
}
