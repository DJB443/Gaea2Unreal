// Fill out your copyright notice in the Description page of Project Settings.


#include "GaeaSubsystem.h"

#include "DesktopPlatformModule.h"
#include "FileHelpers.h"
#include "Widgets/SWindow.h"
#include "GWindow.h"
#include "LandscapeEditorObject.h"
#include "SlateBasics.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeRegionUtils.h"
#include "LandscapeStreamingProxy.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "LandscapeEditorUtils.h"
#include "LandscapeSubsystem.h"
#include "LocationVolume.h"
#include "ActorFactories/ActorFactory.h"
#include "ToolMenus.h"
#include "Builders/CubeBuilder.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "VT/VirtualTexture.h"
#include "GaeaLandscapeComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AsyncTreeDifferences.h"
#include "EditorAssetLibrary.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "LandscapeInfo.h"
#include "Editor.h"
#include "LandscapeEditLayer.h"
#include "LandscapeEdit.h"
#include "LandscapeDataAccess.h"


#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "WorldPartition/WorldPartition.h"


DEFINE_LOG_CATEGORY(GaeaSubsystem)

#define LOCTEXT_NAMESPACE "GaeaSubsystem"


UGaeaSubsystem* UGaeaSubsystem::GetGaeaSubsystem()
{
	return GEditor->GetEditorSubsystem<UGaeaSubsystem>();
}

void UGaeaSubsystem::SpawnGImporterWindow()
{
	if(ImporterWindowValidator.IsValid()==false)
	{
		const TSharedRef<SWindow> WindowRef = SNew(SGaeaImportWindow)
		.Title(FText::FromString("Gaea Landscape Importer"))
		.ClientSize(FVector2D(500, 385))
		.SizingRule(ESizingRule::UserSized);
		
		FSlateApplication::Get().AddWindow(WindowRef);
		ImporterWindowValidator = WindowRef;
		
	}
	else
	{
		ImporterWindowValidator.Pin()->BringToFront(true);
		
	}
}

