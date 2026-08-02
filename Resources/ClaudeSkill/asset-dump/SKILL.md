---
name: asset-dump
description: Use when you need to know what a specific Unreal Engine uasset (Blueprint, Data Asset, State Tree, Gameplay Ability, Widget, etc.) actually contains — properties, graph logic, wiring, component hierarchy — instead of guessing from the binary .uasset or inferring from C++ source alone. Trigger on "what does BP_X do", "tell me about the DA_Y data asset", "how is this ability/state tree structured", or any request to inspect a specific asset's real contents. Runs the AssetDump commandlet (-run=AssetDump -Asset=<PackagePath>) and teaches how to find the editor binary, invoke it, read its BEGIN/END and GUID-alias output, and navigate large dumps with grep instead of reading them whole. Do NOT trigger for questions answerable from C++ source alone, for asset types with no text exporter (Textures, StaticMeshes, SoundWaves — fail with "No exporter found"), or in a project where this plugin isn't present/enabled.
---

# AssetDump — Inspecting Unreal Asset Contents

## Role

Read what a uasset actually contains — instead of guessing from the binary `.uasset` file or inferring behavior from embedded string tables — by running the AssetDump commandlet this skill ships alongside. `Plugins/AssetDump/README.md` is the canonical spec for the tool; this skill covers how to operate it.

## When to use this

Use it for questions about a *specific named asset*: "what does BP_X do", "tell me about the DA_Y data asset", "how is this ability/state tree/widget structured or wired", or any request to inspect an asset's real properties, graph logic, or component hierarchy.

Don't use it for:
- Questions answerable from C++ source alone, with no specific asset involved.
- Asset types with no text exporter — raw `Texture2D`, `StaticMesh`, `SoundWave`, etc. will produce a `No exporter found` warning and possibly exit code `1`. Check the asset type first if unsure.
- A project where the plugin isn't present or enabled — confirm `Plugins/AssetDump/AssetDump.uplugin` exists and is listed in the project's `.uproject` `Plugins` array before assuming it's usable.

## Finding the editor binary and project

Figure this out fresh each time — never hardcode a path in this skill, since it varies per machine and OS.

1. Locate the project's `.uproject` file (usually the repo root; glob for it if not obvious).
2. Read its `EngineAssociation` field (e.g. `"5.7"`) to know which engine version to look for.
3. Check common per-OS install locations for that version, in order, stopping at the first that exists:
   - **macOS:** `/Users/Shared/Epic Games/UE_<version>/Engine/Binaries/Mac/UnrealEditor`
   - **Windows:** `C:\Program Files\Epic Games\UE_<version>\Engine\Binaries\Win64\UnrealEditor.exe`
   - **Linux:** no fixed convention (often a source build) — check for a project-local `Engine/` symlink, or ask.
4. If none of the common locations exist, ask the user for the path rather than guessing further.

## Invocation

```
"<UnrealEditor>" "<Project>.uproject" -run=AssetDump -Asset=<PackagePath> -unattended -nopause -nosplash -nullrhi -stdout -FullStdOutLogOutput
```

- `-Asset=` takes a **package path** (e.g. `/Game/Blueprints/BP_MyActor`), not a filesystem path. A bare package path dumps every top-level object it contains.
- `-unattended -nopause -nosplash` suppress interactive dialogs.
- `-nullrhi` skips renderer initialization — not needed for a data-only dump.
- `-stdout -FullStdOutLogOutput` force complete, immediate log output to the terminal rather than buffering.

Capture stdout to a file right away rather than relying on terminal scrollback — the navigation workflow below depends on being able to grep it.

## Reading the output

- Exit code `0` means every object exported successfully. Any non-zero code means something failed -- codes `1`-`5` each mean something different (missing `-Asset=`, package load failure, no top-level objects, no object exportable, or a *partial* success where some objects exported and others didn't). Don't just treat "non-zero" as one failure mode -- see the exit codes table in `Plugins/AssetDump/README.md` for the exact meaning of each code, especially code `5` (partial success), which still has real, usable output worth reading.
- All output goes through the `LogAssetDump` category, one line per log call — safe to grep/parse line-by-line.
- Each top-level object in the package is wrapped: `---- BEGIN <PackagePath>.<ObjectName> ----` / `---- END ... ----`. A single package can contain several top-level objects (e.g. a Blueprint's asset, generated class, and CDO), so expect possibly multiple blocks in one run.
- A `---- N GUID(s) aliased to short ids for readability (scoped to this dump only) ----` notice precedes the first block when applicable.
- **Important:** aliases (`G12`, `Move_to_Location`, `Move_to_Location_2`, ...) are short IDs scoped to *that single invocation only* — they are not real engine identifiers and are **not stable across separate commandlet runs**. Never try to correlate an alias between two separate invocations (e.g. comparing before/after a change).
- For the full itemized list of what's stripped from the raw export and why, and known limitations — read `Plugins/AssetDump/README.md` directly rather than relying on this skill to restate it.

## Navigating large output without blowing the context budget

This is the most important operational lesson: a 352,000-character raw dump was fully and correctly answered using roughly 4,100 tokens by never reading the whole capture into context — only grepping for structure first, then reading narrow, targeted ranges.

Workflow:
1. Capture full stdout to a file (not just what's visible in the terminal).
2. Get the lay of the land: grep `---- BEGIN` to see what top-level objects are present.
3. For call/event logic, grep `FunctionReference=` / `EventReference=` / `VariableReference=` to get an outline of what's called and referenced before reading any node bodies in full.
4. For component/subobject structure, grep `Begin Object Class=` (or an object's `ExportPath=` suffix, since the class name is encoded there too) to get the object tree.
5. For wiring, grep `LinkedTo=`.
6. Only after grep has narrowed down the region of interest, read specific line ranges using the grep results' line numbers. Never read or `cat` the entire captured file as a first move — treat it the same way you'd treat a large log file.

## Known caveat: macro instance duplication

Unreal auto-expands Blueprint macro instances (`K2Node_MacroInstance`) into a full duplicate copy of the macro's graph per call site, and the commandlet does not currently deduplicate this. Recognize the pattern — several near-identical `EdGraph` blocks under similar names, all containing the same macro body — and read only one copy in depth to understand the macro's behavior; skim the rest only for call-site-specific differences (e.g. differing literal input values) rather than reading every copy in full.

## Source of truth

This skill intentionally does not restate the plugin's full list of stripped fields, the exact output-format grammar, or its known limitations — `Plugins/AssetDump/README.md` is the single source of truth for that. Read it directly if more detail is needed, rather than trusting a paraphrase here that could drift out of sync.
