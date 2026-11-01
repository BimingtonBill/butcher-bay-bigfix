# Static display/window RE

These scripts parse AMD64 PE files as data. They never load or execute a game
module and never write to the game directory. Run from the repository root:

```powershell
python -m venv local/venv
& ./local/venv/Scripts/python.exe -I -m pip install capstone==5.0.9 pefile==2024.8.26
& ./local/venv/Scripts/python.exe -I tools/re/display_static.py --root D:/Games/Riddick --check-iat > local/display-inventory.txt
```

`local/` is ignored. Keep full disassembly and other game-derived scratch output
there. Commit only short evidence excerpts in research notes.

`--root` inspects the seven Task 6 modules. `--module PATH` inspects one module.
All printed addresses are RVAs, except the explicitly labelled preferred image
base. The SHA-256 is calculated from the unmodified on-disk file. Other options:

```powershell
# Enumerate imports, references, and strings (ASCII and UTF-16LE).
& ./local/venv/Scripts/python.exe -I tools/re/display_static.py --root D:/Games/Riddick --string VID_MODE --string ChangeDisplaySettings
# Resolve an inherited method through its exported name.
& ./local/venv/Scripts/python.exe -I tools/re/display_static.py --module D:/Games/Riddick/System/Win64_AMD64/MSystem.dll --export ModeList_Find --at 0x146b30
# Show the rest of the selector: unwind entries can cover only a fragment.
& ./local/venv/Scripts/python.exe -I tools/re/display_static.py --module D:/Games/Riddick/System/Win64_AMD64/MSystem.dll --range 0x146b90 0x146cfe
# Find direct code references and absolute data pointers (including vtables).
& ./local/venv/Scripts/python.exe -I tools/re/display_static.py --module D:/Games/Riddick/System/Win64_AMD64/RndrGL.dll --xref 0x46e40 --xref 0x4db20
```

`--at` and `--xref` are repeatable. `--at` uses PE exception/unwind ranges, which
are **not necessarily whole functions**; leaf functions can have no entry, and
other functions have several entries. Use `--range START END` with instruction
boundaries established by control-flow tracing to cover those cases.

The inventory linearly decodes executable sections and reports RIP-relative
references to selected imports, plus direct immediate references to requested
RVAs. `--check-iat` independently scans executable bytes for RIP-relative
`FF 15`/`FF 25` call/jump sites to those imports, decodes each candidate, and
requires agreement with the linear scan. This catches missed direct IAT sites;
it does not prove runtime reachability, resolve arbitrary computed pointers, or
prove the absence of dynamically constructed API names. No disassembly is
treated as executed behavior without tracing its callers/conditions.

For v0.2 input/timing/audio/AA, `v02_static.py` reuses the same decoder and
supports `--inventory --strings --verify`, `--text`, `--operand`, `--export`,
`--rtti` (optionally `--table-size`), `--data`, `--range` and `--xref`.
`--verify-note` rechecks the EU 1.1 manifest, the note's instruction excerpts,
and selected data/vtable evidence without loading any game code:

```powershell
& ./local/venv/Scripts/python.exe -I tools/re/v02_static.py --verify-note
```

See [the findings](../../research/re/v0.2-input-timing-audio-aa.md) for confidence,
proposed hooks and the exact bounded trace to request from the lead.

For v0.4 HUD/subtitles/FOV, `v04_static.py` verifies the six relevant module
fingerprints, the note's disassembly excerpts and selected data/vtable claims.
It can also dump an RVA range and print the projection formula's predicted
values (the latter is arithmetic, not execution of a game module):

```powershell
& ./local/venv/Scripts/python.exe -I tools/re/v04_static.py --verify-note --projection-examples
& ./local/venv/Scripts/python.exe -I tools/re/v04_static.py --dump world 0x136ef0 0x13701b > local/v04-camera.txt
```

Module aliases are `exe`, `system`, `gl`, `mxr`, `classes`, `world`.
See [the v0.4 findings](../../research/re/v0.4-hud-fov.md) for confidence,
caller-scoped HUD candidates, the unresolved first-person projection boundary,
and the proposed 400-line runtime trace. Neither script launches the game.

`shadow_static.py MODULE` uses the same read-only decoder with full-string
matching for shadows, Eyeshine, copy APIs and scene commands. It accepts
`--pattern REGEX`, repeatable `--xref RVA` / `--at RVA`, and `--range START END`.
Run with `.venv/Scripts/python -I`; see
[shadow-eyeshine.md](../../research/re/shadow-eyeshine.md) for evidence and runs.

## Eyeshine state inventory

`eyeshine_state_static.py` reads PE files as data only. For the current mode-6
flicker investigation, see [evidence, probes and one dock run](../../research/re/eyeshine-variants.md).
Examples with the project venv Python (`-I`):

```powershell
& $py -I tools/re/eyeshine_state_static.py D:/Games/Riddick/Content/GameClasses_Win64_AMD64.dll --field 0x1314
& $py -I tools/re/shadow_static.py D:/Games/Riddick/Content/GameWorld_Win64_AMD64.dll --range 0x5870 0x58ec
& $py -I tools/re/eyeshine_state_static.py D:/Games/Riddick/System/Win64_AMD64/MXR.dll --data 0x2002c0 --data 0x2002d0
```

`bloom_static.py MODULE` supports either AMD64 or x86 SSE2 PE data, with
`--string TEXT`, `--xref RVA` and `--range START END`. It never loads the module.
It prints the file hash and RVAs; x86 operands retain their preferred VAs and
references are annotated as RVAs. See [x64 bloom](../../research/re/x64-bloom.md)
for matching MXR/GameWorld paths, short excerpts, shared shader CRCs and the
lead's runtime capture matrix. This tool does not execute the protected x86 game.
