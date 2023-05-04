// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "CesiumCreditSystem.h"
#include "Cesium3DTilesSelection/CreditSystem.h"
#include "CesiumCreditSystemBPLoader.h"
#include "CesiumRuntime.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ScreenCreditsWidget.h"
#include <string>
#include <tidybuffio.h>
#include <vector>

#if WITH_EDITOR
#include "Editor.h"
#include "EditorSupportDelegates.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#endif

/*static*/ UClass* ACesiumCreditSystem::CesiumCreditSystemBP = nullptr;
namespace {

/**
 * @brief Tries to find the default credit system in the given level.
 *
 * This will search all actors of the given level for a `ACesiumCreditSystem`
 * whose name starts with `"CesiumCreditSystemDefault"` that is *valid*
 * (i.e. not pending kill).
 *
 * @param Level The level
 * @return The default credit system, or `nullptr` if there is none.
 */
ACesiumCreditSystem* findValidDefaultCreditSystem(ULevel* Level) {
  if (!IsValid(Level)) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("No valid level for findValidDefaultCreditSystem"));
    return nullptr;
  }
  TArray<AActor*>& Actors = Level->Actors;
  AActor** DefaultCreditSystemPtr =
      Actors.FindByPredicate([](AActor* const& InItem) {
        if (!IsValid(InItem)) {
          return false;
        }
        if (!InItem->IsA(ACesiumCreditSystem::StaticClass())) {
          return false;
        }
        if (!InItem->GetName().StartsWith("CesiumCreditSystemDefault")) {
          return false;
        }
        return true;
      });
  if (!DefaultCreditSystemPtr) {
    return nullptr;
  }
  AActor* DefaultCreditSystem = *DefaultCreditSystemPtr;
  return Cast<ACesiumCreditSystem>(DefaultCreditSystem);
}
} // namespace

FName ACesiumCreditSystem::DEFAULT_CREDITSYSTEM_TAG =
    FName("DEFAULT_CREDITSYSTEM");

/*static*/ ACesiumCreditSystem*
ACesiumCreditSystem::GetDefaultCreditSystem(const UObject* WorldContextObject) {
  // Blueprint loading can only happen in a constructor, so we instantiate a
  // loader object that retrieves the blueprint class in its constructor. We can
  // destroy the loader immediately once it's done since it will have already
  // set CesiumCreditSystemBP.
  if (!CesiumCreditSystemBP) {
    UCesiumCreditSystemBPLoader* bpLoader =
        NewObject<UCesiumCreditSystemBPLoader>();
    CesiumCreditSystemBP = bpLoader->CesiumCreditSystemBP;
    bpLoader->ConditionalBeginDestroy();
  }

  UWorld* world = WorldContextObject->GetWorld();
  // This method can be called by actors even when opening the content browser.
  if (!IsValid(world)) {
    return nullptr;
  }
  UE_LOG(
      LogCesium,
      Verbose,
      TEXT("World name for GetDefaultCreditSystem: %s"),
      *world->GetFullName());

  // Note: The actor iterator will be created with the
  // "EActorIteratorFlags::SkipPendingKill" flag,
  // meaning that we don't have to handle objects
  // that have been deleted. (This is the default,
  // but made explicit here)
  ACesiumCreditSystem* pCreditSystem = nullptr;
  EActorIteratorFlags flags = EActorIteratorFlags::OnlyActiveLevels |
                              EActorIteratorFlags::SkipPendingKill;
  for (TActorIterator<AActor> actorIterator(
           world,
           ACesiumCreditSystem::StaticClass(),
           flags);
       actorIterator;
       ++actorIterator) {
    AActor* actor = *actorIterator;
    if (actor->ActorHasTag(DEFAULT_CREDITSYSTEM_TAG)) {
      pCreditSystem = Cast<ACesiumCreditSystem>(actor);
      break;
    }
  }
  if (!pCreditSystem) {
    // Legacy method of finding Georeference, for backwards compatibility with
    // existing projects
    ACesiumCreditSystem* pCreditSystemCandidate =
        findValidDefaultCreditSystem(world->PersistentLevel);

    // Test if PendingKill
    if (IsValid(pCreditSystemCandidate)) {
      pCreditSystem = pCreditSystemCandidate;
    }
  }
  if (!pCreditSystem) {
    UE_LOG(
        LogCesium,
        Verbose,
        TEXT("Creating default Credit System for actor %s"),
        *WorldContextObject->GetName());
    // Spawn georeference in the persistent level
    FActorSpawnParameters spawnParameters;
    spawnParameters.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    pCreditSystem = world->SpawnActor<ACesiumCreditSystem>(
        CesiumCreditSystemBP,
        spawnParameters);
    // Null check so the editor doesn't crash when it makes arbitrary calls to
    // this function without a valid world context object.
    if (pCreditSystem) {
      pCreditSystem->Tags.Add(DEFAULT_CREDITSYSTEM_TAG);
    }
  } else {
    UE_LOG(
        LogCesium,
        Verbose,
        TEXT("Using existing CreditSystem %s for actor %s"),
        *pCreditSystem->GetName(),
        *WorldContextObject->GetName());
  }
  return pCreditSystem;
}

