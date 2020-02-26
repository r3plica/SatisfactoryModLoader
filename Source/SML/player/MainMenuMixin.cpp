#include "mod/hooking.h"
#include "FGGameInstance.h"
#include "util/Logging.h"
#include "MainMenuMixin.h"
#include "Slate.h"
#include "mod/ModHandler.h"
#include "SatisfactoryModLoader.h"

class UFGGameInstanceProto {
public: void LoadComplete(const float loadTime, const FString& mapName) {}
};

TSharedRef<SWidget> CreateMenuInfoTextPanel() {
	const FSlateFontInfo& fontInfo = FSlateFontInfo(FString(TEXT("Roboto")), 10, EFontHinting::Auto, FFontOutlineSettings());
	SML::Mod::FModHandler& modHandler = SML::getModHandler();
	const size_t modsLoaded = modHandler.getLoadedMods().size();
	TArray<FString> resultText;
	resultText.Add(FString::Printf(TEXT("Satisfactory Mod Loader v.%s"), SML::getModLoaderVersion().string().c_str()));
	resultText.Add(FString::Printf(TEXT("%llu mod(s) loaded"), modsLoaded));
	resultText.Add(FString::Printf(TEXT("Bootstrapper v.%s"), SML::getBootstrapperVersion().string().c_str()));
	if (SML::getSMLConfig().developmentMode) {
		resultText.Add(TEXT("Development mode enabled."));
	}
	const TSharedRef<STextBlock> MyTextBlock = SNew(STextBlock)
		.Font(fontInfo)
		.ColorAndOpacity(FSlateColor(FLinearColor(0.7, 0.7, 0.7)))
		.Text(FText::FromString(FString::Join(resultText, TEXT("\n"))));
	TSharedRef<SWidget> ResultTextWidget = SNew(SBox)
		.HAlign(EHorizontalAlignment::HAlign_Left)
		.VAlign(EVerticalAlignment::VAlign_Bottom)
		.Padding(FMargin(40.0f, 0.0f, 0.0f, 40.0f))[MyTextBlock];
	return ResultTextWidget;
}

/**
 * Defines widget to be displayed when the given map is loaded, and
 * removed from the screen overlay when another map is load,
 * efficiently allowing to have per-map widget sets
 * For example, SML has mod list information in main menu world, but
 * doesn't have any information overlays while playing
 */
#define DEFINE_LEVEL_WIDGET_PANEL(Name, MapName, InitializerFunc) \
	static TSharedPtr<SWidget> CurrentWidgetPtr_##Name; \
	if (mapName == TEXT(MapName)) { \
		TSharedRef<SWidget> ResultWidget = InitializerFunc(); \
		CurrentWidgetPtr_##Name = ResultWidget; \
		viewport->AddViewportWidgetContent(ResultWidget, 0); \
	} else if (CurrentWidgetPtr_##Name.IsValid()) { \
		TSharedRef<SWidget> ResultWidgetRef = CurrentWidgetPtr_##Name.ToSharedRef(); \
		viewport->RemoveViewportWidgetContent(ResultWidgetRef); \
		CurrentWidgetPtr_##Name.Reset(); \
	}


void SML::registerMainMenuHooks() {
	SUBSCRIBE_METHOD("?LoadComplete@UFGGameInstance@@MEAAXMAEBVFString@@@Z", UFGGameInstanceProto::LoadComplete, [](CallResult<void>&, void* thisPtr, const float, const FString& mapName) {
		UFGGameInstance* gameInstance = reinterpret_cast<UFGGameInstance*>(thisPtr);
		UGameViewportClient* viewport = gameInstance->GetWorld()->GetGameViewport();
		DEFINE_LEVEL_WIDGET_PANEL(MenuSMLInfo, "/Game/FactoryGame/Map/MenuScenes/MenuScene_01", CreateMenuInfoTextPanel);
	});
}