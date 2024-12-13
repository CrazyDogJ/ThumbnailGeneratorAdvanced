// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "ThumbnailCreator.h"

//Engine
#include "LevelEditor.h"
#include "PreviewScene.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Runtime/Core/Public/Containers/Ticker.h"
#include "ContentBrowserModule.h"
#include "ObjectTools.h"
#include "UObject/SavePackage.h"

//Thumbnail Core
#include "Client/ThumbnailViewportClient.h"
#include "Objects/ThumbnailOptions.h"
#include "Runtime/Engine/Classes/Animation/AnimationAsset.h"
#include "ThumbnailCreatorCommands.h"
#include "ThumbnailCreatorStyle.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

//Image
#include "Runtime/Core/Public/Misc/FileHelper.h"
#include "Runtime/Engine/Classes/EditorFramework/AssetImportData.h"
#include "Editor/UnrealEd/Classes/Factories/TextureFactory.h"
#include "Engine/Texture2D.h"
#include "Runtime/Core/Public/HAL/FileManager.h"
#include "Runtime/ImageWrapper/Public/IImageWrapper.h"
#include "Runtime/ImageWrapper/Public/IImageWrapperModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Runtime/Core/Public/Misc/Paths.h"

//Slate
#include "ISettingsModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSplitter.h"
#include "Slate/SThumbnailViewport.h"
#include "PropertyEditorModule.h"
#include "ThumbnailCreatorSettings.h"
#include "Editor/AdvancedPreviewScene/Public/SAdvancedPreviewDetailsTab.h"
#include "Widgets/Images/SImage.h"
#include "Editor/PropertyEditor/Public/IDetailsView.h"

static const FName ThumbnailCreatorTabName("ThumbnailCreator");

#define LOCTEXT_NAMESPACE "FThumbnailCreatorModule"

void FThumbnailCreatorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FThumbnailCreatorStyle::Initialize();
	FThumbnailCreatorStyle::ReloadTextures();

	FThumbnailCreatorCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FThumbnailCreatorCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FThumbnailCreatorModule::PluginButtonClicked),
		FCanExecuteAction());
		
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	
	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension("WindowLayout", EExtensionHook::After, PluginCommands, FMenuExtensionDelegate::CreateRaw(this, &FThumbnailCreatorModule::AddMenuExtension));

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}
	
	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, PluginCommands, FToolBarExtensionDelegate::CreateRaw(this, &FThumbnailCreatorModule::AddToolbarExtension));
		
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}
	
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ThumbnailCreatorTabName, FOnSpawnTab::CreateRaw(this, &FThumbnailCreatorModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FThumbnailCreatorTabTitle", "ThumbnailCreator"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);


	//Setup a timer every 0.03 seconds to crete a new image or process taken image
	ImageTickDelegate = FTickerDelegate::CreateRaw(this, &FThumbnailCreatorModule::NextInQueue);
	FTSTicker::GetCoreTicker().AddTicker(ImageTickDelegate, 0.03f);


	//Get all startup images to exclude from processing
	IFileManager& FileManager = IFileManager::Get();
	
	TArray<FString> StartUpShort;
	FileManager.FindFiles(StartUpShort, *Path);
	for (FString Short : StartUpShort)
	{
		//Add path because Short is just the name, we still need to add the path to the string
		StartupImages.Add(Path + Short);
	}

	//THIS IS NEEDED, if you don't do this you will crash the engine upon shutdown
	FCoreDelegates::OnPreExit.AddLambda([this]()
	{
		if (!ViewportPtr.IsValid())
			return;

		ViewportPtr->PreviewScene.Reset(); 
		ViewportPtr->TypedViewportClient.Reset(); 
		ViewportPtr.Reset();
	});

	AddContentBrowserContextMenuExtender();
	
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "ThumbnailCreator", LOCTEXT("RuntimeSettingsName", "ThumbnailCreator"), LOCTEXT("RuntimeSettingsDescription", "Configure Thumbnail Creator"), GetMutableDefault<UThumbnailCreatorSettings>());
	}
}

void FThumbnailCreatorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.


	FThumbnailCreatorStyle::Shutdown();

	FThumbnailCreatorCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ThumbnailCreatorTabName);

	RemoveContentBrowserContextMenuExtender();
	
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "ThumbnailCreator");
	}
}

