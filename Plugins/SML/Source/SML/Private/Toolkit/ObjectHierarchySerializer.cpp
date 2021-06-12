#include "Toolkit/ObjectHierarchySerializer.h"
#include "Toolkit/PropertySerializer.h"
#include "Toolkit/AssetTypes/AssetHelper.h"
#include "UObject/Package.h"
#include "Toolkit/DefaultSerializableNativeClasses.h"

DECLARE_LOG_CATEGORY_CLASS(LogObjectHierarchySerializer, Warning, Log);

TSet<FName> UObjectHierarchySerializer::UnhandledNativeClasses;

UObjectHierarchySerializer::UObjectHierarchySerializer() {
    this->LastObjectIndex = 0;
    AllowedNativeSerializeClasses.Add(UObject::StaticClass());
    APPEND_DEFAULT_SERIALIZABLE_NATIVE_CLASSES(AllowedNativeSerializeClasses.Add);
}

void UObjectHierarchySerializer::SetPropertySerializer(UPropertySerializer* NewPropertySerializer) {
	check(NewPropertySerializer);
	this->PropertySerializer = NewPropertySerializer;
	NewPropertySerializer->ObjectHierarchySerializer = this;
}

void UObjectHierarchySerializer::InitializeForDeserialization(const TArray<TSharedPtr<FJsonValue>>& ObjectsArray) {
	this->LastObjectIndex = ObjectsArray.Num();
	for (int32 i = 0; i < LastObjectIndex; i++) {
		SerializedObjects.Add(i, ObjectsArray[i]->AsObject());
	}
}

void UObjectHierarchySerializer::InitializeForSerialization(UPackage* NewSourcePackage) {
	check(NewSourcePackage);
	this->SourcePackage = NewSourcePackage;
}

void UObjectHierarchySerializer::SetPackageForDeserialization(UPackage* SelfPackage) {
	check(SelfPackage);
	this->SourcePackage = SelfPackage;
}

void UObjectHierarchySerializer::AllowNativeClassSerialization(UClass* ClassToAllow) {
    this->AllowedNativeSerializeClasses.AddUnique(ClassToAllow);
}

void UObjectHierarchySerializer::SetObjectMark(UObject* Object, const FString& ObjectMark) {
    this->ObjectMarks.Add(Object, ObjectMark);
}

int32 UObjectHierarchySerializer::SerializeObject(UObject* Object) {
    if (Object == nullptr) {
        return INDEX_NONE;
    }
    
    const int32* ObjectIndex = ObjectIndices.Find(Object);
    if (ObjectIndex != nullptr) {
        return *ObjectIndex;
    }
    
    const int32 NewObjectIndex = LastObjectIndex++;
    ObjectIndices.Add(Object, NewObjectIndex);
    UPackage* ObjectPackage = Object->GetOutermost();
    
    TSharedRef<FJsonObject> ResultJson = MakeShareable(new FJsonObject());
    ResultJson->SetNumberField(TEXT("ObjectIndex"), NewObjectIndex);
    SerializedObjects.Add(NewObjectIndex, ResultJson);
    
    if (ObjectPackage != SourcePackage) {
        ResultJson->SetStringField(TEXT("Type"), TEXT("Import"));
        SerializeImportedObject(ResultJson, Object);
        
    } else {
        ResultJson->SetStringField(TEXT("Type"), TEXT("Export"));

        if (ObjectMarks.Contains(Object)) {
            //This object is serialized using object mark string
            ResultJson->SetStringField(TEXT("ObjectMark"), ObjectMarks.FindChecked(Object));

        } else {
            //Serialize object normally
            SerializeExportedObject(ResultJson, Object);
        }
    }
    
    return NewObjectIndex;
}

