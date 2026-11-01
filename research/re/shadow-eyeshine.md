# Eyeshine and soft stencil shadows — EU 1.1 x64

Latest result: [Eyeshine variants and stencil conversion](eyeshine-variants.md).
The sampler probe did not fix the visible flicker and now defaults off.

2026-10-29. Research-only game access: PE files and shader files were read as
data, never loaded or executed. No game launch or game-directory write occurred.

Follow-up with the supplied OFF/ON trace: see
[eyeshine-sampler-fix.md](eyeshine-sampler-fix.md). The projected mapping is
derived and simulated; a default-on `EyeshineFix` corrects the packed-depth
min/mag mismatch. The sampler defect is demonstrated offline; its responsibility
for the user's visible flicker is **not yet confirmed**. The result below records
the earlier investigation, before that evidence and implementation.

**Result:** the soft-shadow/depth-copy path and its shader consumer are located.
Static evidence does **not** identify the Eyeshine flicker root cause. No
`EyeshineFix` switch or speculative shader patch is introduced. The existing
`EyeshineFlickerWorkaround` stays available and **off**; it removes main-light
character shadows, so it is unsuitable as the default fix.

## Evidence and confidence

Addresses are RVAs, not runtime VAs. The examined files match the existing EU
1.1 manifest in `research/fingerprints/efbb-eu-dvd-1.1.sha256`:

| File | SHA-256 |
|---|---|
| RndrGL.dll | `400dc6b50f65c9b884e85daeac7b1b1aaa2f19d2cbf4dbd9e597abae284ea348` |
| MXR.dll | `c2d898b4a69dcd362f25f88ba2426ac5fb7efb879bf0f2721d3c8d5b15aec71f` |
| SbzEngine.exe | `5056ffe030c2ba8687b032f5f2ed95b42c4814db87dec61444a6cf264deaca38` |
| GameWorld_Win64_AMD64.dll | `749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7` |
| GameClasses_Win64_AMD64.dll | `263c03a430f070ebf5f32c59b598b9e8e98a9d06d07172f76f6786bc0dfc7773` |

### Mode selection and mask consumption: high confidence

`Benchmark/Instructions.txt` explicitly describes mode 5 as PS 2.0 and mode 6
as PS 2.0 with **soft stencil shadows**. This is a screen-space stencil-mask
filter, not evidence of a light-space shadow-map renderer.

MXR `+1B2F60` dispatches on the low seven bits of shader state `this+74h`.
Case 6 branches at `+1B2FDE` to `+1B0AA0` when the material permits the soft
path; other material cases fall back to `+1ABC40`. Case 5 uses `+1B02A0`
unless its additional flag selects that fallback.

`+1B0AA0` builds a projected screen-to-mask coordinate transform. It obtains
the image associated with engine texture handle `renderer+E6h` at `+1B0CAD`,
reads viewport dimensions at `+1B0CF4`, divides by image dimensions at
`+1B0D1E..+1B0D43`, and combines viewport/image ratios and view-area offsets
into the transform. This makes viewport, copy bounds and texture dimensions
joint invariants, not interchangeable numbers.

The shader-name/ID table begins at MXR `+278190`. Entries at `+2781D0` and
`+2781F0` name `XRShader_FP20SS_SpecNormal` and its projected variant. Table
lookup occurs at `+1B12F1`; initialization registers names at `+1B1570`.
RTTI identifies `CXR_VirtualAttributes_ShaderFP20SS`: vtable `+20E598`,
attribute application at `+1AC420`, created by `+1B1322`.

The shipped `Gl/NV_Fragment_Program2/XRShader_FP20SS_SpecNormal.fp` is 9467
bytes. The actual source establishes:

| Input | Meaning and use |
|---|---|
| texture[5].a | stencil/shadow mask, sampled at center and 16 offsets |
| texture[6].rg | packed depth, reconstructed as `256*r + g` |
| fragment.texcoord[5] | projected mask coordinates, divided by `.w` |
| program.env[4] | `PixelUV`, scales the offset kernel |
| full precision depth/depthref/dstexel | depth rejection for neighboring mask samples |

The kernel compares neighboring packed depths with a reference, weights with
`0.02000 * ShadowMaskTexCoord.w`, combines mask samples, and finally multiplies
lighting by `ShadowMaskTexel.a`. Commenting out that final multiply removes
the whole mask contribution; it cannot distinguish flicker from valid shadows.

### Generation and copy path: high confidence for calls, medium for handle semantics

RndrGL RTTI identifies `CRenderContextGL` vtable `+90AD8`:

| Slot | Function | Operation |
|---|---|---|
| +A8h | +34E10 | framebuffer pixel copy; selector 1 uses NV packed depth/stencil copy |
| +B0h | +34FA0 | framebuffer rectangle into a named 2D texture |

