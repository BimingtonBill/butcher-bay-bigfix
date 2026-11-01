# Theora video playback — EU 1.1 x64

Static research plus the lead's real runtime trace, 2026-10-29 (EU 1.1 x64).
`VideoAspectFix=1` now defaults on. It fits picture aspect at
`GameWorld!0x39AD0`, only inside paint `0x755D0` and only for the video call
returning to **`0x76463`**. Actual GL viewport proportions compensate the
stretched 640×480 UI coordinate system. The original helper retains texture
scale/offset, clipping, shaders and submission. A black color-buffer clear
before every corrected draw guarantees fresh black bars, restoring clear
color, color-write mask and scissor enable. Menus/HUD/3D keep their behavior.

The implementation agent read the full supplied 1,646-line log at
`local/traces/2026-10-29-1512-video-trace.log`, including repeated payloads
(122 distinct payloads), lifecycle and shutdown. No game was launched, no game
DLL was loaded, and no file under `C:\Games` was written during implementation.
The lead still needs to verify live output from the built fix.

**Corrections to the trace-only research:** the primary vtable rectangle
slot +0xF8 is **`0x38E90`**, not the instrumented alternate `0x36950` path.
Its second argument is a clip context: offset at +0/+4, actual clip bounds at
+8..+0x14. Reading the first four integers as a RECT gave `0,0,0,0`, which
was not evidence of an empty destination clip. See section 5 for the trace
findings, including the absence of Intro/menu records in this particular log.

## Reproduction and binary identity

All addresses below are **RVAs**; add the loaded module base at runtime. All three
DLLs have preferred base `0x10000000`. Descriptive function names are inferred,
unless explicitly identified as RTTI, imports, exports, or literal strings.

| Module | SHA-256 |
|---|---|
| MSystem.dll | `b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500` |
| GameWorld_Win64_AMD64.dll | `749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7` |
| RndrGL.dll | `400dc6b50f65c9b884e85daeac7b1b1aaa2f19d2cbf4dbd9e597abae284ea348` |

Method: [display/window research](display-and-window.md), PE imports, ASCII/RTTI
strings, direct code/data cross-references, MSVC exception ranges and Capstone
disassembly. The initial string/export inventory also covered SbzEngine.exe,
MCCDyn.dll, MXR.dll, dbghelp.dll, and GameClasses_Win64_AMD64.dll. SbzEngine's
`CVideoFile` RTTI is associated with `MAVI.cpp`; that name alone is not evidence
that the Ogg movies use its AVI path. The positive Theora findings are in MSystem.

[video_static.py](../../tools/re/video_static.py) reconstructs initial Ogg packets
from page lacing rather than searching arbitrary compressed payload bytes. It
prints raw metadata and verifies the three hashes, 36 selected instruction
addresses/operands below, and both `CRC_Util2D` vtable slots. It never loads PEs.

```powershell
python -m venv local/venv
& ./local/venv/Scripts/python.exe -I -m pip install capstone==5.0.9 pefile==2024.8.26
& ./local/venv/Scripts/python.exe -I tools/re/video_static.py --root D:/Games/Riddick --verify
# Shorter, targeted disassembly; full scratch output belongs in ignored local/.
& ./local/venv/Scripts/python.exe -I tools/re/video_static.py --root D:/Games/Riddick --dump system 0x1483d4 0x148481
& ./local/venv/Scripts/python.exe -I tools/re/video_static.py --root D:/Games/Riddick --dump world 0x763d5 0x76463
```

## 1. File headers and runtime metadata: picture aspect owns display policy

All eight identification packets are Theora **3.2.0**, with picture offsets 0,0.
The raw pixel-aspect fields are **not zero**, including universal_ntsc. These are
the unsigned 24-bit big-endian fields at packet offsets 30 and 33, after FPS
numerator/denominator at 22 and 26. MSystem's parser independently confirms the
same field order (section 2).

