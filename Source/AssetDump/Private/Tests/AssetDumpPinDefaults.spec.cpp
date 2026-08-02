// Fill out your copyright notice in the Description page of Project Settings.

#include "AssetDumpPinDefaults.h"
#include "Misc/AutomationTest.h"
#include "UObject/PropertyPortFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

// Unlike AssetDumpTextProcessing, this genuinely needs the live engine (constructs a real FEdGraphPinType and
// uses FProperty reflection), so this spec runs with EditorContext and calls the real function directly. This
// is the regression guard that replacing the old hand-typed 16-entry table with runtime engine reflection
// didn't silently change what gets computed -- if a future engine upgrade ever changes one of these defaults,
// this test fails loudly instead of the change silently drifting unnoticed.
BEGIN_DEFINE_SPEC(FAssetDumpPinDefaultsSpec, "AssetDump.PinDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FAssetDumpPinDefaultsSpec)

void FAssetDumpPinDefaultsSpec::Define()
{
	Describe("ComputeDefaultPinFieldValues", [this]
	{
		It("resolves every previously-hardcoded field to today's known real engine default", [this]
		{
			// UEdGraphNode::ExportCustomProperties (EdGraphNode.cpp) always exports each pin via
			// "Pin->ExportTextItem(PinString, PPF_Delimited);", ignoring the outer object exporter's own port
			// flags -- PPF_Delimited is the flag set that actually reaches a pin's field-level export.
			const int32 PortFlags = PPF_Delimited;
			const TArray<TPair<FString, FString>> Fields = AssetDumpPinDefaults::ComputeDefaultPinFieldValues(PortFlags);

			TMap<FString, FString> FieldMap;
			for (const TPair<FString, FString>& Field : Fields)
			{
				FieldMap.Add(Field.Key, Field.Value);
			}

			auto TestField = [this, &FieldMap](const TCHAR* Key, const FString& ExpectedValue)
			{
				const FString* Value = FieldMap.Find(Key);
				if (TestNotNull(*FString::Printf(TEXT("%s is present"), Key), Value))
				{
					TestEqual(*FString::Printf(TEXT("%s resolves to its known real default"), Key), *Value, ExpectedValue);
				}
			};

			TestField(TEXT("PinType.ContainerType"), TEXT("None"));
			TestField(TEXT("PinType.bIsReference"), TEXT("False"));
			TestField(TEXT("PinType.bIsConst"), TEXT("False"));
			TestField(TEXT("PinType.bIsWeakPointer"), TEXT("False"));
			TestField(TEXT("PinType.bIsUObjectWrapper"), TEXT("False"));
			TestField(TEXT("PinType.bSerializeAsSinglePrecisionFloat"), TEXT("False"));
			TestField(TEXT("PinType.PinSubCategory"), TEXT("\"\""));
			TestField(TEXT("PinType.PinSubCategoryObject"), TEXT("None"));
			TestField(TEXT("PinType.PinValueType"), TEXT("()"));
			TestField(TEXT("PinType.PinSubCategoryMemberReference"), TEXT("()"));
			TestField(TEXT("bHidden"), TEXT("False"));
			TestField(TEXT("bNotConnectable"), TEXT("False"));
			TestField(TEXT("bDefaultValueIsReadOnly"), TEXT("False"));
			TestField(TEXT("bDefaultValueIsIgnored"), TEXT("False"));
			TestField(TEXT("bAdvancedView"), TEXT("False"));
			TestField(TEXT("bOrphanedPin"), TEXT("False"));
		});

		It("never returns an empty key or an empty default-value text", [this]
		{
			// UEdGraphNode::ExportCustomProperties (EdGraphNode.cpp) always exports each pin via
			// "Pin->ExportTextItem(PinString, PPF_Delimited);", ignoring the outer object exporter's own port
			// flags -- PPF_Delimited is the flag set that actually reaches a pin's field-level export.
			const int32 PortFlags = PPF_Delimited;
			const TArray<TPair<FString, FString>> Fields = AssetDumpPinDefaults::ComputeDefaultPinFieldValues(PortFlags);

			for (const TPair<FString, FString>& Field : Fields)
			{
				TestFalse(TEXT("key is non-empty"), Field.Key.IsEmpty());
				TestFalse(TEXT("value is non-empty (an empty value would never appear as a real field on a dumped line)"), Field.Value.IsEmpty());
			}
		});

		It("returns the same result on repeated calls (no hidden dependency on external mutable state)", [this]
		{
			// UEdGraphNode::ExportCustomProperties (EdGraphNode.cpp) always exports each pin via
			// "Pin->ExportTextItem(PinString, PPF_Delimited);", ignoring the outer object exporter's own port
			// flags -- PPF_Delimited is the flag set that actually reaches a pin's field-level export.
			const int32 PortFlags = PPF_Delimited;
			const TArray<TPair<FString, FString>> First  = AssetDumpPinDefaults::ComputeDefaultPinFieldValues(PortFlags);
			const TArray<TPair<FString, FString>> Second = AssetDumpPinDefaults::ComputeDefaultPinFieldValues(PortFlags);

			TestEqual(TEXT("same number of fields"), First.Num(), Second.Num());
			for (const TPair<FString, FString>& Field : First)
			{
				const TPair<FString, FString>* Match = Second.FindByPredicate([&Field](const TPair<FString, FString>& Other) { return Other.Key == Field.Key; });
				if (TestNotNull(*FString::Printf(TEXT("%s present in both calls"), *Field.Key), Match))
				{
					TestEqual(*FString::Printf(TEXT("%s has the same value in both calls"), *Field.Key), Match->Value, Field.Value);
				}
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