void FThumbnailCreatorModule::PreUnloadCallback()
{
	
	IModuleInterface::PreUnloadCallback();
}

TSharedRef<SDockTab> FThumbnailCreatorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{

	//Create the new SThumbnailViewport for this tab
	ViewportPtr = SNew(SThumbnailViewport);

	//Setup thumbnail options property details view settings
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.bCustomNameAreaLocation = true;
	Args.bShowObjectLabel = false;
	Args.bHideSelectionTip = false;
	Args.bShowScrollBar = false;

	DetailsView = PropertyModule.CreateDetailView(Args);

	//Create advanced scene preview
	FAdvancedPreviewSceneModule& AdvancedPreviewSceneModule = FModuleManager::LoadModuleChecked<FAdvancedPreviewSceneModule>("AdvancedPreviewScene");
	TSharedRef<SWidget> PreviewDetails = AdvancedPreviewSceneModule.CreateAdvancedPreviewSceneSettingsWidget(ViewportPtr->PreviewScene.ToSharedRef());

	//Create thumbnmail options object
	if (!ThumbnailOptions)
	{
		ThumbnailOptions = NewObject<UThumbnailOptions>(GetTransientPackage(), UThumbnailOptions::StaticClass());
		ThumbnailOptions->AddToRoot();
	}

	auto var = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			// Put your tab content here!

			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+SSplitter::Slot().Value(0.25f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(5, 0, 5, 10)
				.HAlign(HAlign_Fill).VAlign(VAlign_Top).AutoHeight()
				[
					SNew(SButton)
					.OnClicked_Raw(this, &FThumbnailCreatorModule::GenerateFromSelection)
					[
						SNew(STextBlock).Text(FText::FromString("Generate Content Selection"))
					]
				]
				+ SVerticalBox::Slot()
				.Padding(5, 0, 5, 10)
				.HAlign(HAlign_Fill).VAlign(VAlign_Top).AutoHeight()
				[
					SNew(SButton)
					.OnClicked_Raw(this, &FThumbnailCreatorModule::GenerateView)
					[
						SNew(STextBlock).Text(FText::FromString("Generate Current View"))
					]
				]
				+ SVerticalBox::Slot()
				.Padding(5, 0, 5, 10)
				.HAlign(HAlign_Fill).VAlign(VAlign_Top).AutoHeight()
				[
					SNew(SButton)
					.OnClicked_Raw(this, &FThumbnailCreatorModule::PreviewSelected)
					[
						SNew(STextBlock).Text(FText::FromString("Preview Content Selected Mesh"))
					]
				]
				+ SVerticalBox::Slot()
				.Padding(5, 0, 5, 10)
				.HAlign(HAlign_Fill).VAlign(VAlign_Top).AutoHeight()
				[
					SNew(SButton)
					.OnClicked_Raw(this, &FThumbnailCreatorModule::UpdateViewportTransform)
					[
						SNew(STextBlock).Text(FText::FromString("Update Viewport Transform"))
					]
				]
				+ SVerticalBox::Slot()
				.Padding(5, 0, 5, 10)
				.HAlign(HAlign_Fill).VAlign(VAlign_Fill).AutoHeight()
				[
					DetailsView->AsShared()
				]
				+ SVerticalBox::Slot()
				.Padding(5, 0, 5, 10)
				.HAlign(HAlign_Fill).VAlign(VAlign_Fill).FillHeight(1)
				[
					SNew(SBox)
					.WidthOverride(250.f)
					[
						PreviewDetails->AsShared()
					]
				]
			]
		+ SSplitter::Slot()
			[
				SNew(SBox)
				.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.FillWidth(1)
			.Padding(5, 10, 5, 0)
			[
				SNew(SBox)
				[
					ViewportPtr.ToSharedRef()
				]
			]
			]
			]
		];

	//Set th objects for the details view
	DetailsView->SetObject(ThumbnailOptions, true);

	//Set the thumbnail options object in the viewport client
	ViewportPtr.Get()->GetViewportClient()->ThumbnailOptions = ThumbnailOptions;

	return var;
}

void FThumbnailCreatorModule::PluginButtonClicked()
{
	//FGlobalTabmanager::Get()->InvokeTab(ThumbnailCreatorTabName);
	FGlobalTabmanager::Get()->TryInvokeTab(ThumbnailCreatorTabName);
}

