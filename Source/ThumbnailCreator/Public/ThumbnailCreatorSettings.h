// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ThumbnailCreatorSettings.generated.h"

/**
 * 
 */
UCLASS(config = InventorySetting, DefaultConfig, NotPlaceable)
class THUMBNAILCREATOR_API UThumbnailCreatorSettings : public UObject
{
	GENERATED_BODY()

	UThumbnailCreatorSettings(const FObjectInitializer& obj);

public:
	/** If false will store thumbnail in current path */
	UPROPERTY(Config, EditAnywhere)
	bool bUseCustomPath;
	
	UPROPERTY(Config, EditAnywhere)
	FString ThumbnailTexturePath;
};