UObject* UObjectHierarchySerializer::DeserializeObject(int32 Index) {
    if (Index == INDEX_NONE) {
        return nullptr;
    }
	
    UObject** LoadedObject = LoadedObjects.Find(Index);
    if (LoadedObject != nullptr) {
        return *LoadedObject;
    }
	
    if (!SerializedObjects.Contains(Index)) {
        UE_LOG(LogObjectHierarchySerializer, Error, TEXT("DeserializeObject for package %s called with invalid Index: %d"), *SourcePackage->GetName(), Index);
        return nullptr;
    }
	
    const TSharedPtr<FJsonObject>& ObjectJson = SerializedObjects.FindChecked(Index);
    const FString ObjectType = ObjectJson->GetStringField(TEXT("Type"));
    
    if (ObjectType == TEXT("Import")) {
        //Object is imported from another package, and not located in our own
        UObject* NewLoadedObject = DeserializeImportedObject(ObjectJson);
        LoadedObjects.Add(Index, NewLoadedObject);
        return NewLoadedObject;
    }

    if (ObjectType == TEXT("Export")) {
        //Object is defined inside our own package
        UObject* ConstructedObject;

        if (ObjectJson->HasField(TEXT("ObjectMark"))) {
            
            //Object is serialized through object mark
            const FString ObjectMark = ObjectJson->GetStringField(TEXT("ObjectMark"));
            UObject* const* FoundObject = ObjectMarks.FindKey(ObjectMark);
            checkf(FoundObject, TEXT("Cannot resolve object serialized using mark: %s"), *ObjectMark);
            ConstructedObject = *FoundObject;
            
        } else {
            //Perform normal deserialization
            ConstructedObject = DeserializeExportedObject(ObjectJson);
        }
        
        LoadedObjects.Add(Index, ConstructedObject);
        return ConstructedObject;
    }
    
    UE_LOG(LogObjectHierarchySerializer, Fatal, TEXT("Unhandled object type: %s for package %s"), *ObjectType, *SourcePackage->GetPathName());
    return nullptr;
}


TSharedRef<FJsonObject> UObjectHierarchySerializer::SerializeObjectProperties(UObject* Object) {
    TSharedRef<FJsonObject> Properties = MakeShareable(new FJsonObject());
    SerializeObjectPropertiesIntoObject(Object, Properties);
    return Properties;
}

void UObjectHierarchySerializer::SerializeObjectPropertiesIntoObject(UObject* Object, TSharedPtr<FJsonObject> Properties) {
    UClass* ObjectClass = Object->GetClass();
	TArray<int32> ReferencedSubobjects;

	//Serialize actual object property values
    for (FProperty* Property = ObjectClass->PropertyLink; Property; Property = Property->PropertyLinkNext) {
        if (PropertySerializer->ShouldSerializeProperty(Property)) {
            const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Object);
            TSharedRef<FJsonValue> PropertyValueJson = PropertySerializer->SerializePropertyValue(Property, PropertyValue, &ReferencedSubobjects);
        	
            Properties->SetField(Property->GetName(), PropertyValueJson);
        }
    }

	//Also write $ReferencedSubobjects field used for deserialization dependency gathering
	TArray<TSharedPtr<FJsonValue>> ReferencedSubobjectsArray;
	for (const int32 ObjectIndex : ReferencedSubobjects) {
		ReferencedSubobjectsArray.Add(MakeShareable(new FJsonValueNumber(ObjectIndex)));
	}
	
	Properties->SetArrayField(TEXT("$ReferencedObjects"), ReferencedSubobjectsArray);
}