void FThumbnailCreatorModule::AddMenuExtension(FMenuBuilder& Builder)
{
	Builder.AddMenuEntry(FThumbnailCreatorCommands::Get().OpenPluginWindow);
}

void FThumbnailCreatorModule::AddContentBrowserContextMenuExtender()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuAssetExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();

	CBMenuAssetExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateStatic(&OnExtendContentBrowserAssetSelectionMenu));
	ContentBrowserExtenderDelegateHandle = CBMenuAssetExtenderDelegates.Last().GetHandle();
}

void FThumbnailCreatorModule::RemoveContentBrowserContextMenuExtender()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBMenuExtenderDelegates.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& Delegate) { return Delegate.GetHandle() == ContentBrowserExtenderDelegateHandle; });
}

TSharedRef<FExtender> FThumbnailCreatorModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();
	Extender->AddMenuExtension(
		"CommonAssetActions",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateStatic(&ExecuteSaveThumbnailAsTexture, SelectedAssets)
	);
	return Extender;
}

void FThumbnailCreatorModule::ExecuteSaveThumbnailAsTexture(FMenuBuilder& MenuBuilder, const TArray<FAssetData> SelectedAssets)
{
	// A mix of https://forums.unrealengine.com/t/copy-asset-thumbnail-to-new-texture2d/138054/4
	// and https://isaratech.com/save-a-procedurally-generated-texture-as-a-new-asset/
	// and https://arrowinmyknee.com/2020/08/28/asset-right-click-menu-in-ue4/
	MenuBuilder.BeginSection("ThumbnailCreator", LOCTEXT("ASSET_CONTEXT", "ThumbnailCreator"));
	{
		// Add Menu Entry Here
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ButtonName", "Export to Texture"),
			LOCTEXT("Button ToolTip",
			        "Will export asset's thumbnail and put it in setting's folder"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([SelectedAssets]()
			{
				for (const FAssetData& Asset : SelectedAssets)
				{
					int32 pathSeparatorIdx;
					FAssetData obj = Asset;
					FString GamePath = obj.GetAsset()->GetPathName();
					FString AssetName;
					int32 pathEnd;
					if (GamePath.FindLastChar('/', pathEnd))
					{
						++pathEnd;
						AssetName += GamePath;
						AssetName.RightChopInline(pathEnd);
						int32 extensionIdx;
						if (AssetName.FindChar('.', extensionIdx))
						{
							AssetName.LeftInline(extensionIdx);
						}
						GamePath.LeftInline(pathEnd);
						FString Suffix = "T_";
						FString NameWithSuffix = Suffix + AssetName;
						AssetName = NameWithSuffix;
					}
					else
					{
						AssetName = "T_Thumbnail";
					}

					if (AssetName.FindChar('/', pathSeparatorIdx))
					{
						// TextureName should not have any path separators in it
						return;
					}

					FObjectThumbnail* thumb = ThumbnailTools::GenerateThumbnailForObjectToSaveToDisk(obj.GetAsset());
					if (!thumb)
					{
						return;
					}

					//Get settings folder
					FString PackageName;
					if (auto Settings = GetMutableDefault<UThumbnailCreatorSettings>())
					{
						if (Settings->bUseCustomPath)
						{
							PackageName = Settings->ThumbnailTexturePath;
						}
						else
						{
							PackageName = GamePath;
						}
					}
					else
					{
						PackageName = TEXT("/Game/ProceduralTextures/");
					}
					
					if (!PackageName.EndsWith("/"))
					{
						PackageName += "/";
					}
					PackageName += AssetName;

					UPackage* Package = CreatePackage(*PackageName);
					Package->FullyLoad();

					UTexture2D* NewTexture = NewObject<UTexture2D>(Package, *AssetName,
					                                               RF_Public | RF_Standalone | RF_MarkAsRootSet);
					NewTexture->AddToRoot();
					FTexturePlatformData* platformData = new FTexturePlatformData();
					platformData->SizeX = thumb->GetImageWidth();
					platformData->SizeY = thumb->GetImageHeight();
					//platformData->NumSlices = 1;
					platformData->PixelFormat = PF_B8G8R8A8;
					NewTexture->SetPlatformData(platformData);

					FTexture2DMipMap* Mip = new FTexture2DMipMap();
					platformData->Mips.Add(Mip);
					Mip->SizeX = thumb->GetImageWidth();
					Mip->SizeY = thumb->GetImageHeight();

					Mip->BulkData.Lock(LOCK_READ_WRITE);
					uint8* TextureData = Mip->BulkData.Realloc(thumb->GetUncompressedImageData().Num() * 4);
					FMemory::Memcpy(TextureData, thumb->GetUncompressedImageData().GetData(),
					                thumb->GetUncompressedImageData().Num());
					Mip->BulkData.Unlock();

					NewTexture->Source.Init(thumb->GetImageWidth(), thumb->GetImageHeight(), 1, 1, TSF_BGRA8,
					                        thumb->GetUncompressedImageData().GetData());
					NewTexture->LODGroup = TEXTUREGROUP_UI;
					NewTexture->UpdateResource();
					// ReSharper disable once CppExpressionWithoutSideEffects
					Package->MarkPackageDirty();
					Package->FullyLoad();
					FAssetRegistryModule::AssetCreated(NewTexture);

					FSavePackageArgs SaveArgs;
					SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
					SaveArgs.SaveFlags = SAVE_NoError;
					SaveArgs.bForceByteSwapping = true;
					FString PackageFileName = FPackageName::LongPackageNameToFilename(
						PackageName, FPackageName::GetAssetPackageExtension());
					UPackage::SavePackage(Package, NewTexture, *PackageFileName, SaveArgs);
				}
			})),
			NAME_None,
			EUserInterfaceActionType::Button);
	}
	MenuBuilder.EndSection();
}

