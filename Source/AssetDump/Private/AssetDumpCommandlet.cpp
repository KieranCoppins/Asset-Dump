// Fill out your copyright notice in the Description page of Project Settings.


#include "AssetDumpCommandlet.h"

#include "Exporters/Exporter.h"
#include "Misc/Char.h"
#include "Misc/StringOutputDevice.h"
#include "UnrealExporter.h"
#include "UObject/Package.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssetDump, Log, All);

namespace AssetDumpPrivate
{
	static bool IsGuidChar(TCHAR Ch)
	{
		return (Ch >= TEXT('0') && Ch <= TEXT('9')) || (Ch >= TEXT('A') && Ch <= TEXT('F'));
	}

	/** Appends every maximal run of exactly 32 uppercase hex chars in Line (FGuid's "Digits" export format). */
	static void FindGuidsInLine(const FString& Line, TArray<FString>& OutGuids)
	{
		const TCHAR* Chars = *Line;
		const int32  Len   = Line.Len();
		int32        Index = 0;
		while (Index < Len)
		{
			if (IsGuidChar(Chars[Index]))
			{
				const int32 RunStart = Index;
				while (Index < Len && IsGuidChar(Chars[Index]))
				{
					++Index;
				}
				if (Index - RunStart == 32)
				{
					OutGuids.Add(Line.Mid(RunStart, 32));
				}
			}
			else
			{
				++Index;
			}
		}
	}

	/** Reduces a property's human-readable Name value to a short identifier-safe alias base. */
	static FString SanitizeAliasLabel(const FString& RawName)
	{
		FString Result;
		Result.Reserve(RawName.Len());
		for (TCHAR Ch : RawName)
		{
			if (FChar::IsAlnum(Ch))
			{
				Result.AppendChar(Ch);
			}
			else if (!Result.IsEmpty() && Result[Result.Len() - 1] != TEXT('_'))
			{
				Result.AppendChar(TEXT('_'));
			}
		}
		Result.RemoveFromEnd(TEXT("_"));
		if (Result.Len() > 40)
		{
			Result.LeftInline(40);
		}
		return Result;
	}

	/** Tracks the Name="..."/ID=<Guid> sibling properties of the currently-open Begin/End Object block. */
	struct FObjectFrame
	{
		FString Name;
		FString Id;
	};

	/**
	 * Blueprint graph pins (CustomProperties Pin (...) lines) are exported via UEdGraphPin::ExportTextItem,
	 * which -- unlike normal UPROPERTY export -- always writes every field unconditionally with no diff
	 * against a default/archetype. That means every pin restates the same ~12 always-false/empty fields.
	 * Values below are verified against the actual constructor defaults in Engine/Classes/EdGraph/EdGraphPin.h;
	 * a field is only ever stripped when it exactly equals its default, so a non-default value (e.g.
	 * bHidden=True on a hidden self pin, or an actual LinkedTo=) is always left in place.
	 */
	static const TArray<TPair<FString, FString>>& GetDefaultPinFields()
	{
		static const TArray<TPair<FString, FString>> Defaults = {
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
		};
		return Defaults;
	}

	/** Removes the first PersistentGuid=<value>, field from Line, if present. */
	static FString StripPersistentGuidField(const FString& Line)
	{
		static const FString Marker = TEXT("PersistentGuid=");
		const int32          MarkerIndex = Line.Find(Marker);
		if (MarkerIndex == INDEX_NONE)
		{
			return Line;
		}

		const int32 CommaIndex = Line.Find(TEXT(","), ESearchCase::CaseSensitive, ESearchDir::FromStart, MarkerIndex + Marker.Len());
		if (CommaIndex == INDEX_NONE)
		{
			return Line;
		}

		return Line.Left(MarkerIndex) + Line.Mid(CommaIndex + 1);
	}

