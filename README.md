<img src="assets/icon.svg" width="64" height="64" alt="">

# Butcher Bay BigFix

BigFix is an open-source mod for the original 2004 PC release of *The Chronicles of Riddick: Escape from Butcher Bay*. It fixes modern-driver startup crashes, resolutions above 2048 pixels and display-mode selection, adds native-resolution borderless play, raw mouse input and configurable MSAA, and logs every applied or skipped patch.

BigFix does not include any game files. You need your own copy of the game.

**This is for the 2004 PC release only.** *Assault on Dark Athena* (2009) is a different engine and is not supported.

**Development build: EU 1.1 x64 is the reference.** Windows 11 / RTX 3090 is the only development setup. Linux/Wine/Proton/Steam Deck are targets awaiting volunteer verification. Eyeshine, bloom and widescreen/FOV still have [known limitations](#known-issues); this is not a claim that they are all fixed.

## Quick install

### Windows — three steps

1. Install your own retail game and its official **1.1 patch**. On Windows 10/11, run the patch in Windows XP SP3 compatibility mode. BigFix includes no game files or patch.
2. Download and unpack the BigFix release zip outside the game folder. Open PowerShell there and run `powershell -ExecutionPolicy Bypass -File .\install.ps1` (or add `-GameDir "D:\Games\Riddick"`).
3. Launch the installed Start-menu shortcut for the x64 game. Check `System\Win64_AMD64\BigFix.log`; edit the ini beside it for optional settings, then restart.

### Linux / Proton / Steam Deck — three steps

1. Install your own game and official **1.1 patch** inside the same Wine/Proton prefix. Unpack the BigFix release zip; on Deck use Desktop Mode.
2. In the unpacked folder run `sh install.sh --game-dir "/path/to/Riddick"`. Use the actual game root containing `System/Win64_AMD64`; quote paths with spaces. Optionally add `--desktop-mode 1280x800@90` for the Deck OLED panel (the no-detection fallback is 1280×800 @ 60 Hz).
3. Add `System/Win64_AMD64/SbzEngine.exe` as a **non-Steam shortcut**, set **Start In** to its `System/Win64_AMD64` folder, select Proton under **Compatibility**, and set **Launch Options** to `WINEDLLOVERRIDES="winmm=n,b" %command%`. Launch and check for `BigFix.log` beside the plugin. For standalone Wine, use `--prefix "/path/to/pfx"` with the installer, or launch with `WINEDLLOVERRIDES="winmm=n,b"` in the correct prefix.

On Deck, a typical prefix is `~/.local/share/Steam/steamapps/compatdata/<shortcut-id>/pfx`; games installed inside it are under `pfx/drive_c`. Custom Steam libraries and SD cards have different roots. `--prefix` takes **pfx**, not `drive_c` or the shortcut-id directory. Launch Options work without a system `wine` command; no Steam files are edited automatically. See [full install/uninstall instructions](#install).

Help verify your hardware with [the short test script](docs/VERIFY.md) and [compatibility matrix](docs/COMPAT.md), then open a **Hardware verification report** issue. To build or contribute, see [CONTRIBUTING.md](CONTRIBUTING.md).

Built on [TCoRFix by Nezarn](https://github.com/Nezarn/TCoRFix), with [Ultimate ASI Loader by ThirteenAG](https://github.com/ThirteenAG/Ultimate-ASI-Loader), MinHook, and community research from PCGamingWiki. See [credits](#credits), [community-fix research](research/04-community-fixes.md), and [third-party license texts](NOTICE.md).

## Status

In development. Feature milestones are documented below; the packaged version is taken from `src/version.h` and printed in `BigFix.log`. Check that version when reporting results.

BigFix is developed and checked on one machine only: Windows 11, an NVIDIA RTX 3090, 2560×1440 at 180 Hz, the EU DVD release patched to 1.1, 64-bit executable. It has not been tested on AMD or Intel GPUs, on Linux, Wine or Proton, or on a Steam Deck.

Reports from those setups are wanted. Open an issue with `BigFix.log` and `BigFix.prev.log` (see [Troubleshooting](#troubleshooting)) and say what GPU, driver, OS and display mode you use.

## Requirements

- A retail copy of the 2004 PC release of *The Chronicles of Riddick: Escape from Butcher Bay*.
- The official **1.1 patch** for your release. The EU patch is `riddick_update_eu_10_11.exe`; a US patch also exists. BigFix cannot supply it: PCGamingWiki's community files section hosts it.
  - On Windows 10 and 11, run the patch with **Windows XP (Service Pack 3) compatibility mode**: right-click the patch, *Properties*, *Compatibility* tab.
  - On Linux, run the patch inside the **same Wine/Proton prefix** as the game.
  - BigFix stops if it finds version 1.0 and tells you to patch first.
- BigFix only supports the **64-bit executable**, `System\Win64_AMD64\SbzEngine.exe`. That build has no disc check, so no disc is needed while you play. The 32-bit executable is left alone and is not supported.
- On Windows, the installer runs in PowerShell (Windows PowerShell or PowerShell 7). On Linux it needs a POSIX `sh`, and uses `wine` when it can find it.

## Install

Download the release zip and unpack it somewhere outside the game folder. It contains `BigFix.asi`, `BigFix.ini`, `winmm.dll` (Ultimate ASI Loader), `BigFixSetup.cmd`, `BigFixSetup.ps1`, `install.ps1`, `install.sh`, the fingerprint data files, `fingerprint.ps1`, `fingerprint.sh`, `fingerprint.py`, `tga2png.py`, `README.md`, `docs/VERIFY.md`, `docs/COMPAT.md`, `NOTICE.md` and `LICENSE`.

### Windows

Double-click **BigFixSetup.cmd** for the themed installer window (Windows
PowerShell 5.1 or PowerShell 7, no extra dependencies). It detects the game and
build, offers Browse, Install, Uninstall, Launch game and Open log, and exposes
the main settings. Tab/Shift+Tab and Alt mnemonics navigate; Enter installs and
Esc closes when idle. Choose **Preview only** to log changes without applying
them. Registry repair asks for consent before Windows elevation. The scrolling
setup log is also saved as `BigFixSetup-*.log` in your temporary folder.

The window calls `install.ps1` for detection and all changes, including settings,
backups and uninstall. Shader mode defaults to 2.0; 2.0++ is an explicit option.
FOV and hardware limitations below still apply. Run
`pwsh -STA -File .\BigFixSetup.ps1` to use PowerShell 7. For a headless argument
preview, use `powershell -ExecutionPolicy Bypass -File .\BigFixSetup.ps1 -NoGui -WhatIf -GameDir "D:\Games\Riddick"`.

For the command-line installer, open PowerShell in the unpacked folder and run:

```
.\install.ps1
```

If PowerShell blocks the script, run it with `powershell -ExecutionPolicy Bypass -File .\install.ps1`.

The installer finds the game from the registry, or from `-GameDir "D:\Games\Riddick"`, or by asking you. It then:

- copies `BigFix.asi`, `BigFix.ini` and `winmm.dll` into `System\Win64_AMD64`, backing up every file it replaces under `System\Win64_AMD64\.bigfix-backup` and recording the install in `<game>\.bigfix-manifest.json`;
- repairs the truncated registry install path if it finds one: the 2004 installer saves the path without its last character, which makes patch 1.1 fail, so BigFix asks for administrator rights once and writes the correct path (`-NoRegistryRepair` skips this);
- writes `Environment.cfg` with your desktop resolution, your desktop refresh rate, vsync on and shader mode 5 (2.0);
- creates a Start-menu shortcut that starts `SbzEngine.exe` with the right working directory.

Add `-DryRun` (`-WhatIf` also works) to see what would change without changing anything. The installer changes nothing outside the game folder except that shortcut and, with your consent, the registry path repair.

### Linux, Proton and Steam Deck

Open a terminal in the unpacked folder and run:

```
sh install.sh --game-dir "/path/to/Riddick"
```

`--game-dir` is required: Linux has no registry to look the game up in. The installer does the same work as the Windows one: it copies the files into `System/Win64_AMD64`, backs up what it replaces, writes `Environment.cfg` and records a manifest. Without `--desktop-mode`, it asks `xrandr` for your current mode and falls back to 1280×800 at 60 Hz (the Deck's panel).

Pass `--prefix "$WINEPREFIX"` to have it set the `winmm` override in that prefix with `wine reg add`. Add `--dry-run` to preview, or `--print-steam-options` to print the Steam launch option and exit.

**Wine and Proton only load the plugin when `winmm` is set to native first.** If you launch the game from Steam, add the game as a non-Steam shortcut (it is no longer sold on Steam) and set *Properties → General → Launch Options* to the exact string:

```
WINEDLLOVERRIDES="winmm=n,b" %command%
```

### Optional EAX audio backend

An audio-enabled release package can install the pinned **x64 DSOAL + OpenAL
Soft** pair beside `System/Win64_AMD64/SbzEngine.exe`:

```powershell
.\install.ps1 -GameDir "D:\Games\Riddick" -AudioBackend dsoal
```

```sh
sh install.sh --game-dir "/path/to/Riddick" --audio-backend dsoal
```

The default is `system`, which leaves engine audio settings unchanged. DSOAL
installation verifies both DLL hashes, AMD64 PE headers and required exports
before writing files, and merges `SND_USE3D=1` and `SND_USEEAX=1` into the
backed-up `Environment.cfg`. It never installs `eax.dll` or changes x86/system
DLLs. The optional shell component needs Python 3 for PE and ownership checks;
the ordinary shell installer still works without Python. Use `-DryRun` or
`--dry-run` to see the files and keys first.

Existing `dsound.dll` or `dsoal-aldrv.dll` is a conflict. After checking which
wrapper it belongs to, use `-BackupAudioConflicts` / `--backup-audio-conflicts`
to authorize reversible replacement. Both uninstallers restore the same
manifest/backup format. If files or configuration changed after a DSOAL
installation, uninstall refuses before making changes; move the changed files
aside and retry, then reconcile your saved settings. Backups remain available.
To change backend, uninstall first, then reinstall with the desired choice and
restart the game. Setting `SND_USEEAX=0` disables engine effects while keeping
DSOAL installed; `BackgroundAudio` only controls focus behavior.

For an installed DSOAL component, the printed Steam launch option is:

```sh
WINEDLLOVERRIDES="winmm=n,b;dsound=n,b" %command%
```

`--prefix` also sets native-first `dsound` alongside `winmm` in that explicitly
selected prefix. `--game-dir DIR --print-steam-options` reports the option for
the installed component. No HRTF preference is forced. Offline API and synthetic
reverb tests establish dependency behavior; in-game listening, streaming,
latency, speaker layouts and Wine/Proton/Deck compatibility still need testing.

### Uninstall

Windows:

```
.\install.ps1 -Uninstall
```

Linux:

```
sh install.sh --uninstall --game-dir "/path/to/Riddick"
```

Both read `.bigfix-manifest.json` in the game folder and put the folder back: they restore every file they replaced from `.bigfix-backup`, remove `Environment.cfg` unless it existed before (in which case the original is restored), and remove the shortcut. Either uninstaller works on a folder installed by the other script.

## What v0.1 fixes

- **Startup crash on modern drivers.** The game cannot cope with the very long OpenGL extension list modern drivers report. BigFix returns a short list of the extensions the game actually uses, and fixes the `GL_MAX_TEXTURE_UNITS` query that fails on some cards. The extension filter is always on.
- **Resolutions above 2048 px.** The renderer's 2048-pixel limit on width and height is patched in memory, so 1440p and 4K render correctly instead of drawing only the top-left part.
- **Native resolution on monitors with many display modes.** Windows now lists hundreds of modes; the game keeps only the first 512 and can fall back to a small mode. BigFix gives it a short, de-duplicated list that always includes your desktop mode. No CRU or SRE edits are needed.
- **Borderless windowed by default.** The game runs as a borderless window covering your monitor at desktop size, with no exclusive-fullscreen mode switch when it loses focus or is minimised. `WindowMode=fullscreen` gives you the game's own fullscreen back.
- **2.0++ shader compatibility path.** The NVIDIA-specific occlusion query is replaced with the standard one to support AMD, Intel and Mesa; real hardware verification is pending. Select 2.0++ in the game's video options to test it.
- **PNG screenshots.** The capture key (F12 by default) saves a PNG named with the date, resolution and shader mode into `<game>\ScreenShots`. Optional frame-time logging writes `frametimes.csv` next to the plugin.
- **A diagnostic log.** `BigFix.log` sits next to the plugin and records the plugin version, Windows version, the executable's SHA-256, the GPU and OpenGL version, the display modes the game sees and picks, each patch applied or skipped with its reason, and the settings in effect.
- **Safe patching.** Every patch site lists the bytes it expects. BigFix writes nothing unless they match, so unknown builds and repacks run without bad bytes being written; a skipped patch is logged with its reason.

## What v0.2 adds

- **Raw mouse input** (`RawMouse=1`, on by default). Mouse movement comes from Windows raw input instead of the game's DirectInput buffer, so high polling rates no longer drop movement. Buttons, wheel, bindings and the game's own sensitivity are unchanged. Movement made while the game is in the background is discarded.
- **Anti-aliasing from `BigFix.ini`** (`AntiAliasing=2`, `4` or `8`). Applied when the window is created, keeps the stencil buffer the game's shadows need, and falls back to fewer samples, or none, with a log entry. Restart the game after changing it. Only checked on NVIDIA so far.
- **Optional frame cap** (`FrameCap=60`, for example). Off by default.
- **Optional background audio** (`BackgroundAudio=1`). Keeps sound playing when the window loses focus. Off by default.

## What v0.3 adds

- **Eyeshine flicker fix** (`EyeshineFix=1`, on by default): runs reduced-viewport stencil work, mask generation/copy and lighting across the Eyeshine region. Cast shadows remain enabled; visual confirmation is pending.
- **GPU compatibility hardening:** preserves conventional colour alongside generic input 3, translates RGB/alpha pixel-format sizes correctly, and retries MSAA with depth24/no accumulation before lowering samples. Actual chosen formats and unmet AA requests are logged without debug tracing. AMD/Intel/Mesa validation is still pending; see [AA investigation and supersampling recommendation](research/re/amd-aa.md).
- **Skip startup logos**: `SkipLogoVideos=1` goes straight to the “Press ENTER” title, bypassing the static splash screens and five company movies. Intro, Outtro, Eyeshine and in-game cutscenes keep their normal behavior. Set it to `0` to restore the startup sequence.
- **Video aspect correction** and **CaptureBurst** for consecutive PNG captures from one capture-key press.
- **Faster test tooling**: `tools/game_driver.ps1 launch -Map Pa1_Intro -WaitFirstFrame` uses the engine's `QUICKMAP` startup path; `burst` captures the configured number of PNGs. See [static findings and commands](research/re/fast-start-and-logos.md). Live validation of the new startup routes is still pending.

## What v0.4 adds

- **Shape-correct HUD and subtitles** (`HudAspectFix=1`): verified player HUD and subtitle draws share a centred 4:3 region. At 2560×1440 this is 1920 pixels wide with 320-pixel margins; the world remains visible at the sides. Fades, screen effects, bars, video backgrounds and menu controls retain their original coverage and input coordinates.
- **Menu/title background alignment**: the frontend's background quad is aligned to the actual clipped projection, rather than the requested frustum's corners. Artwork fitting and texture coordinates are retained. This is scoped to the verified frontend caller and follows `HudAspectFix`. The reported right/bottom strips at 2560×1440 still require visual confirmation.
- **Optional gameplay FOV** (`FOV=60`–`120`, `0` by default): horizontal degrees at 4:3 equivalent. Relative zoom magnification is retained; authored dialogue, external cameras and the unidentified 110° special camera retain their original FOV. Restart after changing settings.

**Hands/weapon limitation:** a separate first-person projection has not been established statically. Nonzero `FOV` changes the gameplay camera and its saved viewport together; hands may therefore change size or perspective. It does **not** promise an independent weapon FOV. Leave `FOV=0` for the original behavior. `TraceFov=1` records the actual renderer projection uploads for a short comparison run. See [implementation evidence and the opening-dock checklist](research/re/v0.4-hud-fov.md#v04-implementation-2026-10-30).

## Settings

Settings live in `BigFix.ini` next to `BigFix.asi`, in `System\Win64_AMD64`. The plugin always reads it from its own folder, so shortcuts and different working directories do not matter. A missing key takes the default. If the file is missing entirely, the plugin writes a default one.

| Key | Default | Meaning |
| --- | --- | --- |
| `TextureSizeFix` | `1` | Fixes resolutions above 2048 px. `0` leaves the game's limit in place. |
| `FixMiscBugs` | `1` | Fixes the `GL_MAX_TEXTURE_UNITS` query that fails on some cards. |
| `Enable20PlusPlusAnyGpu` | `1` | Lets shader mode 2.0++ run on AMD, Intel and Mesa as well as NVIDIA. Only matters when the game is set to 2.0++. |
| `EyeshineFlickerWorkaround` | `0` | Stops the Eyeshine flicker in 2.0++. It also removes some character shadows, so it is off by default. |
| `EyeshineFix` | `1` | Whole reduced-region stencil work, observed alpha conversion replay, mapped mask copy and lighting. Restores engine state. The previous light-box clamp is diagnostic variant 7 only. |
| `EyeshineFixVariant` | `0` | With `TraceShadowPass=1`, an initial nonzero variant, or `EyeshineAbBurst`, diagnostics override `EyeshineFix`: 0 original, 1 packed-copy state, 2 full stencil clear, 3 mask alpha, 4 window coordinates, 5 clamp taps, 6 full resolution, 7 rejected light-box clamp, 8 whole-region fallback. F9 cycles 0–8 and always logs selection. Without diagnostics, `EyeshineFix` controls the fix. See [evidence and dock A/B run](research/re/eyeshine-variants.md). |
| `DisplayModeFilter` | `1` | Gives the game a short display-mode list with your desktop mode in it. `0` passes Windows' full list through. |
| `HudAspectFix` | `1` | Centre verified HUD/subtitle geometry in a shape-correct 4:3 region; align the frontend background quad to its clipped projection. Full-screen effects stay full width. `0` restores both original paths. Requires matching EU 1.1 bytes. |
| `FOV` | `0` | `0` = original; otherwise finite horizontal degrees at 4:3 equivalent, from `60` to `120`. Gameplay camera only, preserving relative zoom. Hands/weapon projection remains coupled; see the limitation above. |
| `TraceFov` | `0` | Debug: camera classification and complete matrices uploaded at `RndrGL+9754`, with frame/thread/context IDs. First two frames, first two gameplay frames, then every 120th frame; at most 48 uploads per sampled frame and 400 payload lines per process, with gameplay budget reserved. Does not change GL state. Not in the shipped file. |
| `WindowMode` | `borderless` | `borderless`, `fullscreen` (the game's own exclusive fullscreen) or `windowed`. |
| `CaptureKey` | `F12` | Key that saves a PNG. `F1` to `F24`. |
| `CapturePng` | `1` | `0` turns PNG capture off; the game's own screenshots still work. |
| `EyeshineAbBurst` | `0` | Debug: during a CaptureBurst, alternate complete rendered frames between 0 and this variant (1–8); log each PNG index/variant and restore the prior selection afterward. First sample is the frame after the key press. |
| `CaptureBurst` | `1` | Frames saved per capture key press (1 to 16), from consecutive frames. Useful for showing flicker. Not in the shipped ini. |
| `FrameCap` | `0` | Maximum frames per second: `0` leaves the frame rate uncapped, otherwise a number of at least `1` (for example `60` or `59.94`) waits before each present so the game does not draw faster than that. |
| `DumpShaders` | `0` | Debug: writes the shader programs the game uploads to `shader_dump\` next to the plugin. Not in the shipped file. |
| `DumpFrameTimes` | `0` | Debug: writes `frametimes.csv` next to the plugin every 600 frames, and logs average and 1% low frame times every 10 s while `FrameCap` is on. Not in the shipped file. |
| `VideoAspectFix` | `1` | Centre each video at its picture aspect with black bars (720×486 NTSC uses 4:3). Video-only, requires matching EU 1.1 hook bytes. Menus, HUD and 3D retain their existing aspect. |
| `SkipLogoVideos` | `1` | Bypass startup logo movies and static splash screens to the “Press ENTER” title. Story videos and in-game cutscenes stay enabled. Requires matching EU 1.1 startup-command bytes. |
| `BackgroundAudio` | `0` | `1` keeps game audio playing when the window loses focus. It adds `DSBCAPS_GLOBALFOCUS` to secondary DirectSound buffers only; the primary buffer is not changed. |
| `TraceVideo` | `0` | Debug: video/GL trace in `BigFix.log`, at most 2,000 trace lines, including corrected geometry when `VideoAspectFix=1`. Internal hooks require matching EU 1.1 prologues. Not in the shipped file. |
| `TraceShadowPass` | `0` | Debug: compact `frameRecord` only for queued PNGs or variant changes; F11 samples four detailed frames. Idle and writer-drain frames use no compact budget. Records join PNG indexes to mask/mismatch/restriction counts. 1500 detailed + 500 reserved compact lines per process. Enables F9 variants. See [record format and short run](research/re/eyeshine-variants.md). |
| `TraceBloom` | `0` | Debug: sampled GL post-process state, program CRCs, FP parameters, texture formats and blend state in `BigFix.log`; 8,192 records plus a cutoff marker. Diagnostic only. See [bloom research and scene runs](research/re/x64-bloom.md). Not in the shipped file. |
| `TraceAudio` | `0` | Debug: logs each DirectSound buffer's flags, size and format, at most 200 lines. Not in the shipped file. |
| `TraceEax` | `0` | Diagnostic only: loaded DirectSound path/version and pinned DSOAL identity; buffer QI and IKsPropertySet Get/Set/QuerySupport GUIDs, properties, sizes, HRESULTs, support/returned counts and packet fingerprints. Capped at 512 calls plus bounded slot details. Does not enable EAX. |
| `AntiAliasing` | `0` | Window MSAA: `0` keeps game selection; `2`, `4`, `8` request samples while preserving depth/stencil. Falls back to lower samples, then none, with a log entry. Restart the game after changing. |
| `TracePixelFormat` | `0` | Debug: bounded pixel-format requests, chosen attributes and context sample counts in `BigFix.log`. Not in the shipped file. |
| `RawMouse` | `1` | Foreground raw relative mouse motion, replacing DirectInput X/Y only. Buttons, wheel, bindings and game sensitivity are preserved. Requires matching EU 1.1 mouse-poll bytes; `0` keeps the original path. |
| `TraceInput` | `0` | Debug: sampled per-poll DI HRESULT, record count, overflow and raw motion with cumulative counts, at most 1,500 lines. Not in the shipped file. |

Put keys under `[BigFix]` (or `[General]`). TCoRFix's spellings — `Fix Texture Size`, `Fix Misc Bugs`, `Enable 2.0++ on any GPU`, `Eyeshine Flicker Workaround` — are accepted as aliases for the matching keys. Unknown keys and invalid values do not stop anything; they are listed as warnings in the log.

## Known issues

- **2.0++ Eyeshine visual confirmation is pending.** The earlier light-box clamp cut off valid illumination. The new default whole-region fallback preserves cast shadows in native tests and still needs the interleaved A/B game run. The older shadow-removing workaround stays off; the installer still selects shader mode 5 (2.0 = `XR_SHADERMODE=5`).
- **Heavy bloom in one cutscene** on the 64-bit executable. The 32-bit build does not have this bug; BigFix does not fix it yet.
- **Anti-aliasing is untested on AMD and Intel.** It works on NVIDIA; the game's own console method was reported not to work on AMD.
- **v0.4 visual checks are pending.** The scoped HUD/subtitle transform and frontend background alignment have offline coverage; the reported menu strips, ordinary HUD coverage, clipping and subtitle shadows still need an opening-dock/menu comparison.
- **Hands/weapon FOV is coupled to the gameplay camera.** Nonzero `FOV` is opt-in; an independent hands projection remains unresolved.
- **Only the 64-bit executable is supported.** The plugin may load in the 32-bit one, but it is untested there.
- **Unknown builds** are refused when any patched DLL is unverified or missing. A partial match (all patched DLLs match one verified build, other files differ) can install and lists the differences. US 5-CD 1.1, Russian, Polish and repack reports are wanted; see [volunteer build recognition](docs/volunteer-builds.md). The installers print the fingerprint command and the issue template path. Report collection needs Python 3.9+; installation does not.

## Troubleshooting

**No `BigFix.log` next to the plugin means the plugin did not load.** Check that `BigFix.asi` and `winmm.dll` are both in `System\Win64_AMD64`. On Linux, Proton and the Deck, the usual cause is the missing `winmm` override: pass `--prefix` to the installer or set the Steam launch option above.

**Where the log is.** `BigFix.log` is written in the plugin's own folder, `System\Win64_AMD64`. Each launch renames the previous log to `BigFix.prev.log`, so two launches of history are always available. The log says which patches were applied, which were skipped and why, and which display mode was picked.

**For a bug report**, attach both `BigFix.log` and `BigFix.prev.log`, and say what GPU, driver, OS and display mode you use. Do not paste game files; the logs do not contain any.

**Screenshots.** BigFix writes `BigFix_<date>_<width>x<height>_sm<mode>_<n>.png` into `<game>\ScreenShots`. The game's own F12 screenshots are RLE TGAs (`ScreenNN.TGA`) that most image viewers reject; convert them with the included `tga2png.py` (Python 3, standard library only):

```
python tga2png.py ScreenShots
```

**A setting did not take effect.** Edit `BigFix.ini` in `System\Win64_AMD64`, not a copy in the release folder, then restart the game. The log opens with the settings it is using and any warnings about the file.

**The game will not start after installing.** Uninstall with the same installer. It restores every file it replaced.

## Credits

- **TCoRFix** by Nezarn (MIT) — BigFix is built on it: the extension list, the texture-unit fix, the shader rewrite table, the colour remap and the query adapters come from it.
- **Ultimate ASI Loader** by ThirteenAG (MIT) — loads the plugin, shipped as `winmm.dll`.
- **MinHook** by Tsuda Kageyu (BSD-2-Clause) — hooking.
- **doctest** — the test framework.
- **stb** (public domain) — PNG encoding.
- The **PCGamingWiki** contributors and the players who reported and researched these bugs over the years.

The full texts are in [NOTICE.md](NOTICE.md).

## Licence

BigFix is released under the MIT License. See [LICENSE](LICENSE). It contains no game code, game files or copyrighted patches.
