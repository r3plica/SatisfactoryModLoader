// This file has been automatically generated by the Unreal Header Implementation tool

#include "Environment/FGRiver.h"

#if WITH_EDITORONLY_DATA
void AFGRiver::OnSelected(UObject* Object){ }
void AFGRiver::ReDraw(){ }
void AFGRiver::DrawDebugData(){ }
void AFGRiver::StopDrawDebugData(){ }
void AFGRiver::Destroyed(){ }
void AFGRiver::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent){ Super::PostEditChangeProperty(PropertyChangedEvent); }
#endif 
#if WITH_EDITOR
void AFGRiver::UpdateFlowIntensity(){ }
void AFGRiver::PopulateMaterialSettings(){ }
void AFGRiver::BuildRiverFromData(const UObject* WorldContext, TSubclassOf<AFGRiver> BaseClass, TArray<FVector> WorldLocations, TArray<FVector> PointScales, TArray<FRotator> PointRotations, TArray<FVector> ArriaveTangets, TArray<FVector> LeaveTangents){ }
#endif 
AFGRiver::AFGRiver(){ }
void AFGRiver::BeginPlay(){ }
void AFGRiver::OnConstruction(const FTransform & Transform){ }
void AFGRiver::ConstructMesh(){ }
void AFGRiver::ConstructVolumes(){ }
void AFGRiver::SetupMaterialValues(UMaterialInstanceDynamic* Material){ }
void AFGRiver::ApplyVertexColors(TArray<USplineMeshComponent*>& MeshComponents){ }
float AFGRiver::GetRiverLocationHeightOffset(FVector WorldLocation, float IntensityOnSpline) const{ return float(); }
void AFGRiver::HandlePush(float DeltaTime, ACharacter* Actor){ }
void AFGRiver::Tick(float DeltaTime){ }
void AFGRiver::OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult){ }
