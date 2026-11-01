# 02 — Issues with the original 2004 release (OpenGL / Starbreeze engine)

Every issue below assumes patch 1.1 is installed. Paths are relative to `<game>`.
Config files: `<game>\Environment.cfg`, `<game>\Content\User.cfg`, `<game>\Content\Save\<user-id>\_profile`.
Saves: `<game>\Content\Save\` or `%USERPROFILE%\Starbreeze\Riddick\Content\Save\`.
If the game is not run as administrator, writes under Program Files may be redirected to `%LOCALAPPDATA%\VirtualStore` (PCGW, Game data).

---

## A. Startup and stability

### A1. Crash on startup: OpenGL extension string too long
- **Symptom:** the game crashes at launch on modern drivers.
- **Cause:** the modern driver's extension list is longer than the game's fixed buffer ("improper limits for OpenGL extensions", PCGW). TCoRFix's fix is to **"only return extensions the game requires"**, which also fixes the compressed-texture crash on RTX cards ([TCoRFix README](https://github.com/Nezarn/TCoRFix)). [verified]
- **Fixes, best first:**
  1. TCoRFix: needs no renaming and no driver profile edit (PCGW).
  2. NVIDIA only: in NVIDIA Profile Inspector, set *Extension Limit* to `0x00001B58`.
  3. Rename `System\Win64_AMD64\SbzEngine.exe` to `quake3.exe` and run it directly. Drivers ship a built-in extension-limit profile for `quake3.exe` (verified by PCGW user Patrxgt, 2025-03-16).
- A user asked for a "proper fix" for this crash ([#1, Patrxgt](https://github.com/Nezarn/TCoRFix/issues/1)). TCoRFix now provides one.

### A2. `GL_MAX_TEXTURE_UNITS` error on RTX cards
- Fixed by TCoRFix's optional `Fix Misc Bugs` ini setting ([README](https://github.com/Nezarn/TCoRFix)). [verified]

### A3. 2.0++ shader mode crash with "Enable 2.0++ on any GPU" (AMD)
- RX 6750 XT crashed to desktop each time the option was on ([#4](https://github.com/Nezarn/TCoRFix/issues/4), closed).
  v1.4 notes "Fix a typo (fixes crash when only using certain settings in the fix)". [verified: closed after the v1.4 fix]

### A4. Fix fails to load, or the game fails on Windows XP
- XP 32-bit with TCoRFix 1.4 fails: "Entry Point Not Found: AcquireSRWLockExclusive … KERNEL32.dll". The fix is built against Vista+ APIs ([#6](https://github.com/Nezarn/TCoRFix/issues/6), open). The author cannot build or test for XP.
  This matters to players who keep XP for **hardware EAX** (Creative X-Fi). [verified: issue]
- "Doesn't work at all": the system `C:\Windows\winmm.dll` loads instead of the ASI loader ([#9](https://github.com/Nezarn/TCoRFix/issues/9), open).
  Another user could not reproduce it. The likely cause is the wrong loader build or folder. [reported]

---

## B. Resolution, aspect ratio and display

### B1. Resolutions with either side above 2048 px render wrongly
- **Symptom:** above 2048 px wide (1440p, 4K), only the top-left part of the image is drawn and the rest falls outside the screen,
  or the 3D view is distorted or zoomed (PCGW; [Steam thread, 2017–2023](https://steamcommunity.com/app/9860/discussions/0/1456202492176141984)).
  The exact limit is **any axis > 2048**, so 2048×1152 works on 16:9 ([#1, Manu270891, 2025-03-16](https://github.com/Nezarn/TCoRFix/issues/1)). [verified]
- **Fix:** TCoRFix v1.3+ with `Fix Texture Size=1` (v1.3 release notes). [verified]

### B2. Fullscreen will not accept the native high resolution, even with TCoRFix
- Windows 11 25H2, NVIDIA 581.57: `VID_MODE=2560 1440 32 120` is ignored in fullscreen. The mode is listed in the menu, but *Apply* does nothing.
  Windowed mode with `VID_MODE=desktop` / `VID_DWIDTH=2560` / `VID_DHEIGHT=1440` works ([#7](https://github.com/Nezarn/TCoRFix/issues/7), open, 2025-12 to 2026-08). [reported, by several users]
- Repacks show the same symptom ([#1 comments, 2025-09 and 2026-07](https://github.com/Nezarn/TCoRFix/issues/1)).
- **Workarounds:**
  - Add a *detailed* custom resolution in **CRU** (Custom Resolution Utility). This worked for the original reporter.
  - Use **SRE** (from the same author) to delete most of the listed modes. This worked for another user when CRU did not. Turning off FreeSync also exposed more modes.
- **Probable cause:** the game enumerates or filters display modes very strictly, probably through an old Win32 call or a fixed-size mode table. This is not confirmed; it is an inference from the CRU/SRE results.

### B2a. Observation on our test rig: `VID_MODE` from the config was ignored, but the in-game menu worked
- **2026-10-29**, EU 1.1, x64 + TCoRFix 1.4 (all four options on), RTX 3090, 2560×1440 @ 180 Hz monitor.
  Windows offers 2560×1440 at 48/59/60/75/100/119/143/155/159/164/179 Hz.
- `Environment.cfg` was set to `VID_MODE=2560 1440 32 60`, `VID_DWIDTH=2560`, `VID_DHEIGHT=1440`, but the game started at **1600×900**.
  Choosing 1440p in the in-game video menu worked, and the game then saved `VID_MODE=2560 1440 32 180`. [verified: observed; cause unknown]
- On the next launch, with the saved `VID_MODE=2560 1440 32 180`, it **again started at 1600×900 @ 119/120 Hz** (display log, 11:21:13–11:21:55) and overwrote the config with `VID_MODE=1600 900 32 120`.
  The desktop was back at 2560×1440 @ 179 only because the user alt-tabbed out. So the game rejects 1440p at startup whatever the refresh rate.
- **Root cause: the game stores only the first 512 display modes.** [verified statically, 2026-10-29]
  `RndrGL.dll` (EU 1.1 x64) calls `EnumDisplaySettingsA` at RVA `0x4C4AF` with indices 0–511 only. `MSystem.dll` at `+0x146B30` picks the nearest eligible mode by weighted width/height/refresh difference. With no eligible match it falls back to mode 0. Details are in `research/re/display-and-window.md`.
  The supporting observations:
  - `EnumDisplaySettingsW` on this machine returns **803** modes, all 32 bpp: 25 resolutions × 11 refresh rates × 3 duplicate entries each.
  - In Windows' order, 2560×1440 sits at indices 726–736, and indices 495–527 are 1600×900.
  - Index 511, the 512th mode, falls inside the 1600×900 block. The highest refresh among 1600×900's entries before index 512 is **120 Hz**. That matches the fallback exactly: the largest resolution, at its highest refresh, in the first 512 entries.
  - It also explains why users fix this with SRE by deleting most modes, and with CRU by adding a detailed resolution, which changes the order ([TCoRFix #7](https://github.com/Nezarn/TCoRFix/issues/7)). And it explains why it barely shows on older or 60 Hz monitors, which have far fewer modes.
  - The in-game menu still lists and applies 1440p, so the menu evidently builds its list another way.
- **Fix idea:** in our plugin, hook `EnumDisplaySettingsA/W` and return a short, de-duplicated list (unique resolutions, each with the desktop refresh rate plus 60 Hz), with the native mode guaranteed to be included. No system-wide CRU/SRE edits would then be needed.
  To confirm: find the 512 limit in `SbzEngine.exe`/`MSystem.dll` (a loop over `EnumDisplaySettings` bounded by `0x200`), or test with a hook that caps the list.

### B2b. First run switches the desktop to 640×480 and stays there while the game is minimised
- **Observed on 2026-10-29** (EU 1.1, x64 exe with TCoRFix 1.4, RTX 3090, Windows 11).
  On first launch the game writes `VID_MODE=desktop`, `VID_DWIDTH=640`, `VID_DHEIGHT=480`. It then switches the display to **640×480 @ 75 Hz** in exclusive fullscreen.
  It started without foreground focus, because it was launched from a background process, so it minimised at once (window at -32000,-32000). The desktop stayed at 640×480 until the process ended.
  Windows restored the original mode when the process was killed. [verified: observed]
- **For our fix:** write a sensible `Environment.cfg` at install time (native resolution), and do not leave a minimised exclusive-fullscreen game holding the display mode. Borderless windowed avoids this altogether.

### B3. HUD and subtitles stretched in widescreen
- In widescreen and multi-monitor setups, the HUD is stretched from 4:3 (PCGW; requested in [#1](https://github.com/Nezarn/TCoRFix/issues/1)). **No fix exists.** [verified]

### B4. No FOV option
- PCGW lists FOV as unsupported. A user asked for "FOV without breaking the viewmodels" ([#1](https://github.com/Nezarn/TCoRFix/issues/1)). **No fix exists.** [verified]

### B5. No borderless windowed mode
- Windowed mode exists, borderless does not (PCGW; requested in #1). **No dedicated fix.** [verified]

### B6. Anti-aliasing works only through the console, and not on AMD
- Add `GL_ADVPIXELFORMAT=1` to `Environment.cfg`. Then in the console (`Ctrl+Alt+~`) enter `vmultisampletaps 2|4|8`,
  or add `-exec "vmultisampletaps x"` to the command line (PCGW). [verified]
- Values above 8 give a broken image, and 8× looks no better than 4× ([#1, vrubayka](https://github.com/Nezarn/TCoRFix/issues/1)). [reported]
- Does **not work on AMD** (tested on an RX 5700 XT). Use SSAA or downsampling instead (PCGW). [verified]
- A request to make AA persistent and global ([#1](https://github.com/Nezarn/TCoRFix/issues/1)) is not done.

---

## C. Rendering bugs

### C1. Shader mode 2.0++ was NVIDIA-only
- 2.0++ uses an NV-specific occlusion query. TCoRFix v1.2 replaces it with the ARB equivalent, so 2.0++ works on **any GPU, including Mesa on Linux** (`Enable 2.0++ on any GPU`) (v1.2 notes). [verified]

### C2. Eyeshine causes texture flickering in 2.0++
- In 2.0++, using Eyeshine in lit areas makes textures flicker. Reproduced on a GTX 1060 at the Showers checkpoint and on an RTX 3060 laptop ([#1](https://github.com/Nezarn/TCoRFix/issues/1), [#2](https://github.com/Nezarn/TCoRFix/issues/2)). AMD cards need the fix too ([#1, 2025-09-04](https://github.com/Nezarn/TCoRFix/issues/1)). [verified]
- TCoRFix's flicker workaround **makes some shadows disappear**. For example, at the start of a new game, Riddick, Johns and two inmates cast no shadows (PCGW; [#1, GiladN1, 2025-04-19](https://github.com/Nezarn/TCoRFix/issues/1)). **The fix trades one bug for another.** [verified]

- **Reproduced on our rig on 2026-10-29** (EU 1.1, x64 + TCoRFix 1.4 with `Eyeshine Flicker Workaround=1`, RTX 3090, 1600×900), using the game's own F12 screenshots in the opening dock scene:
  - Mode 5 (2.0): Johns and both inmates cast dark, sharp shadows towards the lower left, and Riddick's own body shadow is clearly visible on the floor (Screen01, Screen02).
  - Mode 6 (2.0++) with the workaround on: **all the character shadows from the main light are gone**, including Riddick's own. The soft environment shadows (railings, posts) are still there. The faint shading the tester read as "shadows in a different direction" comes from another light (Screen00, Screen03).
  - The shader mode can be switched in the in-game menu without restarting; the screenshots were taken in one session.
  - **Still to check:** mode 6 with the workaround off, to confirm the workaround removes the shadows and the soft-shadow path does not lose them on its own.

### C3. Bad lighting at the final checkpoint in 2.0++
- Checkpoint "38 Take Off Platform" has wrong lighting in 2.0++, whether or not the flicker fix is on ([#3](https://github.com/Nezarn/TCoRFix/issues/3), closed by v1.2's modified workaround). [verified]

### C4. Heavy bloom in the ship cutscene on the 64-bit exe
- The ship is washed out by bloom in the stepping/ship sequence. This happens only with the **64-bit** exe. The 32-bit (SSE2) exe renders it correctly, as on Xbox (PCGW citation [2]; [#1, danonina, 2025-03 and gittybittycomboe, 2025-09](https://github.com/Nezarn/TCoRFix/issues/1)). [verified]
- Conflict: players use the 64-bit exe to avoid SecuROM, but it has this visual bug.

---

## D. Input

### D1. Mouse twitches above 125 Hz polling
- Mice polling faster than 125 Hz cause twitchy movement (verified by PCGW user Patrxgt). [verified]
- **Fixes:**
  - Set the mouse to 125 Hz.
  - Use **DxWrapper** in `System\Win32_x86_SSE2` (`dxwrapper.dll`, `dxwrapper.ini`, `winmm.dll`) with
    `[Compatibility] Dinputto8=1, EnableDinput8Wrapper=1` and `[dinput8] FixHighFrequencyMouse=1`.
    `[Plugins] LoadPlugins=1` lets it load TCoRFix in place of Ultimate ASI Loader.
    **DxWrapper crashes on the 64-bit exe** (PCGW; [#1, apoketo](https://github.com/Nezarn/TCoRFix/issues/1)).
  - TCoRFix v1.4 notes "Fix high DPI ultra fast mouse issue", and [#5](https://github.com/Nezarn/TCoRFix/issues/5) is closed. PCGW still recommends DxWrapper or 125 Hz.
- Patch 1.1 fixed a separate bug with weapon switching on the mouse wheel.

### D2. Mouse acceleration
- PCGW lists "minor mouse acceleration" with no off switch. Users asked for raw input ([#1](https://github.com/Nezarn/TCoRFix/issues/1)). **No fix exists.** [verified]

### D3. Gamepad off by default
- Since patch 1.1 the gamepad is disabled by default and must be turned on in the options (1.1 changelog). PCGW lists full controller support once it is on. [verified]

---

## E. Audio

- **EAX 4.0** is supported. Patch 1.1 disables it by default because of driver problems (changelog). Modern Windows has no hardware EAX path without a Creative card or a wrapper (ALchemy/OpenAL-based); this is general knowledge and is not specific to this game.
  This is why some players stay on XP ([#6](https://github.com/Nezarn/TCoRFix/issues/6)).
- **The game mutes when it loses focus**, and there is no option to turn this off (PCGW). [verified]

---

## E2. Install and patch

### E2.1. The installer saves the install path in the registry without its last character, so patch 1.1 fails
- **Observed on 2026-10-29** with the EU single-DVD installer on Windows 11, installing to `D:\Games\Riddick`.
  The installer copied the files to the right folder, but it wrote `InstallPath = D:\Games\Riddic\` under
  `HKLM\SOFTWARE\WOW6432Node\Starbreeze\The Chronicles of Riddick: Escape From Butcher Bay`.
  The uninstall key `{A8DE8C34-7F51-4cc8-B326-C425793EE741}` got the same truncated `InstallLocation`, `UninstallString` and `DisplayIcon`. [verified: observed]
- **Effect:** the EU 1.0→1.1 updater (`riddick_update_eu_10_11.exe`) reads `InstallPath` and stops at the first file with "Patch Error: Old File does not exist".
- **Fix:** write the correct path, ending in `\`, to those four values. This needs admin rights. The trigger is not yet known; it may happen when the typed path has no trailing backslash.
- **For our installer:** check `InstallPath` against the real game folder, and repair it before running the patch.

## F. Quality-of-life

- **Skip intro videos:** delete or rename `Content\Videos\` `Intro.ogg`, `Sbz.ogg`, `Tigon.ogg`, `universal_ntsc.ogg`, `vug_new.ogg`, `logo_mixed.ogg` (PCGW, citation needed).
- **Frame rate:** no cap; works at 60 and 120+ FPS (PCGW). No high-FPS physics bug is documented for the *original* (see 03 for the one in *Dark Athena*).
- **Linux / Proton:** TCoRFix works with `WINEDLLOVERRIDES="winmm.dll=n" %command%` ([#1, 2025-03-21](https://github.com/Nezarn/TCoRFix/issues/1)). [reported, confirmed by the user who reported it]

## Appendix: BigFix v0.1 milestone run, 2026-10-29 13:36

The first in-game run of BigFix (`tools/smoke_run.ps1`, about 60 s, intro and main menu only) used EU 1.1 x64, an RTX 3090 (driver 616.92) and Windows 11 build 26300, on a 2560×1440 @ 180 Hz desktop. TCoRFix was moved aside for the run.

**Passed (6 of 8 checks):**
- the plugin loaded through Ultimate ASI Loader
- `build=EU11_x64`
- `texture_size_clamp: Applied`
- borderless active, with the window at (0,0)-(2560,1440)
- **first frame presented at 2560×1440**
- **desktop mode unchanged before, during and after**

**What the log showed:**
- The extension list was filtered to 619 bytes, and the game started without the old crash.
- The game asked for 640×480 twice (first-run default and video playback). Both requests were turned into 2560×1440, and the 640×480 display-mode change was suppressed.

**Failed checks:**
- `modes_filtered`: in borderless the game never enumerates through our hook, because selection is forced instead. The smoke check is being adjusted.
- `capture_png`: neither the plugin's PNG nor the game's own TGA appeared, so the scripted F12 most likely never reached the foreground window. Diagnostics are being added.

Compare with TCoRFix 1.4 on the same rig (§B2a): there the game started at 1600×900 and could not start at 1440p.

### Runs 2–4, 2026-10-29 15:12–15:30
- **Run 2:** the TraceVideo instrumentation captured the geometry of every logo video and the intro. The game reads the Theora aspect field as display aspect ×1000: 1333 for the 640×480 files and 1481 for universal_ntsc.
- **Run 3:** after the `VideoAspectFix` and smoke fixes, **10/10 checks PASS**, including `capture_png` (2560×1440) and `capture_foreground`. The trace shows `aspect correction dst=80,0,560,480` plus a black clear before each video draw.
- **Run 4:** three F12 PNG captures during the logo videos show the 4:3 videos pillarboxed and centred, with black bars of about 320 px each side at 2560×1440 and no stretching.

## Appendix: BigFix v0.2 in-game checks, 2026-10-29 16:52–17:01
EU 1.1 x64, RTX 3090, 2560×1440 @ 180 Hz. Every run kept the v0.1 checks green: 10/10, or 11/11 with the mouse burst.

- **Raw mouse** (`RawMouse=1`, `TraceInput=1`, `-MouseBurst`): 2000/2000 `SendInput` moves arrived as raw packets, DirectInput reported `overflows=0`, and the cumulative applied motion returned to exactly `0,0` after the circle and its reverse. Nothing was dropped and nothing drifted.
- **MSAA** (`AntiAliasing=4`):
  - The first attempt failed. The game requests a PFD with depth 32, and `wglChoosePixelFormatARB` found no format even at 0×.
  - After building the ARB attributes from the effective format (`DescribePixelFormat`), the renderer got format 44 with `sampleBuffers=1 samples=4`, stencil kept.
  - Not yet checked visually on 3D geometry: the menu background is a video.
- **Frame cap** (`FrameCap=60`): 16.67 ms average frame time in the menu; 1% low 20 ms after the first window.
- **Background audio** (`BackgroundAudio=1`): `DirectSoundCreate8` and `CreateSoundBuffer` are hooked. The primary buffer flags are unchanged (0x11), and the secondary buffers go 0x500e0 → 0x580e0 (`GLOBALFOCUS`). Whether sound is actually heard while alt-tabbed is not yet confirmed by listening.
