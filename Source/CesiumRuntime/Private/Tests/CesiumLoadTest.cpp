// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"

#include "Cesium3DTileset.h"
#include "CesiumCameraManager.h"
#include "CesiumGeoreference.h"
#include "CesiumIonRasterOverlay.h"
#include "CesiumRuntime.h"
#include "CesiumSunSky.h"
#include "CesiumTestHelpers.h"
#include "GlobeAwareDefaultPawn.h"

//
// For debugging, it may help to create the scene in the editor
// After the test is run, you can play with their settings and even run PIE
//
#define CREATE_TEST_IN_EDITOR_WORLD 1

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCesiumLoadTest,
    "Cesium.LoadTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)

void tickWorld(UWorld* world, double time) {
  const double minStepTime = 0.001; // Don't loop faster than 100 fps

  const double testStartMark = FPlatformTime::Seconds();
  const double testEndMark = testStartMark + time;
  double lastTimeMark = testStartMark;

  while (1) {
    double frameTimeMark = FPlatformTime::Seconds();

    if (frameTimeMark > testEndMark)
      break;

    double frameElapsedTime = frameTimeMark - lastTimeMark;

    if (frameElapsedTime < minStepTime) {
      double sleepTarget = minStepTime - frameElapsedTime;
      FPlatformProcess::Sleep(sleepTarget);
      continue;
    }

    // Let world tick at same rate as this loop
    world->Tick(ELevelTick::LEVELTICK_All, frameElapsedTime);

    // Derived from TimerManagerTests.cpp, TimerTest_TickWorld
    GFrameCounter++;

    lastTimeMark = frameTimeMark;
  };
}

void setupForGoogleTiles(
    UWorld* world,
    ACesiumGeoreference* georeference,
    AGlobeAwareDefaultPawn* pawn,
    std::vector<ACesium3DTileset*>& createdTilesets) {

  FVector targetOrigin(-122.083969, 37.424492, 142.859116);
  FString targetUrl(
      "https://tile.googleapis.com/v1/3dtiles/root.json?key=AIzaSyCnRPXWDIj1LuX6OWIweIqZFHHoXVgdYss");

  georeference->SetGeoreferenceOriginLongitudeLatitudeHeight(targetOrigin);

  pawn->SetActorLocation(FVector(0, 0, 0));
  pawn->SetActorRotation(FRotator(-25, 95, 0));

  ACesium3DTileset* tileset = world->SpawnActor<ACesium3DTileset>();
  tileset->SetUrl(targetUrl);
  tileset->SetTilesetSource(ETilesetSource::FromUrl);
  tileset->SetActorLabel(TEXT("Google Photorealistic 3D Tiles"));

  createdTilesets.push_back(tileset);
}

