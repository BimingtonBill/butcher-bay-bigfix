# Eyeshine bright flicker: whole-region fallback and diagnostic variants

Updated 2026-10-30, EU DVD 1.1 x64, starting at `f87e3c3` (`bigfix-v0.3`).
Game modules under `D:/Games/Riddick` were read as PE data only. No game
was launched, no game files were written, and no game capture is committed.

## Corrected result: variant 7 clips real illumination

The lead's run-3 evidence (`local/flicker/run3/BigFix.log` and `b4.jpg` in the
main checkout) rejects variant 7: a hard vertical illumination boundary around
41% of the output cuts off lit floor/wall and a character shadow; the light
then disappears. The earlier assertion **"the rectangle is valid before
lighting; widening would reproduce the defect" was unjustified and is
withdrawn**. The copy/draw coverage mismatch is established; the copied light
rectangle cannot be treated as a trustworthy illumination bound under Eyeshine.

`EyeshineFix=1` now selects the **whole-region fallback**, also diagnostic
**variant 8**. Variant **7** retains the old copy-box intersection for diagnosis
only. Visual acceptance of variant 8 still belongs to the lead's game run.

Static RE establishes the rectangle's complete projection/intersection/union
pipeline below. It does **not** establish which live viewport/portal input is
incorrect, so no speculative scale/offset engine patch is installed. This is
the explicitly requested fallback, not a claim that an upstream affine repair
has been proved. In particular, the supplied rectangles alone do not distinguish
a stale portal clip, wrong projection inputs, or mismatched illumination bounds.

## Per-light rectangle derivation (2026-10-30)

