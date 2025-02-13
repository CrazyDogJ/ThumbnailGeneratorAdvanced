// Copyright (c) Panda Studios Comm. V.  - All Rights Reserves. Under no circumstance should this could be distributed, used, copied or be published without written approved of Panda Studios Comm. V. 
#include "Client/ThumbnailViewportClient.h"

//Thumbnail Core
#include "Objects/ThumbnailOptions.h"
#include "ThumbnailCreator.h"

//Image
#include "Runtime/Engine/Public/HighResScreenshot.h"

//Engine
#include "AssetEditorModeManager.h"
#include "EngineGlobals.h"

//Slate
#include "Slate/SThumbnailViewport.h"

//Components
#include "Components/SkeletalMeshComponent.h"
#include "Runtime/Engine/Classes/Components/PostProcessComponent.h"
#include "Runtime/Engine/Classes/Materials/MaterialInterface.h"

//Scene
#include "Runtime/Engine/Public/SceneView.h"
#include "Editor/AdvancedPreviewScene/Public/AdvancedPreviewScene.h"
#include "Runtime/Engine/Public/SceneManagement.h"
#include "Runtime/Engine/Classes/Materials/Material.h"
#include "Runtime/Engine/Public/PreviewScene.h"


FThumbnailViewportClient::FThumbnailViewportClient(const TSharedRef<SThumbnailViewport>& InThumbnailViewport, const TSharedRef<FAdvancedPreviewScene>& InPreviewScene) 
: FEditorViewportClient(nullptr, &InPreviewScene.Get(), StaticCastSharedRef<SEditorViewport>(InThumbnailViewport))
, ViewportPtr(InThumbnailViewport)
{

	AdvancedPreviewScene = static_cast<FAdvancedPreviewScene*>(PreviewScene);

	// Enable RealTime
	SetRealtime(true);

	// Hide grid, we don't need this.
	DrawHelper.bDrawGrid = false;
	DrawHelper.bDrawPivot = false;
	DrawHelper.AxesLineThickness = 5;
	DrawHelper.PivotSize = 5;

	EngineShowFlags.SetScreenPercentage(true);

	// Set the Default type to Ortho and the XZ Plane
	const ELevelViewportType NewViewportType = LVT_Perspective;
	FEditorViewportClient::SetViewportType(NewViewportType);
	
	// View Modes in Persp and Ortho
	SetViewModes(VMI_Lit, VMI_Lit);

	//Create the static mesh component
	MeshComp = ViewportPtr.Pin()->MeshComp;
	MeshComp->bRenderCustomDepth = true;

	MaterialComp = ViewportPtr.Pin()->MaterialComp;
	MaterialComp->bRenderCustomDepth = true;
	MaterialComp->SetVisibility(false);

	//Create skeletal mesh component
	SkelMeshComp = ViewportPtr.Pin()->SkelMeshComp;
	SkelMeshComp->bRenderCustomDepth = true;
	SkelMeshComp->SetVisibility(false);
	
	//Add our own post process on the world
	UPostProcessComponent* PostComp = ViewportPtr.Pin()->PostComp;

	//Add both components to the scene
	PreviewScene->AddComponent(MeshComp, FTransform(), false);
	PreviewScene->AddComponent(SkelMeshComp, FTransform(), false);
	PreviewScene->AddComponent(PostComp, FTransform(), false);
	PreviewScene->AddComponent(MaterialComp, FTransform(), false);

	//Create a new defaults cube
	UStaticMesh* NewMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), NULL, *(FString("StaticMesh'/Engine/VREditor/BasicMeshes/SM_Pyramid_01.SM_Pyramid_01'"))));
	UStaticMesh* MaterialMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), NULL, *(FString("StaticMesh'/Engine/EngineMeshes/MaterialSphere.MaterialSphere'"))));

	//If mesh is valid... set the mesh and render custom depth
	if (NewMesh)
	{
		MeshComp->SetStaticMesh(NewMesh);
	}

	if (MaterialMesh)
	{
		MaterialComp->SetStaticMesh(MaterialMesh);
	}

	//Allow post process materials...
	EngineShowFlags.SetPostProcessMaterial(true);
	EngineShowFlags.SetPostProcessing(true);

	//Force screen percentage higher
	PostComp->Settings.ScreenPercentage_DEPRECATED = 200;

	//Unbound
	PostComp->bUnbound = true;

	//Set that the mask is enabled for screenshots so it records transparency in the output
	GetHighResScreenshotConfig().bMaskEnabled = true;

	//Register components inside the array
	ActorComponents.Add(MeshComp);
	ActorComponents.Add(SkelMeshComp);
	ActorComponents.Add(MaterialComp);

	//Initiate view
	UpdateViewportTransform(30, -11.25, -137.5, 0);
}

void FThumbnailViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	// Tick the preview scene world.
	if (!GIntraFrameDebuggingGameThread)
	{
		if(AdvancedPreviewScene)
		AdvancedPreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
	}
}