	/**
	 * Removes a PinFriendlyName=NSLOCTEXT(...), field, if present. Uses paren-depth tracking rather than a
	 * fixed-length match because the NSLOCTEXT(...) arguments themselves contain commas. PinFriendlyName is
	 * only ever the Blueprint editor's cosmetic display label for a pin (e.g. a pin named "self" is shown
	 * to a designer as "Target") -- it never adds structural or behavioral information beyond PinName.
	 */
	static FString StripPinFriendlyNameField(const FString& Line)
	{
		static const FString Marker = TEXT("PinFriendlyName=NSLOCTEXT(");
		const int32          MarkerIndex = Line.Find(Marker);
		if (MarkerIndex == INDEX_NONE)
		{
			return Line;
		}

		const int32 Len = Line.Len();
		int32       Index = MarkerIndex + Marker.Len();
		int32       Depth = 1;
		while (Index < Len && Depth > 0)
		{
			if (Line[Index] == TEXT('('))
			{
				++Depth;
			}
			else if (Line[Index] == TEXT(')'))
			{
				--Depth;
			}
			++Index;
		}
		if (Index < Len && Line[Index] == TEXT(','))
		{
			++Index;
		}

		return Line.Left(MarkerIndex) + Line.Mid(Index);
	}

	/**
	 * Strips known-default/empty pin sub-fields from a "CustomProperties Pin (...)" line, drops
	 * PersistentGuid entirely (pin-recompile bookkeeping, not part of what the graph does), and drops
	 * PinFriendlyName entirely (cosmetic UI label, redundant with PinName).
	 */
	static FString StripDefaultPinNoise(const FString& Line)
	{
		if (!Line.Contains(TEXT("CustomProperties Pin")))
		{
			return Line;
		}

		FString Result = Line;
		for (const TPair<FString, FString>& Field : GetDefaultPinFields())
		{
			Result.ReplaceInline(*FString::Printf(TEXT("%s=%s,"), *Field.Key, *Field.Value), TEXT(""));
		}

		Result = StripPersistentGuidField(Result);
		Result = StripPinFriendlyNameField(Result);
		return Result;
	}

	/** A Nodes(N)="..." line in an EdGraph is a pure ordering index -- every node it names is already fully
	 *  declared via its own Begin Object block earlier in the same graph, so the line adds no information. */
	static bool IsRedundantNodesIndexLine(const FString& TrimmedLine)
	{
		if (!TrimmedLine.StartsWith(TEXT("Nodes(")))
		{
			return false;
		}
		int32 CloseParenIndex;
		return TrimmedLine.FindChar(TEXT(')'), CloseParenIndex) && TrimmedLine.IsValidIndex(CloseParenIndex + 1) && TrimmedLine[CloseParenIndex + 1] == TEXT('=');
	}
}

UAssetDumpCommandlet::UAssetDumpCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;

	HelpDescription = TEXT("Dumps the human-readable text export of a uasset package to the log.");
	HelpUsage = TEXT("<Editor> <Project> -run=AssetDump -Asset=/Game/Path/To/Asset");
	HelpParamNames.Add(TEXT("Asset"));
	HelpParamDescriptions.Add(TEXT("Long package name or file path of the asset to dump, e.g. /Game/Items/Sword"));
}

int32 UAssetDumpCommandlet::Main(const FString& Params)
{
	TArray<FString>        Tokens;
	TArray<FString>        Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FString* AssetParam = ParamsMap.Find(TEXT("Asset"));
	if (!AssetParam || AssetParam->IsEmpty())
	{
		UE_LOG(LogAssetDump, Error, TEXT("Usage: -run=AssetDump -Asset=/Game/Path/To/Asset"));
		return 1;
	}

	TArray<UObject*> Objects;
	if (!LoadTopLevelObjects(*AssetParam, Objects))
	{
		return 1;
	}

	if (!DumpObjects(Objects))
	{
		return 1;
	}

	return 0;
}

bool UAssetDumpCommandlet::LoadTopLevelObjects(const FString& PackagePathOrFilename, TArray<UObject*>& OutObjects)
{
	UPackage* Package = LoadPackage(nullptr, *PackagePathOrFilename, LOAD_None);
	if (!Package)
	{
		UE_LOG(LogAssetDump, Error, TEXT("Could not load package '%s'."), *PackagePathOrFilename);
		return false;
	}

	for (TObjectIterator<UObject> It; It; ++It)
	{
		// SKEL_ classes are compiler-internal stand-ins for a Blueprint's generated class, used only to
		// resolve circular references during compilation. They restate the same functions/properties as
		// the real generated class with no additional information, so skip them to cut dump size.
		if (It->GetOuter() == Package && !It->GetName().StartsWith(TEXT("SKEL_")))
		{
			OutObjects.Add(*It);
		}
	}

	if (OutObjects.IsEmpty())
	{
		UE_LOG(LogAssetDump, Error, TEXT("Loaded '%s' but it contained no top-level objects."), *PackagePathOrFilename);
		return false;
	}

	OutObjects.Sort([](const UObject& A, const UObject& B) { return A.GetName() < B.GetName(); });
	return true;
}

