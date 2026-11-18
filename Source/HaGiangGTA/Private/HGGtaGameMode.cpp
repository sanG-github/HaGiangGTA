#include "HGGtaGameMode.h"

#include "HGGtaHUD.h"
#include "HGPlayerCharacter.h"

AHGGtaGameMode::AHGGtaGameMode()
{
	DefaultPawnClass = AHGPlayerCharacter::StaticClass();
	HUDClass = AHGGtaHUD::StaticClass();
}
