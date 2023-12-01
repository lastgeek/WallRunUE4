// Copyright Epic Games, Inc. All Rights Reserved.

#include "WallRunGameMode.h"
#include "WallRunHUD.h"
#include "WallRunCharacter.h"
#include "UObject/ConstructorHelpers.h"

AWallRunGameMode::AWallRunGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPersonCPP/Blueprints/FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

	// use our custom HUD class
	HUDClass = AWallRunHUD::StaticClass();
}