bool UObjectHierarchySerializer::CompareUObjects(const int32 ObjectIndex, UObject* Object) {
	//If either of the operands are null, they are only equal if both are NULL
	if (ObjectIndex == INDEX_NONE || Object == NULL) {
		return ObjectIndex == INDEX_NONE && Object == NULL;
	}

	const TSharedPtr<FJsonObject>& ObjectJson = SerializedObjects.FindChecked(ObjectIndex);
	const FString ObjectType = ObjectJson->GetStringField(TEXT("Type"));

	//Object is imported from another package, and not located in our own
	if (ObjectType == TEXT("Import")) {
		//Return early if object name doesn't match
		const FString ObjectName = ObjectJson->GetStringField(TEXT("ObjectName"));
		if (Object->GetName() != ObjectName) {
			return false;
		}

		//If we have outer, compare them too to make sure they match
		if (ObjectJson->HasField(TEXT("Outer"))) {
			const int32 OuterObjectIndex = ObjectJson->GetIntegerField(TEXT("Outer"));
			return CompareUObjects(OuterObjectIndex, Object->GetOuter());
		}
		//We end up here if we have no outer but have matching name, in which case we represent top-level object
		return true;
	}

	//Otherwise we are dealing with the exported object
	//Check if object is serialized through mark first
	if (ObjectJson->HasField(TEXT("ObjectMark"))) {
		const FString ObjectMark = ObjectJson->GetStringField(TEXT("ObjectMark"));
		UObject* const* FoundObject = ObjectMarks.FindKey(ObjectMark);
		checkf(FoundObject, TEXT("Cannot resolve object serialized using mark: %s"), *ObjectMark);

		//Marked objects only match if they point to the same UObject
		UObject* RegisteredObject = *FoundObject;
		return RegisteredObject == Object;
	}

	//Make sure object name matches first
	const FString ObjectName = ObjectJson->GetStringField(TEXT("ObjectName"));
	if (Object->GetName() != ObjectName) {
		return false;
	}

	//Make sure object class matches provided one
	const int32 ObjectClassIndex = ObjectJson->GetIntegerField(TEXT("ObjectClass"));
	if (!CompareUObjects(ObjectClassIndex, Object->GetClass())) {
		return false;
	}
        
    //If object is missing outer, we are dealing with the package itself. Then SourcePackage must match Object,
	//and we do not have any properties recorded for UPackage, so we return early
    if (!ObjectJson->HasField(TEXT("Outer"))) {
        return SourcePackage == Object;
    }

	//Otherwise make sure outer object matches
    const int32 OuterObjectIndex = ObjectJson->GetIntegerField(TEXT("Outer"));
	if (!CompareUObjects(OuterObjectIndex, Object->GetOuter())) {
		return false;
	}

    //Deserialize object properties now
    if (ObjectJson->HasField(TEXT("Properties"))) {
        const TSharedPtr<FJsonObject>& Properties = ObjectJson->GetObjectField(TEXT("Properties"));
        return AreObjectPropertiesUpToDate(Properties.ToSharedRef(), Object);
    }

	//No properties detected, we are matching just fine in that case
	return true;
}

bool UObjectHierarchySerializer::AreObjectPropertiesUpToDate(const TSharedRef<FJsonObject>& Properties, UObject* Object) {
	UClass* ObjectClass = Object->GetClass();

	//Iterate all properties and return false if our values do not match existing ones
	//This will also try to deserialize objects in "read only" mode, incrementing ObjectsNotUpToDate when existing object fields mismatch
	for (FProperty* Property = ObjectClass->PropertyLink; Property; Property = Property->PropertyLinkNext) {
		const FString PropertyName = Property->GetName();
		
		if (PropertySerializer->ShouldSerializeProperty(Property) && Properties->HasField(PropertyName)) {
			const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Object);
			const TSharedPtr<FJsonValue> ValueObject = Properties->Values.FindChecked(PropertyName);

			if (!PropertySerializer->ComparePropertyValues(Property, ValueObject.ToSharedRef(), PropertyValue)) {
				return false;
			}
		}
	}

	return true;
}

void UObjectHierarchySerializer::DeserializeObjectProperties(const TSharedRef<FJsonObject>& Properties, UObject* Object) {
    UClass* ObjectClass = Object->GetClass();
    for (FProperty* Property = ObjectClass->PropertyLink; Property; Property = Property->PropertyLinkNext) {
        const FString PropertyName = Property->GetName();
    	
        if (PropertySerializer->ShouldSerializeProperty(Property) && Properties->HasField(PropertyName)) {
            void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Object);
            const TSharedPtr<FJsonValue>& ValueObject = Properties->Values.FindChecked(PropertyName);
        	
        	PropertySerializer->DeserializePropertyValue(Property, ValueObject.ToSharedRef(), PropertyValue);
        }
    }
}