| File | Frame | Picture | Raw PAR numerator/denominator | Required display from task |
|---|---|---|---|---|
| Intro.ogg | 640×480 | 640×480 | 1777/1000 | 4:3 |
| Outtro.ogg | 640×480 | 640×480 | 1777/1000 | 4:3 |
| Eyeshine.ogg | 640×480 | 640×480 | 1777/1000 | 4:3 |
| Tigon.ogg | 640×480 | 640×480 | 1333/1000 | 4:3 |
| logo_mixed.ogg | 640×480 | 640×480 | 1333/1000 | 4:3 |
| vug_new.ogg | 640×480 | 640×480 | 1333/1000 | 4:3 |
| Sbz.ogg | 640×368 | 640×360 | 1777/1000 | 16:9 |
| universal_ntsc.ogg | 720×496 | 720×486 | 1481/1000 | 4:3 |

Successful decoder exits agree with the static parser for every file actually
opened: vug_new, Tigon and logo_mixed report **1333/1000**, universal_ntsc
**1481/1000**, and Sbz **1777/1000**. The table's 1777/1000 640×480 entries
are **Intro, Outtro and Eyeshine**, none of which is opened in this supplied
log. The apparent mismatch was a comparison of different files, not evidence
of runtime conversion or parser misalignment. Decoder entry events before
initialisation may contain garbage; only successful exit metadata is valid.

These fields resemble display aspect ×1000 (720/486 ≈ 1.481), despite occupying
Theora's pixel-aspect fields. Multiplying them by picture aspect would distort
even the 1333/1000 logos. The explicit display policy therefore uses
**picture_width / picture_height**, except **720×486 -> 4:3**. Raw PAR remains
in diagnostics and never drives the fix. Sbz uses the 640×360 picture rather
than its 640×368 padded frame. The unobserved 1777/1000 640×480 files retain
the requested 4:3 policy; a subsequent live run must verify those too.

## 2. Open, parse, decode, stop: MSystem.dll

The real RTTI type `CTextureContainer_Video_Theora` has type descriptor `0x29DA80`,
complete-object locator `0x222D78`, and vtable **`0x1E7998`**. Its video entries
are `CTC_TheoraTexture` (RTTI descriptor `0x29DA58`), containing a decoder pointer
at **entry +0x18**, and texture IDs for base/Y/UV at **+0x68/+0x6C/+0x70**.
Decoder RTTI is `CTC_TheoraTexture::CTheora` (descriptor `0x29D8E8`), vtable
`0x1E7658`.

| Routine / boundary | MSystem RVA | Evidence / meaning |
|---|---|---|
| Register a video entry (`AddVideo`) | `0xF0B80` | String `AddVideo` at `0x1E7AF0`; constructs entry and registers `*VIDEO_%s`, `*VIDEO_%s_y`, `*VIDEO_%s_uv` names (strings `0x1E7920/0x1E7910/0x1E7900`). Registration is not playback start. |
| Lazy decoder creation / file open | `0xF1E50` | Entry +0x28 is the stored file name; allocates 0x3D8-byte decoder, opens its file object at decoder +0x2B8, calls decoder initialise at `0xF2079 -> 0xEFFF0`. Texture description/size queries can reach this routine. |
| Decoder initialise / start candidate | **`0xEFFF0`** | `RCX=decoder`, `RDX=sound context`; reads Ogg pages/packets, parses three headers, initialises codec, creates two frame buffers; returns success in AL. |
| Theora header dispatcher | `0x148740` | Tests packet type and six-byte `theora` signature (literal `0x1FF75C`); identification parser `0x148370`. |
| Identification parser | **`0x148370`** | `RCX=info`, `RDX=bit reader`; reads version, frame/picture geometry, FPS, PAR; zero success / negative error. A metadata hook candidate, not a playback visibility flag. |
| Codec initialise | `0x148920` | Called with decoder state +0x200 and info +0x240. |
| Decode worker / frame conversion | **`0xEF7B0`** | Consumes queued frame buffers; `0xEF90E -> 0x148A30` decodes packet, `0xEFA5D -> 0x148B30` obtains planar YUV; copies Y and packs U/V into an interleaved image. |
| Container refresh | `0xF2FF0` | Advances entries and retires unused decoders; `0xF3238 -> 0xF0630` closes one. Lifetime is driven by texture usage/timeouts, not solely a fullscreen movie. |
| Decoder close / stop candidate | **`0xF0630`** | Tests/clears decoder +0x2F0, signals/stops worker, releases Ogg/codec state and frame buffers, stops associated sound. |
| Entry cleanup | `0xF1CF0` | Also releases the decoder; error paths and lazy reopen are possible. |

