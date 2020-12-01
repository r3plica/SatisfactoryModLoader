#include "SatisfactoryModLoader.h"
#include "bootstrapper_exports.h"
#include "ConfigManager.h"
#include "FGAssetDumper.h"
#include "FGPlayerController.h"
#include "ItemTooltipSubsystem.h"
#include "LegacyConfigurationHelper.h"
#include "ModContentRegistry.h"
#include "ModHandler.h"
#include "NativeHookManager.h"
#include "NetworkHandler.h"
#include "RemoteCallObjectRegistry.h"
#include "SMLModule.h"
#include "SMLNetworkManager.h"
#include "SMLRemoteCallObject.h"
#include "SMLSubsystemHolder.h"
#include "SubsystemHolderRegistry.h"
#include "Patch/CrashContextPatch.h"
#include "Patch/MainMenuPatch.h"
#include "Patch/OfflinePlayerHandler.h"
#include "Patch/OptionsKeybindPatch.h"
#include "Player/PlayerCheatManagerHandler.h"

extern "C" DLLEXPORT const TCHAR* modLoaderVersionString = TEXT("2.3.0");
extern "C" DLLEXPORT const TCHAR* targetBootstrapperVersionString = TEXT("2.0.11");
extern "C" DLLEXPORT const long targetGameVersion = 137570;

DEFINE_LOG_CATEGORY(LogSatisfactoryModLoader);

const FName FModLoaderExtraAttributes::EA_BootstrapperVersion = TEXT("BootstrapperVersion");

TSharedPtr<FModHandler> FSatisfactoryModLoader::ModHandlerPrivate = NULL;
TSharedPtr<BootstrapAccessors> FSatisfactoryModLoader::BootstrapperAccessors = NULL;
FSMLConfiguration FSatisfactoryModLoader::SMLConfigurationPrivate;

FVersion FSatisfactoryModLoader::GetModLoaderVersion() {
    static FVersion* ModLoaderVersion = NULL;
    if (ModLoaderVersion == NULL) {
        FString OutErrorMessage;
        ModLoaderVersion = new FVersion();
        ModLoaderVersion->ParseVersion(modLoaderVersionString, OutErrorMessage);
    }
    return *ModLoaderVersion;
}

TMap<FName, FString> FSatisfactoryModLoader::GetExtraAttributes() {
    TMap<FName, FString> OutExtraAttributes;
    if (BootstrapperAccessors.IsValid()) {
        OutExtraAttributes.Add(FModLoaderExtraAttributes::EA_BootstrapperVersion, BootstrapperAccessors->version);
    }
    return OutExtraAttributes;
}

void FSatisfactoryModLoader::SetupBootstrapperAccessors(const BootstrapAccessors& Accessors) {
    BootstrapperAccessors = MakeShareable(new BootstrapAccessors(Accessors));
}

void FSatisfactoryModLoader::LoadSMLConfiguration(bool bAllowSave) {
    const FString ConfigLocation = FLegacyConfigurationHelper::GetModConfigFilePath(TEXT("SML"));
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    bool bShouldWriteConfiguration = false;
    
    if (PlatformFile.FileExists(*ConfigLocation)) {
        FString RawSMLConfiguration;
        if (FFileHelper::LoadFileToString(RawSMLConfiguration, *ConfigLocation)) {
            
            TSharedPtr<FJsonObject> OutJsonObject;
            const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(RawSMLConfiguration);
            
            if (FJsonSerializer::Deserialize(JsonReader, OutJsonObject)) {
                FSMLConfiguration::ReadFromJson(OutJsonObject, SMLConfigurationPrivate, &bShouldWriteConfiguration);
                UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Successfully loaded SML configuration from disk"));
                
            } else {
                UE_LOG(LogSatisfactoryModLoader, Warning, TEXT("Failed to load SML configuration, JSON is malformed"));
                bShouldWriteConfiguration = true;
            }
        } else {
            UE_LOG(LogSatisfactoryModLoader, Error, TEXT("Failed to load SML configuration from %s"), *ConfigLocation);   
        }
    } else {
        UE_LOG(LogSatisfactoryModLoader, Display, TEXT("SML configuration file is missing, saving new one"));
        bShouldWriteConfiguration = true;
    }

    if (bShouldWriteConfiguration) {
        const TSharedRef<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
        FSMLConfiguration::WriteToJson(JsonObject, SMLConfigurationPrivate);

        FString OutSerializedConfiguration;
        const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&OutSerializedConfiguration);
        FJsonSerializer::Serialize(JsonObject, JsonWriter);

        //Make sure configuration directory exists
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(ConfigLocation));

        //Write file onto the disk now
        if (FFileHelper::SaveStringToFile(OutSerializedConfiguration, *ConfigLocation)) {
            UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Successfully saved SML configuration"));
        } else {
            UE_LOG(LogSatisfactoryModLoader, Error, TEXT("Failed to save SML configuration to %s"), *ConfigLocation);
        }
    }
}