FReply FThumbnailCreatorModule::GenerateFromSelection()
{
	//Get content selection
	TArray<FAssetData> Selection;
	GEditor->GetContentBrowserSelections(Selection);

	//Set selection as queue
	Queue = Selection;

	return FReply::Handled();
}

FReply FThumbnailCreatorModule::PreviewSelected()
{
	//Get content selection
	TArray<FAssetData> Selection;
	GEditor->GetContentBrowserSelections(Selection);

	//For all selected set the mesh in the viewport.. generally only the latest in selection will be viewed if you select more than one
	for (FAssetData _Data : Selection)
	{
		AssignAsset(_Data, false);
	}
	return FReply::Handled();

}

FReply FThumbnailCreatorModule::GenerateView()
{
	//Take a screnshot of the current view
	ViewportPtr->GetViewportClient()->TakeSingleShot();
	return FReply::Handled();
}

FReply FThumbnailCreatorModule::UpdateViewportTransform()
{
	const auto Options = ViewportPtr->GetViewportClient()->ThumbnailOptions;
	ViewportPtr->GetViewportClient()->UpdateViewportTransform(Options->FOV, Options->ThumbnailPitch, Options->ThumbnailYaw, Options->ThumbnailZoom);
	return FReply::Handled();
}

bool FThumbnailCreatorModule::NextInQueue(float Delta)
{
	//If we have a queue(FAssetData)
	if (Queue.Num() > 0)
	{
		//Take the firs tin queue
		FAssetData _Data = Queue[0];
		//Delete from queue
		Queue.RemoveAt(0);
		AssignAsset(_Data, true);

	}
	//If we have created images...
	else if(CreatedImages.Num() > 0)
	{
		TArray<uint8> RawImage;

		//get first created image and remove from queu
		FString pngfile = CreatedImages[0];
		CreatedImages.RemoveAt(0);

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		// Note: PNG format.  Other formats are supported
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

		if (FFileHelper::LoadFileToArray(RawImage, *pngfile))
		{
			if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(RawImage.GetData(), RawImage.Num()))
			{
				TArray64<uint8> UncompressedBGRA;
				if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBGRA))
				{
					// Setup packagename
					FString AssetName = pngfile.RightChop(pngfile.Find("/", ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1);
					FString USeAssetName = AssetName.LeftChop(AssetName.Len() - AssetName.Find( ".png", ESearchCase::IgnoreCase, ESearchDir::FromEnd));

					FString Folder = "/Game/ThumbnailExports/";
					if (auto Settings = GetMutableDefault<UThumbnailCreatorSettings>())
					{
						if (Settings->bUseCustomPath)
						{
							Folder = Settings->ThumbnailTexturePath;
						}
					}
					FString PackageName = Folder + USeAssetName;
					// Create new UPackage from PackageName
					UPackage* Package = CreatePackage(*PackageName);
					//Try to get the old package if this image already exists
					UPackage* OldPackage = LoadPackage(NULL, *PackageName,0);

					// Create Texture2D Factory
					auto TextureFact = NewObject<UTextureFactory>();
					TextureFact->AddToRoot();
					TextureFact->SuppressImportOverwriteDialog();

					// Get a pointer to the raw image data
					const uint8* PtrTexture = RawImage.GetData();

					// Stupidly use the damn factory
					UTexture2D* Texture = (UTexture2D*)TextureFact->FactoryCreateBinary(UTexture2D::StaticClass(), OldPackage? OldPackage : Package, *USeAssetName, RF_Standalone | RF_Public, NULL, TEXT("png"), PtrTexture, PtrTexture + RawImage.Num(), GWarn);
					
					if (Texture)
					{
						Texture->AssetImportData->Update(IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*USeAssetName));
						
						Package->SetDirtyFlag(true);
						TextureFact->RemoveFromRoot();

						//If we already have an old package we don't want to overwrite settings to defaults
						if (!OldPackage)
						{
							Texture->UpdateResource();
							//Set settings to fit with UI
							Texture->Filter = TextureFilter::TF_Trilinear;
							//2D pixels for UI gives clearest results
							Texture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
							//Add chroma key
							Texture->bChromaKeyTexture = true;
							Texture->ChromaKeyColor = FColor(0,255,0,0);
							Texture->UpdateResource();
						}
						//Notify new asset created or store in the old package
						if (OldPackage)
						{
							OldPackage->SetDirtyFlag(true);
						}
						else
						{
							FAssetRegistryModule::AssetCreated(Texture);
						}
					}
				}
			}
		}
	}
	//If none of the above
	else
	{
		TArray<FString> AllImages;

		//Get all images in teh screenshot folter
		IFileManager& FileManager = IFileManager::Get();
		FileManager.FindFiles(AllImages, *Path);
		for (FString Image : AllImages)
		{
			//Format full string
			FString Full = Path + Image;
			//If this image isn't in the startup(so wasn't known about before) we know it's new and we should process
			if (!StartupImages.Contains(Full))
			{
				//Add to startup images to prevent processing again
				StartupImages.Add(Full);
				CreatedImages.Add(Full);
			}
		}
	}

	return true;
}

