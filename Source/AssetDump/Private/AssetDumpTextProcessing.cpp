// Fill out your copyright notice in the Description page of Project Settings.

#include "AssetDumpTextProcessing.h"

#include "Internationalization/Text.h"
#include "Misc/Char.h"

namespace AssetDumpTextProcessing
{
	static bool IsGuidChar(TCHAR Ch)
	{
		return (Ch >= TEXT('0') && Ch <= TEXT('9')) || (Ch >= TEXT('A') && Ch <= TEXT('F'));
	}

	/** True if the character at Line[Index - 1] (or Index == 0) marks a genuine field boundary -- i.e. the
	 *  text at Index cannot be inside a quoted pin value, only a real "Key=Value" field of its own. */
	static bool IsAtFieldBoundary(const FString& Line, int32 Index)
	{
		return Index == 0 || Line[Index - 1] == TEXT('(') || Line[Index - 1] == TEXT(',');
	}

	/** Finds the first occurrence of Marker in Line, at or after StartFrom, that starts at a real field
	 *  boundary (see IsAtFieldBoundary) rather than in the middle of an unrelated quoted pin value. */
	static int32 FindFieldMarker(const FString& Line, const FString& Marker, int32 StartFrom = 0)
	{
		int32 SearchFrom = StartFrom;
		for (;;)
		{
			const int32 FoundIndex = Line.Find(Marker, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (FoundIndex == INDEX_NONE || IsAtFieldBoundary(Line, FoundIndex))
			{
				return FoundIndex;
			}
			SearchFrom = FoundIndex + 1;
		}
	}

	/** Removes every field-boundary-safe occurrence of FieldPattern (e.g. "bHidden=False,") from Line. */
	static void RemoveFieldAtBoundary(FString& Line, const FString& FieldPattern)
	{
		int32 SearchFrom = 0;
		for (;;)
		{
			const int32 FoundIndex = FindFieldMarker(Line, FieldPattern, SearchFrom);
			if (FoundIndex == INDEX_NONE)
			{
				return;
			}
			Line       = Line.Left(FoundIndex) + Line.Mid(FoundIndex + FieldPattern.Len());
			SearchFrom = FoundIndex;
		}
	}

	void FindGuidsInLine(const FString& Line, TArray<FString>& OutGuids)
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

	FString SanitizeAliasLabel(const FString& RawName)
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

	/** Finds the first *unescaped* '"' in Text, skipping over any \<char> escape sequence (Unreal's text
	 *  export writes an embedded quote in a string value as \", which a naive FindChar('"') would stop at
	 *  instead of the real terminating quote). Returns false if no unescaped quote is found. */
	static bool FindUnescapedQuote(const FString& Text, int32& OutIndex)
	{
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			if (Text[Index] == TEXT('\\'))
			{
				++Index; // Skip the escaped character too, so e.g. \\ doesn't get mistaken for an escape of '"'.
				continue;
			}
			if (Text[Index] == TEXT('"'))
			{
				OutIndex = Index;
				return true;
			}
		}
		return false;
	}

	/** Tracks the Name="..."/ID=<Guid> sibling properties of the currently-open Begin/End Object block. */
	struct FObjectFrame
	{
		FString Name;
		FString Id;
	};

	/**
	 * Blueprint graph pins (CustomProperties Pin (...) lines) are exported via UEdGraphPin::ExportTextItem.
	 * Its PinType.* sub-fields are written through a loop that diffs each FProperty against a default
	 * FEdGraphPinType instance (EdGraphPin.cpp, ExportTextItem) -- but that diff is a no-op for these
	 * specific fields: e.g. FBoolProperty::ExportText_Internal (PropertyBool.cpp) ignores the DefaultValue
	 * parameter entirely and always writes "True"/"False" regardless of it, so every pin restates the same
	 * ~16 fields whether or not they hold a non-default value. Confirmed empirically -- every field below
	 * appears in the raw, unstripped export of every pin, every time, not just some. Values are verified
	 * against the actual constructor defaults in Engine/Classes/EdGraph/EdGraphPin.h; a field is only ever
	 * stripped when it exactly equals its default, so a non-default value (e.g. bHidden=True on a hidden
	 * self pin, or an actual LinkedTo=) is always left in place.
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

	/** The same (Key, DefaultValue) pairs as GetDefaultPinFields(), precomputed once into "Key=Value,"
	 *  search strings so StripDefaultPinNoise doesn't rebuild them from scratch on every pin line. */
	static const TArray<FString>& GetDefaultPinFieldPatterns()
	{
		static const TArray<FString> Patterns = []
		{
			TArray<FString> Result;
			Result.Reserve(GetDefaultPinFields().Num());
			for (const TPair<FString, FString>& Field : GetDefaultPinFields())
			{
				Result.Add(FString::Printf(TEXT("%s=%s,"), *Field.Key, *Field.Value));
			}
			return Result;
		}();
		return Patterns;
	}

