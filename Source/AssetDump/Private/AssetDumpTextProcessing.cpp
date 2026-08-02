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

	TArray<FString> BuildDefaultFieldPatterns(const TArray<TPair<FString, FString>>& Fields)
	{
		TArray<FString> Patterns;
		Patterns.Reserve(Fields.Num());
		for (const TPair<FString, FString>& Field : Fields)
		{
			Patterns.Add(FString::Printf(TEXT("%s=%s,"), *Field.Key, *Field.Value));
		}
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

	FString StripDefaultPinNoise(const FString& Line, const TArray<FString>& DefaultFieldPatterns)
	{
		if (!Line.Contains(TEXT("CustomProperties Pin")))
		{
			return Line;
		}

		FString Result = Line;
		for (const FString& Pattern : DefaultFieldPatterns)
		{
			RemoveFieldAtBoundary(Result, Pattern);
		}

		Result = StripPersistentGuidField(Result);
		Result = StripPinFriendlyNameField(Result);
		return Result;
	}

	bool TryParseArrayIndexLine(const FString& TrimmedLine, FString& OutValue)
	{
		int32 OpenParenIndex;
		if (!TrimmedLine.FindChar(TEXT('('), OpenParenIndex) || OpenParenIndex == 0)
		{
			return false;
		}

		// Everything before '(' must look like a property identifier (alnum/underscore only).
		for (int32 Index = 0; Index < OpenParenIndex; ++Index)
		{
			const TCHAR Ch = TrimmedLine[Index];
			if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
			{
				return false;
			}
		}

		const int32 Len = TrimmedLine.Len();
		int32       Index = OpenParenIndex + 1;
		if (Index >= Len || !FChar::IsDigit(TrimmedLine[Index]))
		{
			return false;
		}
		while (Index < Len && FChar::IsDigit(TrimmedLine[Index]))
		{
			++Index;
		}
		if (Index >= Len || TrimmedLine[Index] != TEXT(')'))
		{
			return false;
		}
		++Index;
		if (Index >= Len || TrimmedLine[Index] != TEXT('='))
		{
			return false;
		}

		OutValue = TrimmedLine.Mid(Index + 1);
		return true;
	}

	void FindObjectPathReferenceNames(const FString& Value, TArray<FString>& OutNames)
	{
		const int32 Len = Value.Len();
		int32       SearchFrom = 0;
		while (SearchFrom < Len)
		{
			const int32 OpenQuoteIndex = Value.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (OpenQuoteIndex == INDEX_NONE)
			{
				break;
			}
			const int32 CloseQuoteIndex = Value.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenQuoteIndex + 1);
			if (CloseQuoteIndex == INDEX_NONE)
			{
				break;
			}

			const FString Path = Value.Mid(OpenQuoteIndex + 1, CloseQuoteIndex - OpenQuoteIndex - 1);

			// The referenced object's own bare name is the trailing segment after the last '.' or ':'
			// separator (a qualified sibling path looks like "Parent:Child"; a package path like
			// "Package.Object"). If there's no separator, the whole path already is the bare name.
			int32 LastSeparatorIndex = INDEX_NONE;
			for (int32 Index = Path.Len() - 1; Index >= 0; --Index)
			{
				if (Path[Index] == TEXT('.') || Path[Index] == TEXT(':'))
				{
					LastSeparatorIndex = Index;
					break;
				}
			}
			const FString Name = (LastSeparatorIndex == INDEX_NONE) ? Path : Path.Mid(LastSeparatorIndex + 1);
			if (!Name.IsEmpty())
			{
				OutNames.Add(Name);
			}

			SearchFrom = CloseQuoteIndex + 1;
		}
	}

	/** Appends every guid immediately following a field-boundary-safe "ID=" or "Id=" marker on Line. Unlike
	 *  FindGuidsInLine (which finds a guid anywhere on the line), this only matches a guid that is the value
	 *  of a field literally named ID/Id -- e.g. the "ID=<guid>" a StateTree task/state/transition embeds as
	 *  one sub-field of its own single-line struct literal (Tasks(0)=(Node=...,ID=<guid>)), not just any
	 *  guid-shaped text appearing incidentally on the line. Both casings are real, distinct engine property
	 *  names (e.g. FStateTreeNodeIdToIndex::Id vs. a state/task's own "ID" field), so both are checked. */
	static void FindIdFieldGuids(const FString& Line, TSet<FString>& OutGuids)
	{
		for (const TCHAR* Marker : {TEXT("ID="), TEXT("Id=")})
		{
			int32 SearchFrom = 0;
			for (;;)
			{
				const int32 MarkerIndex = FindFieldMarker(Line, Marker, SearchFrom);
				if (MarkerIndex == INDEX_NONE)
				{
					break;
				}

				const int32 ValueStart = MarkerIndex + FCString::Strlen(Marker);
				if (Line.IsValidIndex(ValueStart + 31) && !(ValueStart > 0 && IsGuidChar(Line[ValueStart - 1])) && !(Line.IsValidIndex(ValueStart + 32) && IsGuidChar(Line[ValueStart + 32])))
				{
					bool bAllGuidChars = true;
					for (int32 Offset = 0; Offset < 32; ++Offset)
					{
						if (!IsGuidChar(Line[ValueStart + Offset]))
						{
							bAllGuidChars = false;
							break;
						}
					}
					if (bAllGuidChars)
					{
						OutGuids.Add(Line.Mid(ValueStart, 32));
					}
				}

				SearchFrom = ValueStart;
			}
		}
	}

	TSet<int32> FindRedundantLookupArrayLineIndices(const TArray<FString>& Lines)
	{
		// Pass 1: collect every identifier already declared elsewhere in this object's own export. The
		// exporter wraps even the top-level object itself in its own Begin/End Object pair, so a compiled,
		// object-wide lookup/cross-reference property -- IDToStateMappings/IDToNodeMappings/
		// IDToTransitionMappings, or a compiled bindings table like PropertyBindings=(SourceStructs=(...
		// ID=<guid>...)) -- sits at depth 1 (a direct property of that outermost wrapper), while a State's own
		// Tasks(N)=/Transitions(N)=/Evaluators(N)= entries (and the state's own bare "ID=<guid>" line) are
		// always nested at least one level deeper, inside that State's own Begin/End Object frame. Only an
		// ID=/Id= field found at depth > 1 -- i.e. actually within some nested child entity's own declared
		// scope -- ever counts as a real declaration; one found at depth <= 1 is exactly the kind of top-level,
		// object-wide compiled table that restates other entities' guids without being anyone's real
		// declaration, so it must never be treated as one (a prior version of this function didn't make this
		// distinction and let such a table falsely certify a real Tasks(N)= entry as "already known
		// elsewhere", incorrectly stripping real behavioral data).
		//
		// Within depth > 1, two categories still need different treatment:
		//
		// - UnconditionalKnownIds / KnownObjectNames: an ID=<guid>/Id=<guid> field on a line that is NOT
		//   itself array-element-shaped (e.g. a StateTree state's own standalone "ID=<guid>" line, a sibling
		//   of its Name= property within the same Begin/End Object block), or a Begin Object header's own
		//   Name="..." attribute. Neither has any competing line that could also claim to *be* that same
		//   identifier's declaration, so these always count as "known elsewhere", unconditionally.
		//
		// - AmbiguousIdBestLength: an ID=/Id= field embedded *inside* a line that is itself array-element-
		//   shaped -- e.g. a StateTree Tasks(N)=(Node=...,Instance=...,ID=<guid>) entry, which carries real,
		//   unique behavioral data, restates its own guid the exact same way a genuinely redundant
		//   IDToNodeMappings(N)=(Id=<guid>,Index=N) lookup entry does. Both are "array-element lines with an
		//   embedded ID=/Id= field", so a flat known-set would let either one satisfy the other -- including a
		//   rich declaration satisfying *itself* -- which is exactly backwards: the rich entry must never be
		//   treated as redundant merely because a thin lookup elsewhere also mentions its guid. Recording the
		//   length of the *richest* (longest) such line per guid, and later requiring a *strictly longer* line
		//   to count as "known", resolves this: Tasks(N)= is always the longest assertion of its own guid (so
		//   it can never be "known via a richer entry" than itself) and thus never redundant, while
		//   IDToNodeMappings(N)= is always shorter than the real Tasks(N)= declaration it restates (so it
		//   *is* known via a richer entry) and remains eligible for stripping.
		TSet<FString>        UnconditionalKnownIds;
		TMap<FString, int32> AmbiguousIdBestLength;
		TSet<FString>        KnownObjectNames;

		int32 Depth = 0;
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStart();
			if (Trimmed.StartsWith(TEXT("Begin Object")))
			{
				++Depth;
				static const FString NameMarker = TEXT("Name=\"");
				const int32          NameIndex  = Trimmed.Find(NameMarker, ESearchCase::CaseSensitive);
				if (NameIndex != INDEX_NONE)
				{
					const FString Rest = Trimmed.Mid(NameIndex + NameMarker.Len());
					int32         QuoteIndex;
					if (FindUnescapedQuote(Rest, QuoteIndex))
					{
						KnownObjectNames.Add(Rest.Left(QuoteIndex));
					}
				}
				continue;
			}
			if (Trimmed.Equals(TEXT("End Object")))
			{
				--Depth;
				continue;
			}

			if (Depth <= 1)
			{
				// A direct property of the top-level exported object itself -- never a declaration source
				// (see comment above), though it remains an ordinary candidate for the redundancy check below,
				// same as any other line.
				continue;
			}

			TSet<FString> IdsOnThisLine;
			FindIdFieldGuids(Trimmed, IdsOnThisLine);
			if (IdsOnThisLine.Num() == 0)
			{
				continue;
			}

			FString UnusedValue;
			if (TryParseArrayIndexLine(Trimmed, UnusedValue))
			{
				const int32 TrimmedLen = Trimmed.Len();
				for (const FString& Guid : IdsOnThisLine)
				{
					int32& BestLen = AmbiguousIdBestLength.FindOrAdd(Guid, 0);
					BestLen = FMath::Max(BestLen, TrimmedLen);
				}
			}
			else
			{
				UnconditionalKnownIds.Append(IdsOnThisLine);
			}
		}

		// Pass 2: an array-element line is redundant only when every identifier embedded in its value is
		// already known -- a line with no embedded identifiers at all (e.g. a plain-string array unrelated to
		// object/guid lookups) is never touched, and neither is one referencing something not declared
		// elsewhere in this object, nor one that is itself the richest (or only) assertion of its own guid.
		TSet<int32>     Result;
		TArray<FString> GuidTokens;
		TArray<FString> NameTokens;
		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString TrimmedLine = Lines[LineIndex].TrimStart();
			FString       Value;
			if (!TryParseArrayIndexLine(TrimmedLine, Value))
			{
				continue;
			}

			GuidTokens.Reset();
			NameTokens.Reset();
			FindGuidsInLine(Value, GuidTokens);
			FindObjectPathReferenceNames(Value, NameTokens);

			if (GuidTokens.Num() == 0 && NameTokens.Num() == 0)
			{
				continue;
			}

			const int32 ThisLineLength = TrimmedLine.Len();
			bool        bAllKnown       = true;
			for (const FString& Guid : GuidTokens)
			{
				const int32* AmbiguousBestLen     = AmbiguousIdBestLength.Find(Guid);
				const bool   bKnownViaRicherEntry = AmbiguousBestLen && *AmbiguousBestLen > ThisLineLength;
				if (!UnconditionalKnownIds.Contains(Guid) && !bKnownViaRicherEntry)
				{
					bAllKnown = false;
					break;
				}
			}
			for (const FString& Name : NameTokens)
			{
				if (!bAllKnown)
				{
					break;
				}
				if (!KnownObjectNames.Contains(Name))
				{
					bAllKnown = false;
				}
			}

			if (bAllKnown)
			{
				Result.Add(LineIndex);
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
