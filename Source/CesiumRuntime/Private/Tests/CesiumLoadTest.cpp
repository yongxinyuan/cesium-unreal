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
    "Cesium.Performance.LoadTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)

struct LoadTestContext {
  UWorld* world;
  ACesiumGeoreference* georeference;
  ACesiumCameraManager* cameraManager;
  AGlobeAwareDefaultPawn* pawn;
  std::vector<ACesium3DTileset*> tilesets;

  void setCamera(const FCesiumCamera& camera) {
    // Take over first camera, or add if it doesn't exist
    const TMap<int32, FCesiumCamera> cameras = cameraManager->GetCameras();
    if (cameras.IsEmpty()) {
      cameraManager->AddCamera(camera);
    } else {
      cameraManager->UpdateCamera(0, camera);
    }
  }

  void refreshTilesets() {
    std::vector<ACesium3DTileset*>::iterator it;
    for (it = tilesets.begin(); it != tilesets.end(); ++it)
      (*it)->RefreshTileset();
  }

  void setSuspendUpdate(bool suspend) {
    std::vector<ACesium3DTileset*>::iterator it;
    for (it = tilesets.begin(); it != tilesets.end(); ++it)
      (*it)->SuspendUpdate = suspend;
  }
};

bool neverBreak(LoadTestContext& context) { return false; }

bool breakWhenTilesetsLoaded(LoadTestContext& context) {
  std::vector<ACesium3DTileset*>::const_iterator it;
  for (it = context.tilesets.begin(); it != context.tilesets.end(); ++it) {
    ACesium3DTileset* tileset = *it;

    int progress = (int)tileset->GetLoadProgress();
    // UE_LOG(LogCesium, Display, TEXT("Load Progress: %d"), progress);
    if (progress != 100)
      return false;
  }
  return true;
}

bool tickWorldUntil(
    LoadTestContext& context,
    size_t timeoutSecs,
    std::function<bool(LoadTestContext&)> breakFunction) {
  const double minStepTime = 0.050; // Don't loop faster than 20 fps

  const double testStartMark = FPlatformTime::Seconds();
  const double testEndMark = testStartMark + (double)timeoutSecs;
  double lastTimeMark = testStartMark;

  while (1) {
    double frameTimeMark = FPlatformTime::Seconds();

    if (frameTimeMark > testEndMark)
      return true;

    double frameElapsedTime = frameTimeMark - lastTimeMark;

    if (frameElapsedTime < minStepTime) {
      double sleepTarget = minStepTime - frameElapsedTime;
      FPlatformProcess::Sleep(sleepTarget);
      continue;
    }

    //
    // Force a tick. Derived from various examples in this code base
    // Search for FTSTicker::GetCoreTicker().Tick
    //

    // Increment global frame counter once for each app tick.
    GFrameCounter++;

    // Let world tick at same rate as this loop
    context.world->Tick(ELevelTick::LEVELTICK_ViewportsOnly, frameElapsedTime);

    // Application tick
    FTaskGraphInterface::Get().ProcessThreadUntilIdle(
        ENamedThreads::GameThread);
    FTSTicker::GetCoreTicker().Tick(frameElapsedTime);

    if (breakFunction(context))
      return false;

    lastTimeMark = frameTimeMark;
  };
}

void setupForGoogleTiles(LoadTestContext& context) {

  FVector targetOrigin(-122.083969, 37.424492, 142.859116);
  FString targetUrl(
      "https://tile.googleapis.com/v1/3dtiles/root.json?key=AIzaSyCnRPXWDIj1LuX6OWIweIqZFHHoXVgdYss");

  FCesiumCamera camera;
  camera.ViewportSize = FVector2D(1024, 768);
  camera.Location = FVector(0, 0, 0);
  camera.Rotation = FRotator(-25, 95, 0);
  camera.FieldOfViewDegrees = 90;
  context.setCamera(camera);

  context.georeference->SetGeoreferenceOriginLongitudeLatitudeHeight(
      targetOrigin);

  context.pawn->SetActorLocation(FVector(0, 0, 0));
  context.pawn->SetActorRotation(FRotator(-25, 95, 0));

  ACesium3DTileset* tileset = context.world->SpawnActor<ACesium3DTileset>();
  tileset->SetUrl(targetUrl);
  tileset->SetTilesetSource(ETilesetSource::FromUrl);
  tileset->SetActorLabel(TEXT("Google Photorealistic 3D Tiles"));

  context.tilesets.push_back(tileset);
}

