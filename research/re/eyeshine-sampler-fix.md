# Eyeshine follow-up: coordinate derivation and packed-depth sampler correction

**Superseded by live evidence:** the user confirmed `EyeshineFix=1` does not
fix the visible bright flicker. Its default is now **off**. See
[the continued investigation and six live probes](eyeshine-variants.md).
The default-on and confirmation instructions below are historical.

2026-10-29; starting revision ec3904c, EU 1.1 x64. Game files were read as
data only. No game process was launched, game DLL loaded, or game file written.
Python used the main repository's `.venv/Scripts/python.exe -I` throughout.

## Finding and confidence

**A concrete defect is the packed-depth texture's inconsistent filtering:**
u6 uses MIN=LINEAR and MAG=NEAREST, and the shader's sixteen depth taps are
half-integer texel offsets. With an approximately one-texel-per-fragment
projection, minification/magnification selection can change the decoded depth
used by nonlinear rejection. The offline model demonstrates the discontinuity;
the implementation makes both filters NEAREST without removing shadows.

Confidence is **high in this sampler discontinuity and the implemented policy**,
but **moderate in it explaining the reported flicker**. The supplied logs do not
contain derivatives, actual texture pixels, or the generated vertex program's
effective constants. They cannot establish which side of the LOD threshold a
particular flickering fragment uses. The OFF path also has nominal unity
derivatives, so the derivation alone does **not** prove Eyeshine-specific
causation. A unique, visually confirmed root cause is still outstanding. This
report must not be represented as proof that the visible bug is resolved.

No frame-alternation metric from the moving demo was used to accept the fix.

## Exact transform and its owner

All addresses below are RVAs in the fingerprints recorded in
`shadow-eyeshine.md`. The shader source is shipped `Gl/VP.xrg`,
`Gl/VPDefines_ARB.xrg`, and
`Gl/NV_Fragment_Program2/XRShader_FP20SS_SpecNormal.fp`.

1. MXR+1B2FDE selects +1B0AA0 for the soft-stencil path. At +1B0C83..94,
   the builder gets `CRC_Viewport*` via the render context's virtual slot +80h.
2. +1B0CA2..CC5 resolves **renderer+E6h**, using the texture context at
   renderer+20h, slot +80h. The temporary `CImage` starts at stack+60h;
   its width and height are at +1Ch and +20h (stack+7Ch/+80h). The additional
   output at stack+50h is **not read by the transform**.
3. +1B0CD7..CF8 gets the drawable width/height through renderer+50h, slot
   +48h, then its returned object's slot +48h. These are the full drawable
   dimensions, not the reduced GL viewport width/height. They cancel
   algebraically in the scale coefficients below, but their division and
   multiplication still round in scalar SSE.
4. +1B0CFC..D19 calls `CRC_Viewport::GetViewArea` (MSystem+529D0) and
   `Update` (MSystem+56C40), reading projection-in-pixels at viewport+1Ch/+20h.
   `GetViewArea` intersects the view rectangle (+44h..50h) with the clipping
   rectangle (+34h..40h). This is a **camera view area**, not an image's last
   copied rectangle.
5. +1B0D1E..E0B computes scales/biases. Constants at MXR+1F8E0C,
   +1F8E80 and +1F4148 are float 1, float -0, and double 0.5 respectively.
6. +1B0E30..EEC combines these coefficients with the model-to-camera matrix
   at draw-record+30h; +1B0F06..F61 transposes the resulting 4x4 block into
   four planes, beginning at the allocated attribute block+20h.

Let `(X,Y,Z)` be the animated model position transformed by that model-to-camera
matrix, `Px,Py` the viewport's updated pixel projection factors, `Vw,Vh` the
**intersected** view-area size, `Dw,Dh` the drawable dimensions and `Tw,Th` the
E6 image allocation. The builder computes (algebraically):

```text
a = float(float(Px * 0.5 / Dw) * float(Dw / Tw)) = Px/(2*Tw)
c = -float(float(Py * 0.5 / Dh) * float(Dh / Th)) = -Py/(2*Th)
b = 0.5 * (-(1 - Vw/Tw)) + 0.5                 = Vw/(2*Tw)
d = 0.5 * (1 - Vh/Th) + 0.5                    = 1 - Vh/(2*Th)

texcoord[5] = (a*X + b*Z, c*Y + d*Z, 0, Z)
smtc0.xy = texcoord[5].xy / texcoord[5].w + (1e-6,1e-6)
PixelUV = (1/Tw,1/Th,0,0)
```