ACesiumCreditSystem::ACesiumCreditSystem()
    : _pCreditSystem(std::make_shared<Cesium3DTilesSelection::CreditSystem>()),
      _lastCreditsCount(0) {
  PrimaryActorTick.bCanEverTick = true;
}

void ACesiumCreditSystem::BeginPlay() {
  Super::BeginPlay();
  this->updateCreditsViewport(true);
}

static const FName LevelEditorName("LevelEditor");

void ACesiumCreditSystem::OnConstruction(const FTransform& Transform) {
  Super::OnConstruction(Transform);

  this->updateCreditsViewport(false);

#if WITH_EDITOR
  if (!GetWorld()->IsGameWorld() &&
      FModuleManager::Get().IsModuleLoaded(LevelEditorName)) {
    FLevelEditorModule& LevelEditorModule =
        FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorName);
    LevelEditorModule.OnRedrawLevelEditingViewports().RemoveAll(this);
    LevelEditorModule.OnRedrawLevelEditingViewports().AddUObject(
        this,
        &ACesiumCreditSystem::OnRedrawLevelEditingViewports);
    FEditorSupportDelegates::CleanseEditor.RemoveAll(this);
    FEditorSupportDelegates::CleanseEditor.AddUObject(
        this,
        &ACesiumCreditSystem::OnCleanseEditor);
    // LevelEditorModule.OnMapChanged().RemoveAll(this);
    // LevelEditorModule.OnMapChanged().AddUObject(
    //     this,
    //     &ACesiumCreditSystem::OnMapChanged);
    FEditorDelegates::PreBeginPIE.RemoveAll(this);
    FEditorDelegates::PreBeginPIE.AddUObject(
        this,
        &ACesiumCreditSystem::OnPreBeginPIE);
    FEditorDelegates::EndPIE.RemoveAll(this);
    FEditorDelegates::EndPIE.AddUObject(this, &ACesiumCreditSystem::OnEndPIE);
  }
#endif
}

void ACesiumCreditSystem::updateCreditsViewport(bool recreateWidget) {
  if (IsRunningDedicatedServer())
    return;
  if (!IsValid(GetWorld()))
    return;

  if (!IsValid(CreditsWidget) || recreateWidget) {
    CreditsWidget =
        CreateWidget<UScreenCreditsWidget>(GetWorld(), CreditsWidgetClass);
  }

#if WITH_EDITOR
  if (!GetWorld()->IsGameWorld() &&
      FModuleManager::Get().IsModuleLoaded(LevelEditorName)) {
    // Add credits to the active editor viewport
    FLevelEditorModule& levelEditorModule =
        FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorName);
    TSharedPtr<IAssetViewport> pActiveViewport =
        levelEditorModule.GetFirstActiveViewport();
    if (pActiveViewport.IsValid() &&
        this->_pLastEditorViewport != pActiveViewport) {
      this->removeCreditsFromViewports();

      if (!pActiveViewport->HasPlayInEditorViewport()) {
        auto pSlateWidget = CreditsWidget->TakeWidget();
        pActiveViewport->AddOverlayWidget(pSlateWidget);
        this->_pLastEditorViewport = pActiveViewport;
      }
    }
    return;
  }

  this->removeCreditsFromViewports();
#endif

  // Add credits to a game viewport
  CreditsWidget->AddToViewport();
}

void ACesiumCreditSystem::removeCreditsFromViewports() {
#if WITH_EDITOR
  if (this->_pLastEditorViewport.IsValid()) {
    auto pPinned = this->_pLastEditorViewport.Pin();
    pPinned->RemoveOverlayWidget(CreditsWidget->TakeWidget());
    this->_pLastEditorViewport = nullptr;
  }
#endif

  if (IsValid(CreditsWidget)) {
    CreditsWidget->RemoveFromViewport();
  }
}

#if WITH_EDITOR
void ACesiumCreditSystem::OnRedrawLevelEditingViewports(bool) {
  this->updateCreditsViewport(false);
}
void ACesiumCreditSystem::OnMapChanged(
    UWorld* pWorld,
    EMapChangeType changeType) {
  if (changeType == EMapChangeType::TearDownWorld) {
    this->removeCreditsFromViewports();
  }
}
void ACesiumCreditSystem::OnPreBeginPIE(bool bIsSimulating) {
  // When we start play-in-editor, remove the editor viewport credits.
  // The game will often reuse the same viewport, and we don't want to show
  // two sets of credits.
  this->removeCreditsFromViewports();
}
void ACesiumCreditSystem::OnEndPIE(bool bIsSimulating) {
  this->updateCreditsViewport(false);
}
void ACesiumCreditSystem::OnCleanseEditor() {
  this->removeCreditsFromViewports();
}
#endif

bool ACesiumCreditSystem::ShouldTickIfViewportsOnly() const { return true; }