At `+34E99..+34F04`, selector 1 checks capability bits, sets pixel zoom/color
and raster position, then calls `glCopyPixels(0,0,width,height,0x886E)`.
`0x886E` is `GL_DEPTH_STENCIL_TO_RGBA_NV`. It subsequently selects `GL_BACK`
at `+34F19`. Selector 0 calls `glCopyPixels(..., GL_COLOR)` at `+34F68`.

`+34FA0` resolves an engine texture ID and its stored image rectangle, converts
Y coordinates using render height, binds `GL_TEXTURE_2D` at `+35116`, copies
at `+35145`, then binds texture 0 at `+35152`. A null/stale binding here is
a hypothesis to measure, not a demonstrated bug: the attribute layer may
correctly rebind it before the lighting draw.

MXR queues these through callbacks `+83870` (renderer slot A8h) and `+838B0`
(slot B0h). Inside `+8FE50..+9137A`:

* `+90B43` queues a bounded texture copy using engine handle `this+E6h`.
* `+90E65..+90E70` gates the following extra operations on shader mode **6**.
* `+90EF6` stores selector **1**, and `+90F0A` queues `+83870`: the NV packed
  depth/stencil-to-color operation. Its sort value is written as -19.0.
* `+90FBA` takes handle `this+E4h`, and `+90FD2` queues `+838B0` to copy the
  resulting framebuffer into that texture. Its sort value is about -18.9.

This is strong evidence for the packed depth texture producer. Engine handles
E4/E6 must still be correlated with runtime GL texture names at units 5/6;
the names and shader comments alone are not sufficient to assert their mapping.
The complete stencil-volume-to-alpha draw setup has not been fully named.

The other texture-upload function `+39F20..+3D018` temporarily changes viewport
at `+3BB12`; `+3BBEB` copies the clamped intersection when allocation matches,
and `+3BC2C/+3BC62` allocate/copy when it differs. `glPopAttrib` restores state
at `+3BC68`, which is why draw snapshots query **effective** viewport rather
than trusting only intercepted `glViewport` calls.

No pbuffer/WGL render-to-texture or framebuffer-object API name/import was
found in the examined RndrGL/MXR inventories. The located implementation uses
the drawable framebuffer and copy operations. This does not rule out code
outside the scanned modules or computed API names.

### Eyeshine: high confidence for command/state toggle, medium for post-effect family

SbzEngine `+AB290` contains **`cmd togglenightvision`**, referenced at `+B934`
and `+B9AF` for the `EYESHINE` / `EYESHINE2` bindings. The bare token
`togglenightvision` is an input action, not a proven standalone console command.
GameWorld registers that action with numeric ID **0x2F** at `+E750..+E789`.

GameClasses `+1CD061..+1CD13B` checks gameplay state, toggles a byte at player
state `+1314h`, marks a replication/update flag `0x8000`, and selects
`gui_night_on` / `gui_night_off`. It can reject the action; blindly pressing a
key does not prove Eyeshine became active. There is also a `NONIGHTVISION`
property in the binary. No safe, verified console command to grant the power
or to force an absolute on/off state was established.

GameWorld `+5B10..+AD48` contains the screen post-processing renderer and
references `WClientMod_ScreenFX0`, `1`, `1b`, `2`, `3`, `4`.
The FP-capable path at `+7811` tests a renderer capability, then selects
ScreenFX0 at `+7848`, `+7A23`, `+7BF0`; other branches use the other programs.
The ARB files describe themselves as EFBB post-processing, sample texture[0]
and texture[1], and combine color/luminance with environment constants.
The exact Eyeshine state-to-ScreenFX branch and its full copy lifecycle remain
unresolved. Do not claim that Eyeshine changes a viewport or texture until the
off/on traces show it.

## Hypotheses to discriminate

| Hypothesis | Current evidence | Required runtime observation |
|---|---|---|
| Stale or uninitialized mask region after viewport/copy change | Plausible: projected mask transform and separately bounded copies | u5/u6 allocation, every sampled copy rectangle, actual draw viewport/scissor/env4; correlate which texture was written before use |
| Packed depth copy failure or precision mismatch | NV packed copy exists; shader reconstructs two channels | NV copy present, raster valid, dimensions correct, depth scale/bias/range stable; compare native NV and rewritten program runs |
| Wrong texture unit or binding survives post-effect | Explicit bind-to-zero after copy; post-effects use other sampler slots | compare all u0..u7 2D/cube bindings at actual SpecNormal draw, before/after Eyeshine |
| Removing H precision causes instability | Weaker: depth math is already TEMP/full precision; native NVIDIA reports predate rewrite | reproduce with `Enable20PlusPlusAnyGpu=0` on NVIDIA and compare with rewritten run |