The scales use mixed double/scalar-float arithmetic in the actual instructions;
`engine_coefficients` in `tools/re/eyeshine_mapping.py` preserves that rounding
order, while `project` also exposes the simpler exact algebra. Matrix-dot
rounding, interpolation and the GPU's LOD implementation are not emulated.

These are **linear texgen planes**, not a texture-matrix change. MXR+1AC310
initializes the FP20SS attributes: coordinate 5 mode=1 and component mask=Fh
(+1AC356 and +1AC364). +1AC493/+1AC5A6 installs the allocated attribute pointer
at `CRC_Attributes+58h`. +1AC396/+1AC3A7 bind E6 on u5 and E4 on u6.

With the vertex pipeline enabled, RndrGL's generator (+51EA..520E) selects
`TEXGEN5_LINEAR`. `Gl/VP.xrg` lines 1994..1999 perform four DP4 instructions
against `c[$TEXPARAM5+0..3]`, then lines 2076..2078 move R3 into output texcoord5
when no separate texture matrix is selected. `$TEXPARAM5` is dynamically
allocated by the vertex-program generator; it is not a fixed numeric env slot.
`VPDefines_ARB.xrg` defines `c[]` as vertex `program.env[]`. The compatibility
fixed pipeline instead installs the same S/T/R/Q data with
`glTexGeni(...GL_OBJECT_LINEAR)` and `glTexGenfv(...GL_OBJECT_PLANE)` at
RndrGL+10D06..10DF8. FP `program.env[4]` supplies **only PixelUV**.

## Copy ownership, offsets and simulation

MXR+9177F..17A7 selects image index 0 and index 1 of renderer+248h as E4 and E6.
Both allocations are sized together at +91762..79. Runtime names correlate
E4=u6=5133 (packed depth) and E6=u5=5134 (mask, also later scratch).

RndrGL+34FA0 resolves the destination image's allocation, then takes the source
rectangle and destination point from the **queued arguments**. At +350AD..CA
it converts source top/bottom with drawable height; at +350CD..DD it computes
`dstGLY = imageHeight - dstEngineTop - copyHeight`. +35136 loads destination X.
This function mutates the queued source rectangle during its Y conversion.
It does not save a new rectangle into the image for the transform to read.
Thus the old notes' phrase "stored image rectangle" was misleading.

For **top-left-aligned full camera views resized to these two view areas**,
RndrGL+95A0's camera projection/viewport mapping eliminates X,Y,Z and yields:

```text
texelX = screenX + 0.004096
texelY = screenY + (2048 - 1440) + 0.002048
```

The fractional corrections come from the shader's normalized 1e-6 bias. At
screen pixel centers, removing that bias gives exact corresponding texel
centers. The GL viewport Y=360 is the bottom-left representation of engine
top=0, height=1080; it does **not** require adding another 360 to the transform.

| State | Center screen pixel | Center texel, without bias | u5 copied texels | u6 active copied texels |
|---|---|---|---|---|
| OFF | 1280.5,720.5 | 1280.5,1328.5 | [0,2560) x [608,2048) | same |
| ON | 960.5,900.5 | 960.5,1508.5 | [0,1920) x [968,2048) | [0,1920) x [968,2048) within full copy |

The unchanged u6 full-screen copy starts at texture Y=608. The ON world's
framebuffer region starts at Y=360, so its u6 data starts at **968**, exactly
where the smaller u5 copy starts. Under the resized-camera assumption, there
is no 25% coordinate shrink error, missing offset, or interior copy mismatch.

**Unlogged distinction:** GetViewArea returns an intersection; a GL viewport
alone does not establish the *full* camera rectangle. MSystem+56EAA..56F3D
also computes projection translation from the full-view and clipped centers.
Writing full-view center as `(Cx,Cy)` in engine top-left coordinates gives
the more general mapping (before epsilon):

```text
texelX = screenX - Cx + Vw/2
texelY = screenY + Th - Dh + Cy - Vh/2
```

