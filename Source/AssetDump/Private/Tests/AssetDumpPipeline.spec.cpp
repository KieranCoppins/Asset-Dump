// Fill out your copyright notice in the Description page of Project Settings.

#include "AssetDumpTextProcessing.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// AssetDumpCommandlet::DumpObjects always applies GUID-alias substitution BEFORE pin-noise stripping on every
// line -- StripDefaultPinNoise(ApplyGuidAliases(Line, GuidToAlias)). AssetDumpLineFormatting.spec.cpp and
// AssetDumpGuidAliasing.spec.cpp only ever exercise each function in isolation, so neither would catch a
// regression in the actual composed order (or in a future default-pin-field value that happens to be
// GUID-shaped). This file exercises the real two-function chain, in the real order, on a realistic line.
BEGIN_DEFINE_SPEC(FAssetDumpPipelineSpec, "AssetDump.Pipeline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FAssetDumpPipelineSpec)

void FAssetDumpPipelineSpec::Define()
{
	using namespace AssetDumpTextProcessing;

	Describe("The real DumpObjects() composition (alias-then-strip)", [this]
	{
		It("aliases a pin's GUID fields and strips its default fields in the same pass, in the real order", [this]
		{
			const FString Guid = TEXT("F28263CFF1408FA098EB31AD5327FFD5");

			// A realistic object block: an ID=<guid> paired with a Name, plus a pin line referencing the same
			// guid via PersistentGuid= and holding several default-valued fields that should also be stripped.
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"K2Node_Event_0\""),
					TEXT("   Name=\"ReceiveBeginPlay\""),
					TEXT("   ID=") + Guid,
					TEXT("   CustomProperties Pin (PinId=") + Guid + TEXT(",PinName=\"then\",PinType.ContainerType=None,PinType.bIsReference=False,bHidden=False,PersistentGuid=") + Guid + TEXT(",)"),
					TEXT("End Object"),
				}
			};

			const TMap<FString, FString> GuidToAlias = BuildGuidAliasMap(AllLines);
			const FString*               Alias       = GuidToAlias.Find(Guid);
			if (!TestNotNull(TEXT("guid was aliased"), Alias))
			{
				return;
			}

			// Find the pin line specifically and run it through the exact same composition DumpObjects() uses.
			const FString* PinLine = AllLines[0].FindByPredicate([](const FString& Line) { return Line.Contains(TEXT("CustomProperties Pin")); });
			if (!TestNotNull(TEXT("pin line found"), PinLine))
			{
				return;
			}

			const FString Result = StripDefaultPinNoise(ApplyGuidAliases(*PinLine, GuidToAlias));

			// The alias should appear (PinId=), the default fields (ContainerType, bIsReference, bHidden) and
			// PersistentGuid should be gone, and PinName should survive untouched.
			TestTrue(TEXT("PinId aliased"), Result.Contains(FString::Printf(TEXT("PinId=%s,"), **Alias)));
			TestTrue(TEXT("PinName survives"), Result.Contains(TEXT("PinName=\"then\",")));
			TestFalse(TEXT("PinType.ContainerType stripped"), Result.Contains(TEXT("PinType.ContainerType=")));
			TestFalse(TEXT("PinType.bIsReference stripped"), Result.Contains(TEXT("PinType.bIsReference=")));
			TestFalse(TEXT("bHidden stripped"), Result.Contains(TEXT("bHidden=")));
			TestFalse(TEXT("PersistentGuid stripped"), Result.Contains(TEXT("PersistentGuid=")));
			TestFalse(TEXT("no raw 32-char guid survives anywhere in the result"), Result.Contains(Guid));
		});

		It("does not let alias substitution interfere with default-field stripping on the same line", [this]
		{
			// Regression guard: if a future default-pin-field literal value ever happened to be GUID-shaped,
			// applying alias substitution first could change whether the strip pattern still matches. Confirm
			// today's real field set is unaffected by aliasing having already run.
			const FString Guid = TEXT("11111111111111111111111111111111");

			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"K2Node_CallFunction_0\""),
					TEXT("   Name=\"HasTag\""),
					TEXT("   ID=") + Guid,
					TEXT("End Object"),
				}
			};
			const TMap<FString, FString> GuidToAlias = BuildGuidAliasMap(AllLines);

			const FString PinLine = TEXT("CustomProperties Pin (PinId=") + Guid + TEXT(",PinType.bIsConst=False,PinType.PinSubCategory=\"\",bAdvancedView=False,)");
			const FString Result  = StripDefaultPinNoise(ApplyGuidAliases(PinLine, GuidToAlias));

			TestFalse(TEXT("PinType.bIsConst stripped after aliasing"), Result.Contains(TEXT("PinType.bIsConst=")));
			TestFalse(TEXT("PinType.PinSubCategory stripped after aliasing"), Result.Contains(TEXT("PinType.PinSubCategory=\"\",")));
			TestFalse(TEXT("bAdvancedView stripped after aliasing"), Result.Contains(TEXT("bAdvancedView=")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
