// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * Pure text-processing logic used to turn a raw t3d export into a compact, human-readable dump: finding and
 * aliasing GUIDs, and stripping known-default/redundant Blueprint pin fields. Everything here operates on
 * plain FString/TArray data with no dependency on loading a package or any other engine subsystem, which is
 * what makes it unit-testable in isolation (see Tests/AssetDumpLineFormatting.spec.cpp and
 * Tests/AssetDumpGuidAliasing.spec.cpp).
 */
namespace AssetDumpTextProcessing
{
	/** Appends every maximal run of exactly 32 uppercase hex chars in Line (FGuid's "Digits" export format). */
	void FindGuidsInLine(const FString& Line, TArray<FString>& OutGuids);

	/** Reduces a property's human-readable Name value to a short identifier-safe alias base. */
	FString SanitizeAliasLabel(const FString& RawName);

	/**
	 * Removes the first PersistentGuid=<value>, field from Line, if present. Only matches at a real field
	 * boundary (immediately after '(' or ',') so a quoted pin value that happens to contain this literal
	 * text is left untouched.
	 */
	FString StripPersistentGuidField(const FString& Line);

	/**
	 * Removes a PinFriendlyName=<text literal>, field, if present. Parses the value with the engine's own
	 * FTextStringHelper::ReadFromBuffer (the same parser UPROPERTY FText import/export uses) rather than a
	 * hand-rolled scan, so a friendly name containing '(' or ')' characters can't corrupt the line.
	 * PinFriendlyName is only ever the Blueprint editor's cosmetic display label for a pin (e.g. a pin named
	 * "self" is shown to a designer as "Target") -- it never adds information beyond PinName.
	 */
	FString StripPinFriendlyNameField(const FString& Line);

	/**
	 * Turns {Key, DefaultValue} pairs (see AssetDumpPinDefaults::ComputeDefaultPinFieldValues, which computes
	 * these against the live engine rather than a hardcoded table) into "Key=Value," search patterns ready
	 * for StripDefaultPinNoise.
	 */
	TArray<FString> BuildDefaultFieldPatterns(const TArray<TPair<FString, FString>>& Fields);

	/**
	 * Strips default-valued pin sub-fields from a "CustomProperties Pin (...)" line (DefaultFieldPatterns is
	 * the caller-supplied set of "Key=Value," patterns to remove -- see BuildDefaultFieldPatterns), drops
	 * PersistentGuid entirely (pin-recompile bookkeeping, not part of what the graph does), and drops
	 * PinFriendlyName entirely (cosmetic UI label, redundant with PinName). Every strip only matches at a
	 * real field boundary, never inside a quoted pin value.
	 */
	FString StripDefaultPinNoise(const FString& Line, const TArray<FString>& DefaultFieldPatterns);

	/**
	 * Matches a generic array-element property line, e.g. "Nodes(3)=..." or "IDToNodeMappings(0)=...": any
	 * identifier immediately followed by "(<digits>)=". This only checks the line's own shape -- it says
	 * nothing about whether the line is redundant; use FindRedundantLookupArrayLineIndices for that. On a
	 * match, OutValue is set to everything after the "=".
	 */
	bool TryParseArrayIndexLine(const FString& TrimmedLine, FString& OutValue);

	/**
	 * Finds every single-quote-delimited object-reference token in Value (the general UE text-export shape
	 * for any object reference, "ClassPath'Path.To.Object'" -- see FObjectPropertyBase::GetExportPath) and
	 * appends the referenced object's own bare name: the trailing segment after the last '.' or ':' in the
	 * quoted path, e.g. "K2Node_Event_0" out of "/Script/BlueprintGraph.K2Node_Event'K2Node_Event_0'".
	 */
	void FindObjectPathReferenceNames(const FString& Value, TArray<FString>& OutNames);

	/**
	 * Returns the indices within Lines (one exported object's full line list) of array-element lines
	 * (Prop(N)=value, matched via TryParseArrayIndexLine) that are safe to drop because every identifier
	 * embedded in their value -- a 32-hex GUID (FindGuidsInLine) or an object-reference's bare name
	 * (FindObjectPathReferenceNames) -- already appears elsewhere in this same object's own export, either
	 * as an ID=<guid> property or as a Begin Object Name="..." header. This is the single general mechanism
	 * behind two previously separate special cases: StateTree's IDToStateMappings/IDToNodeMappings/
	 * IDToTransitionMappings (GUID lookup tables -- the referenced GUIDs already appear as ID= on the
	 * corresponding state/task objects) and UEdGraph's Nodes(N)="..." index array (the referenced nodes are
	 * already fully declared via their own Begin Object blocks). A same-shaped but unrelated array whose
	 * values are plain strings (e.g. UMovieSceneNodeGroup::Nodes, which holds dotted path strings with no
	 * object-reference quoting) yields no identifier tokens at all and so is never touched. A line with no
	 * embedded identifiers, or with an identifier that isn't already known elsewhere, is left alone.
	 */
	TSet<int32> FindRedundantLookupArrayLineIndices(const TArray<FString>& Lines);

	/**
	 * Scans every exported object's lines and assigns each distinct FGuid-shaped hex token a short alias:
	 * the nearby human-readable Name="..." property when one can be paired with it (disambiguated with a
	 * numeric suffix on collision), otherwise a sequential G<N> fallback. Alias strings are guaranteed
	 * globally unique within a single call -- a disambiguated suffix can never collide with another object's
	 * own literal name (or vice versa). Aliases are scoped to a single commandlet run.
	 */
	TMap<FString, FString> BuildGuidAliasMap(const TArray<TArray<FString>>& AllObjectLines);

	/** Replaces every occurrence of a known GUID in Line with its alias from GuidToAlias. */
	FString ApplyGuidAliases(const FString& Line, const TMap<FString, FString>& GuidToAlias);
}