TArray<TSharedPtr<FJsonValue>> UObjectHierarchySerializer::FinalizeSerialization() {
    TArray<TSharedPtr<FJsonValue>> ObjectsArray;
    for (int32 i = 0; i < LastObjectIndex; i++) {
        if (!SerializedObjects.Contains(i)) {
            checkf(false, TEXT("Object not in serialized objects: %s"), *(*ObjectIndices.FindKey(i))->GetPathName());
        }
        ObjectsArray.Add(MakeShareable(new FJsonValueObject(SerializedObjects.FindChecked(i))));
    }
    return ObjectsArray;
}

void UObjectHierarchySerializer::CollectReferencedPackages(const TArray<TSharedPtr<FJsonValue>>& ReferencedSubobjects, TArray<FString>& OutReferencedPackageNames) {
	TArray<int32> AlreadySerializedObjects;
	CollectReferencedPackages(ReferencedSubobjects, OutReferencedPackageNames, AlreadySerializedObjects);
}

void UObjectHierarchySerializer::CollectReferencedPackages(const TArray<TSharedPtr<FJsonValue>>& ReferencedSubobjects, TArray<FString>& OutReferencedPackageNames, TArray<int32>& ObjectsAlreadySerialized) {
	for (const TSharedPtr<FJsonValue>& JsonValue : ReferencedSubobjects) {
		CollectObjectPackages(JsonValue->AsNumber(), OutReferencedPackageNames, ObjectsAlreadySerialized);
	}
}

void UObjectHierarchySerializer::CollectObjectPackages(const int32 ObjectIndex, TArray<FString>& OutReferencedPackageNames, TArray<int32>& ObjectsAlreadySerialized) {
    if (ObjectIndex == INDEX_NONE) {
        return;
    }
	if (ObjectsAlreadySerialized.Contains(ObjectIndex)) {
		return;
	}
	
	ObjectsAlreadySerialized.Add(ObjectIndex);
    const TSharedPtr<FJsonObject> Object = SerializedObjects.FindChecked(ObjectIndex);
    const FString ObjectType = Object->GetStringField(TEXT("Type"));
    
    if (ObjectType == TEXT("Import")) {
        const FString ClassPackage = Object->GetStringField(TEXT("ClassPackage"));
        if (!ClassPackage.StartsWith(TEXT("/Script/"))) {
            OutReferencedPackageNames.Add(ClassPackage);
        }

        if (Object->HasField(TEXT("Outer"))) {
            const int32 OuterObjectIndex = Object->GetIntegerField(TEXT("Outer"));
            CollectObjectPackages(OuterObjectIndex, OutReferencedPackageNames, ObjectsAlreadySerialized);
        } else {
            const FString PackageName = Object->GetStringField(TEXT("ObjectName"));
            OutReferencedPackageNames.Add(PackageName);
        }

    } else if (ObjectType == TEXT("Export")) {
        if (Object->HasField(TEXT("ObjectMark"))) {
            return;
        }

        const int32 ObjectClassIndex = Object->GetIntegerField(TEXT("ObjectClass"));
        CollectObjectPackages(ObjectClassIndex, OutReferencedPackageNames, ObjectsAlreadySerialized);

        if (Object->HasField(TEXT("Outer"))) {
            const int32 OuterObjectIndex = Object->GetIntegerField(TEXT("Outer"));
            CollectObjectPackages(OuterObjectIndex, OutReferencedPackageNames, ObjectsAlreadySerialized);
        }

        if (Object->HasField(TEXT("Properties"))) {
            const TSharedPtr<FJsonObject> Properties = Object->GetObjectField(TEXT("Properties"));
        	const TArray<TSharedPtr<FJsonValue>> ReferencedSubobjects = Properties->GetArrayField(TEXT("$ReferencedObjects"));

        	CollectReferencedPackages(ReferencedSubobjects, OutReferencedPackageNames, ObjectsAlreadySerialized);
        }
    }
}

