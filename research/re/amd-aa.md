# AMD/Intel/Mesa colour and anti-aliasing compatibility

2026-10-30. Baseline: `bigfix-v0.3`, `f87e3c3`. Static investigation and
game-free tests only. AMD, Intel and Mesa hardware/driver behaviour is **not
verified**. The available Windows test GPU is an NVIDIA RTX 3090. No game was
launched for this work.

## Why the colour remap exists

`git blame`/`git log -- src/gl_hooks.cpp` traces the replacement to BigFix
commit `75af119` (2026-10-29), importing TCoRFix v1.4. It was not introduced by
the Eyeshine investigation or MSAA work. Upstream introduced it in
[TCoRFix v1.2, commit 321fe9f](https://github.com/Nezarn/TCoRFix/commit/321fe9f85a23ad07a217ddfdc13afd5013307a9b),
alongside the any-GPU/Mesa 2.0++ path, NV-to-ARB occlusion adapters and fragment
program rewrites. The hook unconditionally substitutes generic input 3 for
`glColor4f` whenever that option is enabled. The original conventional function
is saved but never called. Upstream supplies no narrower per-program rationale
or evidence that every colour call is a vertex-program draw.

The reason for supplying generic input 3 is the NV primary-colour attribute
mapping: code that expects the NV colour/generic alias must explicitly feed
the generic input on drivers with separate attribute storage. Removing this
feed outright would restore the original non-NVIDIA compatibility problem.
[ARB_vertex_program, issue 5 and Table X.1](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_vertex_program.txt)
permits aliasing, whereas NV_vertex_program requires it. ARB also leaves the
corresponding other attribute undefined after a write, so ARB alone does not
guarantee that a generic write updates current conventional colour. Fixed
function rendering, `vertex.color` inputs, colour material and raster colour
must not rely on NVIDIA's alias.

The repair calls conventional `glColor4f` and then mirrors the same unmodified
four floats into generic input 3. This covers the separate-storage model used
by non-aliasing drivers and the shared-storage NV model, while preserving the
previous generic write last. It does not clamp values, inspect program source
per colour, or query GL state inside `glBegin`/`glEnd`. When any-GPU mode is
off, only the conventional call is made. Entry-point resolution still precedes
either write, allowing the existing export wrapper to forward the conventional
call once if generic dispatch is unavailable.

Tests exercise both storage models, including negative/>1 components, early
colour calls and calls within Begin/End. They are not proof of every permitted
ARB implementation: its cross-attribute undefined-state wording still requires
real driver validation. Programs may not legally consume both potentially
aliased inputs in the same ARB vertex program. This change preserves the
existing explicit-generic strategy rather than inventing shader-use tracking.

## Pixel-format failures: concrete defects versus hypotheses

The renderer's AA console path and RVAs are documented in
[v0.2 input/timing/audio/AA](v0.2-input-timing-audio-aa.md#settings-wgl-attributes-and-restart--high-confidence).
It needs a new window/context format before the first real `SetPixelFormat`;
enabling `GL_MULTISAMPLE` alone cannot allocate samples. BigFix already bypasses
the engine's fragile nominal-16-sample/`glSampleCoverageARB` gate through a
disposable WGL context and renderer IAT hooks. It already retries 8/4/2/none.

1. **RGB versus RGBA bit count (fixed).** The recorded effective PFD has
   `cColorBits=32`, separate RGB8 components and alpha8. Copying 32 into
   `WGL_COLOR_BITS_ARB` asks for at least **32 RGB bits**, excluding alpha,
   according to [WGL_ARB_pixel_format](https://registry.khronos.org/OpenGL/extensions/ARB/WGL_ARB_pixel_format.txt).
   NVIDIA tolerates/reports this combination on the test machine; a driver
   offering RGB24+A8 can reject it. Translation now uses the effective RGB
   component sum and keeps alpha separate. The original best-match PFD is
   still not treated as an exact ARB request.
2. **Depth/accumulation combinations (fallback added).** A driver can expose
   32-bit depth or accumulation on legacy formats without offering the same
   combination with MSAA. At each requested sample count BigFix first tries
   the original constraints, then removes accumulation-size requirements and
   caps the requested depth minimum at 24, the known working game precision.
   Legacy translation already omitted accumulation; advanced ARB requests now
   get the same fallback. Alpha, stencil, RGB components, double buffering,
   stereo, acceleration, auxiliary buffers and unknown constraints survive.
   The renderer's usual depth24/stencil8 requirement is retained. Depth/stencil
   sizes are **minimums**, not exact-match WGL attributes; 24/8 is not inherently
   an AMD-incompatible request.
3. **Unsupported sample tokens/counts.** Unsupported WGL extension attributes
   may fail the chooser. The existing final no-AA retry removes sample tokens
   from both integer and float lists. Counts still follow 8/4/2/none without
   exceeding the requested count. Queried sample counts must match the attempt;
   a chooser's TRUE/zero formats is not success. Unrelated float constraints
   are deliberately retained, including depth/accumulation if supplied there;
   the new relaxation applies to integer constraints, as used by the engine.
4. **Driver returns an unsuitable candidate (guard added).** Query the candidate
   before accepting it: sample buffers/count, RGB/alpha sizes, depth, stencil
   and requested double-buffer state must match the attempt. A query failure,
   wrong sample count or insufficient framebuffer is counted separately in the
   rejection log. Never trade away stencil shadows to obtain AA.

Every selection logs index, sample count, RGB/alpha/depth/stencil sizes and
whether original or compatible constraints succeeded. Exhaustion and reduced
AA explicitly say the request could not be honoured. Real renderer context
bindings also report actual samples, even with `TracePixelFormat=0`, without
logging every repeated bind of the same context. A failed real `SetPixelFormat`
is logged; it is never retried on that window. `AntiAliasing=0` retains the
game's selection. These changes do **not** identify the reported AMD failure
as a confirmed format-selection bug.

## Rendering can fail after format selection succeeds

- **Packed depth/stencil copy:** any-GPU mode advertises
  `GL_NV_copy_depth_to_color` even when absent from the driver's real extension
  string. BigFix does not emulate it. The 2.0++ pipeline uses
  `glCopyPixels(..., GL_DEPTH_STENCIL_TO_RGBA_NV)` (`0x886e`) before copying
  packed colour into textures; see [Eyeshine findings](eyeshine-variants.md).
  An implementation without that operation can reject the enum independently
  of AA. [NV_copy_depth_to_color](https://registry.khronos.org/OpenGL/extensions/NV/NV_copy_depth_to_color.txt)
  requires depth and stencil and describes the packing/pixel-transfer steps;
  it does **not** specify a blanket prohibition on a multisampled default
  framebuffer. We cannot claim MSAA is the cause merely from this call. Sample
  selection/packing differences remain a plausible rendering issue to measure.
- **Copies from the default back buffer:**
  [ARB_multisample](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_multisample.txt)
  defines combined colour values for default-buffer reads. It permits
  implementation-dependent depth/stencil sample reduction, recommending the
  centermost sample. Therefore a colour `glCopyTexSubImage2D` from a multisampled
  **default** back buffer is not inherently invalid. The familiar
  `INVALID_OPERATION` rule for a multisampled **FBO** in
  [ARB_framebuffer_object](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_framebuffer_object.txt)
  explicitly requires nonzero `READ_FRAMEBUFFER_BINDING`. Applying it to the
  engine's default-buffer copies would be an incorrect diagnosis. Depth/stencil
  edge semantics can nevertheless differ from NVIDIA and affect packed masks.

A successful chooser/context log proves sample allocation, not AA image
quality or valid NV packed copies. An AMD/Intel/Mesa follow-up needs real
extension strings, chosen format/context logs, GL errors around these copies
and image comparisons in ordinary rendering and 2.0++/Eyeshine. Do not consume
the game's GL error state in production just to infer a fallback.

## Supersampling feasibility and recommendation

**Feasible as a separate renderer feature; not small or safe as an automatic
fallback here. No supersampling code was added.** The engine draws into the
window's default back buffer. Increasing `glViewport` does not enlarge that
drawable. Enlarging the real window breaks the borderless/window/input policy
and is subject to desktop clipping; shrinking it again loses the larger buffer.

A proper in-plugin path needs a larger offscreen colour/depth/stencil target,
engine render-size integration, and a downsample/present pass. It must redirect
default reads and writes, translate `GL_BACK`/`GL_FRONT` draw/read selections
for FBO attachments, preserve packed depth/stencil copies and mask coordinates,
check texture-size limits, handle all viewport/scissor/copy/readback paths and
restore legacy fixed-function/ARB/NV program state. Even single-sample SSAA
does not supply a missing NV packed-copy implementation. Binding an FBO only
at SwapBuffers is too late; it cannot recover scene samples that were never
rendered. Driver VSR/DSR or a higher-resolution display mode plus external
downscaling is an opt-in user workaround, not a portable plugin fallback.

Recommendation: ship conservative MSAA selection and honest diagnostics;
retain the original no-AA path on failure, without a vendor blacklist. Obtain
an AMD trace distinguishing allocation failure from packed-copy/rendering
failure. Address missing NV semantics at their responsible layer. If SSAA is
still needed, implement an explicit opt-in render-scale feature with separate
acceptance tests for masks, HUD/videos, captures, resizing and context recreation.

## Verification scope

Regression tests use the existing injected GL API and fake GL DLL; the latter
checks independent and NV-style shared colour storage. Native hidden-window
MSAA harnesses use the host driver and restricted chooser/query wrappers for
RGB24-only selection, depth24/no-accumulation fallback, missing stencil, reduced
samples, missing WGL functions/extensions, context recreation, bootstrap
restoration and diagnostics with tracing disabled. These fixtures simulate
constraints and errors; they do not simulate an AMD renderer or certify AMD AA.
Reference-binary tests skip if `RIDDICK_DIR` is unset. No game files are changed
or included in this patch.
