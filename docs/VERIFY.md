# Volunteer verification (about 10–15 minutes)

We have one development PC: Windows 11 / RTX 3090. AMD, Intel, Wine,
Proton and Steam Deck need real hardware reports. See [COMPAT.md](COMPAT.md)
for the evidence so far. A working hook or passing unit test is not a visual pass.
No purchase, game-file upload, or later-level playthrough is needed to report.

## Prepare

1. Use your own original 2004 game, patched to 1.1, and launch
   `System/Win64_AMD64/SbzEngine.exe`. The reference build is EU 1.1 x64;
   report other builds separately. Install using the [README](../README.md).
2. Back up `BigFix.ini`, `Environment.cfg` and your saves outside the repository.
   Record BigFix version, GPU/model/driver, OS, resolution/refresh, mouse polling
   rate, and any Wine/Proton/SteamOS version. On Deck include LCD/OLED, Gaming
   or Desktop Mode, and whether an external mouse or Steam Input is in use.
   Disable other graphics mods and forced driver AA for a baseline, or list them.
3. Keep the initial defaults (`WindowMode=borderless`, `HudAspectFix=1`,
   `RawMouse=1`, `FOV=0`, `AntiAliasing=0`). Restart after editing the ini.
   Each launch rotates `BigFix.log` to `BigFix.prev.log`: copy both to a named
   folder **before** another launch. Logs live beside the plugin, not in the
   unpacked release folder. No log means loading failed; report that result too.

## Short test script

Use a separate test profile if using console commands; direct map loads may
bypass campaign setup. The menu and opening dock are enough for most checks.

| Check | Action and pass condition |
|---|---|
| Starts | Launch, reach the menu, then New Game/opening dock. No crash; a fresh log appears. |
| Native resolution | Compare `First frame presented: WIDTHxHEIGHT` with your chosen desktop mode; check a PNG's dimensions too. World fills the screen with no top-left-only rendering above 2048 px. On Deck expect 1280×800 on the internal panel. |
| Borderless | Alt-tab away and back. No desktop resolution switch, title bar, cut-off edges or lost input. Record behavior in Deck Gaming Mode separately. |
| 2.0++ / Eyeshine | Select **2.0++** in Video options (shader mode 6; installer starts at 2.0/mode 5). Keep cast shadows enabled, `EyeshineFix=1`, `EyeshineFlickerWorkaround=0`, `EyeshineFixVariant=0`, `EyeshineAbBurst=0`, `TraceShadowPass=0`. At the opening dock, watch character shadows and lit edges while standing still and moving, with Eyeshine off and on. Require both retained shadows and no bright flicker or hard illumination cutoff. |
| Ship bloom | Start a normal New Game and watch the opening ship sequence, without skipping it. Record shader mode and whether bright areas wash out faces or the scene. This is a known unresolved x64 defect; report fail or untested honestly. A dock-only run or a clean log cannot pass this check. |
| AA | From the same dock viewpoint, compare `AntiAliasing=0` with `AntiAliasing=4`, restarting between runs. Compare diagonal edges, shadows and Eyeshine; record requested, selected and actual samples plus fallback lines. A lower sample count or no AA is a fallback result, not a pass at 4×. There is no built-in supersampling fallback. |
| HUD / subtitles | At widescreen, health/HUD shapes and subtitles should fit a centred 4:3 region; the world stays full width. At 2560×1440 that region is 1920 px wide, with 320 px per side. Check subtitle shadows/clipping and menu edges; report any strips or misaligned controls. |
| FOV / hands | At the same dock viewpoint compare `FOV=0` with `FOV=100`, restarting each time. World view should widen from the ordinary 90° authored camera, relative zoom should work, and dialogue cameras should retain their authored view. Inspect hands/weapons for size, perspective and clipping. Hands are coupled to camera FOV; an acceptable world view alone cannot pass this item. Restore `FOV=0` if objectionable. |
| Raw mouse | Use an actual mouse if possible: slow/fast sweeps, buttons and wheel. No dropped movement or jump on alt-tab return. Compare `RawMouse=0` if it fails. Deck stick/trackpad input alone does not establish high-polling raw-mouse support; mark that portion untested. |
| Audio | Check dialogue/effects/music. Optionally restart with `BackgroundAudio=1` and alt-tab: audio should continue. Restore default `0` afterward. Do not infer EAX support from basic sound working. |