void UGaeaSubsystem::ReimportGaeaTerrain()
{
	TArray<ULandscapeLayerInfoObject*> InfoObjects;
	TArray<FLandscapeImportDescriptor> WeightOutImportDescriptors;
	TArray<ELandscapeImportResult> WeightImportResults;
	TArray<FLandscapeImportLayerInfo> MaterialImportLayers;
	TArray<FText> WeightOutMessage;
	bool ReimportWeightmaps = false;
	TArray<uint8> WeightOutData;
	TArray<uint8> FinalWeightOutData;

	if (UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>())
	{
		const TArray<AActor*>& SelectedActors = ActorSubsystem->GetSelectedLevelActors();
		UGaeaLandscapeComponent* GaeaComponent = SelectedActors[0]->FindComponentByClass<UGaeaLandscapeComponent>();
		if (SelectedActors.Num() > 0 && GaeaComponent)
		{
			AActor* Actor = SelectedActors[0];
			ALandscape* Landscape = Cast<ALandscape>(Actor);
			// UE 5.7: Use HasLayersContent() instead of bCanHaveLayersContent
			bool EditLayers = Landscape->HasLayersContent();

			ULandscapeInfo* LandscapeActorInfo = Landscape->GetLandscapeInfo();

			FString JsonPath = GaeaComponent->DefinitionFilepath.FilePath;
			FString HeightPath = GaeaComponent->HeightmapFilepath.FilePath;

			bool bStatus = false;
			FString JsonMessage = "";
			const FGaeaJson GaeaDefinition = CreateStructFromJson(JsonPath, bStatus, JsonMessage);
		
			if(bStatus)
			{
				UE_LOG(GaeaSubsystem, Display, TEXT("ScaleX: %f, ScaleY: %f, Height: %f, Resolution: %d"), 
			   GaeaDefinition.ScaleX, GaeaDefinition.ScaleY, GaeaDefinition.Height, GaeaDefinition.Resolution);
				FVector LandscapeLocation = FVector(0,0,GaeaDefinition.Height*100/2);
				FVector LandscapeScale = FVector(GaeaDefinition.ScaleX * 100 / GaeaDefinition.Resolution,GaeaDefinition.ScaleY * 100 / GaeaDefinition.Resolution,GaeaDefinition.Height * 100 / 512);

				constexpr bool bSingleFile = true;
				FLandscapeImportDescriptor OutImportDescriptor;
				OutImportDescriptor.Scale = LandscapeScale;
				FText OutMessage;
				
				
				ULandscapeEditorObject* DefaultValueObject = ULandscapeEditorObject::StaticClass()->GetDefaultObject<ULandscapeEditorObject>();
				check(DefaultValueObject);
	
				int32 OutQuadsPerSection = DefaultValueObject->NewLandscape_QuadsPerSection;
				int32 OutSectionsPerComponent = DefaultValueObject->NewLandscape_SectionsPerComponent;
				FIntPoint OutComponentCount = DefaultValueObject->NewLandscape_ComponentCount;
				
				ELandscapeImportResult ImportResult = FLandscapeImportHelper::GetHeightmapImportDescriptor(HeightPath, bSingleFile, false, OutImportDescriptor, OutMessage);

				int32 DescriptorIndex = OutImportDescriptor.FileResolutions.Num() / 2;
				
				FLandscapeImportHelper::ChooseBestComponentSizeForImport(OutImportDescriptor.ImportResolutions[DescriptorIndex].Width, OutImportDescriptor.ImportResolutions[DescriptorIndex].Height, OutQuadsPerSection, OutSectionsPerComponent, OutComponentCount);
				
				TArray<uint16> ImportData;
				ImportResult = FLandscapeImportHelper::GetHeightmapImportData(OutImportDescriptor, DescriptorIndex, ImportData, OutMessage);

				const int32 QuadsPerComponent = OutSectionsPerComponent * OutQuadsPerSection;
				const int32 SizeX = OutComponentCount.X * QuadsPerComponent + 1;
				const int32 SizeY = OutComponentCount.Y * QuadsPerComponent + 1;

				TArray<uint16> FinalHeightData;
				FLandscapeImportHelper::TransformHeightmapImportData(ImportData, FinalHeightData, OutImportDescriptor.ImportResolutions[DescriptorIndex], FLandscapeImportResolution(SizeX, SizeY), ELandscapeImportTransformType::ExpandCentered);

				// UE 5.7: Use GetSectionBase() instead of LandscapeSectionOffset
				FIntRect ComponentsRect = Landscape->GetBoundingRect() + Landscape->GetSectionBase();
				const int32 CompSizeX = ComponentsRect.Width() + 1;
				const int32 CompSizeY = ComponentsRect.Height() + 1;

				FLandscapeEditDataInterface LandscapeEdit(Landscape->GetLandscapeInfo());
				
				if (EditLayers)
				{
					ULandscapeEditLayerBase* Layer = Landscape->GetEditLayer(0);
					LandscapeEdit.SetEditLayer(Layer->GetGuid()); 
				}
				
				if (LandscapeActorInfo && !GaeaComponent->WeightmapFilepaths.IsEmpty())
				{
					for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeActorInfo->Layers)
					{
						if (ULandscapeLayerInfoObject* LayerInfoObject = LayerSettings.LayerInfoObj)
						{
							InfoObjects.Add(LayerInfoObject);
						}
					}
					
					WeightOutMessage.AddDefaulted(InfoObjects.Num());
					WeightOutImportDescriptors.AddDefaulted(InfoObjects.Num());
					WeightImportResults.AddDefaulted(InfoObjects.Num());

					TArray<uint8> ClearWeightData;
					ClearWeightData.SetNum(CompSizeX * CompSizeY);
					FMemory::Memset(ClearWeightData.GetData(), 255, CompSizeX * CompSizeY);
					

					if (GaeaComponent->WeightmapFilepaths.Num() >= InfoObjects.Num() - 1)
					{
						LandscapeEdit.SetAlphaData(InfoObjects[0], ComponentsRect.Min.X, ComponentsRect.Min.Y, ComponentsRect.Max.X, ComponentsRect.Max.Y, ClearWeightData.GetData(), 0, ELandscapeLayerPaintingRestriction::None, true, false);
						
						for(int32 i = 1; i < InfoObjects.Num(); i++)
						{
							int32 WeightmapIndex = i - 1;
							if (WeightmapIndex < GaeaComponent->WeightmapFilepaths.Num())
							{
								// UE 5.7: Use GetLayerName() instead of direct LayerName access
								FLandscapeImportHelper::GetWeightmapImportDescriptor(GaeaComponent->WeightmapFilepaths[WeightmapIndex].FilePath, true, false, InfoObjects[i]->GetLayerName(), WeightOutImportDescriptors[i], WeightOutMessage[i]);
								FLandscapeImportHelper::GetWeightmapImportData(WeightOutImportDescriptors[i], DescriptorIndex, InfoObjects[i]->GetLayerName(), WeightOutData, WeightOutMessage[i]);
								FLandscapeImportHelper::TransformWeightmapImportData(WeightOutData, FinalWeightOutData, OutImportDescriptor.ImportResolutions[DescriptorIndex], FLandscapeImportResolution(SizeX, SizeY), ELandscapeImportTransformType::ExpandCentered);
								
								if(FinalWeightOutData.Num() == CompSizeX * CompSizeY)
								{
									LandscapeEdit.SetAlphaData(InfoObjects[i], ComponentsRect.Min.X, ComponentsRect.Min.Y, ComponentsRect.Max.X, ComponentsRect.Max.Y, FinalWeightOutData.GetData(), 0, ELandscapeLayerPaintingRestriction::None, true, false );
								}
								
							}
							Landscape->RequestLayersContentUpdateForceAll(Update_All);
						}
						
					}
						
					}

				if (FinalHeightData.Num() == CompSizeX * CompSizeY)
				{
					if(EditLayers)
					{
						LandscapeEdit.SetHeightData( ComponentsRect.Min.X, ComponentsRect.Min.Y, ComponentsRect.Max.X, ComponentsRect.Max.Y,
							FinalHeightData.GetData(),
							0,
							true,
							nullptr,
							nullptr,
							nullptr,
							false,
							nullptr,
							nullptr,
							true,
							true,
							true
							);
			
						
						Landscape->RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_Heightmap_All);
					}
					else
					{
						// UE 5.7: All landscapes should have edit layers now, but keeping fallback
						LandscapeEdit.SetHeightData( ComponentsRect.Min.X, ComponentsRect.Min.Y, ComponentsRect.Max.X, ComponentsRect.Max.Y,
							FinalHeightData.GetData(),
							0,
							true,
							nullptr,
							nullptr,
							nullptr,
							false,
							nullptr,
							nullptr,
							true,
							true,
							true
							);
					}
						
					Landscape->SetActorScale3D(LandscapeScale);
					
					}
				else
				{
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Original landscape resolution does not match the reimport texture resolution"));
					}
				}
				
				}
				
			
			}
		}
}