All addresses are RVAs of the unchanged EU 1.1 files; hashes are in
[shadow-eyeshine.md](shadow-eyeshine.md). Full local disassembly stays ignored.
Reproduce with the project's venv Python, `-I tools/re/shadow_static.py MODULE
--range START END`; `eyeshine_state_static.py --field/--data` supplies cross-checks.

### Projection and portal intersections

The clip interface is `renderContext+6F8h`, assigned at MXR+93306/+9330D from
the world's virtual slot D0h. For BSP2 the interface vtable is **MXR+1FD908**:
slot 40h = **+135860** (nonempty light indices), 48h = **+135610** (all indices),
50h = **+135660** (adjust this by -8, tail-call **+1354C0**). The getter returns
`viewState.lightList + 4 + 24*lightIndex` (+1354DE..1355B3), with three packed
unsigned-word L,T,R,B rectangles at entry+0, +8, +10h. This is stored data;
**+9081E is not a projection routine**.

For BSP2's projected visibility path, **+13497F** calls +1327E0 to intersect
the light's world box with the view volume; **+13498D** calls +132FE0 to traverse
visible BSP/portal regions. **+133124** fetches the view state. At
**+13319B..13324E** the light/world box is intersected with a region's world box.
**+1332E3** projects that box through **+12D290**. The result is intersected
with the region's saved screen rectangle at **+13330E..13337E**, then unioned
into global rectangle +2827E8 at **+133386..1333D9**. **+134999** saves that
rectangle into the light entry at +8. Additional submitted bounds accumulate
at entry+10h through **+135670..1356D3**. These are already clipped rectangles;
rescaling only the final union cannot recover discarded intersections.

**+12D290..12D618** projects eight box corners. Let M be the view state's matrix
at +34Ch and P=(X,Y,Z,1). The instruction offsets give:

```text
d = X*M[+354] + Y*M[+364] + Z*M[+374] + M[+384]
u = (X*M[+34C] + Y*M[+35C] + Z*M[+36C] + M[+37C]) / d
v = (X*M[+350] + Y*M[+360] + Z*M[+370] + M[+380]) / d
screenX = u * state.float[+324] + state.float[+32C]
screenY = v * state.float[+328] + state.float[+330]
```

The eight-corner loop is **+12D3E0..12D4C9**. A corner with d < .1
(constant +1FB0B0) takes **+12D4F7..12D515**, returning the full view dimensions.
Otherwise it takes min/max u/v, maps to pixels, rounds via `trunc(t +/- .5)`
(constant +1F8F20), clamps to [0,width]/[0,height], and enforces right>=left,
bottom>=top (**+12D4CF..12D618**).

The state is built at **+1371A3..1372BE**, with a sibling path at
**+13885A..138964**. For viewport view area (L,T,R,B) at viewport+44h:

```text
state[+334..+340] = (L,T,R,B)
state.int[+344] = R-L; state.int[+348] = B-T
state.float[+324] = .5 * viewport.float[+1C]
state.float[+328] = .5 * viewport.float[+20]
state.float[+32C] = float((L+R) >> 1)
state.float[+330] = float((T+B) >> 1)
```

The matrix is copied to state+34Ch at +1372BE/+138964. `MSystem+52950..52992`
(`CRC_Viewport::SetView`) copies the six-dword clip at +2Ch, four-dword view
area at +44h, and sets dirty byte +54h. `MSystem+56C40` updates projection
coefficients, including view dimensions at +56D36..56DC6. Eyeshine supplies
`.75,.75` at GameWorld+5870, and SbzEngine+39950 rounds each scaled dimension
to `4*trunc(originalDimension*scale/4)` before SetView. See the scale derivation
below. A safe source correction needs the **actual matrix, projection pixels,
view area, and portal rectangles for the offending light in the same frame**;
none is present in run 3's compact records.

### Union, mask geometry, copy and lighting

**MXR+907A0** obtains the entry through slot 50h. For the first light,
**+907BA..90810** uses the current viewport's view area at +44h..50h. For later
lights **+9081E..9089D** computes:

```text
L = min(entry.rect8.L, entry.rect10.L)
T = min(entry.rect8.T, entry.rect10.T)
R = max(entry.rect8.R, entry.rect10.R)
B = max(entry.rect8.B, entry.rect10.B)
```

It rejects empty results at +908A5/+908AF. The resulting **actual quad bounds**
are passed to **+83B90** at +90971 (stencil precursor), +90A00 (opaque alpha fill),
and +90AB0 (GL_LEQUAL/ref128 zero-alpha conversion). +83C68 onward converts the
four word bounds into vertices; widening glScissor alone cannot enlarge these
quads. **+90AF0..90B26** copies L,T,R,B into the E6 copy record and its first
corner into destination x/y. +90B43 queues callback **+838B0**, priority light
index+.12; the alpha fills run at +.10/+.11. CPU listing order is not draw order.

The callback invokes renderer slot B0h. **RndrGL+34FA0**, particularly
**+3509E..350DD/+35145**, converts the source and destination to GL coordinates:

```text
source = (L, drawableHeight-B, R-L, B-T)
destination = (destX, textureBottom-destY-(B-T))
```

The two scissor setters **RndrGL+13CF3..13D2B / +14E5B..14EA7** likewise use
`(L, contextHeight-viewY-B, R-L, B-T)`; +9658/+9681/+968C sets viewport/cache.
At 2560x1440, viewY=0 and reduced view (0,0,1920,1080) becomes GL
**(0,360,1920,1080)**. Therefore the three logged ON boxes correspond to:

| GL box x,y,w,h | Engine L,T,R,B |
|---|---|
| 79,360,1388,114 | 79,966,1467,1080 |
| 743,474,617,966 | 743,0,1360,966 |
| 743,360,805,1080 | 743,0,1548,1080 |

A top at GL y=1440 is the expected top of this reduced viewport. A thin strip
or height 966 alone is **not proof** of a missing +360 or double .75. The run-3
visual rejection proves the old clamp loses real illumination; the exact bad
upstream input remains unresolved. Shared attribute replacement/intersection at
MXR+B5100 and MSystem+4E940 can also change effective scissor coverage, as the
previous trace showed. No blanket patch to those shared setters is justified.

## Implemented whole-region fallback

The fix queries **effective** viewport/scissor at operation time. It recognizes
the top-left anchored .75..1 reduced view (including four-pixel quantization)
against the current WGL drawable's client size. Full-size, differently anchored,
half-size and unrelated-aspect views are no-ops. No forced viewport, texture
filter change, shadow removal, or game bytes patch is used.

* Stencil geometry, stencil clears and the NV packed-copy scissor use the entire
  reduced region, including disabled scissors. Array/indexed/immediate and
  geometry display-list draws are covered. State is restored after the operation;
  all GL queries precede Begin. PopAttrib/state-only display lists are accounted
  for by subsequent effective queries, and list-result scissor boxes are retained.
* The last observed fixed-function constant-alpha stencil conversion is recorded
  per context: compare/ref/value mask/alpha, with no stencil writes and KEEP ops.
  At the alpha-only mask copy the fix **replays the complementary alpha fill and
  that conversion across the whole region**. This removes the original small
  quad geometry limit. It preserves RGB, depth/stencil values, matrices, samplers,
  color masks and other server state. It shares the existing variant-3 replay
  implementation rather than inventing a fixed `stencil!=128` mask.
* The copy source becomes the whole reduced region. Its destination changes by
  the same source delta: `dx'=dx+region.x-x`, `dy'=dy+region.y-y`. Texture extent
  is checked before replay/copy, preserving the engine's pixel-to-texel mapping
  (for the observed 2048-high image, y offset remains 608). Missing conversion
  evidence or insufficient storage skips the expansion and logs once.
* Recognized SpecNormal lighting sampling that fresh u5 texture uses the same
  region. Metadata is context/texture/frame scoped. Frame end and context deletion
  retire it; non-mask copies invalidate their destination. Variant 7 alone keeps
  the historical intersection behavior.

This spends more fill/copy bandwidth than tight per-light scissors. Its exact
upstream cause and live visual/performance acceptance remain pending. Display
lists containing their own in-list scissor changes before geometry cannot be
rewritten by an export wrapper; the located RndrGL geometry-list path and direct
draw paths are covered, and effective changes between draws are queried.

## Compact records and interleaved capture

`TraceShadowPass=1` accumulates counters every frame but emits a compact line
**only for a successfully queued capture (`cap>0`) or an active variant change**.
F11 still arms four detailed frames; F11, idle frames and PNG writer drain do
not consume the 500-line compact reserve. Detailed lines retain their 1500-line
budget within the 2000-line process limit. Every F9/A/B selection also logs
outside that budget.

`frameRecord cap=N variant=V ...` joins `Capture saved:` by capture index; a
filename collision logs `Capture index remap:`. `cap=0` is now a variant-change
record only. `masks/widened/restricted/union` retain their diagnostic meanings;
`restricted` is variant 7's clamp count. **`regionPasses`** counts temporary
region scopes and **`regionCopies`** counts full-region alpha rebuild/copies.
`vp` is presentation-time viewport, which may already be full-size for ScreenFX.

`EyeshineAbBurst=N` (0=disabled; 1..8=variant) enables diagnostics and changes a
CaptureBurst into adjacent rendered-frame A/B samples: **0,N,0,N,...**. The key
press arms at presentation; its already rendered frame is not mislabeled or
captured. The first sample is the next complete frame. Each queued image logs
`EyeshineAbBurst cap=N variant=V sample=i/count` even without TraceShadowPass.
The variant advances only after its frame's capture/record. F9 is held out for
the burst. The previous selection is restored after the last sample or on a
readback/swap failure; writer drain never toggles variants. Odd burst lengths
also restore correctly. Adjacent images still have one frame of camera motion,
but no seconds-long scene drift.

F9 cycles **0..8**: 0 original, 1 packed-state, 2 stencil-clear, 3 mask-alpha,
4 window-coordinates, 5 clamp-taps, 6 full-resolution, 7 old light-scissor
intersection (rejected visually), **8 whole-region fallback**. TraceShadowPass,
a nonzero initial variant or a nonzero EyeshineAbBurst enables this override;
otherwise the default-on EyeshineFix boolean controls the new fallback.

## Static evidence and ownership

All addresses are RVAs; fingerprints are unchanged from `shadow-eyeshine.md`.
The new `tools/re/eyeshine_state_static.py` inventories memory-field references,
call/pointer references and raw constant bytes without executing a PE.

### Night vision to render scale

GameClasses+1CD082..1CD0A1 reads/toggles player+1314. The read at
**+20E7C3** supplies the first float of the ScreenFX parameter output when the
player is local/eligible: Eyeshine sets it to 1 (+20E7CC). This is in the
message/property dispatch family selected through +20E744..20E76F.
GameWorld+6116..61159 initializes the ScreenFX output to zero, builds query
**108Bh** at +61B3, and dispatches through virtual slot 5A0h at +61E6.
Its first float is accepted at +61F5..6203, then approached over time in
client+**1AE0h** (+6206..6252). +6555 reads that same blend for ScreenFX colors
and parameters. Thus the path involves a smoothed effect value, not simply a
shader testing the gameplay byte every fragment.

The leaf at **GameWorld+5870..58EB** (function pointer at **+1DF570**):

```text
if (!(client[209Ch] & 4)) return (1,1)
t = 10 * client.float[1AE0h]
f = (abs(t) - abs(t - 1) + 1) * 0.5
return (1 - 0.25*f, 1 - 0.25*f)
```

Constants are +1DE024=10, +1DE020=.25, +1DDC80=.5, +1DDC84=1, and
+1DDC70 is the double absolute-value mask. For the nonnegative Eyeshine blend,
`f=clamp(t,0,1)`; steady Eyeshine returns **.75,.75**.
GameWorld's client constructor +FF8F installs vtable **+1DEF08**, so +1DF570 is
virtual slot **668h**, not 68h. **SbzEngine+41A03/+41BA4** calls that slot and
multiplies its two returned floats into the engine render scale, then calls
**SbzEngine+39950**. Its ordinary path computes each reduced dimension as
`4 * trunc(originalDimension * scale * .25)` (+39A3C..39A7B), adjusts pixel
aspect, and calls `CRC_Viewport::SetView` at **+39B4D** (thunk +703CA) with a
**new view rectangle (0,0,reducedWidth,reducedHeight)** and the existing
`CClipRect` at viewport+2Ch. Thus the observed main path resizes the actual view;
it does not merely clip an unchanged 2560x1440 camera. The previous report's
(-320,+180) clipped-camera counterexample is ruled down for this path.

Separate World view construction at +7798E..77ACD and +3BFB3..3C1CF also scales
view/clip rectangles through another object's slot 68h. That different slot is
not confused with the client's 668h method. Effective per-model texgen planes
and every volume/camera draw are still unmeasured in the supplied trace.

Variant 6 hooks the **scale owner**, rather than forcing `glViewport` after
projection/copy records have already been built. It calls the original once,
then replaces the returned pair with 1,1 only while selected. The hook requires
GameWorld's SHA-256 `749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7`
and the live ten-byte prefix at +5870. Hook/fingerprint failure is logged and
does not prevent other module work. A higher GPU cost and altered post-effect
sampling are possible; the visual run must verify Eyeshine still functions.

### Depth, stencil volumes, and binary alpha

RndrGL+34EB8 reads **1.0** at +8FEE4. Thus the explicit zoom and white color in
the packed copy are unity, not Eyeshine's .75 scale. +34EE4's pointer is resolved
from the string **glWindowPos2f** at +92F88 (+2E4A9..2E4BD). It explicitly sets
window position (0,0), independent of projection. The copy dimensions are the
context dimensions, independent of viewport. This rules down a theory that its
literal zoom is .75 or that the explicit position is transformed RasterPos.

The [NV_copy_depth_to_color specification](https://registry.khronos.org/OpenGL/extensions/NV/NV_copy_depth_to_color.txt)
packs depth/stencil as UNSIGNED_INT_24_8, then unpacks as RGBA 8_8_8_8. Before
subsequent fragment operations, alpha is therefore **stencil/255** for 8-bit
stencil. Pixel transfer, PixelZoom, alpha/depth/stencil testing, blending, color
masks and scissor can still affect this color-copy operation. A valid raster
position and unity depth scale/bias alone do not establish a correct copy.

MXR's constructor at **+8D177** sets renderer+18Ch to **80h** and +190h to
**FF000000h**. +8FE50 queues the stencil work: model/volume submissions through
the model virtual +90h at +90066/+900EE, +8E880, then the renderer virtual +58h
at +902BC/+902F0. The constructor's vtable **+1FFE18** resolves +58h to
**+8D830**. +8D8E5..8D939 forwards the model's flags OR the requested flags,
model transform (+48h), light data (+8h), current view matrix (+74Ch), current
render context (renderer+50h), and allocated attribute context to the model's
virtual **+98h**. +8E880 reaches the same model +98h boundary at +8EB6F.
There is no separate hardcoded full-window projection in these forwarding
functions and no identified direct read of player+1314 there; the view/context
and model implementation own volume projection. Concrete BSP2 front/back
shadow-volume attribute type names occur at +272460/+2724B0. A particular model's
derived +98h extrusion implementation and back-face state remain unresolved.
In the bounded per-light path, +908DD..90935
constructs stencil-enabled attributes with ref renderer+18Ch, mask FFh, and
stencil operations before queueing the rectangle via **+83B90**.

The mode-6 scheduling at **+90E65..+91128** is especially relevant:

1. +90EF6 selects packed copy type 1; +90F1B assigns priority **-19.0**.
2. +90FBA selects E4 (u6); +90FE3 assigns **-18.9** for the full depth copy.
3. +9103F sets compare **3 = GL_EQUAL**, ref 80h, write mask zero, KEEP ops,
   then queues color renderer+190h (default opaque black) at **-18.8**.
4. +91174 sets compare **6 = GL_NOTEQUAL**, ref 80h, write mask zero and KEEP,
   then queues the config object's color at +164h, also at **-18.8**.

These conversions draw color/alpha from stencil tests. RndrGL+1367F..136A2
maps attribute flag bit 15 to RGB writes and bit 10 to alpha writes.
MXR's conversion flags **C400h** therefore enable RGB and alpha. The stencil
comparison mapping is independently checked in RndrGL's jump table +FBBC:
engine compares 1..8 map to GL_NEVER through GL_ALWAYS in numeric GL order.
RndrGL+352D1..3531E converts the high color byte directly to alpha/255.

There is **another per-light conversion**: mode check +90981, opaque black
fill **FF000000h** at +909C3, then color **0** at +90A80 under compare
**4 = GL_LEQUAL**, ref 80h, no stencil writes. Its priorities are light index
+0.1 and +0.11 (doubles at +2002D8/+2002D0). The E6/u5 bounded copy at +90B43
has light index **+0.12** (+2002C8). The precursor rectangle uses light index
-.001 (+90941..90971). CPU disassembly order is therefore **not render order**.
This explains why two same-size u5 copies can occur before a lighting draw;
they cannot automatically be labelled duplicate scratch copies.

For GL_LEQUAL, the comparison is `ref <= stencil`; the zero-alpha quad writes
where stencil >=128, leaving alpha 1 in the complementary region after the
opaque fill. Global equal/not-equal conversions have different polarity/config
inputs. Variant 3 observes the last eligible conversion draw and reuses its
actual compare/ref/mask and binary constant alpha, with a complementary initial
alpha. It never assumes that arbitrary volume counts should become 255, or
that a single !=128 rule describes every mask. Trace records expose the recipe.

The shader then computes **1 − saturate(.125 × sum(16 depth-weighted taps) +
center alpha)**. The .125 DP4 weight is shipped code; it is not a normalized
1/16 average. A false low alpha or wrong depth rejection can therefore create
a bright unshadowed flash. The final shadow multiply remains in every probe.

## Earlier ranked candidates (superseded by the live scissor evidence)

| Rank | Mechanism and evidence | Probe / interpretation |
|---|---|---|
| 1 | Reduced-resolution **mask conversion coverage or ordering**: Eyeshine demonstrably selects .75 scale; mode 6 interleaves full-context packed depth, global stencil conversions, per-light alpha conversions, and bounded E6 copies. The old budget omitted the fixed-function conversion draws. An incomplete/overwritten alpha region can decrease the mask and flash bright. | **3** replays the observed conversion across the actual copied source rectangle; **6** removes reduction at its owner. If 3 works with shadows intact, investigate conversion geometry/state. If only 6 works, reduction is causal but the exact volume/quad/texgen stage remains unresolved. |
| 2 | **Packed-copy fragment/pixel state leakage**: NV copy obeys tests/masks/blend/transfer. Eyeshine adds ScreenFX work; old snapshots do not record these states. Explicit zoom=1 and WindowPos=(0,0) rule down those particular literals. | **1** temporarily neutralizes pixel/fragment state. A success needs the effective-state trace and intact shadows; it does not identify which neutralized field was responsible. |
| 3 | **Projection/texgen disagreement**: previous mapping derivation is correct for resized cameras, but clipping differs. Static view construction scales both rectangles, so a universal full-projection/reduced-viewport claim is unsupported. | **4** obtains u5/u6 lookup coordinates from fragment window position and the observed copy offset, preserving the 16 taps and depth rejection. A success points toward effective texgen inputs, not necessarily the camera projection itself. |
| 4 | **Stencil clear leaves previous-frame counts outside the reduced scissor**. NV copy still spans the context, and filter support can reach outside current coverage. | **2** adds a full stencil-only clear before the combined frame clear, using the original clear value and restoring masks/scissor. The supplied OFF **and ON** frame clear already show scissor disabled and full write mask, so existing positive evidence ranks this lower. |
| 5 | **Padding/stale scratch at the soft kernel boundary**. Allocation CLAMP_TO_EDGE does not clamp to a 1920x1080 copy inside 4096x2048; +/-3.5 taps can read old alpha. Interior coordinates remain in bounds in the resized model. | **5** clamps center and all 16 mask/depth tap coordinates to current copied texel centers. A success only near borders fits this mechanism; it is weak as an explanation for broad interior flicker. |

The user saw flicker regardless of intentional overexposure and with Eyeshine
in both the opening dock and sodini01. Exposure alone, H-opcode conversion,
hardcoded texture names, and packed-depth sampler discontinuity are consequently
weak explanations. No new shader brightness clamp or shadow-removal workaround
is presented as a fix.

## Runtime controls and recognition limits

`EyeshineFixVariant=0` is original when diagnostics are enabled. `TraceShadowPass=1` enables experiments and F9
even when starting at zero; a nonzero initial variant or EyeshineAbBurst also
enables F9 without detailed trace output. With all three off, experiment shader
instrumentation stays out.
F9 uses a held-key latch on successful presents and only accepts the hotkey when
this process owns the foreground window. Each press selects the next independent
variant, including 6 -> 7 -> 8 -> 0, logs it, and arms **one** next trace frame. F11
retains the four-frame trace control. Variant 7 is the rejected diagnostic clamp;
variant 8 is the current fallback. Diagnostics override EyeshineFix so 0 remains
an original baseline. A/B temporarily holds out F9.

For the older variants 3/4/5, the shared copy hook recognizes the packed type/rectangle/context, consumes its
next matching u6 copy, then records subsequent distinct level-0 RGBA8 mask-copy
targets with the observed LINEAR/LINEAR filter pair and valid allocation bounds
rectangles until the first recognized SpecNormal draw or presentation. It never
hardcodes GL names. Bad rectangles, context mismatch and intervening CopyTexImage
fail closed. Later post-effect scratch copies are excluded. Variants 3/4/5 may
therefore be **inconclusive** for a path whose relevant mask copy happens only
after the first SpecNormal draw, or whose mask/producer use unrelated contexts.
The copy metadata is cleared per present and retired on context deletion.

Variant 3 additionally requires an observed constant-color, binary-alpha,
stencil-tested conversion with no depth/alpha/blend test and no stencil writes
or color array. It replays that recipe over the copy rectangle, preserves RGB
and stencil, and restores viewport, scissor, fixed texture enables, programs,
colors and matrices. Missing eligibility means no repair, not visual acceptance.
An `applied` record is required for every claimed probe result.

Variants 4/5 instrument only recognized SpecNormal source layouts with no
existing program.local use, using local slots 0..2 for observed copy mapping,
bounds and two enable flags. Other variants and zero have both flags zero.
They retain original lighting, shadow multiply, soft taps and rejection. Driver
compilation of the shipped rewritten program is covered by the native fixture.

## Trace changes

Each packed/texture copy and each recognized shadow/ScreenFX draw now has an
effective state record: zoom, raster validity/window position and raster color,
current color/write mask, stencil enable/func/ref/value mask/write mask/ops/clear,
depth enable/func/write, alpha test/func/ref, blend enable/src/dst, logic/dither,
scissor, RGBA pixel transfer scales/biases, depth scale/bias, index shift/offset,
map-color/map-stencil, and projection matrix. Up to eight stencil-enabled draws
also get snapshots, including fixed-function volumes/conversions. Queries happen
before glBegin. No glGetError or framebuffer readback occurs in production.

Export events cover PixelZoom, PixelTransfer f/i, RasterPos2 f/i, ColorMask,
StencilMask/Func/Op, AlphaFunc and BlendFunc. Effective snapshots also catch
WindowPos and state restored by PopAttrib without intercepting every extension.
The original producer trace is retained before variant 1; a second effective
record shows its temporary state. Texture binds have a separate **four/frame**
allowance; state events reserve 48, copies reserve 24, with separate draw
allowances and the existing **2000-line/process** cap. Omission counts still
limit conclusions. Separate back-face ATI/EXT state is not reconstructed;
the added standard stencil state captures the conversion recipe and front face.

## Separate AMD/Mesa compatibility finding (not changed)

Follow-up (2026-10-30): conventional colour is now preserved alongside the
generic write. See [AMD AA/colour investigation](amd-aa.md) for history,
regression coverage and remaining hardware/specification limits. The finding
below describes the pre-fix implementation.

`GlHooks::color4f` sends only `glVertexAttrib4f(3,...)` in any-GPU mode. It does
not call the conventional glColor4f entry. NVIDIA's NV attribute aliases make
this equivalent there. The
[ARB_vertex_program specification](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_vertex_program.txt)
allows implementations to alias or not alias generic/conventional attributes.
Consequently the remap is not portable proof that fixed-function current color
or raster color was updated on AMD/Mesa. This affects stencil-to-alpha quads and
WindowPos capture of current color as well as general fixed-function rendering.
It is a separate compatibility defect candidate, not an explanation for the
reported RTX run. No generic color change was bundled here: it needs native
fixed-function/vertex-program tests on an unaliased driver. The advertised but
unemulated NV packed-copy extension remains a second existing non-NVIDIA issue.

## One short confirmation run (lead only; not executed here)

Use shader mode 6, 2560x1440, cast shadows enabled, and the opening only.
Set these diagnostic keys in the lead's existing launcher/settings override:

```ini
EyeshineFix=1
EyeshineFixVariant=0
EyeshineAbBurst=8
EyeshineFlickerWorkaround=0
TraceShadowPass=1
TestForceEyeshine=1
Enable20PlusPlusAnyGpu=1
AntiAliasing=0
SkipLogoVideos=1
CapturePng=1
CaptureBurst=8
```

Use the built `C:/bf-light/Release/BigFix.asi`. Enable Eyeshine once it can be
seen (`cmd togglenightvision`, if needed). Press the capture key (default F12)
**once** while the affected illumination is visible, let all eight PNGs finish,
then quit. No F9 switches or separate bursts are needed. The PNGs must join to
`cap` labels alternating 0,8,0,8,0,8,0,8, with eight consecutive rendered frames.
Require `regionCopies>0` on the variant-8 images. Compare neighboring pairs for
bright flicker, hard vertical cutoffs, retained cast shadows, and Eyeshine's
appearance. A clean log does not establish visual acceptance. Keep files local.

## Validation

Native fixtures use repository-built DLLs and hidden GL windows, never game
modules. Coverage includes real stencil/alpha pixels outside the old rectangle,
RGB/stencil preservation, all draw forms, full-region copy/lighting, engine
state restoration, PopAttrib and list state, full-resolution/baseline no-ops,
variant 7 retention and 8 rollback, A/B order/restoration/failure, idle/writer
budget preservation, and existing shader compilation/config/installer tests.
Release configure/build succeeded in `C:/bf-light` (MSVC, x64, C++17).
CTest passed **76/76** without reference modules and **76/76** with
`RIDDICK_DIR=D:/Games/Riddick`. The final strengthened region/copy/depth
fixtures and complete reference-file suite passed after the last code change.
`python -I -m pytest tools/tests installer/tests -q` passed **114**, with **1
optional skip**: PyYAML is absent for `test_workflow_yaml_parses`. The native
GL fixtures ran without capability skips. `git diff --check` passed.

The emitted ASI is `C:/bf-light/Release/BigFix.asi`, SHA-256
`4644015ff218fdc69f2db56f46dcd2656521ffaf93f026aa58e36d73f462e369`.
No game run, install, push, game file change or game-capture commit was performed.
