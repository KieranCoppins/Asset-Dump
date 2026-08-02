// Fill out your copyright notice in the Description page of Project Settings.


#include "AssetDumpCommandlet.h"

#include "AssetDumpPinDefaults.h"
#include "AssetDumpTextProcessing.h"
#include "Exporters/Exporter.h"
#include "Misc/StringOutputDevice.h"
#include "UnrealExporter.h"
#include "UObject/Package.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssetDump, Log, All);

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
		return static_cast<int32>(EAssetDumpExitCode::MissingAssetParam);
	}

	TArray<UObject*>         Objects;
	const EAssetDumpExitCode LoadResult = LoadTopLevelObjects(*AssetParam, Objects);
	if (LoadResult != EAssetDumpExitCode::Success)
	{
		return static_cast<int32>(LoadResult);
	}

	return static_cast<int32>(DumpObjects(Objects));
}

EAssetDumpExitCode UAssetDumpCommandlet::LoadTopLevelObjects(const FString& PackagePathOrFilename, TArray<UObject*>& OutObjects)
{
	UPackage* Package = LoadPackage(nullptr, *PackagePathOrFilename, LOAD_None);
	if (!Package)
	{
		UE_LOG(LogAssetDump, Error, TEXT("Could not load package '%s'."), *PackagePathOrFilename);
		return EAssetDumpExitCode::PackageLoadFailed;
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
		return EAssetDumpExitCode::NoTopLevelObjects;
	}

	OutObjects.Sort([](const UObject& A, const UObject& B) { return A.GetName() < B.GetName(); });
	return EAssetDumpExitCode::Success;
}

EAssetDumpExitCode UAssetDumpCommandlet::DumpObjects(const TArray<UObject*>& Objects)
{
	using namespace AssetDumpTextProcessing;

	const FString                   Extension = TEXT("t3d");
	const FExportObjectInnerContext Context;

	// PPF_SeparateDefine skips the exporter's "declare only" pass over nested subobjects (which just restates
	// their class/name a second time with no property values) and goes straight to the pass that fills in
	// property values. Type information isn't lost: ExportPath= (still emitted regardless of this flag)
	// already encodes each subobject's class inline.
	const int32 PortFlags = PPF_ExportsNotFullyQualified | PPF_SeparateDefine;

	// Export every object first, without logging, so the GUID alias map below can be built from -- and
	// then consistently applied across -- all of this run's output rather than just one object at a time.
	TArray<UObject*>        ExportedObjects;
	TArray<TArray<FString>> ExportedObjectLines;
	bool                     bAnyObjectFailed = false;

	for (UObject* Object : Objects)
	{
		UExporter* Exporter = UExporter::FindExporter(Object, *Extension);
		if (!Exporter)
		{
			UE_LOG(LogAssetDump, Warning, TEXT("No exporter found for '%s' (class '%s'); skipping."), *Object->GetName(), *Object->GetClass()->GetName());
			bAnyObjectFailed = true;
			continue;
		}

		FStringOutputDevice Buffer;
		UExporter::ExportToOutputDevice(&Context, Object, Exporter, Buffer, *Extension, 0, PortFlags, false);

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
			bAnyObjectFailed = true;
			continue;
		}

		TArray<FString> Lines;
		Buffer.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);
		ExportedObjects.Add(Object);
		ExportedObjectLines.Add(MoveTemp(Lines));
	}

	if (ExportedObjects.IsEmpty())
	{
		return EAssetDumpExitCode::NoObjectExported;
	}

	const TMap<FString, FString> GuidToAlias = BuildGuidAliasMap(ExportedObjectLines);
	if (GuidToAlias.Num() > 0)
	{
		UE_LOG(LogAssetDump, Display, TEXT("---- %d GUID(s) aliased to short ids for readability (scoped to this dump only) ----"), GuidToAlias.Num());
	}

	// Computed once per run (engine-constant, not per-object) against the live engine's own default pin
	// values rather than a hardcoded table -- see AssetDumpPinDefaults::ComputeDefaultPinFieldValues. The
	// port flags here are PPF_Delimited, not the object-level PortFlags above: UEdGraphNode::ExportCustomProperties
	// (EdGraphNode.cpp) exports each pin via "Pin->ExportTextItem(PinString, PPF_Delimited);", ignoring
	// whatever flags the outer object exporter used, so that's the flag set that actually reaches a pin's
	// field-level export and must be matched here.
	const TArray<FString> DefaultPinFieldPatterns = BuildDefaultFieldPatterns(AssetDumpPinDefaults::ComputeDefaultPinFieldValues(PPF_Delimited));

	for (int32 ObjectIndex = 0; ObjectIndex < ExportedObjects.Num(); ++ObjectIndex)
	{
		UObject*                Object = ExportedObjects[ObjectIndex];
		const TArray<FString>&  Lines  = ExportedObjectLines[ObjectIndex];
		const TSet<int32>       RedundantLookupArrayLineIndices = FindRedundantLookupArrayLineIndices(Lines);

		UE_LOG(LogAssetDump, Display, TEXT("---- BEGIN %s ----"), *Object->GetPathName());

		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString& Line = Lines[LineIndex];

			// A "Prop(N)=value" array-element line whose value is entirely made up of identifiers (guids or
			// object references) already declared elsewhere in this same object's export -- e.g. StateTree's
			// IDToStateMappings/IDToNodeMappings/IDToTransitionMappings, or a UEdGraph's Nodes(N)="..." index
			// -- is pure lookup/ordering bookkeeping that adds no readable information. See
			// FindRedundantLookupArrayLineIndices for the general detection rule.
			if (RedundantLookupArrayLineIndices.Contains(LineIndex))
			{
				UE_LOG(LogAssetDump, Verbose, TEXT("Dropping redundant lookup-array line: %s"), *Line);
				continue;
			}

			UE_LOG(LogAssetDump, Display, TEXT("%s"), *StripDefaultPinNoise(ApplyGuidAliases(Line, GuidToAlias), DefaultPinFieldPatterns));
		}

		UE_LOG(LogAssetDump, Display, TEXT("---- END %s ----"), *Object->GetPathName());
	}

	return bAnyObjectFailed ? EAssetDumpExitCode::PartialExportFailure : EAssetDumpExitCode::Success;
}