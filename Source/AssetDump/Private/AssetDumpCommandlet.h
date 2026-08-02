// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AssetDumpCommandlet.generated.h"

/**
 * Loads a single uasset package and prints a human-readable text export of its
 * contents to the log/console, mirroring Epic's UDiffAssetsCommandlet export
 * pipeline (UExporter + FExportObjectInnerContext) but targeting the console
 * instead of a file, so an AI coding agent can read an asset's real contents.
 *
 * Usage:
 *     <UnrealEditor> <Project.uproject> -run=AssetDump -Asset=/Game/Path/To/Asset
 */
UCLASS()
class ASSETDUMP_API UAssetDumpCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAssetDumpCommandlet();

	virtual int32 Main(const FString& Params) override;

private:
	static bool LoadTopLevelObjects(const FString& PackagePathOrFilename, TArray<UObject*>& OutObjects);
	static bool DumpObjects(const TArray<UObject*>& Objects);

	/**
	 * Scans every exported object's lines and assigns each distinct FGuid-shaped hex token a short alias:
	 * the nearby human-readable Name="..." property when one can be paired with it (disambiguated on
	 * collision), otherwise a sequential G<N> fallback. Aliases are scoped to a single commandlet run.
	 */
	static TMap<FString, FString> BuildGuidAliasMap(const TArray<TArray<FString>>& AllObjectLines);

	/** Replaces every occurrence of a known GUID in Line with its alias from GuidToAlias. */
	static FString ApplyGuidAliases(const FString& Line, const TMap<FString, FString>& GuidToAlias);
};