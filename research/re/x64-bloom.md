# x64 heavy bloom — static comparison and runnable diagnostics

2026-10-29, original 2004 EU 1.1. **No defect is proven; no BloomFix or binary
patch is implemented.** The rendering path is located with high confidence.
The numeric differences below are candidates, not a justification for changing
the picture. Only instrumentation and a scene launcher were added.

No game executable was launched, no game DLL was loaded, and no game file was
written during this investigation. All x86 inspection was static. Synthetic DLLs
and existing hidden-window GL tests are the runtime tests used here.

## Scope and identities

Paths are relative to `D:\Games\Riddick`. All addresses below are RVAs.
The DLLs have preferred image base `0x10000000`; x86 disassembly operands retain
preferred VAs, so subtract that base when comparing with the listed RVAs.

| Module | AMD64 SHA-256 | x86 SSE2 SHA-256 |
|---|---|---|
| `System/<arch>/RndrGL.dll` | `400dc6b50f65c9b884e85daeac7b1b1aaa2f19d2cbf4dbd9e597abae284ea348` | `f38010a8aa470e5f3e00a671e7240b55b15f08ed24da9514e71db58f19d855a8` |
| `System/<arch>/MXR.dll` | `c2d898b4a69dcd362f25f88ba2426ac5fb7efb879bf0f2721d3c8d5b15aec71f` | `5a19fa7db255ab5088ee94d75c9a45258b9d78a583dec80de8c5b4632816f4db` |
| `Content/GameWorld_<arch>.dll` | `749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7` | `8fca7a49395cb2c5009af8a11b2033cfd7135f848d0e0de94bbffc59140dc91f` |
| `Content/GameClasses_<arch>.dll` | `263c03a430f070ebf5f32c59b598b9e8e98a9d06d07172f76f6786bc0dfc7773` | `3c85eb46840d83f5939c3a3fd49b6040ca44598185069005741e52f0034b985b` |
| `System/<arch>/MSystem.dll` | `b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500` | `80a1a95c69e266f2d4d7381286262602328891359449a83b6b2f649252698981` |

