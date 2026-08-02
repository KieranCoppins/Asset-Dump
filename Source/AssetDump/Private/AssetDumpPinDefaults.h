// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * Computes the real engine default value for every Blueprint pin field that UEdGraphPin::ExportTextItem
 * (EdGraphPin.cpp) always emits regardless of whether it holds a non-default value, by rendering each field
 * through the exact same FProperty export call the engine's own exporter uses, fed a real
 * default-constructed FEdGraphPinType. This can never silently drift from the real engine implementation
 * across a UE version upgrade the way a hand-typed value table can -- see AssetDumpPinDefaults.cpp for the
 * per-field grounding.
 */
namespace AssetDumpPinDefaults
{
	/**
	 * Returns {Key, DefaultValueText} pairs -- e.g. {"PinType.ContainerType", "None"}, {"bHidden", "False"} --
	 * for every pin field the real exporter unconditionally emits. PortFlags should be the same port flags
	 * used to export the pin lines being compared against (see AssetDumpCommandlet::DumpObjects), since some
	 * property types' rendered text depends on them.
	 */
	TArray<TPair<FString, FString>> ComputeDefaultPinFieldValues(int32 PortFlags);
}
