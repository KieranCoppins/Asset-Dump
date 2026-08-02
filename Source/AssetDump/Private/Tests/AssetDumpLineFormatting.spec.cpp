// Fill out your copyright notice in the Description page of Project Settings.

#include "AssetDumpTextProcessing.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FAssetDumpLineFormattingSpec, "AssetDump.LineFormatting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FAssetDumpLineFormattingSpec)

void FAssetDumpLineFormattingSpec::Define()
{
	using namespace AssetDumpTextProcessing;

	Describe("FindGuidsInLine", [this]
	{
		It("finds an exact 32-char uppercase hex run", [this]
		{
			TArray<FString> Guids;
			FindGuidsInLine(TEXT("ID=F28263CFF1408FA098EB31AD5327FFD5"), Guids);
			TestEqual(TEXT("one guid found"), Guids.Num(), 1);
			if (Guids.Num() == 1)
			{
				TestEqual(TEXT("guid matches"), Guids[0], TEXT("F28263CFF1408FA098EB31AD5327FFD5"));
			}
		});

		It("does not match a 31-char run", [this]
		{
			TArray<FString> Guids;
			FindGuidsInLine(TEXT("ID=F28263CFF1408FA098EB31AD5327FFD"), Guids);
			TestEqual(TEXT("no guid found"), Guids.Num(), 0);
		});

		It("does not match a 33-char run", [this]
		{
			TArray<FString> Guids;
			FindGuidsInLine(TEXT("ID=F28263CFF1408FA098EB31AD5327FFD5A"), Guids);
			TestEqual(TEXT("no guid found"), Guids.Num(), 0);
		});

		It("does not match lowercase hex (format is uppercase-only)", [this]
		{
			TArray<FString> Guids;
			FindGuidsInLine(TEXT("ID=f28263cff1408fa098eb31ad5327ffd5"), Guids);
			TestEqual(TEXT("no guid found"), Guids.Num(), 0);
		});

		It("splits a run interrupted by one lowercase char into two sub-runs, neither matching", [this]
		{
			TArray<FString> Guids;
			// 16 valid chars, one lowercase break, 15 more valid chars -- neither side reaches 32.
			FindGuidsInLine(TEXT("AAAAAAAAAAAAAAAAxBBBBBBBBBBBBBBB"), Guids);
			TestEqual(TEXT("no guid found"), Guids.Num(), 0);
		});

		It("does not match a 40-char run (rejects the whole run, not a truncated match)", [this]
		{
			TArray<FString> Guids;
			FindGuidsInLine(TEXT("0123456789ABCDEF0123456789ABCDEF01234567"), Guids);
			TestEqual(TEXT("no guid found"), Guids.Num(), 0);
		});

		It("finds two distinct guids on one line in order", [this]
		{
			TArray<FString> Guids;
			FindGuidsInLine(TEXT("A=11111111111111111111111111111111,B=22222222222222222222222222222222"), Guids);
			TestEqual(TEXT("two guids found"), Guids.Num(), 2);
			if (Guids.Num() == 2)
			{
				TestEqual(TEXT("first guid"), Guids[0], TEXT("11111111111111111111111111111111"));
				TestEqual(TEXT("second guid"), Guids[1], TEXT("22222222222222222222222222222222"));
			}
		});

		It("finds a guid at the very start and end of the line", [this]
		{
			TArray<FString> Guids;
			FindGuidsInLine(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), Guids);
			TestEqual(TEXT("one guid found"), Guids.Num(), 1);
		});

		It("returns empty for an empty line", [this]
		{
			TArray<FString> Guids;
			FindGuidsInLine(TEXT(""), Guids);
			TestEqual(TEXT("no guid found"), Guids.Num(), 0);
		});

		It("returns empty for a line with no hex-looking content", [this]
		{
			TArray<FString> Guids;
			FindGuidsInLine(TEXT("Name=\"Move to Location\""), Guids);
			TestEqual(TEXT("no guid found"), Guids.Num(), 0);
		});
	});

	Describe("SanitizeAliasLabel", [this]
	{
		It("leaves a simple alnum name unchanged", [this]
		{
			TestEqual(TEXT("unchanged"), SanitizeAliasLabel(TEXT("Interact")), TEXT("Interact"));
		});

		It("replaces spaces with underscores", [this]
		{
			TestEqual(TEXT("spaces replaced"), SanitizeAliasLabel(TEXT("Interact with Post")), TEXT("Interact_with_Post"));
		});

		It("collapses doubled separators instead of doubling underscores", [this]
		{
			TestEqual(TEXT("collapsed"), SanitizeAliasLabel(TEXT("Interact  with--Post")), TEXT("Interact_with_Post"));
		});

		It("produces no leading underscore for leading non-alnum input", [this]
		{
			TestEqual(TEXT("no leading underscore"), SanitizeAliasLabel(TEXT("  Interact")), TEXT("Interact"));
		});

		It("trims a trailing underscore from trailing non-alnum input", [this]
		{
			TestEqual(TEXT("no trailing underscore"), SanitizeAliasLabel(TEXT("Interact!!")), TEXT("Interact"));
		});

		It("returns empty for empty input", [this]
		{
			TestEqual(TEXT("empty"), SanitizeAliasLabel(TEXT("")), TEXT(""));
		});

		It("returns empty for all-non-alnum input", [this]
		{
			TestEqual(TEXT("empty"), SanitizeAliasLabel(TEXT("!!!")), TEXT(""));
		});

		It("truncates a sanitized name longer than 40 chars to exactly 40", [this]
		{
			const FString LongName = TEXT("ThisIsAVeryLongStateNameThatExceedsFortyCharactersInLength");
			const FString Result   = SanitizeAliasLabel(LongName);
			TestEqual(TEXT("truncated to 40 chars"), Result.Len(), 40);
			TestEqual(TEXT("content is the first 40 sanitized chars"), Result, LongName.Left(40));
		});

		It("does not double an underscore already present in the input", [this]
		{
			TestEqual(TEXT("unchanged"), SanitizeAliasLabel(TEXT("State_02")), TEXT("State_02"));
		});
	});

	Describe("StripPersistentGuidField", [this]
	{
		It("removes the field and its trailing comma, preserving the rest of the line", [this]
		{
			const FString Result = StripPersistentGuidField(TEXT("PinName=\"self\",PersistentGuid=G3,bHidden=True,"));
			TestEqual(TEXT("field removed"), Result, TEXT("PinName=\"self\",bHidden=True,"));
		});

		It("returns the line unchanged when PersistentGuid is not present", [this]
		{
			const FString Line = TEXT("PinName=\"self\",bHidden=True,");
			TestEqual(TEXT("unchanged"), StripPersistentGuidField(Line), Line);
		});

		It("returns the line unchanged when PersistentGuid has no following comma (malformed)", [this]
		{
			const FString Line = TEXT("PinName=\"self\",PersistentGuid=G3");
			TestEqual(TEXT("unchanged"), StripPersistentGuidField(Line), Line);
		});

		It("removes the field cleanly when it is the last field on the line", [this]
		{
			const FString Result = StripPersistentGuidField(TEXT("PinName=\"self\",PersistentGuid=G3,"));
			TestEqual(TEXT("field removed"), Result, TEXT("PinName=\"self\","));
		});

		It("does not strip a quoted pin value that merely contains the same literal text", [this]
		{
			// A designer-authored String/Text pin default value that happens to literally contain
			// "PersistentGuid=...," must not be treated as the real field, since it isn't preceded by a
			// genuine field-boundary '(' or ',' at that position -- it's in the middle of a quoted value.
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,DefaultValue=\"see PersistentGuid=G3, for details\",)");
			TestEqual(TEXT("unchanged"), StripPersistentGuidField(Line), Line);
		});
	});

	Describe("StripPinFriendlyNameField", [this]
	{
		It("removes the field and its trailing comma", [this]
		{
			const FString Result = StripPinFriendlyNameField(TEXT("PinName=\"self\",PinFriendlyName=NSLOCTEXT(\"K2Node\", \"Target\", \"Target\"),bHidden=True,"));
			TestEqual(TEXT("field removed"), Result, TEXT("PinName=\"self\",bHidden=True,"));
		});

		It("removes the field as one unit when its NSLOCTEXT text argument contains commas", [this]
		{
			const FString Result = StripPinFriendlyNameField(TEXT("PinName=\"self\",PinFriendlyName=NSLOCTEXT(\"NS\", \"Key\", \"Text, with, commas\"),bHidden=True,"));
			TestEqual(TEXT("field removed"), Result, TEXT("PinName=\"self\",bHidden=True,"));
		});

		It("correctly finds the true closing paren when the NSLOCTEXT text contains literal parens", [this]
		{
			const FString Result = StripPinFriendlyNameField(TEXT("PinName=\"self\",PinFriendlyName=NSLOCTEXT(\"NS\", \"Key\", \"Text (with parens)\"),bHidden=True,"));
			TestEqual(TEXT("field removed, following field survives"), Result, TEXT("PinName=\"self\",bHidden=True,"));
		});

		It("returns the line unchanged when PinFriendlyName=NSLOCTEXT( is not present", [this]
		{
			const FString Line = TEXT("PinName=\"self\",bHidden=True,");
			TestEqual(TEXT("unchanged"), StripPinFriendlyNameField(Line), Line);
		});

		It("removes cleanly when the field is the last thing on the line with no trailing comma", [this]
		{
			const FString Result = StripPinFriendlyNameField(TEXT("PinName=\"self\",PinFriendlyName=NSLOCTEXT(\"K2Node\", \"Target\", \"Target\")"));
			TestEqual(TEXT("field removed, no crash"), Result, TEXT("PinName=\"self\","));
		});

		It("cleanly strips a friendly name whose source text contains an unmatched ')' (regression)", [this]
		{
			// The old hand-rolled paren-depth counter treated every raw '(' / ')' as structural, so a
			// literal unmatched ')' inside the NSLOCTEXT source text made it stop scanning before the real
			// closing paren, splicing a mangled "), fragment into the output. FTextStringHelper::ReadFromBuffer
			// parses the text literal properly and isn't fooled by punctuation inside the quoted string.
			const FString Result = StripPinFriendlyNameField(TEXT("PinName=\"self\",PinFriendlyName=NSLOCTEXT(\"K2Node\", \"Label\", \"Text)\"),bHidden=True,"));
			TestEqual(TEXT("field removed cleanly, no orphaned punctuation"), Result, TEXT("PinName=\"self\",bHidden=True,"));
		});

		It("does not strip a quoted pin value that merely contains the same literal text", [this]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,DefaultValue=\"see PinFriendlyName=NSLOCTEXT for details\",)");
			TestEqual(TEXT("unchanged"), StripPinFriendlyNameField(Line), Line);
		});
	});

	Describe("StripDefaultPinNoise", [this]
	{
		It("leaves a non-pin line completely unchanged, even if it contains a default-looking field", [this]
		{
			const FString Line = TEXT("bHidden=False,Config=(Traits=(\"Foo\"))");
			TestEqual(TEXT("unchanged"), StripDefaultPinNoise(Line), Line);
		});

		It("strips every listed default field when present at its default value", [this]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",PinType.ContainerType=None,PinType.bIsReference=False,PinType.bIsConst=False,PinType.bIsWeakPointer=False,PinType.bIsUObjectWrapper=False,PinType.bSerializeAsSinglePrecisionFloat=False,PinType.PinSubCategory=\"\",PinType.PinSubCategoryObject=None,PinType.PinValueType=(),PinType.PinSubCategoryMemberReference=(),bHidden=False,bNotConnectable=False,bDefaultValueIsReadOnly=False,bDefaultValueIsIgnored=False,bAdvancedView=False,bOrphanedPin=False,)");
			const FString Result = StripDefaultPinNoise(Line);
			TestEqual(TEXT("only PinId/PinName remain"), Result, TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",)"));
		});

		It("preserves bHidden=True completely untouched (highest-priority regression case)", [this]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",bHidden=True,bNotConnectable=False,)");
			const FString Result = StripDefaultPinNoise(Line);
			TestEqual(TEXT("bHidden=True survives, only bNotConnectable stripped"), Result, TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",bHidden=True,)"));
		});

		It("leaves a populated LinkedTo= untouched", [this]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,LinkedTo=(K2Node_IfThenElse_0 G2,),bHidden=False,)");
			const FString Result = StripDefaultPinNoise(Line);
			TestEqual(TEXT("LinkedTo preserved"), Result, TEXT("CustomProperties Pin (PinId=G1,LinkedTo=(K2Node_IfThenElse_0 G2,),)"));
		});

		It("strips PersistentGuid regardless of its value", [this]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PersistentGuid=G99,bHidden=False,)");
			const FString Result = StripDefaultPinNoise(Line);
			TestEqual(TEXT("PersistentGuid stripped"), Result, TEXT("CustomProperties Pin (PinId=G1,)"));
		});

		It("strips PinFriendlyName as part of the combined pipeline", [this]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PinFriendlyName=NSLOCTEXT(\"K2Node\", \"Target\", \"Target\"),bHidden=False,)");
			const FString Result = StripDefaultPinNoise(Line);
			TestEqual(TEXT("PinFriendlyName stripped"), Result, TEXT("CustomProperties Pin (PinId=G1,)"));
		});

		It("handles a realistic line exercising all three strip mechanisms together", [this]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",bHidden=True,bNotConnectable=False,PersistentGuid=G99,PinFriendlyName=NSLOCTEXT(\"K2Node\", \"Target\", \"Target\"),PinType.PinSubCategory=\"float\",)");
			const FString Result = StripDefaultPinNoise(Line);
			TestEqual(TEXT("only non-default/non-strippable fields remain, in order"), Result, TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",bHidden=True,PinType.PinSubCategory=\"float\",)"));
		});

		It("leaves a pin line with none of the strippable fields unchanged, no crash", [this]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",)");
			TestEqual(TEXT("unchanged"), StripDefaultPinNoise(Line), Line);
		});

		It("does not strip a default-field pattern when it appears inside a quoted pin value", [this]
		{
			// A String/Text pin's designer-authored DefaultValue happens to literally contain the exact text
			// of a strippable default field. Because it isn't preceded by a real field-boundary '(' or ',' at
			// that position, it must survive untouched rather than being deleted out of the quoted value.
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,DefaultValue=\"example: bHidden=False, is the default\",)");
			TestEqual(TEXT("quoted lookalike text preserved"), StripDefaultPinNoise(Line), Line);
		});
	});

	Describe("IsRedundantNodesIndexLine", [this]
	{
		It("matches Nodes(3)=", [this]
		{
			TestTrue(TEXT("matches"), IsRedundantNodesIndexLine(TEXT("Nodes(3)=\"/Script/BlueprintGraph.K2Node_Event'K2Node_Event_0'\"")));
		});

		It("matches the Nodes(0)= boundary case", [this]
		{
			TestTrue(TEXT("matches"), IsRedundantNodesIndexLine(TEXT("Nodes(0)=\"Foo\"")));
		});

		It("does not match a line not starting with Nodes(", [this]
		{
			TestFalse(TEXT("no match"), IsRedundantNodesIndexLine(TEXT("NodesSomethingElse=\"Foo\"")));
		});

		It("does not match Nodes(3) with no = immediately after the close-paren", [this]
		{
			TestFalse(TEXT("no match"), IsRedundantNodesIndexLine(TEXT("Nodes(3)")));
		});

		It("does not false-match NodesArray(3)=... which merely starts with the substring Nodes", [this]
		{
			TestFalse(TEXT("no match"), IsRedundantNodesIndexLine(TEXT("NodesArray(3)=\"Foo\"")));
		});

		It("does not match when the line still has leading whitespace (trimming is the caller's job)", [this]
		{
			TestFalse(TEXT("no match"), IsRedundantNodesIndexLine(TEXT("   Nodes(3)=\"Foo\"")));
		});
	});

	Describe("FindRedundantNodesIndexLineIndices", [this]
	{
		It("drops Nodes() lines inside a real EdGraph object (has a Schema=...EdGraphSchema... marker)", [this]
		{
			const TArray<FString> Lines = {
				TEXT("Begin Object Name=\"EventGraph\" ExportPath=\"/Script/Engine.EdGraph'/Game/BP.BP:EventGraph'\""),
				TEXT("   Schema=\"/Script/CoreUObject.Class'/Script/BlueprintGraph.EdGraphSchema_K2'\""),
				TEXT("   Nodes(0)=\"/Script/BlueprintGraph.K2Node_Event'K2Node_Event_0'\""),
				TEXT("   Nodes(1)=\"/Script/BlueprintGraph.K2Node_CallFunction'K2Node_CallFunction_0'\""),
				TEXT("   bAllowDeletion=False"),
				TEXT("End Object"),
			};
			const TSet<int32> Result = FindRedundantNodesIndexLineIndices(Lines);
			TestEqual(TEXT("both Nodes() lines flagged"), Result.Num(), 2);
			TestTrue(TEXT("index 2 flagged"), Result.Contains(2));
			TestTrue(TEXT("index 3 flagged"), Result.Contains(3));
		});

		It("does NOT drop Nodes() lines on an object with no EdGraphSchema marker (e.g. UMovieSceneNodeGroup)", [this]
		{
			const TArray<FString> Lines = {
				TEXT("Begin Object Name=\"NodeGroup_0\" ExportPath=\"/Script/MovieScene.MovieSceneNodeGroup'/Game/LS.LS:NodeGroup_0'\""),
				TEXT("   Nodes(0)=\"Tracks.Group1.Node\""),
				TEXT("   Nodes(1)=\"Tracks.Group1.OtherNode\""),
				TEXT("End Object"),
			};
			const TSet<int32> Result = FindRedundantNodesIndexLineIndices(Lines);
			TestEqual(TEXT("nothing flagged -- real MovieScene node-path data preserved"), Result.Num(), 0);
		});

		It("still recognizes the Schema marker even when Nodes() lines appear before it in the block", [this]
		{
			const TArray<FString> Lines = {
				TEXT("Begin Object Name=\"EventGraph\" ExportPath=\"/Script/Engine.EdGraph'/Game/BP.BP:EventGraph'\""),
				TEXT("   Nodes(0)=\"/Script/BlueprintGraph.K2Node_Event'K2Node_Event_0'\""),
				TEXT("   Schema=\"/Script/CoreUObject.Class'/Script/BlueprintGraph.EdGraphSchema_K2'\""),
				TEXT("End Object"),
			};
			const TSet<int32> Result = FindRedundantNodesIndexLineIndices(Lines);
			TestEqual(TEXT("Nodes() line still flagged regardless of ordering"), Result.Num(), 1);
			TestTrue(TEXT("index 1 flagged"), Result.Contains(1));
		});

		It("keeps a nested non-EdGraph block's Nodes() separate from its EdGraph parent", [this]
		{
			const TArray<FString> Lines = {
				TEXT("Begin Object Name=\"EventGraph\" ExportPath=\"/Script/Engine.EdGraph'/Game/BP.BP:EventGraph'\""),
				TEXT("   Schema=\"/Script/CoreUObject.Class'/Script/BlueprintGraph.EdGraphSchema_K2'\""),
				TEXT("   Begin Object Name=\"NodeGroup_0\" ExportPath=\"/Script/MovieScene.MovieSceneNodeGroup'.../NodeGroup_0'\""),
				TEXT("      Nodes(0)=\"Tracks.Group1.Node\""),
				TEXT("   End Object"),
				TEXT("   Nodes(0)=\"/Script/BlueprintGraph.K2Node_Event'K2Node_Event_0'\""),
				TEXT("End Object"),
			};
			const TSet<int32> Result = FindRedundantNodesIndexLineIndices(Lines);
			TestEqual(TEXT("only the EdGraph's own Nodes() line is flagged"), Result.Num(), 1);
			TestTrue(TEXT("index 5 (EdGraph's Nodes) flagged"), Result.Contains(5));
			TestFalse(TEXT("index 3 (nested MovieSceneNodeGroup's Nodes) not flagged"), Result.Contains(3));
		});

		It("returns an empty set for empty input", [this]
		{
			const TArray<FString> Lines;
			TestEqual(TEXT("empty"), FindRedundantNodesIndexLineIndices(Lines).Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