The community evidence is [TCoRFix issue #1](https://github.com/Nezarn/TCoRFix/issues/1):
danonina reports the washed-out ship and later says x86 avoids it; apoketo reports
an Xbox-like image; gittybittycomboe independently confirms the SSE2 executable
avoids the ship-cutscene bloom on AMD. These observations establish a useful
architecture comparison, not a particular shader mode or root cause.

## Located post-process and comparisons

**GameWorld, high confidence:** the AMD64 dispatcher has unwind range
`0x5B10..0xAD48`. It reads `CL_NODAMNGLOW` (`0x1DE280`, reference `0x5FD0`),
then selects named `WClientMod_ScreenFX0/1/1b/2/3/4` program descriptors according
to renderer feature bits. The SSE2 counterpart references the same switch at
`0x4EBF` (string `0x149CD0`). Selected matching descriptor writes:

| Program | AMD64 name reference | SSE2 name reference |
|---|---|---|
| ScreenFX0 | `0x7848`, `0x7A23`, `0x7BF0` | `0x6608`, `0x67C2`, `0x682D` |
| ScreenFX1 | `0x85DD`, `0x863E`, `0x876B` | `0x6FDD`, `0x7025`, `0x715D` |
| ScreenFX2 | `0x9477`, `0x9525`, `0x95C5` | `0x7CC1`, `0x7D72`, `0x7DD8` |
| ScreenFX3 | `0x9E95`, `0x9FC0`, `0xA0E0` | `0x87F2`, `0x8C0A`, `0x8C7C` |

`CTextureContainer_Blur` is instantiated at AMD64 `0x62AC` (name `0x1DE250`),
SSE2 `0x515C` (name `0x149CA8`). This leads to **MXR's bloom extraction and
blur, high confidence**: AMD64 RTTI COL `0x217BD0`, vtable `0x1FFAC8`, slot
`+0xA0 -> 0x89AF0`; SSE2 COL `0x16F060`, vtable `0x15CB6C`, slot
`+0x50 -> 0x5AEC0`. Both select `XREngine_Blur0` for level zero and
`XREngine_Blur1` for subsequent levels. Their name references include AMD64
`0x89C89/0x8A6DA`, SSE2 `0x5AFDB/0x5B755`.

The shared `Gl/ARB_Fragment_Program/XREngine_Blur0.fp` subtracts env0 from the
screen texture, multiplies by env1 and four, applies luminance/desaturation
through env2..4, and multiplies by fragment color. `Blur1` sums four weighted
texture samples. `ScreenFX2` also sums four weighted inputs; `ScreenFX3` combines
two weighted inputs. These programs contain no explicit saturation instruction
in their brightness arithmetic. Both executables load the same architecture
independent assets; adding a saturation instruction would change both paths'
intended math without evidence.

**Normalization:** AMD64 uses division by 255; SSE2 multiplies by a single
precision reciprocal. Excerpts from matching Blur0 env0 setup:

```text
MXR AMD64
00089CAF movzx    eax, byte ptr [rsi + 0x5e]
00089CB3 cvtsi2ss xmm0, eax
00089CB7 movss    xmm2, dword ptr [rip + 0x1713fd] ; RVA 1FB0BC = 255.0
00089CBF divss    xmm0, xmm2
00089CC3 movss    dword ptr [rip + 0x1f7945], xmm0 ; env0.r buffer at 281610

MXR SSE2
0005AFCD movss    xmm0, dword ptr [0x1015ac8c]     ; RVA 15AC8C = float(1/255)
0005AFFE movzx    ecx, byte ptr [esi + 0x3a]
0005B002 cvtsi2ss xmm1, ecx
0005B006 mulss    xmm1, xmm0
0005B00A movss    dword ptr [0x10192230], xmm1
```

The remaining Blur0 constants match: 64/255, 128/255, 96/255, 160/255 at
AMD64 `0x1FFC34/30/2C/28`, SSE2 `0x15CBF0/EC/E8/E4`. GameWorld's scale
constants 256, 128, 112 also match (`0x1DE200/204/208` versus
`0x149C5C/68/64`). Ordinary normalized byte inputs differ only in final float
rounding here; this alone is weak evidence for a large brightness error.

**Float to integer conversion:** both dispatchers use `cvttss2si` in some
paths. Some rounded color conversions differ:

```text
GameWorld AMD64
000076EC movss    xmm0, dword ptr [rsp + 0xf8]
000076F5 mulss    xmm0, xmm8                      ; 255
000076FA comiss   xmm7, xmm0                     ; compare with zero
000076FD jbe      0x7706
000076FF subss    xmm0, xmm10                    ; 0.5
00007704 jmp      0x770b
00007706 addss    xmm0, xmm10
0000770B cvttss2si r14d, xmm0

GameWorld SSE2
0000634F movss    xmm1, dword ptr [ebp - 0x21c]
00006357 movss    xmm0, dword ptr [0x10149c60]     ; 255
0000635F mulss    xmm1, xmm0
00006363 movss    dword ptr [ebp - 0x180], xmm1
0000636B fld      dword ptr [ebp - 0x180]
00006371 fistp    dword ptr [ebp - 0x14c]
```

Thus x86 rounding depends on its x87 control word; x64 explicitly rounds away
from zero at half integers in this path. For ordinary finite in-range colors
the difference is small. Out-of-range/NaN source values and differing control
modes need runtime evidence. The trace includes MXCSR, including rounding,
FTZ and DAZ flags; it does not read the protected x86 process's control word.
No identified branch here establishes a denormal or x87 precision defect.

**Clamp and layout:** AMD64 `0x7747..0x7780` clamps packed channels against
255 using `cmovl`; SSE2 `0x63B2..0x63D6` uses comparisons/branches for the same
upper bound. `ScreenFX3` expands packed bytes into two vec4 constants at AMD64
`0x9EBB..0x9F63` and SSE2 `0x872E..0x8810`. The descriptor fields account for
pointer growth: AMD64 name/constants/count at +8/+24/+32; SSE2 +4/+12/+16.
MXR blur sizing (AMD64 vtable +0xB8 -> `0x844C0`, SSE2 +0x5C -> `0x561B0`)
stores each level's shifted width/height into 16-bit fields +8/+10 of 12-byte
records. The list header pointers move from +0x1C/+0x20 to +0x38/+0x40, while
the record stride stays 12. This inspected path shows no wrong doubled stride
or vtable slot. Full initialization and all gameplay sources of blur strength
have **not** been proven equivalent; no uninitialized-field or size_t bug was
identified. This is a targeted comparison, not a whole-engine equivalence proof.

**Render targets, medium confidence:** RndrGL uses framebuffer-to-texture copies.
AMD64 calls `glCopyTexImage2D` at `0x3BC2C` (IAT `0x8EB58`) and
`glCopyTexSubImage2D` at `0x3BBEB/0x3BC62` (IAT `0x8EB00`); SSE2 equivalents
are `0x303EF` (IAT `0x645B0`) and `0x30422` (IAT `0x64584`). The inspected
branches compare allocated width/height, reallocate on mismatch, then copy
bounded extents. The internal format is passed from local state, not established
as a fixed HDR/float format by these calls. Texture allocation also calls
`glTexImage2D` at AMD64 `0x3ACDE/0x3AE85/0x3AEE3`, SSE2
`0x2F71D/0x2F886/0x2F8CD`. Capture the actual internal formats before deciding
that x64 uses a different buffer range or clamps at a different stage.

**Other leads:** the only found literal `vid_fakeoverbright` lookup in MSystem
AMD64 is at `0x147A51`, within the screenshot path also formatting
`\\Screen%.2d.TGA` at `0x147ACF`. It defaults the lookup to 1. That lookup is
not evidence that toggling it fixes live bloom. GameClasses' `pickupglow`
strings (`0x2A75D0` AMD64, `0x1DD1B0` SSE2) concern a different named feature;
no post-process ownership was found there. Searches for bloom, glow,
overbright, exposure and HDR did not produce an independently named HDR pass.
The useful discovery term was **Blur**, rather than bloom.

## Scene entry points

SbzEngine AMD64 SHA-256
`5056ffe030c2ba8687b032f5f2ed95b42c4814db87dec61444a6cf264deaca38` registers
`map<str_expr>` at `0x47342` with handler `0x43000`, `play<str_expr>` at
`0x473FE` with handler `0x436D0`, `benchmark<str_expr>` at `0x4746E` with
handler `0x43EB0`, and `loadgame<str_expr>` at `0x475F4`. `changemap` and
`campaignmap` also exist; no exact ship-cutscene script invocation was established.
`Benchmark/Launch.bat` supplies `-exec "benchmark \"Panoptical01\""`;
`Benchmark/Instructions.txt` documents `play "<demo>"`. This is strong evidence
for command syntax, not proof these x64 runs reach the visual defect.

The five shipped XDM resource headers, starting at file offset `0x1E4`, identify:

| Demo | World named in header | Use |
|---|---|---|
| panoptical01 | pa2_srf2.XW | repeatable rendering baseline |
| panoptical02 | i1_showers.XW | alternative lit interior |
| panoptical03 | pa1_riot.XW | alternative gameplay effects |
| sodini01 | pa2_lair.XW | alternative interior |
| sodini02 | i1_pigsville.XW | alternative gameplay |

No header names the opening ship map. `map "Pa1_Intro"` is the best opening-scene
candidate from shipped world names; `Pa1_Arrival` is second. **Medium/low confidence
in reproducing the reported image until the lead runs them.** `loadgame
"Checkpoint"` or `loadgame "01.Escape"` is another candidate if the active profile
contains a relevant save; the existing Bran profile has those names, but their
contents and proximity to the defect were not established. No save is modified
by the research tools.

## TraceBloom and exact lead runs

`TraceBloom=1` is opt-in and observes through the existing GL context, draw and
swap detours. It adds no game-RVA hook. It records program bindings with original
and submitted CRC32, FP env0..7 at the draw, current color/generic attribute 3,
MXCSR, viewport, context/DC, draw/read buffers, available FBO binding, blend
enable/factors/equation, and texture IDs/sizes/internal formats for 2D units 0..3.
Screen-copy records include source extents and destination target. The active
server texture unit is restored, including on a contained C++ diagnostic failure.
Queries occur before glBegin and after glEnd, never between them. glEnd captures
the final color when the game sets it inside the immediate-mode primitive.

First three frames and then one frame per second are sampled. Each keeps its last
48 bind/draw/copy events so early geometry cannot use the entire output budget.
`omitted` reports truncated earlier events. There are at most 8192 emitted records
process-wide plus one cutoff marker, and at most 256 upload records/context.
The context retains at most 512 CRC associations, clearing on overflow; `unknown`
can also mean an upload happened before observation or in a shared context. It
does not mean a corrupt program. The shader dump remains the existing optional
CRC-named on-disk dump; the logging cap does not limit that directory.

These seven original asset CRCs are pinned by read-only binary tests:
`XREngine_Blur0=CC2FE5F9`, `Blur1=BE7E5F79`,
`ScreenFX0=DB80471D`, `1=E3D4D1B5`, `2=62E11CB4`,
`3=478D5FF9`, `4=2590A258`. Loader preprocessing may change runtime CRCs;
identify those through the archived source in `shader_dump`, not filenames alone.

From this checkout, with the game closed, the lead can deploy the built ASI
without replacing the existing loader. This task itself did not deploy it:

```powershell
$g = 'D:\Games\Riddick'
Copy-Item "$g\System\Win64_AMD64\BigFix.asi" .\local\BigFix.before-bloom.asi
Copy-Item .\build\Release\BigFix.asi "$g\System\Win64_AMD64\BigFix.asi"

# Read-only plan; this command was verified here and does NOT launch.
pwsh -NoProfile -File .\tools\bloom_scene.ps1 -GameDir $g -DryRun

# Best ship candidate; repeat the identical scene with modes 5 and 6.
pwsh -NoProfile -File .\tools\bloom_scene.ps1 -GameDir $g -ShaderMode 5
pwsh -NoProfile -File .\tools\bloom_scene.ps1 -GameDir $g -ShaderMode 6

# If the first map misses the ship, try its arrival successor.
pwsh -NoProfile -File .\tools\bloom_scene.ps1 -GameDir $g -Action Map -Scene Pa1_Arrival -ShaderMode 5

# Repeatable baseline (playback avoids benchmark auto-exit before captures).
pwsh -NoProfile -File .\tools\bloom_scene.ps1 -GameDir $g -Action Play -Scene panoptical01 -ShaderMode 5
pwsh -NoProfile -File .\tools\bloom_scene.ps1 -GameDir $g -Action Play -Scene panoptical01 -ShaderMode 6
```

The launcher defaults to F12 at 10/25/40 seconds after first presentation, requires
foreground ownership before input, closes only the process it launched, and
archives PNGs, `BigFix.log`, `shader_dump`, timing notes and the exact command in
fresh `local/bloom-<scene>-sm<mode>-<timestamp>/` directories. It temporarily enables
TraceBloom, DumpShaders and CapturePng, selects F12 and the requested shader mode,
and restores BigFix.ini/Environment.cfg byte-for-byte in finally. It requires the
ASI/loader already installed. Capture filename `sm` is a requested-config label,
not a query of the engine's selected shader. Normal map/loadgame runs may trigger
the game's own saves; the script preserves settings, not savegame state.

**Required lead evidence:** preserve one archive showing the ship at normal
brightness and/or peak washout, state which image is actually wrong, and provide
the corresponding mode-5/mode-6 archive. Verify the map command really starts the
sequence rather than recording the menu. We need the extraction pass Blur0's
env0..4 and color/attrib3, subsequent Blur1 texture sizes/formats and weights,
and final ScreenFX combine env parameters and blend state in the frames around
that image. Include all source dumps and the entire bounded log, not just lines
containing a presumed asset CRC. If the peak is later, pass e.g.
`& .\tools\bloom_scene.ps1 -GameDir $g -CaptureSeconds @(60,80,100)` from
PowerShell; startup defaults may miss it. If `omitted` removes the entire blur chain,
or the 8192-record cutoff precedes the image, a more targeted sample/CRC filter is
the next instrumentation change. No x86 execution is needed or authorized.

## Reproduce the static evidence and validation

```powershell
& .\local\venv\Scripts\python.exe -I tools\re\bloom_static.py "$g\System\Win64_AMD64\MXR.dll" --string XREngine_Blur --range 0x89caf 0x89dd7
& .\local\venv\Scripts\python.exe -I tools\re\bloom_static.py "$g\System\Win32_x86_SSE2\MXR.dll" --range 0x5afcd 0x5b12b
& .\local\venv\Scripts\python.exe -I tools\re\bloom_static.py "$g\Content\GameWorld_Win64_AMD64.dll" --range 0x76ec 0x772f
& .\local\venv\Scripts\python.exe -I tools\re\bloom_static.py "$g\Content\GameWorld_Win32_x86_SSE2.dll" --range 0x634f 0x6397
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
$env:RIDDICK_DIR = $g
ctest --test-dir build -C Release --output-on-failure
python -m unittest discover -s tools/tests -p test_bloom_scene.py -v
```

The Python decoder uses the existing pinned pefile 2024.8.26 / Capstone 5.0.9
venv setup documented in `tools/re/README.md`. Full scratch disassembly stays
under ignored `local/`. New tests cover opt-in/default-off, CRC correlation and
replacement, independent contexts, sampled frame tail, no queries on unsampled
frames, restoration on exceptions, actual MinHook forwarding and begin/end safety,
video-trace coexistence, the process-wide output cutoff, launcher dry-run quoting
and AMD64 refusal, and reference constants/shared shader rewrite preservation.
Visual correctness and scene reachability remain unverified until the lead runs.

Validation on this checkout: VS2022 x64 configure and Release build succeeded;
CTest **55/55 passed** with RIDDICK_DIR unset, then **55/55 passed** with
`RIDDICK_DIR=D:\Games\Riddick`. The two focused reference bloom tests
passed 68 assertions; four PowerShell dry-run tests passed. `git diff --check`
was clean. Initial verification caught and corrected an unintended change in
the video-only GL hook count; video-only retains 24 hooks, bloom-only uses 6,
and combined tracing uses 26. No visual-fix claim follows from these tests.