UObject* UObjectHierarchySerializer::DeserializeExportedObject(TSharedPtr<FJsonObject> ObjectJson) {
    //Object is defined inside our own package, so we should have
    const int32 ObjectClassIndex = ObjectJson->GetIntegerField(TEXT("ObjectClass"));
    UClass* ObjectClass = Cast<UClass>(DeserializeObject(ObjectClassIndex));
    if (ObjectClass == nullptr) {
        UE_LOG(LogObjectHierarchySerializer, Error, TEXT("DeserializeObject for package %s failed: Cannot resolve object class %d"), *SourcePackage->GetName(), ObjectClassIndex);
        return nullptr;
    }
        
    //Outer will be missing for root UPackage export, e.g SourcePackage
    if (!ObjectJson->HasField(TEXT("Outer"))) {
        check(ObjectClass == UPackage::StaticClass());
        return SourcePackage;
    }
        
    const int32 OuterObjectIndex = ObjectJson->GetIntegerField(TEXT("Outer"));
    UObject* OuterObject = DeserializeObject(OuterObjectIndex);
    if (OuterObject == nullptr) {
        UE_LOG(LogObjectHierarchySerializer, Error, TEXT("DeserializeObject for package %s failed: Cannot resolve outer object %d"), *SourcePackage->GetName(), OuterObjectIndex);
        return nullptr;
    }
     
    const FString ObjectName = ObjectJson->GetStringField(TEXT("ObjectName"));

	UObject* ConstructedObject;

	//Try to resolve existing object inside of the outer first
	if (UObject* ExistingObject = StaticFindObjectFast(ObjectClass, OuterObject, *ObjectName)) {
		ConstructedObject = ExistingObject;
		
	} else {
		//Construct new object if we cannot otherwise
		const EObjectFlags ObjectLoadFlags = (EObjectFlags) ObjectJson->GetIntegerField(TEXT("ObjectFlags"));
		
		UObject* Template = GetArchetypeFromRequiredInfo(ObjectClass, OuterObject, *ObjectName, ObjectLoadFlags);
		ConstructedObject = StaticConstructObject_Internal(ObjectClass, OuterObject, *ObjectName, ObjectLoadFlags, EInternalObjectFlags::None, Template);	
	}

    //Deserialize object properties now
    if (ObjectJson->HasField(TEXT("Properties"))) {
        const TSharedPtr<FJsonObject>& Properties = ObjectJson->GetObjectField(TEXT("Properties"));
        if (Properties.IsValid()) {
            DeserializeObjectProperties(Properties.ToSharedRef(), ConstructedObject);
        }
    }
	
    return ConstructedObject;
}

UObject* UObjectHierarchySerializer::DeserializeImportedObject(TSharedPtr<FJsonObject> ObjectJson) {
    const FString PackageName = ObjectJson->GetStringField(TEXT("ClassPackage"));
    const FString ClassName = ObjectJson->GetStringField(TEXT("ClassName"));
    
    if (UPackage* Package = LoadPackage(NULL, *PackageName, LOAD_None)) {
        if (UClass* ObjectClass = FindObjectFast<UClass>(Package, *ClassName)) {
            const FString ObjectName = ObjectJson->GetStringField(TEXT("ObjectName"));
            //Outer is absent for root UPackage imports - Use ObjectName with LoadPackage directly
            if (!ObjectJson->HasField(TEXT("Outer"))) {
                check(ObjectClass == UPackage::StaticClass());
                UPackage* ResultPackage = LoadPackage(NULL, *ObjectName, LOAD_None);
                if (ResultPackage == nullptr) {
                    UE_LOG(LogObjectHierarchySerializer, Error, TEXT("DeserializeObject for package %s failed: Cannot resolve package %s"), *SourcePackage->GetName(), *ObjectName);
                }
                return ResultPackage;
            }
            //Otherwise, it is a normal object inside some outer
            const int32 OuterObjectIndex = ObjectJson->GetIntegerField(TEXT("Outer"));
            UObject* OuterObject = DeserializeObject(OuterObjectIndex);
            if (OuterObject == nullptr) {
                UE_LOG(LogObjectHierarchySerializer, Error, TEXT("DeserializeObject for package %s failed: Cannot resolve outer object %d"), *SourcePackage->GetName(), OuterObjectIndex);
                return nullptr;
            }
            //Use FindObjectFast now to resolve our object inside Outer
            UObject* ResultObject = StaticFindObjectFast(ObjectClass, OuterObject, *ObjectName);
            if (ResultObject == nullptr) {
                UE_LOG(LogObjectHierarchySerializer, Error, TEXT("DeserializeObject for package %s failed: Cannot find object %s inside outer %s"), *SourcePackage->GetName(), *OuterObject->GetPathName(), *ObjectName);
                return nullptr;
            }
            return ResultObject;
        }
    }

    //We are here if LoadPackage/FindObjectFast for class are failed
    UE_LOG(LogObjectHierarchySerializer, Error, TEXT("DeserializeObject for package %s failed: Cannot resolve object class %s.%s"), *SourcePackage->GetName(), *PackageName, *ClassName);
    return NULL;
}


