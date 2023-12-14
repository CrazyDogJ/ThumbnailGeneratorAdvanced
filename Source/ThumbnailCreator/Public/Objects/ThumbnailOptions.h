// Copyright (c) Panda Studios Comm. V.  - All Rights Reserves. Under no circumstance should this could be distributed, used, copied or be published without written approved of Panda Studios Comm. V. 

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailOptions.generated.h"


UCLASS(Transient)
class THUMBNAILCREATOR_API UThumbnailOptions : public UObject
{
	GENERATED_BODY()


public:
	//Screenshot X Size
	UPROPERTY(EditAnywhere, Category = "Setup")
		int32 ScreenshotXSize = 512;
	//Screenshot Y Size
	UPROPERTY(EditAnywhere, Category = "Setup")
		int32 ScreenshotYSize = 512;

	UPROPERTY(EditAnywhere, Category = "Setup")
		bool bUseAutoSize = true;

	UPROPERTY(EditAnywhere, Category = "Viewport")
		double ThumbnailPitch = -11.25;

	UPROPERTY(EditAnywhere, Category = "Viewport")
		double ThumbnailYaw = -137.5;

	UPROPERTY(EditAnywhere, Category = "Viewport")
		double ThumbnailZoom = 0;

	UPROPERTY(EditAnywhere, Category = "Viewport")
		int32 FOV = 30;
	
};