void FSatisfactoryModLoader::CheckGameAndBootstrapperVersion() {
    const uint32 CurrentChangelist = FEngineVersion::Current().GetChangelist();
    const uint32 MinChangelistSupported = (uint32) targetGameVersion;
    
    if (!(CurrentChangelist >= MinChangelistSupported)) {
        UE_LOG(LogSatisfactoryModLoader, Fatal, TEXT("Game version check failed: Game version is %d, but this SML version is built for %d"), CurrentChangelist, MinChangelistSupported);
    }

    FString OutErrorMessage;
    FVersion MinSupportedBootstrapperVersion;
    MinSupportedBootstrapperVersion.ParseVersion(targetBootstrapperVersionString, OutErrorMessage);

    if (BootstrapperAccessors.IsValid()) {
        FVersion BootstrapperVersion;
        BootstrapperVersion.ParseVersion(BootstrapperAccessors->version, OutErrorMessage);
        if (BootstrapperVersion.Compare(MinSupportedBootstrapperVersion) < 0) {
            UE_LOG(LogSatisfactoryModLoader, Fatal, TEXT("Bootstrapp version check failed: Bootstrapper version is %s, but this SML version only supports %s"), *BootstrapperVersion.ToString(), *MinSupportedBootstrapperVersion.ToString());
        }
    }
    UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Version check passed successfully! Game Changelist: %d"), CurrentChangelist);
}

void FSatisfactoryModLoader::RegisterSubsystemPatches() {
    //Initialize patches required for subsystem holder registry to function
    USubsystemHolderRegistry::InitializePatches();
    
    //Disable vanilla content resolution by patching vanilla lookup methods
    AModContentRegistry::DisableVanillaContentRegistration();

    //Register remote call object registry hook
    URemoteCallObjectRegistry::InitializePatches();
    
    //Register SML chat commands subsystem patch (should actually be in CommandSubsystem i guess)
    USMLRemoteCallObject::RegisterChatCommandPatch();

    //Initialize network manager handling mod packets
    UModNetworkHandler::InitializePatches();

    //Initialize tooltip handler
    UItemTooltipSubsystem::InitializePatches();

    //Register offline player handler, providing ability to fallback to offline username and net id
    FOfflinePlayerHandler::RegisterHandlerPatches();

    //Register main menu additions, like mod list and labels
    FMainMenuPatch::RegisterPatch();

    //Register options menu key bindings patch, providing better keybind categorization
    FOptionsKeybindPatch::RegisterPatch();
}

void FSatisfactoryModLoader::RegisterSubsystems() {
    //Register cheat manager handling, allowing access to cheat commands if desired
    FPlayerCheatManagerHandler::RegisterHandler();

    //Register version checker for remote connections
    FSMLNetworkManager::RegisterMessageTypeAndHandlers();

    //Register asset dumping related console commands
    FGameAssetDumper::RegisterConsoleCommands();
}