Short start/decode/stop excerpts:

```asm
; MSystem — decoder initialise, after Ogg packet extraction
F012E  lea r8,[rsi+1D8h]      ; packet
F0135  lea rdx,[rsi+220h]     ; comments
F013C  lea rcx,[rsi+240h]     ; theora_info
F0143  call 148740h
F0148  test eax,eax
; remaining header packets repeat the call at F020D
F0347  lea rcx,[rsi+200h]     ; decode state
F034E  lea rdx,[rsi+240h]     ; parsed info
F0355  call 148920h
F0509  mov byte ptr [rsi+2F0h],1 ; initialised flag

; MSystem — worker
EF900  lea rdx,[rbp+1D8h]
EF907  lea rcx,[rbp+200h]
EF90E  call 148A30h           ; packet decode
EFA51  lea rcx,[rbp+200h]
EFA58  lea rdx,[rsp+38h]
EFA5D  call 148B30h           ; YUV buffer output

; MSystem — close
F0654  cmp byte ptr [rcx+2F0h],0
F065B  je F0757h
F0661  mov byte ptr [rcx+2F0h],0
F0668  add rcx,308h
F066F  call 4430h             ; signal
F0674  mov r11,[rbp]
F0678  mov rcx,rbp
F067B  call [r11+38h]         ; worker lifetime boundary
F0688  lea rcx,[rbp+200h]
F068F  call 1482A0h           ; codec clear
```

Metadata is available **after successful initialise**, in decoder +0x240:

| Info offset | Decoder offset | Field |
|---|---|---|
| +0 / +4 | +0x240 / +0x244 | Frame width / height (multiples of 16) |
| +8 / +0xC | +0x248 / +0x24C | Picture width / height |
| +0x10 / +0x14 | +0x250 / +0x254 | Picture offsets |
| +0x18 / +0x1C | +0x258 / +0x25C | FPS numerator / denominator |
| +0x20 / +0x24 | +0x260 / +0x264 | Raw PAR numerator / denominator |

```asm
; MSystem — identification parser; calls read 24 bits before these stores
14840E mov [rbx+8],eax        ; picture width
14841E mov [rbx+0Ch],eax      ; picture height
14845E mov [rbx+1Ch],eax      ; FPS denominator (32 bits)
148461 call 123F80h          ; read 24 bits
14846E mov [rbx+20h],eax      ; PAR numerator
148471 call 123F80h          ; read 24 bits
14847E mov [rbx+24h],eax      ; PAR denominator
```

The container exposes picture-size getters `0xF2C30` / `0xF2E10` (vtable
+0x108/+0x110). The width getter ends with `0xF2DA5: mov eax,[rcx+248h]`.
GameWorld helper `0x3E010` resolves the base video texture's owning container
and invokes those slots; its caller receives **picture**, not padded frame size.

## 3. Texture upload and GL boundary

`MSystem!0xF1E50` rounds picture width/height up to powers of two after successful
open. `0xF20B0` doubles ECX until picture width is covered, storing entry +0x90
at `0xF20BE`; `0xF20D1` doubles EBX for height, storing +0x94 at `0xF20DF`.
All supplied movies therefore select **1024×512** padded full-size textures.

