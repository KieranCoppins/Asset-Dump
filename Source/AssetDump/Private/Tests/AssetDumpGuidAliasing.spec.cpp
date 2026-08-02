// Fill out your copyright notice in the Description page of Project Settings.

#include "AssetDumpTextProcessing.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FAssetDumpGuidAliasingSpec, "AssetDump.GuidAliasing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FAssetDumpGuidAliasingSpec)

void FAssetDumpGuidAliasingSpec::Define()
{
	using namespace AssetDumpTextProcessing;

	// 32-char placeholder guids, each a single repeated character for readability. All lengths are verified
	// to be exactly 32 (or, for GuidShort16, exactly 16) -- FindGuidsInLine only matches runs of exactly 32.
	const FString GuidA      = TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
	const FString GuidB      = TEXT("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");
	const FString GuidC      = TEXT("CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC");
	const FString GuidD      = TEXT("DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD");
	const FString GuidParent = TEXT("33333333333333333333333333333333");
	const FString GuidChild  = TEXT("44444444444444444444444444444444");
	const FString GuidEmpty  = TEXT("55555555555555555555555555555555");
	const FString GuidShort16 = TEXT("6666666666666666");

	Describe("BuildGuidAliasMap", [this, GuidA, GuidB, GuidC, GuidD, GuidParent, GuidChild, GuidEmpty]
	{
		It("aliases a paired guid to its sibling Name", [this, GuidA]
		{
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"StateTreeState_0\" ExportPath=\"...\""),
					TEXT("   Name=\"Foo\""),
					TEXT("   ID=") + GuidA,
					TEXT("End Object"),
					TEXT("SomeReference=") + GuidA,
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			TestEqual(TEXT("one alias"), Result.Num(), 1);
			const FString* Alias = Result.Find(GuidA);
			if (TestNotNull(TEXT("guid present"), Alias))
			{
				TestEqual(TEXT("aliased to Name"), *Alias, TEXT("Foo"));
			}
		});

		It("disambiguates two objects sharing the same Name with a _2 suffix", [this, GuidA, GuidB]
		{
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"State1\""),
					TEXT("   Name=\"Interact with Post\""),
					TEXT("   ID=") + GuidA,
					TEXT("End Object"),
					TEXT("Begin Object Name=\"State2\""),
					TEXT("   Name=\"Interact with Post\""),
					TEXT("   ID=") + GuidB,
					TEXT("End Object"),
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			TestEqual(TEXT("two aliases"), Result.Num(), 2);
			const FString* AliasA = Result.Find(GuidA);
			const FString* AliasB = Result.Find(GuidB);
			if (TestNotNull(TEXT("guidA present"), AliasA) && TestNotNull(TEXT("guidB present"), AliasB))
			{
				TestEqual(TEXT("first gets base name"), *AliasA, TEXT("Interact_with_Post"));
				TestEqual(TEXT("second gets _2 suffix"), *AliasB, TEXT("Interact_with_Post_2"));
			}
		});

		It("continues the suffix counter to _3 for a third collision", [this, GuidA, GuidB, GuidC]
		{
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"State1\""),
					TEXT("   Name=\"Interact with Post\""),
					TEXT("   ID=") + GuidA,
					TEXT("End Object"),
					TEXT("Begin Object Name=\"State2\""),
					TEXT("   Name=\"Interact with Post\""),
					TEXT("   ID=") + GuidB,
					TEXT("End Object"),
					TEXT("Begin Object Name=\"State3\""),
					TEXT("   Name=\"Interact with Post\""),
					TEXT("   ID=") + GuidC,
					TEXT("End Object"),
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			const FString* AliasC = Result.Find(GuidC);
			if (TestNotNull(TEXT("guidC present"), AliasC))
			{
				TestEqual(TEXT("third gets _3 suffix"), *AliasC, TEXT("Interact_with_Post_3"));
			}
		});

		It("falls back to a G<N> alias when an object has an ID but no sibling Name", [this, GuidD]
		{
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"NoNameState\""),
					TEXT("   ID=") + GuidD,
					TEXT("End Object"),
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			const FString* Alias = Result.Find(GuidD);
			if (TestNotNull(TEXT("guidD present"), Alias))
			{
				TestTrue(TEXT("fallback alias starts with G"), Alias->StartsWith(TEXT("G")));
			}
		});

		It("falls back to G<N> for a guid that never appears as an ID= inside any object frame", [this, GuidA]
		{
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("SomeRef=") + GuidA + TEXT(",Other=Value"),
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			const FString* Alias = Result.Find(GuidA);
			if (TestNotNull(TEXT("guidA present"), Alias))
			{
				TestTrue(TEXT("fallback alias starts with G"), Alias->StartsWith(TEXT("G")));
			}
		});

		It("increments fallback numbers in first-seen order, counting only fallback cases", [this, GuidA, GuidB, GuidC]
		{
			const TArray<TArray<FString>> AllLines = {
				{
					// GuidA is named (not a fallback case); GuidB and GuidC are unpaired (fallback cases).
					TEXT("Begin Object Name=\"State1\""),
					TEXT("   Name=\"Named\""),
					TEXT("   ID=") + GuidA,
					TEXT("End Object"),
					TEXT("RefB=") + GuidB,
					TEXT("RefC=") + GuidC,
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			const FString* AliasB = Result.Find(GuidB);
			const FString* AliasC = Result.Find(GuidC);
			if (TestNotNull(TEXT("guidB present"), AliasB) && TestNotNull(TEXT("guidC present"), AliasC))
			{
				TestEqual(TEXT("first fallback is G1"), *AliasB, TEXT("G1"));
				TestEqual(TEXT("second fallback is G2"), *AliasC, TEXT("G2"));
			}
		});

		It("pairs Name and ID regardless of which appears first in the object", [this, GuidA, GuidB]
		{
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"NameFirst\""),
					TEXT("   Name=\"NameFirstLabel\""),
					TEXT("   ID=") + GuidA,
					TEXT("End Object"),
					TEXT("Begin Object Name=\"IdFirst\""),
					TEXT("   ID=") + GuidB,
					TEXT("   Name=\"IdFirstLabel\""),
					TEXT("End Object"),
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			const FString* AliasA = Result.Find(GuidA);
			const FString* AliasB = Result.Find(GuidB);
			if (TestNotNull(TEXT("guidA present"), AliasA))
			{
				TestEqual(TEXT("Name-then-ID pairs correctly"), *AliasA, TEXT("NameFirstLabel"));
			}
			if (TestNotNull(TEXT("guidB present"), AliasB))
			{
				TestEqual(TEXT("ID-then-Name pairs correctly"), *AliasB, TEXT("IdFirstLabel"));
			}
		});

		It("keeps a nested child object's Name/ID isolated from its parent's frame", [this, GuidParent, GuidChild]
		{
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"Parent\""),
					TEXT("   ID=") + GuidParent,
					TEXT("   Begin Object Name=\"Child\""),
					TEXT("      Name=\"ChildName\""),
					TEXT("      ID=") + GuidChild,
					TEXT("   End Object"),
					TEXT("   Name=\"ParentName\""),
					TEXT("End Object"),
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			const FString* ParentAlias = Result.Find(GuidParent);
			const FString* ChildAlias  = Result.Find(GuidChild);
			if (TestNotNull(TEXT("parent guid present"), ParentAlias))
			{
				TestEqual(TEXT("parent aliased to its own Name"), *ParentAlias, TEXT("ParentName"));
			}
			if (TestNotNull(TEXT("child guid present"), ChildAlias))
			{
				TestEqual(TEXT("child aliased to its own Name"), *ChildAlias, TEXT("ChildName"));
			}
		});

		It("falls back to G<N> when a Name sanitizes to an empty string", [this, GuidEmpty]
		{
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"X\""),
					TEXT("   Name=\"!!!\""),
					TEXT("   ID=") + GuidEmpty,
					TEXT("End Object"),
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			const FString* Alias = Result.Find(GuidEmpty);
			if (TestNotNull(TEXT("guid present"), Alias))
			{
				TestTrue(TEXT("falls back to G<N>, not an empty alias"), Alias->StartsWith(TEXT("G")) && Alias->Len() > 1);
			}
		});

		It("assigns exactly one alias per distinct guid even when it appears on many lines", [this, GuidA]
		{
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"X\""),
					TEXT("   Name=\"RepeatedRef\""),
					TEXT("   ID=") + GuidA,
					TEXT("End Object"),
					TEXT("Ref1=") + GuidA,
					TEXT("Ref2=") + GuidA,
					TEXT("Ref3=") + GuidA,
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			TestEqual(TEXT("one map entry for one distinct guid"), Result.Num(), 1);
		});

		It("returns an empty map for empty input", [this]
		{
			const TArray<TArray<FString>> AllLines;
			const TMap<FString, FString>  Result = BuildGuidAliasMap(AllLines);
			TestEqual(TEXT("empty map"), Result.Num(), 0);
		});

		It("never assigns two different guids the same alias, even when a disambiguated suffix collides with another object's own literal name (regression)", [this, GuidA, GuidB, GuidC]
		{
			// GuidA and GuidB are both named "Foo" -> GuidA gets "Foo", GuidB would naively get "Foo_2".
			// GuidC is a THIRD, unrelated object literally named "Foo_2" -- a different AliasBaseUsage bucket
			// (first use of that base), so it would ALSO naively get "Foo_2", colliding with GuidB. Aliases
			// must be globally unique: GuidC should be bumped to a further-disambiguated alias instead.
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"State1\""),
					TEXT("   Name=\"Foo\""),
					TEXT("   ID=") + GuidA,
					TEXT("End Object"),
					TEXT("Begin Object Name=\"State2\""),
					TEXT("   Name=\"Foo\""),
					TEXT("   ID=") + GuidB,
					TEXT("End Object"),
					TEXT("Begin Object Name=\"State3\""),
					TEXT("   Name=\"Foo_2\""),
					TEXT("   ID=") + GuidC,
					TEXT("End Object"),
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			TestEqual(TEXT("three distinct aliases"), Result.Num(), 3);
			const FString* AliasA = Result.Find(GuidA);
			const FString* AliasB = Result.Find(GuidB);
			const FString* AliasC = Result.Find(GuidC);
			if (TestNotNull(TEXT("guidA present"), AliasA) && TestNotNull(TEXT("guidB present"), AliasB) && TestNotNull(TEXT("guidC present"), AliasC))
			{
				TestEqual(TEXT("guidA gets the base name"), *AliasA, TEXT("Foo"));
				TestEqual(TEXT("guidB gets the first disambiguation"), *AliasB, TEXT("Foo_2"));
				TestNotEqual(TEXT("guidC's alias must not collide with guidB's"), *AliasC, *AliasB);
				TestNotEqual(TEXT("guidC's alias must not collide with guidA's"), *AliasC, *AliasA);
			}
		});

		It("correctly captures a Name value containing an escaped embedded quote (regression)", [this, GuidA]
		{
			// Unreal's text export writes an embedded quote in a string value as \" -- a naive FindChar('"')
			// would stop at that escaped quote instead of the real terminating one, truncating the name.
			const TArray<TArray<FString>> AllLines = {
				{
					TEXT("Begin Object Name=\"X\""),
					TEXT("   Name=\"6\\\" Sword\""),
					TEXT("   ID=") + GuidA,
					TEXT("End Object"),
				}
			};
			const TMap<FString, FString> Result = BuildGuidAliasMap(AllLines);
			const FString* Alias = Result.Find(GuidA);
			if (TestNotNull(TEXT("guid present"), Alias))
			{
				// SanitizeAliasLabel strips non-alnum characters anyway, so the exact alias text just needs to
				// reflect the FULL name ("6 Sword", sanitized) rather than a truncated fragment like "6".
				TestEqual(TEXT("full name captured, not truncated at the escaped quote"), *Alias, TEXT("6_Sword"));
			}
		});
	});

	Describe("ApplyGuidAliases", [this, GuidA, GuidB, GuidShort16]
	{
		It("replaces a known guid exactly, leaving surrounding text untouched", [this, GuidA]
		{
			TMap<FString, FString> AliasMap;
			AliasMap.Add(GuidA, TEXT("MyAlias"));
			const FString Result = ApplyGuidAliases(TEXT("ID=") + GuidA + TEXT(",Next=Foo"), AliasMap);
			TestEqual(TEXT("substituted"), Result, TEXT("ID=MyAlias,Next=Foo"));
		});

		It("leaves a well-formed guid untouched when it is not in the alias map", [this, GuidA, GuidB]
		{
			TMap<FString, FString> AliasMap;
			AliasMap.Add(GuidA, TEXT("MyAlias"));
			const FString Line = TEXT("ID=") + GuidB + TEXT(",Next=Foo");
			TestEqual(TEXT("unchanged"), ApplyGuidAliases(Line, AliasMap), Line);
		});

		It("leaves a hex run of a different length completely untouched", [this, GuidA, GuidShort16]
		{
			TMap<FString, FString> AliasMap;
			AliasMap.Add(GuidA, TEXT("MyAlias"));
			const FString Line = TEXT("Name=/Engine/Transient.PropertyBag_") + GuidShort16 + TEXT("(Foo)");
			TestEqual(TEXT("unchanged"), ApplyGuidAliases(Line, AliasMap), Line);
		});

		It("replaces multiple distinct known guids on one line", [this, GuidA, GuidB]
		{
			TMap<FString, FString> AliasMap;
			AliasMap.Add(GuidA, TEXT("Alpha"));
			AliasMap.Add(GuidB, TEXT("Beta"));
			const FString Result = ApplyGuidAliases(TEXT("A=") + GuidA + TEXT(",B=") + GuidB, AliasMap);
			TestEqual(TEXT("both substituted"), Result, TEXT("A=Alpha,B=Beta"));
		});

		It("replaces the same guid twice on one line with the same alias", [this, GuidA]
		{
			TMap<FString, FString> AliasMap;
			AliasMap.Add(GuidA, TEXT("MyAlias"));
			const FString Result = ApplyGuidAliases(TEXT("First=") + GuidA + TEXT(",Second=") + GuidA, AliasMap);
			TestEqual(TEXT("both occurrences substituted"), Result, TEXT("First=MyAlias,Second=MyAlias"));
		});

		It("returns the line unchanged when the alias map is empty, even with a well-formed guid present", [this, GuidA]
		{
			const TMap<FString, FString> EmptyMap;
			const FString                Line = TEXT("ID=") + GuidA;
			TestEqual(TEXT("unchanged via early return"), ApplyGuidAliases(Line, EmptyMap), Line);
		});

		It("returns the line unchanged when there is no hex content", [this, GuidA]
		{
			TMap<FString, FString> AliasMap;
			AliasMap.Add(GuidA, TEXT("MyAlias"));
			const FString Line = TEXT("Name=\"Foo\"");
			TestEqual(TEXT("unchanged"), ApplyGuidAliases(Line, AliasMap), Line);
		});

		It("replaces only the guid run, preserving adjacent punctuation exactly", [this, GuidA]
		{
			TMap<FString, FString> AliasMap;
			AliasMap.Add(GuidA, TEXT("MyAlias"));
			const FString Result = ApplyGuidAliases(TEXT("ID=") + GuidA + TEXT(","), AliasMap);
			TestEqual(TEXT("ID= and trailing comma preserved"), Result, TEXT("ID=MyAlias,"));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