void FSatisfactoryModLoader::PreInitializeModLoading() {
    UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Satisfactory Mod Loader v.%s pre-initializing..."), modLoaderVersionString);

    //Don't try to save configuration in the editor, because it will make new folders with no real reason
    const bool bAllowSavingConfiguration = !WITH_EDITOR;
    LoadSMLConfiguration(bAllowSavingConfiguration);

    if (BootstrapperAccessors.IsValid()) {
        UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Using bootstrapper v.%s for mod loading"), BootstrapperAccessors->version);
    }

    //Check versions before actually trying to load mods
    CheckGameAndBootstrapperVersion();

    //Setup hooking with bootstrapper accessors if we have them
    //TODO hooking should be perfectly capable of working without accessors or PDB loading
    if (BootstrapperAccessors.IsValid()) {
        FNativeHookManagerInternal::SetupWithAccessors(*BootstrapperAccessors);
    }

    //Initialize ModHandler and perform mods discovery and pre initialization
    ModHandlerPrivate = MakeShareable(new FModHandler());
    if (BootstrapperAccessors.IsValid()) {
        ModHandlerPrivate->SetupWithAccessors(*BootstrapperAccessors);
    }
    
    //Perform mod discovery and check for stage errors
    UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Performing mod discovery"));
    ModHandlerPrivate->DiscoverMods();

    UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Performing mod sorting"));
    ModHandlerPrivate->PerformModListSorting();

    //Perform mods pre initialization (load native module DLLs into process)
    UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Pre-initializing mods"));
    ModHandlerPrivate->PreInitializeMods();

    //Register crash context patch very early, but after mod loading
    //So debug symbols can be flushed now from loaded native modules
    if (BootstrapperAccessors.IsValid()) {
        FCrashContextPatch::SetupWithAccessors(*BootstrapperAccessors);
    }
    FCrashContextPatch::RegisterPatch();

    //Show console if we have been asked to in configuration
    //Console can also be shown using -LOG command line switch
    if (GetSMLConfiguration().bConsoleWindow) {
        GLogConsole->Show(true);
    }

    UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Pre-initialization finished!"));
}

void FSatisfactoryModLoader::InitializeModLoading() {
    UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Performing mod loader initialization"));

    //Setup SML subsystems and custom content registries
    UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Registering subsystem patches..."));
    RegisterSubsystemPatches();
    UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Registering global subsystems..."));
    RegisterSubsystems();

    //Subscribe to world lifecycle event for mod initializers
    ModHandlerPrivate->SubscribeToWorldEvents();

    //Perform actual mod loading
    UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Initializing mods"));
    ModHandlerPrivate->InitializeMods();

    //Initialize game instance subsystems and give mods opportunity to register global content
    UGameInstance* GameInstance = Cast<UGameEngine>(GEngine)->GameInstance;
    ModHandlerPrivate->InitializeGameInstanceModules(GameInstance);

    //Reload configuration manager to handle mod configs
    UConfigManager* ConfigManager = GEngine->GetEngineSubsystem<UConfigManager>();
    ConfigManager->ReloadModConfigurations(true);
    ModHandlerPrivate->PostInitializeGameInstanceModules(GameInstance);

    UE_LOG(LogSatisfactoryModLoader, Display, TEXT("Initialization finished!"));
}

//Helper to access private methods from mod loader class without exposing them to everyone
class FSatisfactoryModLoaderInternal {
public:
    static void EnsureSMLModuleInitialized() {
        //Make sure SML module is already loaded at this point because mod loading depends on it
        FModHandler::LoadModuleChecked(TEXT("SML"), InitializeModule);
    }
    
    static void BootstrapModLoaderHelper(BootstrapAccessors& BootstrapAccessors) { 
        //Set bootstrapper accessors
        FSatisfactoryModLoader::SetupBootstrapperAccessors(BootstrapAccessors);

        //Make sure SML module is already loaded at this point because mod loading depends on it
        FCoreDelegates::OnInit.AddStatic(EnsureSMLModuleInitialized);
        
        //Basic subsystems like logging are initialized on OnInit
        FCoreDelegates::OnInit.AddStatic(FSatisfactoryModLoader::PreInitializeModLoading);
        
        //UObject subsystem and Engine are initialized on PostEngineInit
        FCoreDelegates::OnPostEngineInit.AddStatic(FSatisfactoryModLoader::InitializeModLoading);
    }
};


//Called by bootstrapper very early to initialize mod loader
extern "C" SML_API void BootstrapModule(BootstrapAccessors& Accessors) {
	FSatisfactoryModLoaderInternal::BootstrapModLoaderHelper(Accessors);
}