`MSystem!0xF2810`, vtable +0x78, is the texture-description path. Local texture
index is divided by 3 to select the entry/plane. It creates the full-size Y
image using format `0x2000` at `0xF2AB1`, and the half-size packed UV image using
format `0x20000` at `0xF2A22`: **512×256** for the supplied movies. The base
texture has another full-size image path. Thus texture dimensions alone cannot
identify a movie, distinguish Sbz's picture height, or determine display aspect.

`MSystem!0xF3C50` (vtable +0xA8) builds image pixels from the current decoder
frame; `0xF3DA5 -> 0xF1E50` can open lazily. Its UV branch and the YUV shader
demonstrate the two-plane scheme. Actual texture upload belongs to the shared
**RndrGL routine `0x39F20`** (exception range through `0x3D018`). It has these
eleven direct `glTexImage2D` call RVAs, all via IAT **`0x8EC70`**:

`0x3ACDE, 0x3AE85, 0x3AEE3, 0x3BE3E, 0x3BE94, 0x3C170, 0x3C1F0, 0x3C232,
0x3CB3B, 0x3CCD6, 0x3CD33`.

```asm
; RndrGL — one shared upload branch, NOT a video-only call site
3ACAF mov [rsp+40h],r13       ; pixels, ninth argument
3ACB4 mov dword ptr [rsp+38h],1401h ; GL_UNSIGNED_BYTE
3ACBC mov dword ptr [rsp+30h],190Ah ; GL_LUMINANCE_ALPHA
3ACC4 mov [rsp+28h],esi       ; border
3ACC8 mov [rsp+20h],edi       ; height
3ACCC mov r9d,ebx            ; width
3ACCF xor edx,edx            ; level
3ACD1 mov r8d,190Ah          ; internal format
3ACDE call [rip+53F8Ch]      ; glTexImage2D
```

No `glTexSubImage2D` import or literal lookup name was found in RndrGL. Do not
infer that the observed runtime must use subimage updates. Conversely the static
inventory does not exclude computed function lookups. **The exact upload branch
and actual GL dimensions during the intro have not been observed.**

## 4. Video paint and existing centred fitting: GameWorld + RndrGL

`GameWorld!0xCB754` loads the `CTextureContainer_Video_Theora` class name
(`0x1F9408`) during world-resource setup. The following scan uses directory
`Videos\\` (`0x1F93A8`, instruction `0xCB7CB`). This scans entries in advance;
it must not set a global “video playing” flag.

Video paint **`GameWorld!0x755D0`** receives the UI object in RCX and a
`CRC_Util2D` pointer in RDX. It is shared by several UI vtables, including
`CMWnd_ModTexture` (RTTI at `0x27E8D8`, vtable `0x1F1208`, paint pointer at
`0x1F1440`). The object holds its texture name at +0x1D0, a video control helper
at +0x1E0, and `PIXELASPECT` at **+0x238**. The literal `PIXELASPECT` at
`0x1ECB20` is matched by property parsing at `0x6960D`; `0x69647` stores the
parsed float to +0x238. Several constructors initialise that field to 1.0.

The paint derives UI scale from the current viewport divided by **640/480**
(`0x756C0/0x756D2`), submits a zero-colour background with texture ID 0, resolves
`_y`/`_uv` textures, selects shader `CMWnd_ModTexture_PaintVideo_YUV2RGB`, gets
picture size through `0x3E010`, and calls the fitting primitive:

```asm
; GameWorld — background geometry, then video submission (separate excerpts)
75756 mov dword ptr [rsp+0D0h],280h ; logical width 640
75761 mov dword ptr [rsp+0D4h],1E0h ; logical height 480
75775 call [rax+0B8h]        ; texture ID 0; EDX=0
757B9 call [rax+0F8h]        ; background rectangle; zero colour at rsp+80h
75932 lea rax,[rip+17C577h]  ; named YUV2RGB shader
; ...
76407 call 3E010h            ; fills picture w/h at rsp+70h/+74h
76429 call [rax+138h]        ; UI destination rectangle
7642F mov r8,rax
7643F movss xmm0,[rsi+238h]  ; UI PIXELASPECT
76447 movss [rsp+20h],xmm0   ; fifth argument
7644D lea r9,[rsp+70h]       ; picture w/h
76452 lea rdx,[rsp+0C0h]     ; clip
7645A mov rcx,rbp            ; CRC_Util2D
7645D call [rbx+100h]        ; return RVA 76463
```

`CRC_Util2D`'s primary vtable is **`0x1E59C8`**, slot +0x100 at **`0x1E5AC8`**
points to **`0x39AD0`**. This is the correction boundary rather than a global
GL viewport hook: it has picture size, destination,
clip, and a call-site discriminator. The supplied trace confirms this callee and return site for all five observed
logos; section 5 describes their actual incoming geometry and the correction.

```asm
; GameWorld — CRC_Util2D fitted rectangle
39AF2 mov r13d,[r8+8]
39AFE sub r13d,[r8]          ; destination width
39AFA mov r14d,[r8+0Ch]
39B01 sub r14d,[r8+4]        ; destination height
; get display context twice through renderer slot +48h
39B59 call [rdx+80h]        ; screen aspect
39B68 call [rdx+90h]        ; pixel aspect
39B76 movss xmm5,[rsp+0E0h] ; fifth incoming argument
39B8F mulss xmm6,xmm0
39B93 divss xmm5,xmm6        ; UI aspect / (display screen * pixel aspect)
39B9D comiss xmm4,xmm5      ; xmm4=1
39BA0 jbe 39BE1h
; first branch shrinks width, other branch shrinks height
39BD3 mulss xmm7,[rip+1A40A5h] ; half horizontal remainder
39C0D mulss xmm8,[rip+1A406Ah] ; half vertical remainder
39CD0 call [rax+0F8h]       ; submit fitted rectangle
```

These screen-aspect units are **normalised to 4:3**, not raw display W/H:
exported `MSystem!CDisplayContext::SetScreenRatio`, `0x146930`, calculates
`0.75 * width / height * supplied_pixel_aspect` before calling setter +0x78.
`RndrGL!0x47ED0` (+0x80) reads context +0x44; `0x47EF0` (+0x90) reads +0x48.
The correction accounts for these units; passing raw 4/3 as the existing
primitive's fifth argument would itself be an error.

The primary vtable's rectangle slot +0xF8 maps to **`GameWorld!0x38E90`**.
The former `0x36950 -> +0x120 / 0x3CD50` interpretation describes an alternate
implementation, not the live video submission. The current four-argument
submission hook observes `(util, clipContext, rectangle, color)` at `0x38E90`.
The alternate dispatch hook remains diagnostic only. `video_static.py --verify`
now verifies both the fitting and actual submission vtable slots.

The on-disk ARB shader `Gl/ARB_Fragment_Program/CMWnd_ModTexture_PaintVideo_YUV2RGB.fp`
samples `texture[0]` for Y and `texture[1]` for U/V. An ATI shader variant also
exists, and the paint has fallback paths. A shader-name filter alone would need
to cover these variants and context/program lifetime.

RndrGL uses shared `glViewport` sites **`0x9658, 0x3BB12, 0x477C3`**. At
`0x9658` it derives viewport from a `CRC_Viewport` rectangle and flips Y against
the display image; at `0x9692` it selects GL_PROJECTION and subsequently builds
the matrix. There is no imported or literal `glOrtho` name: absence of that API
does not mean absence of orthographic 2D drawing. Shared `glDrawElements` sites
are **`0x328D2, 0x3633D, 0x37918, 0x379A9, 0x43779`**; immediate-mode paths also
exist. None is proven video-exclusive. Shared full clear is at **`0x4812A`**.