void UGaeaSubsystem::ReimportGaeaWPTerrain()
{
	TArray<ULandscapeLayerInfoObject*> InfoObjects;
	TArray<FLandscapeImportDescriptor> WeightOutImportDescriptors;
	TArray<ELandscapeImportResult> WeightImportResults;
	TArray<FText> WeightOutMessage;
	bool ReimportWeightmaps = false;
	TArray<uint8> WeightOutData;
	TArray<uint8> FinalWeightOutData;
	
	UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (ActorSubsystem)
	{
		const TArray<AActor*>& SelectedActors = ActorSubsystem->GetSelectedLevelActors();
		if (SelectedActors.Num() > 0)
		{
			AActor* Actor = SelectedActors[0];
			ALandscape* Landscape = Cast<ALandscape>(Actor);
			ULandscapeInfo* LandscapeActorInfo = Landscape->GetLandscapeInfo();
        	
			if (Landscape)
			{
				if (UGaeaLandscapeComponent* GaeaComponent = Actor->FindComponentByClass<UGaeaLandscapeComponent>())
				{
					FString JsonPath = GaeaComponent->DefinitionFilepath.FilePath;
					FString HeightPath = GaeaComponent->HeightmapFilepath.FilePath;

					bool bStatus = false;
					FString JsonMessage = "";
					const FGaeaJson GaeaDefinition = CreateStructFromJson(JsonPath, bStatus, JsonMessage);

					if(bStatus)
					{
						FVector LandscapeLocation = FVector(0, 0, GaeaDefinition.Height * 100 / 2);
						FVector LandscapeScale = FVector(
							GaeaDefinition.ScaleX * 100 / GaeaDefinition.Resolution,
							GaeaDefinition.ScaleY * 100 / GaeaDefinition.Resolution,
							GaeaDefinition.Height * 100 / 512
						);

						constexpr bool bSingleFile = true;
						FLandscapeImportDescriptor OutImportDescriptor;
						OutImportDescriptor.Scale = LandscapeScale;
						FText OutMessage;

						ELandscapeImportResult ImportResult = FLandscapeImportHelper::GetHeightmapImportDescriptor(HeightPath, bSingleFile, false, OutImportDescriptor, OutMessage);

						if (ImportResult == ELandscapeImportResult::Success)
						{
							int32 DescriptorIndex = OutImportDescriptor.FileResolutions.Num() / 2;

							ULandscapeEditorObject* DefaultValueObject = ULandscapeEditorObject::StaticClass()->GetDefaultObject<ULandscapeEditorObject>();
							check(DefaultValueObject);

							int32 OutQuadsPerSection = DefaultValueObject->NewLandscape_QuadsPerSection;
							int32 OutSectionsPerComponent = DefaultValueObject->NewLandscape_SectionsPerComponent;
							FIntPoint OutComponentCount = DefaultValueObject->NewLandscape_ComponentCount;

							FLandscapeImportHelper::ChooseBestComponentSizeForImport(
								OutImportDescriptor.ImportResolutions[DescriptorIndex].Width,
								OutImportDescriptor.ImportResolutions[DescriptorIndex].Height,
								OutQuadsPerSection,
								OutSectionsPerComponent,
								OutComponentCount
							);

							TArray<uint16> ImportData;
							ImportResult = FLandscapeImportHelper::GetHeightmapImportData(OutImportDescriptor, DescriptorIndex, ImportData, OutMessage);

							if (ImportResult == ELandscapeImportResult::Success)
							{
								TArray<uint16> FinalHeightData;

								const int32 QuadsPerComponent = OutSectionsPerComponent * OutQuadsPerSection;
								const int32 SizeX = OutComponentCount.X * QuadsPerComponent + 1;
								const int32 SizeY = OutComponentCount.Y * QuadsPerComponent + 1;

								FLandscapeImportHelper::TransformHeightmapImportData(
									ImportData,
									FinalHeightData,
									OutImportDescriptor.ImportResolutions[DescriptorIndex],
									FLandscapeImportResolution(SizeX, SizeY),
									ELandscapeImportTransformType::ExpandCentered
								);
                            	
								if (LandscapeActorInfo && !GaeaComponent->WeightmapFilepaths.IsEmpty())
								{
									for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeActorInfo->Layers)
									{
										if (ULandscapeLayerInfoObject* LayerInfoObject = LayerSettings.LayerInfoObj)
										{
											InfoObjects.Add(LayerInfoObject);
										}
									}
					
									WeightOutMessage.AddDefaulted(InfoObjects.Num());
									WeightOutImportDescriptors.AddDefaulted(InfoObjects.Num());
									WeightImportResults.AddDefaulted(InfoObjects.Num());
								}

								TArray<ALandscapeProxy*> AllProxies;
								Landscape->GetLandscapeInfo()->ForEachLandscapeProxy([&AllProxies](ALandscapeProxy* Proxy)
								{
									AllProxies.Add(Proxy);
									return true;
								});

								for (ALandscapeProxy* Proxy : AllProxies)
								{
									FLandscapeEditDataInterface LandscapeEdit(Proxy->GetLandscapeInfo());

									// UE 5.7: Use GetSectionBase() instead of LandscapeSectionOffset
									FIntRect ComponentsRect = Proxy->GetBoundingRect() + Proxy->GetSectionBase();
									const int32 CompSizeX = ComponentsRect.Width() + 1;
									const int32 CompSizeY = ComponentsRect.Height() + 1;
									
									TArray<uint16> ProxyHeightData;
									ProxyHeightData.SetNum(CompSizeX * CompSizeY);

									for (int32 Y = 0; Y < CompSizeY; ++Y)
									{
										for (int32 X = 0; X < CompSizeX; ++X)
										{
											int32 SrcX = ComponentsRect.Min.X + X;
											int32 SrcY = ComponentsRect.Min.Y + Y;
											int32 SrcIndex = SrcY * SizeX + SrcX;

											int32 DestIndex = Y * CompSizeX + X;
											if (FinalHeightData.IsValidIndex(SrcIndex) && ProxyHeightData.IsValidIndex(DestIndex))
											{
												ProxyHeightData[DestIndex] = FinalHeightData[SrcIndex];
											}
										}
									}

									if (ProxyHeightData.Num() == CompSizeX * CompSizeY)
									{
										// UE 5.7: Use HasLayersContent() instead of bCanHaveLayersContent
										if (Landscape->HasLayersContent())
										{
											ULandscapeEditLayerBase* Layer = Landscape->GetEditLayer(0);
											LandscapeEdit.SetEditLayer(Layer->GetGuid()); 
											
											LandscapeEdit.SetHeightData(
												ComponentsRect.Min.X, ComponentsRect.Min.Y,
												ComponentsRect.Max.X, ComponentsRect.Max.Y,
												ProxyHeightData.GetData(),
												0, true, nullptr, nullptr, nullptr,
												false, nullptr, nullptr,
												true, true, true
											);

											Landscape->RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_All);
										}
										else
										{
											LandscapeEdit.SetHeightData(
												ComponentsRect.Min.X, ComponentsRect.Min.Y,
												ComponentsRect.Max.X, ComponentsRect.Max.Y,
												ProxyHeightData.GetData(),
												CompSizeX, true, nullptr, nullptr, nullptr,
												false, nullptr, nullptr,
												true, true, true
											);
										}
										
										Proxy->SetActorScale3D(LandscapeScale);
									}
									else
									{
										if (GEngine)
										{
											GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Resolution mismatch for proxy."));
										}
									}
									
									// UE 5.7: Use HasLayersContent() instead of bCanHaveLayersContent
									if(Landscape->HasLayersContent() && InfoObjects.Num() > 0)
									{
										TArray<uint8> FirstLayerData;
										FirstLayerData.SetNum(CompSizeX * CompSizeY);
										FMemory::Memset(FirstLayerData.GetData(), 255, CompSizeX * CompSizeY);
										LandscapeEdit.SetAlphaData(InfoObjects[0],ComponentsRect.Min.X, ComponentsRect.Min.Y,ComponentsRect.Max.X, ComponentsRect.Max.Y,FirstLayerData.GetData(),0,ELandscapeLayerPaintingRestriction::None,true, false);
										
										for (int32 i = 1; i < InfoObjects.Num(); i++)
										{
											int32 WeightmapIndex = i - 1;

											if (WeightmapIndex < GaeaComponent->WeightmapFilepaths.Num())
											{
												// UE 5.7: Use GetLayerName() instead of direct LayerName access
												FLandscapeImportHelper::GetWeightmapImportDescriptor(GaeaComponent->WeightmapFilepaths[WeightmapIndex].FilePath, true, false, InfoObjects[i]->GetLayerName(), WeightOutImportDescriptors[i], WeightOutMessage[i]);
												FLandscapeImportHelper::GetWeightmapImportData(WeightOutImportDescriptors[i], DescriptorIndex, InfoObjects[i]->GetLayerName(), WeightOutData, WeightOutMessage[i]);
												FLandscapeImportHelper::TransformWeightmapImportData(WeightOutData, FinalWeightOutData, OutImportDescriptor.ImportResolutions[DescriptorIndex], FLandscapeImportResolution(SizeX, SizeY), ELandscapeImportTransformType::ExpandCentered);

												TArray<uint8> ProxyWeightData;
												ProxyWeightData.SetNum(CompSizeX * CompSizeY);

												for (int32 Y = 0; Y < CompSizeY; ++Y)
												{
													for (int32 X = 0; X < CompSizeX; ++X)
													{
														int32 SrcX = ComponentsRect.Min.X + X;
														int32 SrcY = ComponentsRect.Min.Y + Y;
														int32 SrcIndex = SrcY * SizeX + SrcX;

														int32 DestIndex = Y * CompSizeX + X;
														if (FinalWeightOutData.IsValidIndex(SrcIndex) && ProxyWeightData.IsValidIndex(DestIndex))
														{
															ProxyWeightData[DestIndex] = FinalWeightOutData[SrcIndex];
														}
													}
												}

												if (ProxyWeightData.Num() == CompSizeX * CompSizeY)
												{
													if (Landscape->HasLayersContent())
													{
														ULandscapeEditLayerBase* Layer = Landscape->GetEditLayer(0);
														LandscapeEdit.SetEditLayer(Layer->GetGuid()); 
														
														LandscapeEdit.SetAlphaData(
															InfoObjects[i], 
															ComponentsRect.Min.X, ComponentsRect.Min.Y,
															ComponentsRect.Max.X, ComponentsRect.Max.Y, 
															ProxyWeightData.GetData(),
															0, ELandscapeLayerPaintingRestriction::None, true, true
														);
													}
												}
												else
												{
													if (GEngine)
													{
														GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Original landscape resolution does not match the reimport texture resolution"));
													}
												}
											}
											
										}
									}
									else
									{
										UE_LOG(GaeaSubsystem, Warning, TEXT("Weightmap reimport only supported via edit layer path."));
									}
									
								}
							}
						}
					}
				}
			}
		}
	}
	
}