	FString StripPersistentGuidField(const FString& Line)
	{
		static const FString Marker = TEXT("PersistentGuid=");
		const int32          MarkerIndex = FindFieldMarker(Line, Marker);
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

	FString StripPinFriendlyNameField(const FString& Line)
	{
		static const FString Marker = TEXT("PinFriendlyName=");
		const int32          MarkerIndex = FindFieldMarker(Line, Marker);
		if (MarkerIndex == INDEX_NONE)
		{
			return Line;
		}

		// Parse the text literal (NSLOCTEXT(...), LOCTEXT(...), INVTEXT(...), or a plain quoted string) with
		// the engine's own text parser rather than hand-rolled paren counting -- ReadFromBuffer correctly
		// treats '(' and ')' characters inside the literal's own quoted arguments as ordinary text, so a
		// friendly name like "Location (World Space)" or "Text)" can't cause it to stop in the wrong place.
		const TCHAR* LineStart  = *Line;
		const TCHAR* ValueStart = LineStart + MarkerIndex + Marker.Len();
		FText        ParsedValue;
		const TCHAR* AfterValue = FTextStringHelper::ReadFromBuffer(ValueStart, ParsedValue);
		if (!AfterValue)
		{
			// Not a recognizable text literal -- leave the line untouched rather than guess.
			return Line;
		}

		int32 Index = static_cast<int32>(AfterValue - LineStart);
		if (Index < Line.Len() && Line[Index] == TEXT(','))
		{
			++Index;
		}

		return Line.Left(MarkerIndex) + Line.Mid(Index);
	}

	FString StripDefaultPinNoise(const FString& Line)
	{
		if (!Line.Contains(TEXT("CustomProperties Pin")))
		{
			return Line;
		}

		FString Result = Line;
		for (const FString& Pattern : GetDefaultPinFieldPatterns())
		{
			RemoveFieldAtBoundary(Result, Pattern);
		}

		Result = StripPersistentGuidField(Result);
		Result = StripPinFriendlyNameField(Result);
		return Result;
	}

	bool IsRedundantNodesIndexLine(const FString& TrimmedLine)
	{
		if (!TrimmedLine.StartsWith(TEXT("Nodes(")))
		{
			return false;
		}
		int32 CloseParenIndex;
		return TrimmedLine.FindChar(TEXT(')'), CloseParenIndex) && TrimmedLine.IsValidIndex(CloseParenIndex + 1) && TrimmedLine[CloseParenIndex + 1] == TEXT('=');
	}

	/** Tracks, for one open Begin/End Object block, whether it carries a Schema=...EdGraphSchema... property
	 *  (the marker only a real UEdGraph, or subclass, ever exports) and which of its own Nodes(N)= line
	 *  indices are candidates to drop if that marker turns out to be present. */
	struct FNodesFilterFrame
	{
		bool          bHasEdGraphSchema = false;
		TArray<int32> CandidateLineIndices;
	};

	TSet<int32> FindRedundantNodesIndexLineIndices(const TArray<FString>& Lines)
	{
		TArray<FNodesFilterFrame> Stack;
		TSet<int32>               Result;

		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString Trimmed = Lines[LineIndex].TrimStart();
			if (Trimmed.StartsWith(TEXT("Begin Object")))
			{
				Stack.AddDefaulted();
				continue;
			}
			if (Trimmed.Equals(TEXT("End Object")))
			{
				if (Stack.Num() > 0)
				{
					FNodesFilterFrame Frame = Stack.Pop();
					if (Frame.bHasEdGraphSchema)
					{
						Result.Append(Frame.CandidateLineIndices);
					}
				}
				continue;
			}
			if (Stack.Num() == 0)
			{
				continue;
			}

			FNodesFilterFrame& Top = Stack.Top();
			if (Trimmed.StartsWith(TEXT("Schema=")) && Trimmed.Contains(TEXT("EdGraphSchema")))
			{
				Top.bHasEdGraphSchema = true;
			}
			else if (IsRedundantNodesIndexLine(Trimmed))
			{
				Top.CandidateLineIndices.Add(LineIndex);
			}
		}

		return Result;
	}

	TMap<FString, FString> BuildGuidAliasMap(const TArray<TArray<FString>>& AllObjectLines)
	{
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
					if (FindUnescapedQuote(Rest, QuoteIndex))
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
		// UsedAliases tracks every alias string assigned so far, across *both* the name-derived and G<N>
		// fallback namespaces, so a disambiguated suffix (e.g. "Foo_2") can never collide with a different
		// object's own literal name (or a fallback alias), even though each is computed independently.
		TMap<FString, FString> GuidToAlias;
		TMap<FString, int32>   AliasBaseUsage;
		TSet<FString>          UsedAliases;
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
							do
							{
								++UsageCount;
								Alias = (UsageCount == 1) ? Base : FString::Printf(TEXT("%s_%d"), *Base, UsageCount);
							} while (UsedAliases.Contains(Alias));
						}
					}

					if (Alias.IsEmpty())
					{
						do
						{
							Alias = FString::Printf(TEXT("G%d"), ++FallbackCounter);
						} while (UsedAliases.Contains(Alias));
					}

					UsedAliases.Add(Alias);
					GuidToAlias.Add(Guid, Alias);
				}
			}
		}

		return GuidToAlias;
	}

	FString ApplyGuidAliases(const FString& Line, const TMap<FString, FString>& GuidToAlias)
	{
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
}