TMap<FString, FString> UAssetDumpCommandlet::BuildGuidAliasMap(const TArray<TArray<FString>>& AllObjectLines)
{
	using namespace AssetDumpPrivate;

	// Pass 1: within each object's own Begin/End Object nesting, pair up an ID=<Guid> property with its
	// sibling Name="..." property (both direct children of the same object block) to get a human-readable
	// label for that guid.
	TMap<FString, FString> GuidToName;
	for (const TArray<FString>& Lines : AllObjectLines)
	{
		TArray<FObjectFrame> Stack;
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStart();
			if (Trimmed.StartsWith(TEXT("Begin Object")))
			{
				Stack.AddDefaulted();
				continue;
			}
			if (Trimmed.Equals(TEXT("End Object")))
			{
				if (Stack.Num() > 0)
				{
					FObjectFrame Frame = Stack.Pop();
					if (!Frame.Name.IsEmpty() && !Frame.Id.IsEmpty() && !GuidToName.Contains(Frame.Id))
					{
						GuidToName.Add(Frame.Id, Frame.Name);
					}
				}
				continue;
			}
			if (Stack.Num() == 0)
			{
				continue;
			}

			FObjectFrame& Top = Stack.Top();
			if (Top.Name.IsEmpty() && Trimmed.StartsWith(TEXT("Name=\"")))
			{
				const FString Rest = Trimmed.Mid(6);
				int32         QuoteIndex;
				if (Rest.FindChar(TEXT('"'), QuoteIndex))
				{
					Top.Name = Rest.Left(QuoteIndex);
				}
			}
			else if (Top.Id.IsEmpty() && Trimmed.StartsWith(TEXT("ID=")))
			{
				const FString Candidate = Trimmed.Mid(3);
				if (Candidate.Len() == 32)
				{
					Top.Id = Candidate;
				}
			}
		}
	}

	// Pass 2: find every distinct GUID-shaped token anywhere in the dump, in first-seen order, and assign
	// each a short alias -- the paired human name when one was found (disambiguated on collision, since
	// e.g. two different StateTree states can share the same Name), otherwise a sequential G<N> fallback.
	TMap<FString, FString> GuidToAlias;
	TMap<FString, int32>   AliasBaseUsage;
	int32                  FallbackCounter = 0;

	TArray<FString> LineGuids;
	for (const TArray<FString>& Lines : AllObjectLines)
	{
		for (const FString& Line : Lines)
		{
			LineGuids.Reset();
			FindGuidsInLine(Line, LineGuids);
			for (const FString& Guid : LineGuids)
			{
				if (GuidToAlias.Contains(Guid))
				{
					continue;
				}

				FString Alias;
				if (const FString* Name = GuidToName.Find(Guid))
				{
					const FString Base = SanitizeAliasLabel(*Name);
					if (!Base.IsEmpty())
					{
						int32& UsageCount = AliasBaseUsage.FindOrAdd(Base);
						++UsageCount;
						Alias = (UsageCount == 1) ? Base : FString::Printf(TEXT("%s_%d"), *Base, UsageCount);
					}
				}

				if (Alias.IsEmpty())
				{
					Alias = FString::Printf(TEXT("G%d"), ++FallbackCounter);
				}

				GuidToAlias.Add(Guid, Alias);
			}
		}
	}

	return GuidToAlias;
}

FString UAssetDumpCommandlet::ApplyGuidAliases(const FString& Line, const TMap<FString, FString>& GuidToAlias)
{
	using namespace AssetDumpPrivate;

	if (GuidToAlias.Num() == 0)
	{
		return Line;
	}

	FString      Result;
	const TCHAR* Chars = *Line;
	const int32  Len   = Line.Len();
	int32        Index = 0;
	Result.Reserve(Len);

	while (Index < Len)
	{
		if (IsGuidChar(Chars[Index]))
		{
			const int32 RunStart = Index;
			while (Index < Len && IsGuidChar(Chars[Index]))
			{
				++Index;
			}
			const int32 RunLength = Index - RunStart;
			if (RunLength == 32)
			{
				if (const FString* Alias = GuidToAlias.Find(Line.Mid(RunStart, 32)))
				{
					Result += *Alias;
					continue;
				}
			}
			Result += Line.Mid(RunStart, RunLength);
		}
		else
		{
			Result.AppendChar(Chars[Index]);
			++Index;
		}
	}

	return Result;
}