void UGaeaSubsystem::ImportHeightmap(FString& Heightmap, FString& JSON, FVector& Scale, FVector& Location, TArray<FString>& Weightmaps, FString& CachedPath)
{
	check(ImporterWindowValidator!= nullptr);
	JSON.Empty();
	Heightmap.Empty();
	
	const TSharedPtr<FGenericWindow> LocalNativeWindow = ImporterWindowValidator.Pin()->GetNativeWindow();
	check(LocalNativeWindow!= nullptr);
	const void* ParentWindow = LocalNativeWindow->GetOSWindowHandle();

	const FString DialogTitle = TEXT("Import Heightmap");
	const FString DefaultPath = DefaultDialogPath.IsEmpty() ? FPaths::ProjectDir() : DefaultDialogPath;
	const FString DefaultFile = TEXT("");
	FString OutPath = TEXT("");
	const FString FileFilter = TEXT("Heightmap files (*.r16, *.raw, *.png)|*.r16;*.raw;*.png|");

	TArray<FString> SelectedFilePath;

	if (FDesktopPlatformModule::Get()->OpenFileDialog(ParentWindow, DialogTitle, DefaultPath, DefaultFile, FileFilter, EFileDialogFlags::None, SelectedFilePath)) {
		
		Heightmap = SelectedFilePath[0];
		
		OutPath = FPaths::GetPath(Heightmap);

		CachedPath = OutPath;

		DefaultDialogPath = OutPath;
		
		IFileManager& FileManager = IFileManager::Get();
		

		TArray<FString> WeightFilesInDirectory;
		
		FileManager.FindFiles(WeightFilesInDirectory, *(OutPath / TEXT("*.png*")), true, false);
		
		for(int i = 0; i < WeightFilesInDirectory.Num(); i++) 
		{
			if (WeightFilesInDirectory[i].Contains(TEXT("W_"), ESearchCase::IgnoreCase))
			{
				Weightmaps.Add(WeightFilesInDirectory[i]);
			}
		}

		TArray<FString> FilesInDirectory;
		
		FileManager.FindFiles(FilesInDirectory, *(OutPath / TEXT("*.json*")), true, false);
		
		if (FilesInDirectory.IsEmpty())
		{
			JSON = TEXT("None");
			UE_LOG(GaeaSubsystem, Log, TEXT("No json files found."));
			return;
		}
		
		
		bool bDefinitionFound = false;

		for(int i = 0; i < FilesInDirectory.Num(); i++) 
		{
			if (FilesInDirectory[i].Contains(TEXT("definition"), ESearchCase::IgnoreCase))
			{
				JSON = FPaths::Combine(*OutPath, *FilesInDirectory[i]);
				bDefinitionFound = true;
				break;
			}
		}
	
		if (!bDefinitionFound)
		{
			UE_LOG(GaeaSubsystem, Warning, TEXT("No Definition.json found."));
			return;
		}
	}

	else
	{
		UE_LOG(GaeaSubsystem, Log, TEXT("Dialog was closed. No files selected"));
			return;
		
	}
		bool bStatus = false;
		FString JsonMessage = "";
		const FGaeaJson GaeaDefinition = CreateStructFromJson(JSON, bStatus, JsonMessage);
	
		if(bStatus)
		{
			UE_LOG(GaeaSubsystem, Display, TEXT("ScaleX: %f, ScaleY: %f, Height: %f, Resolution: %d"), 
		   GaeaDefinition.ScaleX, GaeaDefinition.ScaleY, GaeaDefinition.Height, GaeaDefinition.Resolution);
			Location = FVector(0,0,GaeaDefinition.Height*100/2);
			Scale = FVector(GaeaDefinition.ScaleX * 100 / GaeaDefinition.Resolution,GaeaDefinition.ScaleY * 100 / GaeaDefinition.Resolution,GaeaDefinition.Height * 100 / 512);
		}
		
		
	
}	

