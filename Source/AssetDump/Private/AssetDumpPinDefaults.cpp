// Fill out your copyright notice in the Description page of Project Settings.

#include "AssetDumpPinDefaults.h"

#include "EdGraph/EdGraphPin.h"
#include "UObject/UnrealType.h"

namespace AssetDumpPinDefaults
{
	TArray<TPair<FString, FString>> ComputeDefaultPinFieldValues(int32 PortFlags)
	{
		TArray<TPair<FString, FString>> Result;

		// Mirrors UEdGraphPin::ExportTextItem's own PinType.* field loop (EdGraphPin.cpp) exactly -- the same
		// TFieldIterator<FProperty>(FEdGraphPinType::StaticStruct()) + ShouldPort() enumeration, and the same
		// FProperty::ExportTextItem_Direct call per field. Two *separate* default-constructed FEdGraphPinType
		// instances are used for the "actual" and "default" pointers (not the same instance for both) because
		// FProperty::ExportText_Direct -- what a struct-typed field's own nested per-field export recurses
		// into (UScriptStruct::ExportText, Class.cpp) -- treats Data==Delta (the *same* pointer) as an
		// unconditional "always render" bypass around its normal Identical()-based default check (Property.cpp).
		// Passing the same instance for both would make every nested struct sub-field always render regardless
		// of whether it's truly default, producing a non-empty struct literal that would never actually appear
		// in a real dump. Two distinct-but-equal-content instances exercise the real Identical()-based path
		// instead, correctly collapsing a truly-default struct field (e.g. PinType.PinValueType) to empty, same
		// as it would for a genuinely default-valued pin. This automatically tracks every current and future
		// FEdGraphPinType field with zero hardcoded field names: a property type whose ExportText_Internal
		// honors the diff renders empty here exactly as it would for a real default-valued pin -- meaning the
		// real exporter already omits that field for a default pin, so there's nothing to strip and it's
		// correctly left out of Result. A property type that ignores the diff and always renders (confirmed for
		// FBoolProperty in PropertyBool.cpp and FNameProperty in PropertyName.cpp) renders its true default text
		// here, which is exactly the noise AssetDump strips.
		static const FEdGraphPinType DefaultPinTypeActual;
		static const FEdGraphPinType DefaultPinTypeDefault;
		for (TFieldIterator<FProperty> FieldIt(FEdGraphPinType::StaticStruct()); FieldIt; ++FieldIt)
		{
			FProperty* Prop = *FieldIt;
			if (!Prop->ShouldPort())
			{
				continue;
			}

			FString      PropertyStr;
			const uint8* ActualAddr  = Prop->ContainerPtrToValuePtr<uint8>(&DefaultPinTypeActual);
			const uint8* DefaultAddr = Prop->ContainerPtrToValuePtr<uint8>(&DefaultPinTypeDefault);
			Prop->ExportTextItem_Direct(PropertyStr, ActualAddr, DefaultAddr, nullptr, PortFlags, nullptr);

			if (!PropertyStr.IsEmpty())
			{
				Result.Emplace(FString::Printf(TEXT("PinType.%s"), *Prop->GetName()), MoveTemp(PropertyStr));
			}
		}

		// UEdGraphPin::ExportTextItem emits these six fields via a *separate*, unconditional block (not the
		// generic PinType.* loop above) that never passes a default at all -- so there's no per-field
		// reflection to mirror here, only the value. UEdGraphPin's constructor can't be called from outside
		// the class (it's private -- "Create pins using CreatePin since all pin instances are managed by
		// TSharedPtr" -- and CreatePin itself requires a real, non-null owning node), so unlike PinType.*
		// above this can't render off a genuinely live default instance. Instead, render the literal `false`
		// each of these six is unconditionally initialized to in UEdGraphPin::UEdGraphPin's constructor
		// initializer list (EdGraphPin.cpp) through the real FBoolProperty CDO -- the same
		// GetDefault<FBoolProperty>() the engine's own exporter uses -- so at least the rendered *text* (not
		// just the hand-typed value) stays tied to the live engine's FBoolProperty::ExportText_Internal
		// implementation rather than being duplicated by hand.
		const FBoolProperty* BoolPropCDO = GetDefault<FBoolProperty>();
		auto AddDefaultBool = [&Result, BoolPropCDO, PortFlags](const TCHAR* Key)
		{
			FString    PropertyStr;
			const bool DefaultValue = false;
			BoolPropCDO->ExportTextItem_Direct(PropertyStr, &DefaultValue, nullptr, nullptr, PortFlags, nullptr);
			Result.Emplace(Key, MoveTemp(PropertyStr));
		};

		AddDefaultBool(TEXT("bHidden"));
		AddDefaultBool(TEXT("bNotConnectable"));
		AddDefaultBool(TEXT("bDefaultValueIsReadOnly"));
		AddDefaultBool(TEXT("bDefaultValueIsIgnored"));
		AddDefaultBool(TEXT("bAdvancedView"));
		AddDefaultBool(TEXT("bOrphanedPin"));

		return Result;
	}
}