bool UAssetDumpCommandlet::DumpObjects(const TArray<UObject*>& Objects)
{
	using namespace AssetDumpPrivate;

	const FString                   Extension = TEXT("t3d");
	const FExportObjectInnerContext Context;

	// Export every object first, without logging, so the GUID alias map below can be built from -- and
	// then consistently applied across -- all of this run's output rather than just one object at a time.
	TArray<UObject*>        ExportedObjects;
	TArray<TArray<FString>> ExportedObjectLines;

	for (UObject* Object : Objects)
	{
		UExporter* Exporter = UExporter::FindExporter(Object, *Extension);
		if (!Exporter)
		{
			UE_LOG(LogAssetDump, Warning, TEXT("No exporter found for '%s' (class '%s'); skipping."), *Object->GetName(), *Object->GetClass()->GetName());
			continue;
		}

		FStringOutputDevice Buffer;

		// PPF_SeparateDefine skips the exporter's "declare only" pass over nested subobjects (which just
		// restates their class/name a second time with no property values) and goes straight to the pass
		// that fills in property values. Type information isn't lost: ExportPath= (still emitted regardless
		// of this flag) already encodes each subobject's class inline.
		UExporter::ExportToOutputDevice(&Context, Object, Exporter, Buffer, *Extension, 0, PPF_ExportsNotFullyQualified | PPF_SeparateDefine, false);

		TMap<FString, FString> NativePropertyValues;
		if (Object->GetNativePropertyValues(NativePropertyValues) && NativePropertyValues.Num() > 0)
		{
			for (const TPair<FString, FString>& Pair : NativePropertyValues)
			{
				Buffer.Logf(TEXT("  %s=%s"), *Pair.Key, *Pair.Value);
			}
		}

		if (Buffer.Len() == 0)
		{
			UE_LOG(LogAssetDump, Warning, TEXT("Exporter produced no text for '%s'."), *Object->GetName());
			continue;
		}

		TArray<FString> Lines;
		Buffer.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);
		ExportedObjects.Add(Object);
		ExportedObjectLines.Add(MoveTemp(Lines));
	}

	if (ExportedObjects.IsEmpty())
	{
		return false;
	}

	const TMap<FString, FString> GuidToAlias = BuildGuidAliasMap(ExportedObjectLines);
	if (GuidToAlias.Num() > 0)
	{
		UE_LOG(LogAssetDump, Display, TEXT("---- %d GUID(s) aliased to short ids for readability (scoped to this dump only) ----"), GuidToAlias.Num());
	}

	for (int32 ObjectIndex = 0; ObjectIndex < ExportedObjects.Num(); ++ObjectIndex)
	{
		UObject* Object = ExportedObjects[ObjectIndex];

		UE_LOG(LogAssetDump, Display, TEXT("---- BEGIN %s ----"), *Object->GetPathName());

		for (const FString& Line : ExportedObjectLines[ObjectIndex])
		{
			// StateTree's compiled IDToStateMappings/IDToNodeMappings/IDToTransitionMappings arrays are
			// GUID->array-index lookup tables used only by the editor at runtime; the same GUIDs already
			// appear as ID= on the corresponding source objects above, so these add no readable information.
			const FString TrimmedLine = Line.TrimStart();
			if (TrimmedLine.StartsWith(TEXT("IDTo")) && TrimmedLine.Contains(TEXT("Mappings(")))
			{
				continue;
			}

			// A Blueprint EdGraph's Nodes(N)="..." lines are a pure ordering index -- every node they name
			// is already fully declared via its own Begin Object block earlier in the same graph.
			if (IsRedundantNodesIndexLine(TrimmedLine))
			{
				continue;
			}

			UE_LOG(LogAssetDump, Display, TEXT("%s"), *StripDefaultPinNoise(ApplyGuidAliases(Line, GuidToAlias)));
		}

		UE_LOG(LogAssetDump, Display, TEXT("---- END %s ----"), *Object->GetPathName());
	}

	return true;
}