FString UGaeaSubsystem::ReadStringFromFile(FString Path, bool& bOutSuccess, FString& OutMessage)
{
	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*Path))
	{
		bOutSuccess = false;
		OutMessage = FString::Printf(TEXT("Failed to read file - File does not exist - '%s'"), *Path);
	
		UE_LOG(GaeaSubsystem, Error, TEXT("Failed to read file - File does not exist."));
		return "";
	}

	FString OutString = "";

	if(!FFileHelper::LoadFileToString(OutString, *Path))
	{
		bOutSuccess = false;
		OutMessage = FString::Printf(TEXT("Failed to read file - Is this a text file? - '%s'"), *Path);
		UE_LOG(GaeaSubsystem, Error, TEXT("Failed to read file - Is this a text file?"));
		return "";
	}

	bOutSuccess = true;
	OutMessage = FString::Printf(TEXT("File read successfully - '%s'"), *Path);
	return OutString;
	
}

TSharedPtr<FJsonObject> UGaeaSubsystem::ReadJson(FString Path, bool& bOutSuccess, FString& OutMessage)
{
	const FString JSONString = ReadStringFromFile(Path, bOutSuccess, OutMessage);
	if (!bOutSuccess)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> RetJsonObject;

	if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JSONString), RetJsonObject))
	{
		bOutSuccess = false;
		OutMessage = FString::Printf(TEXT("Json read failed - '%s'"), *Path);
		UE_LOG(GaeaSubsystem, Error, TEXT("Json read failed."));
		return nullptr;
	}

	bOutSuccess = true;
	OutMessage = FString::Printf(TEXT("Successfully read Json - '%s'"), *Path);
	UE_LOG(GaeaSubsystem, Log, TEXT("Successfully read Json."));
	return RetJsonObject;
}