//remove from startup images so we can process the image again
void FThumbnailCreatorModule::RemoveFromPreKnown(const FString ToRemove)
{
	FString ToUseString = Path  + "Thumb_" + ToRemove + ".png";
	StartupImages.Remove(ToUseString);
}

void FThumbnailCreatorModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddToolBarButton(FThumbnailCreatorCommands::Get().OpenPluginWindow);
}


void FThumbnailCreatorModule::AssignAsset(FAssetData _Data, bool bTakeShot)
{
	//Cast to static mesh
	UStaticMesh* Mesh = Cast<UStaticMesh>(_Data.GetAsset());
	if (Mesh)
	{
		//If static mesh, set the static mesh and take a screenshot
		ViewportPtr->SetMesh(Mesh, bTakeShot);

		return;
	}

	//If not a mesh... try a skeletal mesh
	USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(_Data.GetAsset());
	if (SkelMesh)
	{
		ViewportPtr->GetViewportClient()->SetSkelMesh(SkelMesh, nullptr, bTakeShot);

		return;
	}

	//If also not a skeletal mesh... try an animation
	UAnimationAsset* AnimationAsset = Cast<UAnimationAsset>(_Data.GetAsset());
	if (AnimationAsset)
	{
		ViewportPtr->GetViewportClient()->SetSkelMesh(AnimationAsset->GetSkeleton()->GetPreviewMesh(), AnimationAsset, bTakeShot);
		return;
	}

	//If also not a skeletal mesh... try an animation
	UMaterialInterface* MaterialAsset = Cast<UMaterialInterface>(_Data.GetAsset());
	if (MaterialAsset)
	{
		ViewportPtr->GetViewportClient()->SetMaterial(MaterialAsset, bTakeShot);
		return;
	}
}


#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FThumbnailCreatorModule, ThumbnailCreator)