void setupForDenver(LoadTestContext& context) {

  FVector targetOrigin(-104.988892, 39.743462, 1798.679443);
  FString ionToken(
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiI2NmZhZTk4NS01MDFmLTRjODgtOTlkYy04NjIwODhiZWExOGYiLCJpZCI6MjU5LCJpYXQiOjE2ODg1MTI4ODd9.haoe5hsJyfHk1dQAHVK6N8dW_kfmtdbyuhlGwFdEHbM");

  FCesiumCamera camera;
  camera.ViewportSize = FVector2D(1024, 768);
  camera.Location = FVector(0, 0, 0);
  camera.Rotation = FRotator(-5.2, -149.4, 0);
  camera.FieldOfViewDegrees = 90;
  context.setCamera(camera);

  context.georeference->SetGeoreferenceOriginLongitudeLatitudeHeight(
      targetOrigin);

  context.pawn->SetActorLocation(FVector(0, 0, 0));
  context.pawn->SetActorRotation(FRotator(-5.2, -149.4, 0));

  // Add Cesium World Terrain
  ACesium3DTileset* worldTerrainTileset =
      context.world->SpawnActor<ACesium3DTileset>();
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
  ACesium3DTileset* aerometrexTileset =
      context.world->SpawnActor<ACesium3DTileset>();
  aerometrexTileset->SetTilesetSource(ETilesetSource::FromCesiumIon);
  aerometrexTileset->SetIonAssetID(354307);
  aerometrexTileset->SetIonAccessToken(ionToken);
  aerometrexTileset->SetMaximumScreenSpaceError(2.0);
  aerometrexTileset->SetActorLabel(TEXT("Aerometrex Denver"));

  context.tilesets.push_back(worldTerrainTileset);
  context.tilesets.push_back(aerometrexTileset);
}

void createCommonWorldObjects(LoadTestContext& context) {

#if CREATE_TEST_IN_EDITOR_WORLD
  context.world = FAutomationEditorCommonUtils::CreateNewMap();
#else
  context.world = UWorld::CreateWorld(EWorldType::Game, false);
  FWorldContext& worldContext =
      GEngine->CreateNewWorldContext(EWorldType::Game);
  worldContext.SetCurrentWorld(context.world);
#endif

  ACesiumSunSky* sunSky = context.world->SpawnActor<ACesiumSunSky>();
  APlayerStart* playerStart = context.world->SpawnActor<APlayerStart>();

  context.cameraManager =
      ACesiumCameraManager::GetDefaultCameraManager(context.world);

  FSoftObjectPath objectPath(
      TEXT("Class'/CesiumForUnreal/DynamicPawn.DynamicPawn_C'"));
  TSoftObjectPtr<UObject> DynamicPawn = TSoftObjectPtr<UObject>(objectPath);

  context.georeference =
      ACesiumGeoreference::GetDefaultGeoreference(context.world);
  context.pawn = context.world->SpawnActor<AGlobeAwareDefaultPawn>(
      Cast<UClass>(DynamicPawn.LoadSynchronous()));

  context.pawn->AutoPossessPlayer = EAutoReceiveInput::Player0;
}

bool FCesiumLoadTest::RunTest(const FString& Parameters) {

  //
  // Programmatically set up the world
  //
  UE_LOG(LogCesium, Display, TEXT("Creating world objects..."));
  LoadTestContext context;
  createCommonWorldObjects(context);

  // Configure location specific objects
  const size_t locationIndex = 1;
  switch (locationIndex) {
  case 0:
    setupForGoogleTiles(context);
    break;
  case 1:
    setupForDenver(context);
    break;
  default:
    break;
  }

  // Halt tileset updates and reset them
  context.setSuspendUpdate(true);
  context.refreshTilesets();

  // Let world settle for 1 second
  UE_LOG(LogCesium, Display, TEXT("Letting world settle for 1 second..."));
  tickWorldUntil(context, 1, neverBreak);

  // Start test mark, turn updates back on
  double loadStartMark = FPlatformTime::Seconds();
  UE_LOG(LogCesium, Display, TEXT("-- Load start mark --"));
  context.setSuspendUpdate(false);

  // Spin for a maximum of 20 seconds, or until tilesets finish loading
  const size_t testTimeout = 20;
  UE_LOG(
      LogCesium,
      Display,
      TEXT("Tick world until tilesets load, or %d seconds elapse..."),
      testTimeout);
  bool timedOut = tickWorldUntil(context, testTimeout, breakWhenTilesetsLoaded);

  double loadEndMark = FPlatformTime::Seconds();
  UE_LOG(LogCesium, Display, TEXT("-- Load end mark --"));

  // Cleanup
#if CREATE_TEST_IN_EDITOR_WORLD
  // Let all objects be available for viewing after test

  // Let world settle for 1 second
  UE_LOG(LogCesium, Display, TEXT("Letting world settle for 1 second..."));
  tickWorldUntil(context, 1, neverBreak);

  // Freeze updates
  context.setSuspendUpdate(true);
#else
  GEngine->DestroyWorldContext(context.world);
  context.world->DestroyWorld(false);
#endif

  double loadElapsedTime = loadEndMark - loadStartMark;

  if (timedOut) {
    UE_LOG(
        LogCesium,
        Error,
        TEXT("TIMED OUT: Loading stopped after %.2f seconds"),
        loadElapsedTime);
  } else {
    UE_LOG(
        LogCesium,
        Display,
        TEXT("Tileset load completed in %.2f seconds"),
        loadElapsedTime);
  }

  return !timedOut;
}

#endif