FGaeaJson UGaeaSubsystem::CreateStructFromJson(FString Path, bool& bOutSuccess, FString& OutMessage)
{
	const TSharedPtr<FJsonObject> JsonObject = ReadJson(Path, bOutSuccess, OutMessage);
	if(!bOutSuccess)
	{
		return FGaeaJson();
	}

	FGaeaJson RetGaeaJson;

	if(!FJsonObjectConverter::JsonObjectToUStruct<FGaeaJson>(JsonObject.ToSharedRef(), &RetGaeaJson))
	{
		bOutSuccess = false;
		OutMessage = FString::Printf(TEXT("Json conversion failed - '%s'"), *Path);
		UE_LOG(GaeaSubsystem, Error, TEXT("Json conversion failed."));
		return FGaeaJson();
	}

	bOutSuccess = true;
	OutMessage = FString::Printf(TEXT("Json conversion succeeded - '%s'"), *Path);
	return RetGaeaJson;
	
}

ALandscape* UGaeaSubsystem::GetLandscape(ULandscapeInfo* LandscapeInfo) const
{
	ALandscape* LandscapeActor = LandscapeInfo->LandscapeActor.Get();
	if (LandscapeActor != nullptr) 
	{
		return LandscapeActor;
	}
	return nullptr;
}

