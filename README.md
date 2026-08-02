# AssetDump

Editor-only Unreal Engine plugin that adds a commandlet for exporting a single `.uasset` package to human-readable text on the console. It exists so a person or a tool can inspect what a Blueprint, State Tree, Data Asset, etc. actually contains without opening the editor UI or inferring structure from the asset's binary contents.

## What it does

- Loads one asset package and prints a text representation of every top-level object it contains to the log/console.
- Uses the same underlying text-export pipeline (`UExporter` + `FExportObjectInnerContext`, t3d format) that Unreal's own built-in asset-diffing tooling (`UDiffAssetsCommandlet`) uses — but writes to the console instead of a file, and applies the additional size reduction described below.
- Applies no interpretation or summarization: the output is a faithful, structural text form of the asset's actual property data (object hierarchy, property values, Blueprint graph nodes and wiring, etc.), just with duplicate/default/cosmetic noise removed.

## Requirements

- Unreal Engine 5.7.
- Editor-only (`Type: Editor` in `AssetDump.uplugin`) — compiled into editor targets only, never a packaged game build.
- No dependency on any other plugin or project-specific module; only `Core`, `CoreUObject`, `Engine`, `UnrealEd`.

## Installing

1. Add this plugin's folder under the host project's `Plugins/` directory (this repo is intended to be added as a git submodule there).
2. Enable it in the project's `.uproject`:
   ```json
   { "Name": "AssetDump", "Enabled": true }
   ```
3. Build the editor target. No further configuration is required.

## Usage

Run via Unreal's commandlet system from a terminal:

```
<Engine>/Binaries/<Platform>/UnrealEditor <Project>.uproject -run=AssetDump -Asset=<PackagePath> -unattended -nopause -nosplash -nullrhi -stdout -FullStdOutLogOutput
```

Example (macOS):
```
"/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor" "/path/to/Project.uproject" -run=AssetDump -Asset=/Game/Blueprints/BP_MyActor -unattended -nopause -nosplash -nullrhi -stdout -FullStdOutLogOutput
```

