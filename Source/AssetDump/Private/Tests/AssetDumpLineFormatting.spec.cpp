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
			TestEqual(TEXT("unchanged"), SanitizeAliasLabel(TEXT("Example")), TEXT("Example"));
		});

		It("replaces spaces with underscores", [this]
		{
			TestEqual(TEXT("spaces replaced"), SanitizeAliasLabel(TEXT("Example Name Test")), TEXT("Example_Name_Test"));
		});

		It("collapses doubled separators instead of doubling underscores", [this]
		{
			TestEqual(TEXT("collapsed"), SanitizeAliasLabel(TEXT("Example  Name--Test")), TEXT("Example_Name_Test"));
		});

		It("produces no leading underscore for leading non-alnum input", [this]
		{
			TestEqual(TEXT("no leading underscore"), SanitizeAliasLabel(TEXT("  Example")), TEXT("Example"));
		});

		It("trims a trailing underscore from trailing non-alnum input", [this]
		{
			TestEqual(TEXT("no trailing underscore"), SanitizeAliasLabel(TEXT("Example!!")), TEXT("Example"));
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
		// StripDefaultPinNoise no longer owns its own field/value table (see AssetDumpPinDefaults, which
		// computes it against the live engine) -- it just applies whatever patterns the caller supplies. This
		// is a representative hand-built stand-in, kept here so these tests stay pure/engine-independent; the
		// real computed table is exercised separately in Tests/AssetDumpPinDefaults.spec.cpp.
		const TArray<FString> TestPatterns = BuildDefaultFieldPatterns({
			{TEXT("PinType.ContainerType"), TEXT("None")},
			{TEXT("PinType.bIsReference"), TEXT("False")},
			{TEXT("PinType.bIsConst"), TEXT("False")},
			{TEXT("PinType.bIsWeakPointer"), TEXT("False")},
			{TEXT("PinType.bIsUObjectWrapper"), TEXT("False")},
			{TEXT("PinType.bSerializeAsSinglePrecisionFloat"), TEXT("False")},
			{TEXT("PinType.PinSubCategory"), TEXT("\"\"")},
			{TEXT("PinType.PinSubCategoryObject"), TEXT("None")},
			{TEXT("PinType.PinValueType"), TEXT("()")},
			{TEXT("PinType.PinSubCategoryMemberReference"), TEXT("()")},
			{TEXT("bHidden"), TEXT("False")},
			{TEXT("bNotConnectable"), TEXT("False")},
			{TEXT("bDefaultValueIsReadOnly"), TEXT("False")},
			{TEXT("bDefaultValueIsIgnored"), TEXT("False")},
			{TEXT("bAdvancedView"), TEXT("False")},
			{TEXT("bOrphanedPin"), TEXT("False")},
		});

		It("leaves a non-pin line completely unchanged, even if it contains a default-looking field", [this, TestPatterns]
		{
			const FString Line = TEXT("bHidden=False,Config=(Traits=(\"Foo\"))");
			TestEqual(TEXT("unchanged"), StripDefaultPinNoise(Line, TestPatterns), Line);
		});

		It("strips every listed default field when present at its default value", [this, TestPatterns]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",PinType.ContainerType=None,PinType.bIsReference=False,PinType.bIsConst=False,PinType.bIsWeakPointer=False,PinType.bIsUObjectWrapper=False,PinType.bSerializeAsSinglePrecisionFloat=False,PinType.PinSubCategory=\"\",PinType.PinSubCategoryObject=None,PinType.PinValueType=(),PinType.PinSubCategoryMemberReference=(),bHidden=False,bNotConnectable=False,bDefaultValueIsReadOnly=False,bDefaultValueIsIgnored=False,bAdvancedView=False,bOrphanedPin=False,)");
			const FString Result = StripDefaultPinNoise(Line, TestPatterns);
			TestEqual(TEXT("only PinId/PinName remain"), Result, TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",)"));
		});

		It("preserves bHidden=True completely untouched (highest-priority regression case)", [this, TestPatterns]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",bHidden=True,bNotConnectable=False,)");
			const FString Result = StripDefaultPinNoise(Line, TestPatterns);
			TestEqual(TEXT("bHidden=True survives, only bNotConnectable stripped"), Result, TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",bHidden=True,)"));
		});

		It("leaves a populated LinkedTo= untouched", [this, TestPatterns]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,LinkedTo=(K2Node_IfThenElse_0 G2,),bHidden=False,)");
			const FString Result = StripDefaultPinNoise(Line, TestPatterns);
			TestEqual(TEXT("LinkedTo preserved"), Result, TEXT("CustomProperties Pin (PinId=G1,LinkedTo=(K2Node_IfThenElse_0 G2,),)"));
		});

		It("strips PersistentGuid regardless of its value", [this, TestPatterns]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PersistentGuid=G99,bHidden=False,)");
			const FString Result = StripDefaultPinNoise(Line, TestPatterns);
			TestEqual(TEXT("PersistentGuid stripped"), Result, TEXT("CustomProperties Pin (PinId=G1,)"));
		});

		It("strips PinFriendlyName as part of the combined pipeline", [this, TestPatterns]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PinFriendlyName=NSLOCTEXT(\"K2Node\", \"Target\", \"Target\"),bHidden=False,)");
			const FString Result = StripDefaultPinNoise(Line, TestPatterns);
			TestEqual(TEXT("PinFriendlyName stripped"), Result, TEXT("CustomProperties Pin (PinId=G1,)"));
		});

		It("handles a realistic line exercising all three strip mechanisms together", [this, TestPatterns]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",bHidden=True,bNotConnectable=False,PersistentGuid=G99,PinFriendlyName=NSLOCTEXT(\"K2Node\", \"Target\", \"Target\"),PinType.PinSubCategory=\"float\",)");
			const FString Result = StripDefaultPinNoise(Line, TestPatterns);
			TestEqual(TEXT("only non-default/non-strippable fields remain, in order"), Result, TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",bHidden=True,PinType.PinSubCategory=\"float\",)"));
		});

		It("leaves a pin line with none of the strippable fields unchanged, no crash", [this, TestPatterns]
		{
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,PinName=\"self\",)");
			TestEqual(TEXT("unchanged"), StripDefaultPinNoise(Line, TestPatterns), Line);
		});

		It("does not strip a default-field pattern when it appears inside a quoted pin value", [this, TestPatterns]
		{
			// A String/Text pin's designer-authored DefaultValue happens to literally contain the exact text
			// of a strippable default field. Because it isn't preceded by a real field-boundary '(' or ',' at
			// that position, it must survive untouched rather than being deleted out of the quoted value.
			const FString Line = TEXT("CustomProperties Pin (PinId=G1,DefaultValue=\"example: bHidden=False, is the default\",)");
			TestEqual(TEXT("quoted lookalike text preserved"), StripDefaultPinNoise(Line, TestPatterns), Line);
		});
	});

	Describe("TryParseArrayIndexLine", [this]
	{
		It("matches Nodes(3)= and extracts the value", [this]
		{
			FString Value;
			TestTrue(TEXT("matches"), TryParseArrayIndexLine(TEXT("Nodes(3)=\"/Script/BlueprintGraph.K2Node_Event'K2Node_Event_0'\""), Value));
			TestEqual(TEXT("value extracted"), Value, TEXT("\"/Script/BlueprintGraph.K2Node_Event'K2Node_Event_0'\""));
		});

		It("matches the Nodes(0)= boundary case", [this]
		{
			FString Value;
			TestTrue(TEXT("matches"), TryParseArrayIndexLine(TEXT("Nodes(0)=\"Foo\""), Value));
		});

		It("matches any property identifier, not just Nodes (e.g. IDToNodeMappings)", [this]
		{
			FString Value;
			TestTrue(TEXT("matches"), TryParseArrayIndexLine(TEXT("IDToNodeMappings(2)=(Id=G1,Index=0)"), Value));
			TestEqual(TEXT("value extracted"), Value, TEXT("(Id=G1,Index=0)"));
		});

		It("matches NodesArray(3)=... -- a same-shaped but differently-named property is a legitimate array-index line too", [this]
		{
			// This is an intentional generalization from the old Nodes-only detector: the shape alone (any
			// identifier followed by "(<digits>)=") is what TryParseArrayIndexLine checks. Whether the line is
			// actually redundant is a separate question, answered by FindRedundantLookupArrayLineIndices.
			FString Value;
			TestTrue(TEXT("matches"), TryParseArrayIndexLine(TEXT("NodesArray(3)=\"Foo\""), Value));
		});

		It("does not match a line with no parens at all", [this]
		{
			FString Value;
			TestFalse(TEXT("no match"), TryParseArrayIndexLine(TEXT("NodesSomethingElse=\"Foo\""), Value));
		});

		It("does not match when there is no = immediately after the close-paren", [this]
		{
			FString Value;
			TestFalse(TEXT("no match"), TryParseArrayIndexLine(TEXT("Nodes(3)"), Value));
		});

		It("does not match a non-identifier character before the open-paren", [this]
		{
			FString Value;
			TestFalse(TEXT("no match"), TryParseArrayIndexLine(TEXT("Foo-Bar(3)=\"Baz\""), Value));
		});

		It("does not match when the parens contain no digits", [this]
		{
			FString Value;
			TestFalse(TEXT("no match"), TryParseArrayIndexLine(TEXT("Nodes()=\"Foo\""), Value));
		});

		It("does not match when the line still has leading whitespace (trimming is the caller's job)", [this]
		{
			FString Value;
			TestFalse(TEXT("no match"), TryParseArrayIndexLine(TEXT("   Nodes(3)=\"Foo\""), Value));
		});
	});

	Describe("FindObjectPathReferenceNames", [this]
	{
		It("extracts the bare object name from a ClassPath'Name' reference", [this]
		{
			TArray<FString> Names;
			FindObjectPathReferenceNames(TEXT("\"/Script/BlueprintGraph.K2Node_Event'K2Node_Event_0'\""), Names);
			TestEqual(TEXT("one name"), Names.Num(), 1);
			if (Names.Num() == 1)
			{
				TestEqual(TEXT("bare name"), Names[0], TEXT("K2Node_Event_0"));
			}
		});

		It("extracts only the trailing segment of a qualified sibling path", [this]
		{
			TArray<FString> Names;
			FindObjectPathReferenceNames(TEXT("\"/Script/Engine.EdGraphNode'EventGraph:K2Node_Event_0'\""), Names);
			TestEqual(TEXT("one name"), Names.Num(), 1);
			if (Names.Num() == 1)
			{
				TestEqual(TEXT("trailing segment only"), Names[0], TEXT("K2Node_Event_0"));
			}
		});

		It("finds multiple quoted references in one value", [this]
		{
			TArray<FString> Names;
			FindObjectPathReferenceNames(TEXT("(Class'A',Class'B')"), Names);
			TestEqual(TEXT("two names"), Names.Num(), 2);
			if (Names.Num() == 2)
			{
				TestEqual(TEXT("first"), Names[0], TEXT("A"));
				TestEqual(TEXT("second"), Names[1], TEXT("B"));
			}
		});

		It("returns empty for a plain string with no single-quote decoration", [this]
		{
			TArray<FString> Names;
			FindObjectPathReferenceNames(TEXT("\"Tracks.Group1.Node\""), Names);
			TestEqual(TEXT("no names"), Names.Num(), 0);
		});

		It("returns empty for empty input", [this]
		{
			TArray<FString> Names;
			FindObjectPathReferenceNames(TEXT(""), Names);
			TestEqual(TEXT("no names"), Names.Num(), 0);
		});
	});

	Describe("FindRedundantLookupArrayLineIndices", [this]
	{
		It("drops Nodes() lines whose referenced nodes are actually declared via their own Begin Object blocks", [this]
		{
			const TArray<FString> Lines = {
				TEXT("Begin Object Name=\"EventGraph\" ExportPath=\"/Script/Engine.EdGraph'/Game/BP.BP:EventGraph'\""),
				TEXT("   Begin Object Name=\"K2Node_Event_0\" ExportPath=\"/Script/BlueprintGraph.K2Node_Event'/Game/BP.BP:EventGraph.K2Node_Event_0'\""),
				TEXT("   End Object"),
				TEXT("   Begin Object Name=\"K2Node_CallFunction_0\" ExportPath=\"/Script/BlueprintGraph.K2Node_CallFunction'/Game/BP.BP:EventGraph.K2Node_CallFunction_0'\""),
				TEXT("   End Object"),
				TEXT("   Nodes(0)=\"/Script/BlueprintGraph.K2Node_Event'K2Node_Event_0'\""),
				TEXT("   Nodes(1)=\"/Script/BlueprintGraph.K2Node_CallFunction'K2Node_CallFunction_0'\""),
				TEXT("   bAllowDeletion=False"),
				TEXT("End Object"),
			};
			const TSet<int32> Result = FindRedundantLookupArrayLineIndices(Lines);
			TestEqual(TEXT("both Nodes() lines flagged"), Result.Num(), 2);
			TestTrue(TEXT("index 5 flagged"), Result.Contains(5));
			TestTrue(TEXT("index 6 flagged"), Result.Contains(6));
		});

		It("does NOT drop Nodes() lines whose values are plain strings, not object references (e.g. UMovieSceneNodeGroup)", [this]
		{
			const TArray<FString> Lines = {
				TEXT("Begin Object Name=\"NodeGroup_0\" ExportPath=\"/Script/MovieScene.MovieSceneNodeGroup'/Game/LS.LS:NodeGroup_0'\""),
				TEXT("   Nodes(0)=\"Tracks.Group1.Node\""),
				TEXT("   Nodes(1)=\"Tracks.Group1.OtherNode\""),
				TEXT("End Object"),
			};
			const TSet<int32> Result = FindRedundantLookupArrayLineIndices(Lines);
			TestEqual(TEXT("nothing flagged -- real MovieScene node-path data preserved"), Result.Num(), 0);
		});

		It("drops a StateTree-style IDToNodeMappings(N)=(Id=<guid>,...) line whose guid is declared several nesting levels deep", [this]
		{
			// IDToNodeMappings is a property of the top-level object itself (not nested in any Begin Object),
			// while the guid it references can be declared arbitrarily deep inside nested state/task objects --
			// proving the detector isn't scoped to a single frame, only to "somewhere in this same object".
			const FString         Guid  = TEXT("11111111111111111111111111111111");
			const TArray<FString> Lines = {
				TEXT("Begin Object Name=\"RootState\" ExportPath=\"/Script/StateTreeModule.StateTreeState'/Game/ST.ST:RootState'\""),
				TEXT("   Begin Object Name=\"NestedTask\" ExportPath=\"/Script/StateTreeModule.StateTreeTask'/Game/ST.ST:RootState.NestedTask'\""),
				TEXT("      ID=") + Guid,
				TEXT("   End Object"),
				TEXT("End Object"),
				TEXT("IDToNodeMappings(0)=(Id=") + Guid + TEXT(",Index=0)"),
			};
			const TSet<int32> Result = FindRedundantLookupArrayLineIndices(Lines);
			TestEqual(TEXT("the mapping line is flagged"), Result.Num(), 1);
			TestTrue(TEXT("index 5 flagged"), Result.Contains(5));
		});

		It("does not drop an array line referencing an identifier that is not declared anywhere else", [this]
		{
			const TArray<FString> Lines = {
				TEXT("Begin Object Name=\"Known\" ExportPath=\"/Script/Engine.Object'/Game/X.X:Known'\""),
				TEXT("End Object"),
				TEXT("SomeArray(0)=\"/Script/Engine.Object'UnknownThing'\""),
			};
			const TSet<int32> Result = FindRedundantLookupArrayLineIndices(Lines);
			TestEqual(TEXT("nothing flagged -- referenced identifier is genuinely unknown"), Result.Num(), 0);
		});

		It("keeps a real Tasks(N)= entry (with its own trailing ID=) while still stripping its IDToNodeMappings restatement (regression)", [this]
		{
			// Reproduces a real StateTree dump structure: Tasks(0) is nested inside a State's own Begin/End
			// Object block (a real, richly-detailed declaration with its own trailing self-identity tag),
			// while IDToNodeMappings is a direct property of the top-level object itself (a thin compiled
			// lookup restating the same guid). Both match the array-index shape and both embed the guid via an
			// ID=/Id= field, so without the richness/self-exclusion logic this regressed to stripping the real
			// Tasks(0) declaration instead of just the redundant lookup.
			const FString Guid = TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
			const TArray<FString> Lines = {
				TEXT("Begin Object Name=\"ExampleStateTree\" ExportPath=\"...\""),
				TEXT("   Begin Object Name=\"StateTreeEditorData_0\" ExportPath=\"...\""),
				TEXT("      Begin Object Name=\"StateTreeState_0\" ExportPath=\"...\""),
				TEXT("         Tasks(0)=(Node=/Script/ExampleModule.ExampleTask(bTaskEnabled=True,TransitionHandlingPriority=Normal,bConsideredForCompletion=False,bCanEditConsideredForCompletion=True,Name=\"\",BindingsBatch=(Value=65535),OutputBindingsBatch=(Value=65535),InstanceTemplateIndex=(Value=65535),ExecutionRuntimeTemplateIndex=(Value=65535),InstanceDataHandle=(Source=None,Index=65535,StateHandle=(Index=65535))),Instance=/Script/ExampleModule.ExampleTaskInstanceData(TargetEntity=(),Location=(X=0.000000,Y=0.000000,Z=0.000000)),ID=") + Guid + TEXT(")"),
				TEXT("      End Object"),
				TEXT("   End Object"),
				TEXT("   IDToNodeMappings(0)=(Id=") + Guid + TEXT(",Index=(Value=0))"),
				TEXT("End Object"),
			};
			const TSet<int32> Result = FindRedundantLookupArrayLineIndices(Lines);
			TestFalse(TEXT("Tasks(0) must survive"), Result.Contains(3));
			TestTrue(TEXT("IDToNodeMappings(0) should be stripped"), Result.Contains(6));
		});

		It("does not let a top-level compiled bindings table falsely certify a nested Tasks(N)= entry as already-known (regression)", [this]
		{
			// A real StateTree also exports a compiled, object-wide bindings table (PropertyBindings=(
			// SourceStructs=(...ID=<guid>...))) as a direct property of the top-level object, restating every
			// task/state/evaluator's guid alongside unrelated metadata. Because that whole line doesn't match
			// the array-index shape, it used to be treated as an unconditional declaration source -- letting it
			// falsely certify Tasks(0)'s own guid as "known elsewhere" and incorrectly strip the real
			// declaration. Only identifiers declared *within* a nested child entity's own scope (depth > 1)
			// may ever satisfy another line's redundancy; a depth-1 property of the top-level object never can,
			// regardless of its own shape.
			const FString Guid = TEXT("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");
			const TArray<FString> Lines = {
				TEXT("Begin Object Name=\"ExampleStateTree\" ExportPath=\"...\""),
				TEXT("   Begin Object Name=\"StateTreeEditorData_0\" ExportPath=\"...\""),
				TEXT("      Begin Object Name=\"StateTreeState_0\" ExportPath=\"...\""),
				TEXT("         Tasks(0)=(Node=/Script/ExampleModule.ExampleTask(...),ID=") + Guid + TEXT(")"),
				TEXT("      End Object"),
				TEXT("   End Object"),
				TEXT("   PropertyBindings=(SourceStructs=((DataHandle=(Source=ActiveInstanceData),DataSource=Task,Struct=\"...\",Name=\"Example Task\",ID=") + Guid + TEXT(")))"),
				TEXT("End Object"),
			};
			const TSet<int32> Result = FindRedundantLookupArrayLineIndices(Lines);
			TestFalse(TEXT("Tasks(0) must survive -- PropertyBindings is a depth-1 lookup, not a declaration"), Result.Contains(3));
		});

		It("does not drop an array line with no embedded identifiers at all", [this]
		{
			const TArray<FString> Lines = {
				TEXT("Weights(0)=3.5"),
			};
			const TSet<int32> Result = FindRedundantLookupArrayLineIndices(Lines);
			TestEqual(TEXT("nothing flagged -- no guid or object-reference tokens to match"), Result.Num(), 0);
		});

		It("flags a redundant line regardless of whether its declaration appears before or after it", [this]
		{
			const TArray<FString> Lines = {
				TEXT("Nodes(0)=\"/Script/BlueprintGraph.K2Node_Event'K2Node_Event_0'\""),
				TEXT("Begin Object Name=\"K2Node_Event_0\" ExportPath=\"/Script/BlueprintGraph.K2Node_Event'/Game/BP.BP:K2Node_Event_0'\""),
				TEXT("End Object"),
			};
			const TSet<int32> Result = FindRedundantLookupArrayLineIndices(Lines);
			TestEqual(TEXT("flagged even though its declaration comes later"), Result.Num(), 1);
			TestTrue(TEXT("index 0 flagged"), Result.Contains(0));
		});

		It("returns an empty set for empty input", [this]
		{
			const TArray<FString> Lines;
			TestEqual(TEXT("empty"), FindRedundantLookupArrayLineIndices(Lines).Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