void UGaeaSubsystem::CreateLandscapeActor(UImporterPanelSettings* Settings)
{
	constexpr bool bSingleFile = true;
	FLandscapeImportDescriptor OutImportDescriptor;
	TArray<FLandscapeImportDescriptor> WeightOutImportDescriptors;
	TArray<ELandscapeImportResult> WeightImportResults;
	TArray<FLandscapeImportLayerInfo> MaterialImportLayers;
	TArray<ULandscapeLayerInfoObject*> LayerInfoObjects;
	OutImportDescriptor.Scale = Settings->Scale;
	FText OutMessage;
	TArray<FText> WeightOutMessage;
	bool ImportWeightmaps = false;
	
	UWorld* World = nullptr;
	{
		FWorldContext& EditorWorldContext = GEditor->GetEditorWorldContext();
		World = EditorWorldContext.World();
	}
	
	ULandscapeSubsystem* LandscapeSubsystem = World->GetSubsystem<ULandscapeSubsystem>();
	bool bIsGridBased = LandscapeSubsystem->IsGridBased();
	
	
	ELandscapeImportResult ImportResult = FLandscapeImportHelper::GetHeightmapImportDescriptor(Settings->HeightMapFileName, bSingleFile, Settings->FlipYAxis, OutImportDescriptor, OutMessage);
	int32 DescriptorIndex = OutImportDescriptor.FileResolutions.Num() / 2;
	
	ULandscapeEditorObject* DefaultValueObject = ULandscapeEditorObject::StaticClass()->GetDefaultObject<ULandscapeEditorObject>();
	check(DefaultValueObject);
	
	int32 OutQuadsPerSection = DefaultValueObject->NewLandscape_QuadsPerSection;
	int32 OutSectionsPerComponent = DefaultValueObject->NewLandscape_SectionsPerComponent;
	FIntPoint OutComponentCount = DefaultValueObject->NewLandscape_ComponentCount;

	FLandscapeImportHelper::ChooseBestComponentSizeForImport(OutImportDescriptor.ImportResolutions[DescriptorIndex].Width, OutImportDescriptor.ImportResolutions[DescriptorIndex].Height, OutQuadsPerSection, OutSectionsPerComponent, OutComponentCount);
	
	TArray<uint16> ImportData;
	ImportResult = FLandscapeImportHelper::GetHeightmapImportData(OutImportDescriptor, DescriptorIndex, ImportData, OutMessage);
	
	
	const int32 QuadsPerComponent = OutSectionsPerComponent * OutQuadsPerSection;
	const int32 SizeX = OutComponentCount.X * QuadsPerComponent + 1;
	const int32 SizeY = OutComponentCount.Y * QuadsPerComponent + 1;

	TArray<uint16> FinalHeightData;
	FLandscapeImportHelper::TransformHeightmapImportData(ImportData, FinalHeightData, OutImportDescriptor.ImportResolutions[DescriptorIndex], FLandscapeImportResolution(SizeX, SizeY), ELandscapeImportTransformType::ExpandCentered);

	FString PackagePath = Settings->LayerInfoFolder.Path;
	FString Name;
	bool PathExists = false;

	if(!PackagePath.IsEmpty())
	{
		PathExists = UEditorAssetLibrary::DoesDirectoryExist(PackagePath);
	}

	UE_LOG(GaeaSubsystem, Display, TEXT("LandscapeMaterialLayerNames Count: %d"), Settings->LandscapeMaterialLayerNames.Num());
	UE_LOG(GaeaSubsystem, Display, TEXT("LayerInfoFolder Path: %s"), *Settings->LayerInfoFolder.Path);

	if (!Settings->LandscapeMaterialLayerNames.IsEmpty() && PathExists && Settings->LandscapeMaterialLayerNames.Num() >= 2 && Settings->WeightmapFileNames.Num() == (Settings->LandscapeMaterialLayerNames.Num() - 1))
	{
		ImportWeightmaps = true;
	}

	if(ImportWeightmaps)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools"); 
		
		for(int i = 0; i < Settings->LandscapeMaterialLayerNames.Num(); i++)
		{
			Name = Settings->LandscapeMaterialLayerNames[i].ToString();
			Name = Name.Replace(TEXT(" "), TEXT(""));
			FString NewAssetName;
			FString DummyPackageName;
			AssetToolsModule.Get().CreateUniqueAssetName(PackagePath / Name, TEXT(""), DummyPackageName, NewAssetName);
			UObject* CreatedAsset = AssetToolsModule.Get().CreateAsset(NewAssetName, PackagePath, ULandscapeLayerInfoObject::StaticClass(), nullptr);

			ULandscapeLayerInfoObject* LayerInfoObj = Cast<ULandscapeLayerInfoObject>(CreatedAsset);

			if(LayerInfoObj)
			{
				// UE 5.7: Use SetLayerName() instead of direct assignment - requires second bool param
				LayerInfoObj->SetLayerName(Settings->LandscapeMaterialLayerNames[i], true);
			}
			LayerInfoObjects.Add(LayerInfoObj);
		}
	
	
		for(int i = 0; i < Settings->LandscapeMaterialLayerNames.Num(); i++)
		{
			FLandscapeImportLayerInfo LayerInfo;
			LayerInfo.LayerName = Settings->LandscapeMaterialLayerNames[i];
			MaterialImportLayers.Add(LayerInfo);
			MaterialImportLayers[i].LayerName = LayerInfo.LayerName;
		}
		
		MaterialImportLayers[0].LayerData = TArray<uint8>();
		MaterialImportLayers[0].SourceFilePath = "";
		MaterialImportLayers[0].LayerInfo = LayerInfoObjects[0];
		const int32 DataSize = SizeX * SizeY;
		MaterialImportLayers[0].LayerData.AddUninitialized(DataSize);
		uint8* ByteData = MaterialImportLayers[0].LayerData.GetData();
		FMemory::Memset(ByteData, 255, DataSize);
		
	
		WeightOutImportDescriptors.AddDefaulted(Settings->LandscapeMaterialLayerNames.Num());
		WeightOutMessage.AddDefaulted(Settings->LandscapeMaterialLayerNames.Num());
		WeightImportResults.AddDefaulted(Settings->LandscapeMaterialLayerNames.Num());

		if(Settings->WeightmapFilePaths.IsEmpty())
		{
			for(int i = 0; i < Settings->WeightmapFileNames.Num(); i++) 
			{
				FString FullPath = FPaths::Combine(*Settings->StoredPath, *Settings->WeightmapFileNames[i]);
				UE_LOG(GaeaSubsystem, Display, TEXT("Weightmap Full Path: %s"), *FullPath);
				Settings->WeightmapFilePaths.Add(FullPath); 
			}
		}

		for(int32 i = 1; i < Settings->LandscapeMaterialLayerNames.Num(); i++)
		{
			int32 WeightmapIndex = i - 1; 
			if (WeightmapIndex < Settings->WeightmapFilePaths.Num()) 
			{ 
				WeightImportResults[i] = FLandscapeImportHelper::GetWeightmapImportDescriptor(Settings->WeightmapFilePaths[WeightmapIndex], bSingleFile, Settings->FlipYAxis, Settings->LandscapeMaterialLayerNames[i], WeightOutImportDescriptors[i],WeightOutMessage[i]); 
    
				TArray<uint8> WeightOutData; 
				FLandscapeImportHelper::GetWeightmapImportData(WeightOutImportDescriptors[i], DescriptorIndex, Settings->LandscapeMaterialLayerNames[i], WeightOutData, WeightOutMessage[i]); 
    
				TArray<uint8> FinalWeightOutData; 
				FLandscapeImportHelper::TransformWeightmapImportData(WeightOutData, FinalWeightOutData, OutImportDescriptor.ImportResolutions[DescriptorIndex], FLandscapeImportResolution(SizeX, SizeY), ELandscapeImportTransformType::ExpandCentered); 

				MaterialImportLayers[i].LayerName = Settings->LandscapeMaterialLayerNames[i];
				MaterialImportLayers[i].LayerInfo = LayerInfoObjects[i];
				MaterialImportLayers[i].LayerData = MoveTemp(FinalWeightOutData); 
			
			}
		}

		UE_LOG(GaeaSubsystem, Display, TEXT("MaterialImportLayers Length: %d"), MaterialImportLayers.Num());
	}
	
	const FVector Offset = FTransform(Settings->Rotation,FVector::ZeroVector, Settings->Scale).TransformVector(FVector(-OutComponentCount.X * QuadsPerComponent / 2., -OutComponentCount.Y * QuadsPerComponent / 2., 0.));
	
	ALandscape* Landscape = World->SpawnActor<ALandscape>(Settings->Location + Offset, Settings->Rotation);
	// UE 5.7: All landscapes have edit layers by default now
	Landscape->LandscapeMaterial = Settings->LandscapeMaterial;
	Landscape->SetActorRelativeScale3D(Settings->Scale);

	UGaeaLandscapeComponent* GaeaComponent = NewObject<UGaeaLandscapeComponent>(Landscape, UGaeaLandscapeComponent::StaticClass(), TEXT("Gaea Landscape Component"));

	if(GaeaComponent)
	{
		GaeaComponent->RegisterComponent(); 
		Landscape->AddInstanceComponent(GaeaComponent);
		Landscape->AddOwnedComponent(GaeaComponent);
		
		GaeaComponent->HeightmapFilepath.FilePath = Settings->HeightMapFileName;
		GaeaComponent->DefinitionFilepath.FilePath = Settings->jsonFileName;

		if (!Settings->WeightmapFilePaths.IsEmpty())
		{
			GaeaComponent->WeightmapFilepaths.SetNum(Settings->WeightmapFilePaths.Num());
			
			for (int32 i = 0; i < Settings->WeightmapFilePaths.Num(); i++)
			{
				GaeaComponent->WeightmapFilepaths[i].FilePath = Settings->WeightmapFilePaths[i];
			}
		}
		
	}
	
	TMap<FGuid, TArray<uint16>> HeightmapDataPerLayers;
	HeightmapDataPerLayers.Add(FGuid(), MoveTemp(FinalHeightData));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;

	if(Settings->LandscapeMaterialLayerNames.IsEmpty() || !PathExists || Settings->LandscapeMaterialLayerNames.Num() < 2)
	{
		MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());
	}
	else
	{
		MaterialLayerDataPerLayers.Add(FGuid(), MaterialImportLayers);
	}

	Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, OutSectionsPerComponent, OutQuadsPerSection, HeightmapDataPerLayers, *Settings->HeightMapFileName, MaterialLayerDataPerLayers, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
	
	Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);
	
	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	check(LandscapeInfo);
	LandscapeInfo->UpdateLayerInfoMap(Landscape);

	Landscape->RegisterAllComponents();
	
	for(int32 i = 0; i < Settings->LandscapeMaterialLayerNames.Num(); i++)
	{
		if(MaterialImportLayers[i].LayerInfo != nullptr)
		{
			Landscape->AddTargetLayer(MaterialImportLayers[i].LayerName, FLandscapeTargetLayerSettings(MaterialImportLayers[i].LayerInfo, MaterialImportLayers[i].SourceFilePath));
		}
	}

	if (bIsGridBased && Settings->bIsWorldPartition)
	{
		FScopedSlowTask SlowTask(100.f, FText::FromString(TEXT("Creating landscape chunks, importing bits and bytes. Might be here for awhile.")));
		SlowTask.MakeDialog(false);
		SlowTask.EnterProgressFrame(10.f);
		LandscapeSubsystem->ChangeGridSize(LandscapeInfo, Settings->WorldPartitionGridSize);
		SlowTask.EnterProgressFrame(90.f);
	}
	
	ImporterWindowValidator.Pin()->RequestDestroyWindow();
}