void setupForDenver(
    UWorld* world,
    ACesiumGeoreference* georeference,
    AGlobeAwareDefaultPawn* pawn,
    std::vector<ACesium3DTileset*>& createdTilesets) {

  FVector targetOrigin(-104.988892, 39.743462, 1798.679443);
  FString ionToken(
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiI2NmZhZTk4NS01MDFmLTRjODgtOTlkYy04NjIwODhiZWExOGYiLCJpZCI6MjU5LCJpYXQiOjE2ODg1MTI4ODd9.haoe5hsJyfHk1dQAHVK6N8dW_kfmtdbyuhlGwFdEHbM");

  georeference->SetGeoreferenceOriginLongitudeLatitudeHeight(targetOrigin);

  pawn->SetActorLocation(FVector(0, 0, 0));
  pawn->SetActorRotation(FRotator(-5.2, -149.4, 0));

  // Add Cesium World Terrain
  ACesium3DTileset* worldTerrainTileset = world->SpawnActor<ACesium3DTileset>();
  worldTerrainTileset->SetTilesetSource(ETilesetSource::FromCesiumIon);
  worldTerrainTileset->SetIonAssetID(1);
  worldTerrainTileset->SetIonAccessToken(ionToken);
  worldTerrainTileset->SetActorLabel(TEXT("Cesium World Terrain"));

  // Bing Maps Aerial overlay
  UCesiumIonRasterOverlay* pOverlay = NewObject<UCesiumIonRasterOverlay>(
      worldTerrainTileset,
      FName("Bing Maps Aerial"),
      RF_Transactional);
  pOverlay->MaterialLayerKey = TEXT("Overlay0");
  pOverlay->IonAssetID = 2;
  pOverlay->SetActive(true);
  pOverlay->OnComponentCreated();
  worldTerrainTileset->AddInstanceComponent(pOverlay);

  // Aerometrex Denver
  ACesium3DTileset* aerometrexTileset = world->SpawnActor<ACesium3DTileset>();
  aerometrexTileset->SetTilesetSource(ETilesetSource::FromCesiumIon);
  aerometrexTileset->SetIonAssetID(354307);
  aerometrexTileset->SetIonAccessToken(ionToken);
  aerometrexTileset->SetMaximumScreenSpaceError(2.0);
  aerometrexTileset->SetActorLabel(TEXT("Aerometrex Denver"));

  createdTilesets.push_back(worldTerrainTileset);
  createdTilesets.push_back(aerometrexTileset);
}

bool FCesiumLoadTest::RunTest(const FString& Parameters) {

  //
  // Programmatically set up the world
  //

#if CREATE_TEST_IN_EDITOR_WORLD
  UWorld* world = FAutomationEditorCommonUtils::CreateNewMap();
#else
  UWorld* world = UWorld::CreateWorld(EWorldType::Game, false);
  FWorldContext& context = GEngine->CreateNewWorldContext(EWorldType::Game);
  context.SetCurrentWorld(world);
#endif
  TestNotNull("world is valid", world);

  // Create common objects across all locations
  ACesiumCameraManager* cameraManager =
      ACesiumCameraManager::GetDefaultCameraManager(world);
  ACesiumGeoreference* georeference =
      ACesiumGeoreference::GetDefaultGeoreference(world);
  ACesiumSunSky* sunSky = world->SpawnActor<ACesiumSunSky>();
  APlayerStart* playerStart = world->SpawnActor<APlayerStart>();

  FSoftObjectPath objectPath(
      TEXT("Class'/CesiumForUnreal/DynamicPawn.DynamicPawn_C'"));
  TSoftObjectPtr<UObject> DynamicPawn = TSoftObjectPtr<UObject>(objectPath);
  AGlobeAwareDefaultPawn* pawn = world->SpawnActor<AGlobeAwareDefaultPawn>(
      Cast<UClass>(DynamicPawn.LoadSynchronous()));
  pawn->AutoPossessPlayer = EAutoReceiveInput::Player0;

  // Configure location specific objects
  std::vector<ACesium3DTileset*> tilesets;
  const size_t locationIndex = 1;
  switch (locationIndex) {
  case 0:
    setupForGoogleTiles(world, georeference, pawn, tilesets);
    break;
  case 1:
    setupForDenver(world, georeference, pawn, tilesets);
    break;
  default:
    break;
  }

  // Halt tileset updates and reset them
  std::vector<ACesium3DTileset*>::iterator it;
  for (it = tilesets.begin(); it != tilesets.end(); ++it) {
    ACesium3DTileset* tileset = *it;
    tileset->SuspendUpdate = true;
    tileset->RefreshTileset();
  }

  // Turn updates back on
  for (it = tilesets.begin(); it != tilesets.end(); ++it) {
    ACesium3DTileset* tileset = *it;
    tileset->SuspendUpdate = false;
  }

  // Spin for 5 seconds, letting our game objects tick
  const double testMaxTime = 5.0;
  tickWorld(world, testMaxTime);

  // Cleanup
#if CREATE_TEST_IN_EDITOR_WORLD
  // Let all objects persist
#else
  GEngine->DestroyWorldContext(world);
  world->DestroyWorld(false);
#endif

  return true;
}

#endif
