// Fill out your copyright notice in the Description page of Project Settings.


#include "ThumbnailCreatorSettings.h"

UThumbnailCreatorSettings::UThumbnailCreatorSettings(const FObjectInitializer& obj)
{
	bUseCustomPath = true;
	ThumbnailTexturePath = TEXT("/Game/Thumbnails/");
}