- `-Asset=` (required): the long package path of the asset to dump, e.g. `/Game/Blueprints/BP_MyActor`. A bare package path is sufficient — the commandlet resolves and dumps every top-level object the package contains, without needing a fully-qualified object name.
- The remaining flags aren't required by the commandlet itself but are recommended for scripted use: `-unattended -nopause -nosplash` suppress interactive dialogs, `-nullrhi` skips renderer initialization (not needed for a data-only dump), and `-stdout -FullStdOutLogOutput` force complete, immediate log output to the terminal.
- Exit code: see [Exit codes](#exit-codes) below — each failure mode has its own distinct value.

## Exit codes

Every failure mode returns a distinct process exit code, so a caller can tell *why* a run failed without parsing log output — in particular, a run where some objects failed to export (`5`) is never indistinguishable from a fully successful run (`0`), unlike a plain success/failure boolean.

| Code | Meaning |
|---|---|
| `0` | Success — every top-level object in the package was exported. |
| `1` | `-Asset=` was missing or empty. |
| `2` | The package failed to load (`LoadPackage` returned null) — bad path, or the asset doesn't exist. |
| `3` | The package loaded but contained no top-level objects (e.g. everything present was filtered as a `SKEL_*` class). |
| `4` | The package had top-level objects, but none of them could be exported (no registered `UExporter` for any of their classes, or every exporter produced empty output). |
| `5` | Partial success — at least one object exported, but at least one other object in the same package failed to export (check the log for `Warning`-level `No exporter found` / `Exporter produced no text` lines to see which). |

## Output format

All output is written through a single log category, `LogAssetDump`, at `Display` verbosity — one line per `UE_LOG` call (never one call with embedded newlines), so the output is straightforward to capture and search line-by-line from a process's stdout.

Each top-level object in the package is wrapped in a delimited block:
```
---- BEGIN <PackagePath>.<ObjectName> ----
... object's text export ...
---- END <PackagePath>.<ObjectName> ----
```

If any GUIDs in the dump were aliased (see below), a one-line notice stating how many precedes the first block.

## What's stripped, and why

Unreal's raw text exporter is extremely verbose. A large share of its output is either literally redundant (the same information stated twice) or default/empty boilerplate that carries no information about what the asset actually is or does. This commandlet removes exactly that category — nothing is stripped unless doing so cannot change what a reader can determine about the asset's structure, connections, or behavior.

1. **`SKEL_*` generated classes are never dumped.** Every Blueprint compiles a `SKEL_<Name>_C` class as a compiler-internal stand-in used only to resolve circular references during compilation. It restates the same functions/properties as the real generated class (`<Name>_C`) with nothing new, so it's excluded from the set of objects exported.
2. **The exporter's duplicate "declare" pass is skipped.** Unreal's text exporter normally runs two passes over each nested subobject: one that declares only its class and name, and a second that fills in its actual property values. This commandlet requests only the second pass (`PPF_SeparateDefine` port flag). No type information is lost — each subobject's class remains fully recoverable from its `ExportPath=` attribute, which this flag does not affect.
3. **StateTree `IDToStateMappings` / `IDToNodeMappings` / `IDToTransitionMappings` lines are dropped.** These are GUID → array-index lookup tables used only by the compiled State Tree at runtime. The same GUIDs already appear as `ID=` on the corresponding state/task objects earlier in the same dump.
4. **`EdGraph` `Nodes(N)="..."` index lines are dropped.** Every node an `EdGraph`'s `Nodes()` array names is already fully declared, with all its properties, via its own `Begin Object` block earlier in the same graph — the array is pure ordering bookkeeping. This is scoped specifically to objects that carry a `Schema=...EdGraphSchema...` property (the marker only a real `UEdGraph`, or subclass — material graphs, anim graphs, sound cue graphs, etc. — ever exports), not to any line merely shaped like `Nodes(N)=`. A same-shaped but unrelated `Nodes` array on another class (e.g. `UMovieSceneNodeGroup::Nodes`, real non-redundant node-tree-path data on Level Sequence assets) is never dropped.
5. **Known-default/empty Blueprint pin sub-fields are stripped.** Graph pins (`CustomProperties Pin (...)`) are exported through a code path (`UEdGraphPin::ExportTextItem`) that, unlike normal property export, never compares against a default — every pin restates the same set of fields whether or not they hold a non-default value. A field below is removed only when it is present with exactly the listed value (verified against the pin type's real constructor defaults); any pin where a value differs from its default (e.g. `bHidden=True`, an actual `LinkedTo=`) is left completely untouched:
   - `PinType.ContainerType=None`
   - `PinType.bIsReference=False`
   - `PinType.bIsConst=False`
   - `PinType.bIsWeakPointer=False`
   - `PinType.bIsUObjectWrapper=False`
   - `PinType.bSerializeAsSinglePrecisionFloat=False`
   - `PinType.PinSubCategory=""`
   - `PinType.PinSubCategoryObject=None`
   - `PinType.PinValueType=()`
   - `PinType.PinSubCategoryMemberReference=()`
   - `bHidden=False`
   - `bNotConnectable=False`
   - `bDefaultValueIsReadOnly=False`
   - `bDefaultValueIsIgnored=False`
   - `bAdvancedView=False`
   - `bOrphanedPin=False`
6. **`PersistentGuid` is dropped unconditionally.** It's bookkeeping Unreal uses to reconnect a pin's wires across a Blueprint recompile — not part of what the graph does — so it's removed regardless of value.
7. **`PinFriendlyName` is dropped unconditionally.** It's the Blueprint editor's cosmetic display label for a pin (e.g. a pin literally named `self` is shown to a designer as "Target"). It is always a reworded form of `PinName` and never adds structural or behavioral information.
8. **Remaining GUIDs are aliased to short IDs, scoped to a single commandlet run.** Every remaining `FGuid`-shaped hex value (node IDs, pin IDs, struct IDs, etc.) is replaced with a short alias: the nearby human-readable `Name="..."` value when one can be paired with it (disambiguated on collision — two different objects with the same display name get suffixed `_2`, `_3`, ...), otherwise a sequential `G<N>`. The same real GUID always maps to the same alias everywhere it's referenced within that run, so cross-references (Blueprint pin wiring, State Tree property bindings, etc.) stay fully traceable. Aliases are **not** stable across separate invocations of the commandlet.

## Known limitations

- Unreal auto-expands Blueprint macro instances (`K2Node_MacroInstance`) into a full duplicate copy of the macro's graph per call site. This commandlet does not currently deduplicate those expansions, so a Blueprint that calls the same macro several times will contain several near-identical copies of it in the dump.
- There is no filtering by object or graph name — the commandlet always dumps every top-level object in the package. Extracting a specific section (e.g. only one function graph) is currently left to the caller, e.g. by scanning between a graph's own `Begin Object`/`End Object` markers.

## Claude Code skill

A ready-made Claude Code skill that teaches an agent how to invoke this commandlet correctly (finding the editor binary, output format, GUID-alias scoping, navigating large dumps efficiently) ships alongside the plugin at `Resources/ClaudeSkill/asset-dump/`. Claude Code only auto-discovers skills from a `.claude/skills/` directory at a project's root (or the user's home directory) — not from plugin folders — so each consuming project needs a one-time step, the same way the plugin itself needs a one-time step in the project's `.uproject` (see Installing above).

**macOS / Linux** (from the consuming project's root):
```bash
mkdir -p .claude/skills
ln -s "$(pwd)/Plugins/AssetDump/Resources/ClaudeSkill/asset-dump" .claude/skills/asset-dump
```
A symlink stays current automatically as the `AssetDump` submodule is updated.

**Windows** (symlinks need Developer Mode or an elevated shell, so a copy is simpler):
```powershell
mkdir .claude\skills
Copy-Item -Recurse Plugins\AssetDump\Resources\ClaudeSkill\asset-dump .claude\skills\asset-dump
```
A copy won't pick up future updates automatically — re-run this after pulling a newer `AssetDump` submodule commit.