## 5. Supplied runtime trace: geometry, dispatch and black coverage

- **First frame:** log line **56**, `First frame presented: 2560x1440`, two
  seconds after launch. All sampled video viewports remain 2560×1440.
- **First vug_new paint/fit:** **99–106**, callee `0x39AD0`, return `0x76463`,
  destination `(0,0)-(640,480)`, picture 640×480, PIXELASPECT=1, display +0x44=1
  and +0x48=1. Universal (**444**) supplies 720×486 with the same aspect fields.
  Tigon and logo_mixed follow the same path. Sbz (**1562–1568**, **1599–1605**)
  supplies 640×360 with PIXELASPECT=1.333333.
- **Cause:** display aspect is stale at 1 on the physical 16:9 viewport.
  Original `aspect / (screen * pixel)` is 1 for the 4:3 logos, so the helper
  leaves the full UI rectangle to stretch over 16:9. For Sbz it is 4/3, so the
  helper instead shrinks logical height to 360, adding unwanted bars on 16:9.
- **Actual submission:** every sampled video swap has `rect=0 dispatch=0`
  because the old instrumentation detoured an unused alternate rectangle
  implementation. Reading the live receiver's primary vtable on disk confirms
  `+0xF8 -> 0x38E90`. The background indexed draw is synchronous inside paint:
  stack `GameWorld+0x39346 -> 0x757BF`. The corrected submission hook will expose
  both background and fitted rectangles in the next trace.
- **GL evidence/limits:** projection uses `glLoadMatrixf`; there are no
  `glOrtho` events during video paint. The GL detail budget is consumed before
  the fitted draw. This log therefore does not establish unique fitted GL
  vertices or complete deferred-flush behavior. The geometry correction is
  made before engine submission and does not rely on either.
- **Clearing:** sampled frames clear black (`mask=0x4500`, all color writes
  enabled, scissor off, for example **73**). This is no guarantee for every
  future frame; the zero-color background submission is also not proof of
  opaque full-buffer coverage. The fix clears black every corrected paint.
- **Decoder metadata:** successful starts at **69/110** (vug_new), **410**
  (universal), **673** (Tigon), **1195** (logo_mixed), **1532** (Sbz). Last
  lifecycle events are Sbz close **1644–1645**, followed by desktop retained
  **1646**. Contrary to the run description, this file contains **no Intro
  start, menu transition, or successful capture**. The readiness bug below
  explains the approximately 60-second timeout without a scripted ESC/capture.

## 6. Implementation and invariants

`VideoAspectFix=1` defaults on in Settings, generated INIs and the installer.
TraceVideo remains separately opt-in (default off; absent from shipped INI).
Only already-loaded modules are inspected; complete relocation-free EU 1.1
prologues must match before MinHook creates a detour. Fix-only mode installs
paint/fit hooks and resolves GL functions, without any GL tracing detours.
TraceVideo adds the bounded decoder, submission and GL instrumentation.

Inside the paint scope and exact video return site **`0x76463`**, the fix
computes a centred destination in 640×480 UI units using actual `GL_VIEWPORT`
proportions and picture dimensions. It passes that destination and the product
of display +0x44/+0x48 to the original fitting helper, making the helper's
additional shrink factor exactly one. The original helper still sets texture
scale/offset from the corrected destination and unchanged picture pointer,
and retains the original clip context and color. Global display fields are
never overwritten. Shared-helper calls from other sites or outside paint are
forwarded unchanged. `VideoAspectFix=0` leaves geometry and GL untouched even
with TraceVideo enabled.

For recorded 2560×1440 input: 4:3 and NTSC -> UI `(80,0)-(560,480)` ->
physical **`(320,0)-(2240,1440)`**. Sbz -> UI `(0,0)-(640,480)` -> full viewport.
Narrower screens letterbox 16:9; wider screens pillarbox both aspect classes.