If the ON camera remains 2560x1440 and is only **clipped** to 1920x1080,
the same center pixel instead maps to `(640.5,1688.5)`: offset `(-320,+180)`
from its expected copied pixel. The simulation includes this counterexample.
The supplied trace does not record full view/clip rectangles or effective
vertex constants, and the exact Eyeshine setter of those rectangles has not
been isolated. Consequently this pass does **not** rule out that transform
error as the primary cause. The default simulation is the resized-camera case,
not a claim that its unlogged camera inputs were observed. This is an explicit
remaining part of the requested static investigation.

The simulator evaluates all sixteen shipped offsets (extrema +/-3.5 texels)
and LINEAR support. Interior footprints stay inside both current copies.
Footprints within roughly four pixels of a view edge can read beyond the
copied mask rectangle. CLAMP_TO_EDGE clamps the **allocation**, not this
rectangle: the ON right edge at X=1920 can see stale scratch up to X=4095,
and the lower edge at Y=968 can see old texels below it. Similar allocation
padding exists OFF. This is a real boundary hazard in the resized model, but does not demonstrate
interior flicker, and this patch deliberately does not change the kernel or
claim to repair that separate issue. Per-light bounded copy regions may need
their own guard band if an edge-only artifact remains.

## Per-frame state and program audit

The builder reads the current model matrix, current viewport projection factors,
current view/clip area, drawable size and E6 allocation. It has no frame counter,
alternation flag, last-copy rectangle, or ScreenFX time input. Model/animation
and camera changes can change plane coefficients and interpolated derivative
rounding even with a still camera. Allocation changes are possible at a mode
or drawable-size change, not established in the eight supplied frames.

The second 5134 copy can replace its contents with scratch **after** lighting.
The next observed pre-lighting copy refreshes the active region; unrefreshed
padding remains suspect only where the kernel reaches it. The transform does
not consume state written by either copy. ScreenFX's observed 5131/5137/249
bindings provide no positive evidence of a u5/u6 alias. Trace omissions still
prevent a claim that no other copies occur.

The SpecNormal FP uses full TEMP for smtc0/smtc1/depth/depthref/dstexel and the
nonlinear rejection calculation. Native SHORT mask temporaries hold mask
values, not the projected coordinates. Our ARB rewrite removes H opcodes and
expands ABS/normalization; it preserves the coordinate division, epsilon,
kernel, `256*r+g` decoder, PixelUV and final shadow multiply. There is no
hardcoded 2560/1920 assumption in this shader. `256*r+g` itself is linear;
filtering before the subsequent ABS/SAT/LRP rejection is the problematic
operation. The mask's LINEAR/LINEAR filtering is intentional soft filtering.

At nominal unity footprint (`rho=1`, lambda=log2(rho)=0), LINEAR/NEAREST depth
changes behavior at the min/mag boundary. Every half-integer offset moves a
texel-centered pixel to approximately a texel **edge**. The simulation puts
synthetic decoded depths 64*256/255 and 192*256/255 on opposite sides of one
edge. At texel coordinate 100.004096, rho just below/above 1 selects decoded
depth about 192.75 versus 129.03. With the shipped tolerance at distance 10,
rejection changes from 0 to 1 without coordinate motion. NEAREST/NEAREST
gives the same rejection on both sides. These are synthetic values, not a
claim about measured game texture contents.

## Implementation and rollback

`EyeshineFix=1` defaults on in settings parsing, generated and shipped INIs.
`EyeshineFlickerWorkaround=0` remains independent and defaults off.

The existing shadow-copy export owner shares three detours with the new
`eyeshine_fix` policy. A successful call return from
`glCopyPixels(...GL_DEPTH_STENCIL_TO_RGBA_NV)` arms a same-thread/context token.
The **next** texture copy consumes it, whether or not it matches. Only a
matching source rectangle, level-0 2D texture, nonzero binding, RGBA8 allocation,
in-bounds destination and observed LINEAR/NEAREST pair get a MIN=NEAREST write.
The NV API has no returned success value; the hook does not drain GL errors or
assert that a driver accepted the packed copy. Unsupported NV copy remains a
separate compatibility limitation. Another CopyTexImage invalidates the token;
a cross-context/mismatching copy cannot change texture parameters. Missing
exports/incomplete hook sets disable the correction with a log entry.

