// This file has been automatically generated by the Unreal Header Implementation tool

#include "FGRemoteCallObject.h"

bool UFGRemoteCallObject::IsSupportedForNetworking() const{ return bool(); }
int32 UFGRemoteCallObject::GetFunctionCallspace(UFunction* Function, FFrame* Stack){ return int32(); }
bool UFGRemoteCallObject::CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack){ return bool(); }
AFGGameState* UFGRemoteCallObject::GetGameState(){ return nullptr; }
void UFGRemoteCallObject::SendHologramConstructMessage( AFGBuildGun* buildGun,  AFGHologram* hologram, AActor* upgradedActor){ }
void UFGRemoteCallObject::Server_ConstructHologram_Implementation(FNetConstructionID netConstructionID, FConstructHologramMessage message){ }
bool UFGRemoteCallObject::Server_ConstructHologram_Validate(FNetConstructionID netConstructionID, FConstructHologramMessage message){ return bool(); }
void UFGRemoteCallObject::Server_AnswerCall_Implementation(TSubclassOf<  UFGAudioMessage > messageClass){ }
bool UFGRemoteCallObject::Server_AnswerCall_Validate(TSubclassOf<  UFGAudioMessage > messageClass){ return bool(); }
AFGPlayerController* UFGRemoteCallObject::GetOuterFGPlayerController(){ return nullptr; }