Optional scene commands: open the console with **Ctrl+Alt+~** (keyboard layout
may affect the key), enter `map "Pa1_Intro"` for the opening candidate or
`map "Pa1_Arrival"` for the dock candidate. Use `cmd togglenightvision` to
toggle Eyeshine for a controlled test if normal progression has not enabled it.
These command handlers are established by static research; reaching the exact
ship bloom defect through a direct map is not confirmed. Say what scene actually
loaded, or use normal New Game instead. Do not count an unavailable effect as a pass.
Avoid later-level demos for this short test; label any later-level evidence as spoilers.

F12 saves PNGs in `<game>/ScreenShots`. For flicker, a short recording is better
than a single still; `CaptureBurst=8` can also capture consecutive rendered
frames. Remove that diagnostic key after testing. Keep comparisons at the same
viewpoint and label each image with settings.

## Log evidence to include

Attach the **full** `BigFix.log` and comparison `BigFix.prev.log` after reviewing
personal paths (`exe=` can contain your username). Preserve hashes and messages.
These are actual message prefixes from `src/`; include failures/skips as well as successes:

- `BigFix `, `exe=`, `GL context: vendor=`, `settings:` and fingerprint/SHA-256 lines.
- `Display modes:`, `Borderless render selection:`, `Borderless window style:`,
  `First frame presented:` — distinguish selected mode from actual viewport.
- `EyeshineFix: whole reduced region stencil/alpha/copy/lighting ready` and
  `EyeshineFix applied: full Eyeshine region stencil work and observed alpha conversion, expanded mask copy and lighting; engine state restored`.
  Copy `EyeshineFix skipped:` or `EyeshineFix whole-region copy skipped:` if present.
  “Ready” alone does not prove the fix was applied or that it looks correct.
- `MSAA requested=`, `MSAA context requested=`, `MSAA request `,
  `MSAA ARB formats exhausted; fallback to game chooser without sample attributes`.
  The request failure line can say `fallback selected=…`; keep the full line.
  `TracePixelFormat=1` adds `PixelFormat chosen ARB`, `PixelFormat SetPixelFormat`
  and `PixelFormat context` details for a short follow-up run.
- `settings: HudAspectFix=`, `Widescreen `, and, with `TraceFov=1`,
  `TraceFov camera` / `TraceFov upload`. Record `hands=unresolved` as a limitation.
- `RawMouse mouse_poll RVA=9BAC0: verified prologue; hook created`,
  `RawMouse foreground relative input registered; existing WndProc chained`,
  any `RawMouse … skipped` / `unavailable`, and `Audio:` / `BackgroundAudio:`.
  Default `Audio: BackgroundAudio=0 TraceAudio=0; no DirectSound hooks` is expected.

To extract a useful summary (full logs are still needed):

```powershell
Select-String -Path BigFix.log -Pattern 'BigFix |GL context:|settings:|SHA|Display modes:|Borderless |First frame|EyeshineFix|MSAA|PixelFormat|Widescreen |TraceFov|RawMouse|Audio:|BackgroundAudio|skipped|mismatch'
```

```sh
grep -Ei 'BigFix |GL context:|settings:|sha|Display modes:|Borderless |First frame|EyeshineFix|MSAA|PixelFormat|Widescreen |TraceFov|RawMouse|Audio:|BackgroundAudio|skipped|mismatch' BigFix.log
```

For bloom follow-up, `TraceBloom=1` records diagnostic state only; it applies no
bloom correction. Enable verbose traces only when requested, then restore them.
Open a **Hardware verification report** issue with pass/fail/untested for every
item, the settings, scene, logs, and optionally screenshots. Never upload game
executables, DLLs, saves, disc images, cracks or the official patch.