Before every corrected draw, the fix clears **color only** to black, temporarily
disabling scissor and enabling all channel writes. It restores clear color,
write mask and scissor enable; it never changes the scissor box, viewport,
projection, depth/stencil buffer, or decoder metadata. This happens after the
engine's background and before video submission, so deferred video submission
still uses the corrected engine geometry. Missing GL capabilities or invalid
geometry/display fields forward unchanged.

TraceVideo retains its 2,000-line hard limit, 1,500 detail/250 summary budgets,
32 GL plus 12 reserved internal events per sampled frame, burst/1Hz sampling,
first non-video frame buffering and per-thread/context identity. Correction messages
and actual `rectangle_submit kind=fitted_video` geometry now accompany inputs.
There is no decoder-lifetime flag controlling rendering.

## 7. Smoke capture readiness regression

`Read-LogText` used `File.ReadAllText`, whose **FileShare.Read** open conflicts
with the game's still-open `std::ofstream` writer. Its catch returned an empty
string throughout `Wait-FirstFrame`. After game exit, the same reader succeeded,
so final `first_frame_native` passed while `capture_png` said no first frame.
The real FirstFrame line was present well before timeout: the regex and timing
window were not the cause.

The reader now uses a read-only stream with **FileShare.ReadWrite** and a
StreamReader with BOM detection, disposing handles on every path. A regression
keeps a writer open with the real FirstFrame line flushed while executing
Wait-FirstFrame. It failed on both PowerShell 7 and Windows PowerShell 5.1
before the change and passes on both for UTF-8 and UTF-16 BOMs. No game is
launched by these tests.

## 8. Verification and lead's live run

Pure rectangle cases cover 16:9, 16:10, 21:9 and 4:3 screens with 4:3, 16:9
and NTSC pictures, plus invalid and offset bounds. The hook harness seeds
artificial function bodies in a data-only fixture DLL, calls from the recorded
`0x76463` return site under the paint detour, and measures output rectangles
using independent original-helper arithmetic. It covers the recorded inputs,
stale/non-unit display fields, off, tracing on, other callers and non-video
calls. A hidden native OpenGL test context reads back fresh black on every
corrected call and verifies clear color, write mask and scissor restoration.
Neither harness loads a game executable or game DLL.

The existing trace harness covers forwarding, prologue mismatches, context
switches, bounds, reserved records and menu transitions. `RIDDICK_DIR` tests
read real prologues without loading game code. `video_static.py --verify`
checks all eight headers, three binary hashes, 36 instructions and both
fitting/submission vtable slots.

Implementation verification: `cmake --build build --config Release` passed;
`ctest --test-dir build -C Release --output-on-failure` passed **29/29** with
RIDDICK_DIR unset and again with `RIDDICK_DIR=D:\Games\Riddick`;
`.venv/Scripts/python.exe -m pytest tools/tests installer/tests -q` reported
**69 passed, 2 skipped**. The read-only `video_static.py --verify` check also
passed. The author reviewed the complete diff; no additional agent was started.

From the repository root, the lead can run (not run by the implementation agent):

```powershell
pwsh -NoProfile -File .\tools\smoke_run.ps1 -GameDir 'D:\Games\Riddick' -PluginDir .\build\Release -LoaderDll 'D:\Games\Riddick\System\Win64_AMD64\winmm.dll' -TraceVideo -Seconds 60 -Restore
```

The installed loader path is the existing Ultimate ASI Loader 10.0.2. Staging
uses the shipped default INI if Release has none; ensure a pre-existing Release
INI does not explicitly disable VideoAspectFix. `-Seconds` is the FirstFrame
timeout, not total runtime. One ESC is sent 20 seconds after observed FirstFrame,
then capture follows after five seconds. This short run may skip later logos;
verify corrected submission rectangles, PNG bars/cropping, menu capture, and
unchanged menu/HUD/3D. Intro/Outtro/Eyeshine, resize/end/skip, other physical
screen aspects and deferred visual output remain live verification items.