BigFix currently **advertises** `GL_NV_copy_depth_to_color` when any-GPU mode
is enabled, even if absent from the real driver. It does not emulate the NV
`glCopyPixels` type. That could produce a separate failure on unsupported AMD
drivers; it cannot alone explain the reported native NVIDIA flicker. Do not
replace packed depth with a different format without matching the shader's
`256*r+g` decoder. This pass deliberately does not consume the game's GL error
queue to probe failures.

## Instrumentation

Set `[BigFix] TraceShadowPass=1`, restart, then press **F11** in the test scene.
Four consecutive subsequent successful-swap frames are sampled in detail,
with compact records only for queued captures or variant changes. Idle and PNG
writer-drain frames do not consume the compact reserve. Holding F11 does not
repeatedly arm. Total `TraceShadowPass` output is capped at **2000 lines per
process**, with 1500 detailed lines and 500 reserved compact lines. Restart for
another budget; F11 never resets it. See the current
[record format and capture-index mapping](eyeshine-variants.md#compact-records-and-capture-correlation).

Each frame has reserved budgets: 56 general events, 24 copy/viewport events,
8 recognized program binds, 8 shadow draw snapshots and 4 ScreenFX draw
snapshots. A summary reports omitted events/draws. Material bind floods cannot
consume the copy or draw budgets. Upload records identify source length and
class: 1=shadow, 2=SpecNormal family, 3=ScreenFX. Class 2 is identified by source
tokens, not just byte length; a projected variant may share that class.

Logged data includes frame/context/DC, program IDs/binds, viewport and scissor,
clear color/write mask/stencil state, copy rectangles/type/active unit/texture,
read/draw buffers, raster validity/position, depth scale/bias, depth bits/range,
`program.env[4]`, and units 0–7 2D bindings plus dimensions/internal format/filter/
wrap and cube bindings. Source uploads remain unmodified and `DumpShaders=1`
writes original CRC32-named programs under the plugin's `shader_dump` folder.

Queries happen before draws, including **before** `glBegin`. Only server active
texture is temporarily changed and restored; no readback or GL error draining
is used. Shared export hooks are owned once by the existing video hook layer,
so `TraceVideo=1` can coexist. Program metadata is keyed by upload context and
ID; programs uploaded only in a different shared context are not classified.
This is a diagnostic limitation to check if a run has copies but no class-2
draws. Snapshots are bounded, so they cannot prove all pixels were initialized
or reconstruct events omitted by a full per-frame budget.

## Commands established by static evidence

| Purpose | Syntax | Evidence |
|---|---|---|
| Startup command | `SbzEngine.exe -env "path.cfg" -exec "command"` | EXE `+1129B`, `-EXEC` parser; shipped benchmark batch files |
| Load saved game | `loadgame "Checkpoint"` or a save basename | EXE registration `+475ED..+47606`, handler `+44730`; GameWorld menu format `+1E97F8` |
| Replay demo | `play "sodini01"` | EXE registration `+473FE`; Benchmark/Instructions.txt |
| Benchmark replay | `benchmark "Sodini01"` | EXE `+4746E`; Benchmark/DemoLaunchers/Sodini01.bf |
| Map load | `map "name"`, also startup `-map "name"` | EXE `+47342`, `+1114E`; exact playable map name not established here |
| Shader mode | `XR_SHADERMODE=5` / `6` in `-env` config | shipped Benchmark/Configs/ShaderMode5/6; runtime console `xr_shadermode<i_expr>` in MXR `+1AD110` |
| Eyeshine toggle | **`cmd togglenightvision`** | EXE built-in binding and GameWorld action dispatch |

`Content/Demos` contains panoptical01/02/03 and sodini01/02 XDM files. Demo
playback is repeatable, but this research does not prove a shipped demo shows
lit Eyeshine or accepts a live toggle while replaying recorded input. The first
available save here is `Content/Save/Bran/Checkpoint` (also `01.Escape`); neither
filename proves the power is unlocked. No game save was modified to obtain it.

## Lead run: exact commands and required artifacts

Build first; install the x64 ASI loader by the existing project process and move
TCoRFix aside. The script can temporarily replace an installed BigFix.asi and
restores it, the INI and Environment.cfg afterwards. It does not install a
loader or silently disable another mod.

```powershell
# Safe dry run: prints plans only, no game/config writes or native input.
pwsh -NoProfile -File tools/shadow_scene.ps1 -PlanOnly -GameDir D:\Games\Riddick

# Main comparison: both modes, same lit save. Default save candidate is Checkpoint.
pwsh -NoProfile -File tools/shadow_scene.ps1 -GameDir D:\Games\Riddick -SaveName Checkpoint -PluginPath build\Release\BigFix.asi -OutputDir local\shadow-rewrite

# NVIDIA only: mode 6, original NV precision/program options and native queries.
pwsh -NoProfile -File tools/shadow_scene.ps1 -GameDir D:\Games\Riddick -Modes 6 -SaveName Checkpoint -NativeNv -PluginPath build\Release\BigFix.asi -OutputDir local\shadow-native

# Optional shadow-loss control, only after the normal comparison.
pwsh -NoProfile -File tools/shadow_scene.ps1 -GameDir D:\Games\Riddick -Modes 6 -SaveName Checkpoint -Workaround -PluginPath build\Release\BigFix.asi -OutputDir local\shadow-workaround

# Replay candidate; verify the scene and toggle work before accepting captures.
pwsh -NoProfile -File tools/shadow_scene.ps1 -GameDir D:\Games\Riddick -SceneCommand 'play "sodini01"' -OutputDir local\shadow-demo
```

The script launches the x64 engine from its system directory, passes a config
containing the requested shader mode and a startup scene command, waits 30 s,
then prompts the lead to verify mode, lit scene and **Eyeshine OFF**. Navigate
to an unlocked save/scene if the candidate does not qualify. It arms F11 and
takes three F12 PNGs, opens the console with Ctrl+Alt+OEM3, sends the verified
toggle command using scan-code input, closes it, prompts to verify **ON**, then
repeats captures. Input aborts if the game is not foreground. The console chord
assumes the usual US OEM3 key; other layouts may need manual toggling. Only
use `-Unattended` after validating the save starts with Eyeshine off, the mode
applies, and this console sequence works. The script has not been run in game.

Return each mode directory: `BigFix.log`, `BigFix.ini`, `scene.cfg`,
`launch.json`, `shader_dump/*.fp`, `off-0..2.png`, `on-0..2.png`. Verify:

1. Mode 5 control and mode 6 workaround-off show valid main-light character
   shadows before Eyeshine. Do not rely on the PNG filename's `_sm` tag:
   existing capture metadata reads Environment.cfg, while this script uses
   `-env`; confirm mode in video settings and correlate class-2 uploads/draws.
2. Mode 6 ON visibly flickers in the chosen lit area, without moving the view
   between off/on. If the first scene does not flicker, use Showers/a confirmed
   reproduction save. Keep a character and its cast shadow in view.
3. Trace has at least one `class=2 bytes=9467` upload, class-2 draws, both u5/u6 entries, NV copy events
   with `format/type=886E`, and texture-copy events. Compare four sampled frames
   in each state. Missing lines or nonzero omission counts limit conclusions.
4. Match copy destination GL IDs to the u5/u6 IDs, compare allocation versus
   copied region and `env4`, check whether wrong/null/reused IDs appear only ON,
   and check NV raster validity/depth state. If these all agree while flicker
   persists, a follow-up texture-content capture or focused internal pass hook
   is needed; this trace alone cannot establish pixel contents or GPU errors.
5. Native NV versus rewrite: if both flicker identically, deprioritize H-math
   conversion. The workaround control should suppress flicker **and** remove
   main-light shadows, confirming that the mask contribution is the affected
   term without proving which producer failed.

## Reproduce static research and tests

```powershell
python -m venv .venv
.venv\Scripts\python -I -m pip install capstone==5.0.9 pefile==2024.8.26 pytest
.venv\Scripts\python -I tools/re/shadow_static.py D:\Games\Riddick\System\Win64_AMD64\RndrGL.dll
.venv\Scripts\python -I tools/re/shadow_static.py D:\Games\Riddick\System\Win64_AMD64\MXR.dll --at 0x1b2f60 --at 0x90f11
.venv\Scripts\python -I tools/re/shadow_static.py D:\Games\Riddick\System\Win64_AMD64\RndrGL.dll --at 0x34f04 --at 0x35145
.venv\Scripts\python -I tools/re/shadow_static.py D:\Games\Riddick\System\Win64_AMD64\SbzEngine.exe --pattern 'night|loadgame|benchmark|^play|^map|exec'
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
$env:RIDDICK_DIR='D:\Games\Riddick'
ctest --test-dir build -C Release --output-on-failure
.venv\Scripts\python -I -m pytest tools/tests -q
```

The long worktree path initially prevented a doctest example from checking
out. Enabling `core.longpaths=true` **only in build/_deps/doctest-src** and
restoring that fetched checkout resolved configure; no project/global git
setting or game file changed. Scratch disassembly stays in ignored `local/`.

Validation: Release configure/build succeeded; CTest passed **54/54** both
without and with `RIDDICK_DIR`. `tools/tests` passed **56**, with **2 skipped**.
New coverage checks diagnostic opt-out, four-frame arming/held-key behavior,
the process-wide 2000-line cap, restored active texture, copy/bind budget
reservation, forwarding through real MinHook detours, and coexistence with
TraceVideo. PowerShell 7 and 5.1 tests parse and dry-run the scene script and
compile its C# input helper without sending input or launching the game.