GL names such as 5133 are never hardcoded. The policy does not change the
mask sampler, copy offsets, shaders, pixel contents, active texture unit,
bindings, main-light shadow term, or depth decoder. RndrGL sets texture
parameters in +39300 (upload at +3CD93, bulk refresh at +3962C), rather than
on every material bind; each recognized packed copy can reapply the correction
if the engine refreshed its parameters. Only the first correction is logged.

`EyeshineFix=0` with a restart restores original behavior. No binary on disk
is patched. Existing TraceShadowPass remains bounded to four frames/F11 and
2000 lines/process; existing u6 draw snapshots expose the corrected filters.

## Shortest confirmation attempt, one process, hard stop before 60 seconds

The research worker did **not** run this. After the lead installs this build and
the independent logo-skip work, use `tools/re/eyeshine-confirm.cfg` (mode 6,
2560x1440, character shadows enabled). Set BigFix.ini to:

```ini
[BigFix]
EyeshineFix=1
EyeshineFlickerWorkaround=0
Enable20PlusPlusAnyGpu=1
AntiAliasing=0
TraceShadowPass=1
CapturePng=1
CaptureBurst=8
```

Launch directly; run these commands from this worktree:

```powershell
$cfg = (Resolve-Path tools/re/eyeshine-confirm.cfg).Path
Push-Location D:\Games\Riddick\System\Win64_AMD64
try { & .\SbzEngine.exe -env $cfg -map "Pa1_Intro" } finally { Pop-Location }
```

Use only the opening dock. As soon as a lit guard/character and cast shadow
are visible, keep the camera still. Press F11 then F12 with Eyeshine OFF;
open the console (Ctrl+Alt+~ on the established layout), enter
`cmd togglenightvision`, and close it. Verify activation visually and in the
trace's 1920x1080 viewport, then press F11 then F12 again. Each F12 press saves
eight consecutive PNGs. Look at the scene live as well as the burst: absence
of frame alternation in a metric is not acceptance. Quit immediately after
the second burst; hard stop the attempt at 55 seconds including startup.

Acceptance: log contains `EyeshineFix: packed-depth texture=...`, u6 at actual
SpecNormal draws has filter2600,2600, u5 retains filter2601,2601, the ON viewport
and copy rectangles match this model, main-light cast shadows remain, and the
previously obvious flicker is absent with a still camera. Return the log and
both PNG bursts. Filter2601,2600 surviving at u6 means the policy did not persist
to the consumer, so visual absence alone would not validate this mechanism.

The map name exists and `-map` is statically verified, but this worker cannot
verify its startup timing, playable camera or Eyeshine availability. The action
at GameClasses+1CD061 rejects object+258h bit20 and state nibble=3
(+205910 reads object+180h bits8..11); the static evidence does not guarantee
the dock clears those gates. If activation is rejected, or no qualifying dock
view appears promptly, abort within the same 55-second budget and label the
attempt **inconclusive**. Do not spend another long demo run, infer activation
from the command alone, or report a negative visual result as a pass.

## Reproduce offline proof

```powershell
$py = 'D:\Source\BigFix\.venv\Scripts\python.exe'
& $py -I tools/re/eyeshine_mapping.py
& $py -I -m pytest tools/tests -q
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
$env:RIDDICK_DIR='D:\Games\Riddick'
ctest --test-dir build -C Release --output-on-failure
```

The new integration test first failed on `packed-depth minification policy`
before implementation, then passed on/off/coexist. It exercises actual
MinHook detours against synthetic GL, once-only forwarding, distinct depth/mask
bindings, context mismatch, level/dimension/destination rejection, intervening
CopyTexImage, idempotence and untouched active unit. Unit tests cover recognition
and independent defaults. Python tests cover both copy mappings, SSE coefficient
rounding, all 16 taps, out-of-region edge support, and the LOD rejection failure.
No test executes any game binary, including with RIDDICK_DIR set.

Final verification: Release configure/build succeeded; CTest passed **57/57**
both without and with `RIDDICK_DIR=D:\Games\Riddick`. Python tools/tests
passed **65**, with **1 skipped**. `git diff --check` passed. The initial doctest
checkout hit the known Windows long-path limit; setting `core.longpaths=true`
only in `build/_deps/doctest-src` and restoring that fetched checkout resolved
it, without changing project/global Git settings or any game file.
