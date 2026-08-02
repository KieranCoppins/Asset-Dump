// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AssetDumpCommandlet.generated.h"

/**
 * Process exit codes this commandlet can return. Each failure mode gets its own distinct value (documented
 * in Plugins/AssetDump/README.md) so a caller can tell *why* a run failed from the exit code alone, without
 * having to parse log output -- and, importantly, so "some objects failed to export" (PartialExportFailure)
 * is never indistinguishable from a fully successful run.
 */
enum class EAssetDumpExitCode : int32
{
	Success               = 0,
	MissingAssetParam     = 1,
	PackageLoadFailed     = 2,
	NoTopLevelObjects     = 3,
	NoObjectExported      = 4,
	PartialExportFailure  = 5,
};

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
	static EAssetDumpExitCode LoadTopLevelObjects(const FString& PackagePathOrFilename, TArray<UObject*>& OutObjects);
	static EAssetDumpExitCode DumpObjects(const TArray<UObject*>& Objects);
};