void UObjectHierarchySerializer::SerializeImportedObject(TSharedPtr<FJsonObject> ResultJson, UObject* Object) {
    //Object is imported from different package
    UClass* ObjectClass = Object->GetClass();
    ResultJson->SetStringField(TEXT("ClassPackage"), ObjectClass->GetOutermost()->GetName());
    ResultJson->SetStringField(TEXT("ClassName"), ObjectClass->GetName());
    UObject* OuterObject = Object->GetOuter();

    //Outer object can be null if import is the top UPackage object
    if (OuterObject != nullptr) {
        const int32 OuterObjectIndex = SerializeObject(OuterObject);
        ResultJson->SetNumberField(TEXT("Outer"), OuterObjectIndex);
    }
        
    ResultJson->SetStringField(TEXT("ObjectName"), Object->GetName());
}

void UObjectHierarchySerializer::SerializeExportedObject(TSharedPtr<FJsonObject> ResultJson, UObject* Object) {
    //Object is located inside our own package, so we need to serialize it properly
    UClass* ObjectClass = Object->GetClass();
    ResultJson->SetNumberField(TEXT("ObjectClass"), SerializeObject(ObjectClass));
    UObject* OuterObject = Object->GetOuter();

    //Object being serialized is this package itself
    //Make sure object is package and write only object class, that is enough
    if (OuterObject == NULL) {
        check(ObjectClass == UPackage::StaticClass());
        return;
    }
        
    const int32 OuterObjectIndex = SerializeObject(OuterObject);
    ResultJson->SetNumberField(TEXT("Outer"), OuterObjectIndex);
    ResultJson->SetStringField(TEXT("ObjectName"), Object->GetName());
        
    //Serialize object flags that match RF_Load specification
    ResultJson->SetNumberField(TEXT("ObjectFlags"), (int32) (Object->GetFlags() & RF_Load));

    //If we have native serializer set, let it decide whenever we want normal property serialization or not
    const bool bShouldSerializeProperties = true;

    UClass* ClassWithSerialize = FAssetHelper::FindClassWithSerializeImplementation(ObjectClass);
    //checkf(AllowedNativeSerializeClasses.Contains(ClassWithSerialize), TEXT("Attempt to serialize object of class %s (%s) which has custom Serialize"),
    //    *ClassWithSerialize->GetPathName(), *Object->GetPathName());
    if (!AllowedNativeSerializeClasses.Contains(ClassWithSerialize)) {
        UnhandledNativeClasses.Add(ClassWithSerialize->GetFName());
    }
        
    //Serialize UProperties for this object if requested
    if (bShouldSerializeProperties) {
        const TSharedRef<FJsonObject> Properties = SerializeObjectProperties(Object);
        ResultJson->SetObjectField(TEXT("Properties"), Properties);
    }
} 