TArray<UMaterialExpressionLandscapeLayerBlend*> UGaeaSubsystem::GetLandscapeLayerBlendNodes(UMaterialInterface* MaterialInterface)
{
	TArray<const UMaterialExpressionLandscapeLayerBlend*> ConstAllExpressions;
	TArray<UMaterialExpressionLandscapeLayerBlend*> OutExpressions;
    
	if (MaterialInterface && MaterialInterface->GetBaseMaterial())
	{
		MaterialInterface->GetBaseMaterial()->GetAllExpressionsOfType<UMaterialExpressionLandscapeLayerBlend>(ConstAllExpressions);

		for (const UMaterialExpressionLandscapeLayerBlend* Expression : ConstAllExpressions)
		{
			OutExpressions.Add(const_cast<UMaterialExpressionLandscapeLayerBlend*>(Expression));
		}
	}
	return OutExpressions;
}

TArray<FName> UGaeaSubsystem::GetLandscapeLayerBlendNames(TArray<UMaterialExpressionLandscapeLayerBlend*> LayerBlends, TArray<FName>& Names)
{
	
		TArray<FName> OutNames;

		for (UMaterialExpressionLandscapeLayerBlend* LayerBlend : LayerBlends)
		{
			if (LayerBlend)
			{
				for (const FLayerBlendInput& Layer : LayerBlend->Layers)
				{
					OutNames.AddUnique(Layer.LayerName);
				}
			}
		}
		Names = OutNames;
		return OutNames;
	
}

void UGaeaSubsystem::GetLandscapeActorProxies(ALandscape* Landscape,TArray<ALandscapeProxy*>& LandscapeStreamingProxies)
{
	if (Landscape)
	{
		Landscape->GetLandscapeInfo()->ForEachLandscapeProxy([&LandscapeStreamingProxies](ALandscapeProxy* Proxy)
		{
			LandscapeStreamingProxies.Add(Proxy);
			return true; 
		});
	}
}




#undef LOCTEXT_NAMESPACE
