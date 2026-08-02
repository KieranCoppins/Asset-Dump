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
	 * Strips known-default/empty pin sub-fields from a "CustomProperties Pin (...)" line, drops
	 * PersistentGuid entirely (pin-recompile bookkeeping, not part of what the graph does), and drops
	 * PinFriendlyName entirely (cosmetic UI label, redundant with PinName). Every strip only matches at a
	 * real field boundary, never inside a quoted pin value.
	 */
	FString StripDefaultPinNoise(const FString& Line);

	/** A Nodes(N)="..." line is a pure ordering index shape shared by both UEdGraph (redundant -- every node
	 *  it names is already fully declared via its own Begin Object block earlier in the graph) and unrelated
	 *  classes with their own real Nodes array (e.g. UMovieSceneNodeGroup). This only checks the line's own
	 *  shape; use FindRedundantNodesIndexLineIndices to additionally scope this to real EdGraph objects. */
	bool IsRedundantNodesIndexLine(const FString& TrimmedLine);

	/**
	 * Returns the indices within Lines (one exported object's full line list) of Nodes(N)="..." lines that
	 * are safe to drop -- scoped to Begin/End Object blocks that carry a Schema=...EdGraphSchema... property,
	 * which only a real UEdGraph (or subclass: material graphs, anim graphs, sound cue graphs, etc.) ever
	 * exports. A same-shaped Nodes property on an unrelated class (UMovieSceneNodeGroup::Nodes) is never
	 * included, since its enclosing block has no such Schema marker.
	 */
	TSet<int32> FindRedundantNodesIndexLineIndices(const TArray<FString>& Lines);

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
