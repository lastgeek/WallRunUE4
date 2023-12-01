// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "WallRunHUD.generated.h"

UCLASS()
class AWallRunHUD : public AHUD
{
	GENERATED_BODY()

public:
	AWallRunHUD();

	/** Primary draw call for the HUD */
	virtual void DrawHUD() override;

private:
	/** Crosshair asset pointer */
	class UTexture2D* CrosshairTex;

};