void FThumbnailViewportClient::TakeSingleShot()
{
	//set the size of the screenshot
	GScreenshotResolutionX = ThumbnailOptions->ScreenshotXSize;
	GScreenshotResolutionY = ThumbnailOptions->ScreenshotYSize;

	//Set the name of the screenshot
	FString UseName = GetAssetName();

	GetHighResScreenshotConfig().FilenameOverride = FPaths::ProjectSavedDir() + "Thumbnails/Thumb_" + UseName;

	if (ThumbnailOptions->bUseAutoSize)
	{
		UpdateViewportTransform(ThumbnailOptions->FOV, ThumbnailOptions->ThumbnailPitch, ThumbnailOptions->ThumbnailYaw, ThumbnailOptions->ThumbnailZoom);
	}
	//Take the shots
	TakeHighResScreenShot();

	//Remove this image from known images so we can process it again
	auto ModulePtr = FModuleManager::LoadModulePtr<FThumbnailCreatorModule>(FName("ThumbnailCreator"));
	if (ModulePtr)
	{
		ModulePtr->RemoveFromPreKnown(UseName);
	}
}

void FThumbnailViewportClient::UpdateViewportTransform(const int32 FOV, const double Pitch, const double Yaw, const float Zoom)
{
	float radius = 0;
	FVector orbitPoint = FVector(0,0,0);
	if (MeshComp->IsVisible())
	{
		radius = MeshComp->Bounds.SphereRadius;
		orbitPoint = MeshComp->Bounds.Origin;
	}
	else if (SkelMeshComp->IsVisible())
	{
		radius = SkelMeshComp->Bounds.SphereRadius;
		orbitPoint = SkelMeshComp->Bounds.Origin;
	}
	ViewFOV = FOV;
	SetViewRotation(FRotator(Pitch, Yaw, 0));
	SetViewLocationForOrbiting(orbitPoint, radius*4+Zoom);
}

void FThumbnailViewportClient::ResetScene()
{
	//PreviewScene = NULL;
}

void FThumbnailViewportClient::SetMesh(class UStaticMesh* inMesh, bool bTakeShot)
{
	//Update static mesh to new one
	MeshComp->SetStaticMesh(inMesh);
	MeshComp->SetRenderCustomDepth(true);
	MeshComp->MarkRenderStateDirty();

	SetComponentVisibility(MeshComp, EScreenshotType::Mesh);

	//If we need to take a shot, take one
	if (bTakeShot)
	{
		TakeSingleShot();
	}


}

void FThumbnailViewportClient::SetSkelMesh(class USkeletalMesh* inMesh, class UAnimationAsset* AnimAsset /*= NULL*/, bool bTakeShot /*= false*/)
{
	if (inMesh || AnimAsset)
	{
		//Only update mesh if we have one
		if(inMesh)
		SkelMeshComp->SetSkeletalMesh(inMesh);

		//If our anim asset is valid and we have a skeletal mesh then play it
		if (AnimAsset && SkelMeshComp->GetSkinnedAsset())
		{
			SkelMeshComp->PlayAnimation(AnimAsset, true);
		}

		//Flip visibility
		MeshComp->SetVisibility(false);
		SkelMeshComp->SetVisibility(true);
		SkelMeshComp->UpdateBounds();
	}

	SetComponentVisibility(SkelMeshComp, EScreenshotType::Skeletal);

	//If we need to take a shot, take one
	if (bTakeShot)
	{
		TakeSingleShot();
	}
}

void FThumbnailViewportClient::SetMaterial(UMaterialInterface* inMaterial, bool bTakeShot /*= false*/)
{
	if (MaterialComp && inMaterial)
	{
		MaterialComp->SetMaterial(0, inMaterial);
	}

	SetComponentVisibility(MaterialComp, EScreenshotType::Material);

	if (bTakeShot)
	{
		TakeSingleShot();
	}
}

void FThumbnailViewportClient::SetComponentVisibility(UActorComponent* ComponentToActivate, EScreenshotType Type)
{
	//go over all components and only show the one we want to activate
	for (UPrimitiveComponent* Comp : ActorComponents)
	{
		Comp->SetVisibility(ComponentToActivate == Comp);
	}

	//Set the active type afterwards
	ActiveType = Type;
}


FString FThumbnailViewportClient::GetAssetName()
{
	if (ActiveType == EScreenshotType::Mesh)
	{
		FString Name;
		MeshComp->GetStaticMesh()->GetName(Name);
		return Name;
	}
	else if (ActiveType == EScreenshotType::Skeletal)
	{
		FString Name;
		SkelMeshComp->GetSkinnedAsset()->GetName(Name);
		return Name;
	}
	else
	{
		FString Name;
		MaterialComp->GetMaterial(0)->GetName(Name);
		return Name;
	}
}