void ACesiumCreditSystem::Tick(float DeltaTime) {
  Super::Tick(DeltaTime);

  if (!_pCreditSystem || !IsValid(CreditsWidget)) {
    return;
  }

  const std::vector<Cesium3DTilesSelection::Credit>& creditsToShowThisFrame =
      _pCreditSystem->getCreditsToShowThisFrame();

  // if the credit list has changed, we want to reformat the credits
  CreditsUpdated =
      creditsToShowThisFrame.size() != _lastCreditsCount ||
      _pCreditSystem->getCreditsToNoLongerShowThisFrame().size() > 0;

  if (CreditsUpdated) {
    FString OnScreenCredits;
    FString Credits;

    _lastCreditsCount = creditsToShowThisFrame.size();

    bool firstCreditOnScreen = true;
    for (int i = 0; i < creditsToShowThisFrame.size(); i++) {
      const Cesium3DTilesSelection::Credit& credit = creditsToShowThisFrame[i];

      FString CreditRtf;
      const std::string& html = _pCreditSystem->getHtml(credit);

      auto htmlFind = _htmlToRtf.find(html);
      if (htmlFind != _htmlToRtf.end()) {
        CreditRtf = htmlFind->second;
      } else {
        CreditRtf = ConvertHtmlToRtf(html);
        _htmlToRtf.insert({html, CreditRtf});
      }

      if (_pCreditSystem->shouldBeShownOnScreen(credit)) {
        if (firstCreditOnScreen) {
          firstCreditOnScreen = false;
        } else {
          OnScreenCredits += TEXT(" \u2022 ");
        }

        OnScreenCredits += CreditRtf;
      } else {
        if (i != 0) {
          Credits += "\n";
        }

        Credits += CreditRtf;
      }
    }

    if (!Credits.IsEmpty()) {
      OnScreenCredits += "<credits url=\"popup\" text=\" Data attribution\"/>";
    }

    CreditsWidget->SetCredits(Credits, OnScreenCredits);
  }
  _pCreditSystem->startNextFrame();
}

namespace {
void convertHtmlToRtf(
    std::string& output,
    std::string& parentUrl,
    TidyDoc tdoc,
    TidyNode tnod,
    UScreenCreditsWidget* CreditsWidget) {
  TidyNode child;
  TidyBuffer buf;
  tidyBufInit(&buf);
  for (child = tidyGetChild(tnod); child; child = tidyGetNext(child)) {
    if (tidyNodeIsText(child)) {
      tidyNodeGetText(tdoc, child, &buf);
      if (buf.bp) {
        std::string text = reinterpret_cast<const char*>(buf.bp);
        tidyBufClear(&buf);
        // could not find correct option in tidy html to not add new lines
        if (text.size() != 0 && text[text.size() - 1] == '\n') {
          text.pop_back();
        }
        if (!parentUrl.empty()) {
          output +=
              "<credits url=\"" + parentUrl + "\"" + " text=\"" + text + "\"/>";
        } else {
          output += text;
        }
      }
    } else if (tidyNodeGetId(child) == TidyTagId::TidyTag_IMG) {
      auto srcAttr = tidyAttrGetById(child, TidyAttrId::TidyAttr_SRC);
      if (srcAttr) {
        auto srcValue = tidyAttrValue(srcAttr);
        if (srcValue) {
          output += "<credits id=\"" +
                    CreditsWidget->LoadImage(
                        std::string(reinterpret_cast<const char*>(srcValue))) +
                    "\"";
          if (!parentUrl.empty()) {
            output += " url=\"" + parentUrl + "\"";
          }
          output += "/>";
        }
      }
    }
    auto hrefAttr = tidyAttrGetById(child, TidyAttrId::TidyAttr_HREF);
    if (hrefAttr) {
      auto hrefValue = tidyAttrValue(hrefAttr);
      parentUrl = std::string(reinterpret_cast<const char*>(hrefValue));
    }
    convertHtmlToRtf(output, parentUrl, tdoc, child, CreditsWidget);
  }
  tidyBufFree(&buf);
}
} // namespace

FString ACesiumCreditSystem::ConvertHtmlToRtf(std::string html) {
  TidyDoc tdoc;
  TidyBuffer tidy_errbuf = {0};
  int err;

  tdoc = tidyCreate();
  tidyOptSetBool(tdoc, TidyForceOutput, yes);
  tidyOptSetInt(tdoc, TidyWrapLen, 0);
  tidyOptSetInt(tdoc, TidyNewline, TidyLF);

  tidySetErrorBuffer(tdoc, &tidy_errbuf);

  html = "<!DOCTYPE html><html><body>" + html + "</body></html>";

  std::string output, url;
  err = tidyParseString(tdoc, html.c_str());
  if (err < 2) {
    convertHtmlToRtf(output, url, tdoc, tidyGetRoot(tdoc), CreditsWidget);
  }
  tidyBufFree(&tidy_errbuf);
  tidyRelease(tdoc);
  return UTF8_TO_TCHAR(output.c_str());
}
