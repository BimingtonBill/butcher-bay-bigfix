# Startup logos and fast test start — EU 1.1 x64

2026-10-29. Static analysis and isolated unit/plugin/tool tests only. No game
process was launched, no game DLL was loaded, and nothing under
`D:\Games\Riddick` was written. The fast-start route below is established
from control flow; elapsed startup time, level readiness, and live output remain
for the lead to confirm. FirstFrame means a presented GL frame, not a playable
level or completion of a level's own scripted introduction.

## Reproduce the evidence

From this worktree, using the existing project Python (Capstone/pefile):

```powershell
& 'D:\Source\BigFix\.venv\Scripts\python.exe' -I tools/re/startup_static.py --root D:/Games/Riddick --verify
```

The tool reads PE files as data. It checks three SHA-256 identities, 17 selected
instructions, the sole code reference to the default startup command, the GUI
sequence in source and its compiled string pool, the separate story-video
window, and the existence of both opening world files. All addresses are RVAs.

| File | SHA-256 |
|---|---|
| `System/Win64_AMD64/SbzEngine.exe` | `5056ffe030c2ba8687b032f5f2ed95b42c4814db87dec61444a6cf264deaca38` |
| `Content/GameWorld_Win64_AMD64.dll` | `749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7` |
| `System/Win64_AMD64/MSystem.dll` | `b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500` |
| `Content/Gui/CubeWnd.xrg` | `8ecd3358bfe13b6a51eb53e5dc8266db2e80128cb0c16e7dc4efa73e1a0fefff` |
| `Content/Gui/CubeWnd.xcr` | `8b201a7a43969f936e3430b7e791d68c77f78ade1d20225a68f3261b74b44acf` |

## The startup sequence and chosen patch

The sequence lives in `Content/Gui/CubeWnd.xrg`, not in the Theora decoder or
GUI.xtc (which contains texture data). `CubeWnd.xcr` contains its compiled
menu names, commands and video texture references. Source lines 98–280 define:

```text
intro -> legal -> kiosk_fork -> main_blank_logos -> esrb
      -> vug_logo -> universal_logo -> tigon_logo -> mixed_logo -> sbz_logo -> main
```

`intro` is a GUI fork name, distinct from the story video `Intro.ogg`.
The initial timed screens include a blank pause, legal screen and static splash.
All are upstream of `main`, so the hardware-branding/static screen described in
the task is bypassed too, without needing to identify which image depicts it.
`main` at source line 714 has `SHOW_PRESSSTART bottom`, the normal cube startup
script and GUI music; it retains the ENTER/START accelerator into profile/menu
selection. It is the title screen, not a loaded level.

| GUI window | Registered video texture | Movie bypassed |
|---|---|---|
| `vug_logo` | `*VIDEO_VUG_NEW` | `vug_new.ogg` |
| `universal_logo` | `*VIDEO_UNIVERSAL_NTSC` | `universal_ntsc.ogg` |
| `tigon_logo` | `*VIDEO_TIGON` | `Tigon.ogg` |
| `mixed_logo` | `*VIDEO_LOGO_MIXED` | `logo_mixed.ogg` |
| `sbz_logo` | `*VIDEO_SBZ` | `Sbz.ogg` |

The observed trace order in [video-playback.md](video-playback.md) agrees with
this order. Each video window's `TEXTURE_MODE LINK` goes to the next window.
The separate `video_intro` window at line 656 uses `*VIDEO_INTRO`.

GameWorld frontend initialization spans **0x46D80–0x470ED**. Its fallback at
**0x470AA** takes the literal **0x1E80E0**, `cg_rootmenu "intro"`, constructs a
CStr, then executes it at **0x470C1**. There is exactly one static code reference
to this literal. `skipLogoVideosSite()` in `src/patch_table.cpp` verifies all
20 bytes, including the terminating NUL, then replaces them with
`cg_rootmenu "main"` and two trailing zero bytes. This preserves the original
allocation and adjacent data. No instruction or decoder state is modified.

The existing deferred GameWorld module callback applies the patch after the
loader returns and before control returns to the game's loading caller. The
module registry dispatches once. Applied patches produce one diagnostic for
each of the five bypassed movies, explicitly labeled `startup sequence bypass`;
these announce the installed sequence bypass, not decoder-open events. Off,
mismatch and bad-address paths do not report skipped movies. With QUICKMAP or
QUICKMENU configured, the engine already avoids the default startup command.

`SkipLogoVideos=1` defaults on in Settings, generated INIs and the installer.
`0` leaves the command untouched. The patch is independent of VideoAspectFix
and TraceVideo. Those still own the geometry/trace hooks described in the video
research. There is no filename interception, global end-of-stream override,
keypress loop, or decoder hook: Intro/Outtro/Eyeshine, other movies, credits and
in-game cutscenes retain their existing paths. Unit tests therefore pin exact
startup-command/name matching, including every one-byte mismatch and rejection
of story names, rather than adding an unused filename filter.

## Fast start: use the existing QUICKMAP frontend branch

| Entry | Static behavior | Tooling decision |
|---|---|---|
| `-map "Pa1_Intro"` | EXE compares `-MAP` at 0x11101, formats `map "%s"` at 0x1114E, executes at 0x1116D during startup parsing | Recognized native syntax, but earlier than frontend initialization. Live success/readiness at this point was not tested. |
| `-exec "command"` | EXE compares `-EXEC` at 0x11242, stores the following token at object +0x968 (0x1127F), executes/clears it in the early loop at 0x15DC4–0x15DF3 | No after-title barrier or readiness retry. The reported ignored startup `loadgame` is consistent with this timing; do not use it for fast-start save loading. |
| `-env "path.cfg"` | MSystem finds `-env` at 0x756FE; quoted/unquoted filename parsing follows, then environment-file loading at 0x75A5D | Selects a config file, not a console command or `KEY=VALUE` command-line assignment. |
| `QUICKMAP=Pa1_Intro` in that environment | GameWorld reads QUICKMAP at 0x46F1D after frontend/resource initialization, builds `map "<value>"`, executes at 0x46F9E, jumps over the title/default command at 0x46FCF | Chosen path: bypasses title, profile selection and menu navigation at their owning layer. |
| `QUICKMENU=<name>` | Checked at 0x46FD7 only if QUICKMAP is empty; constructs `cg_rootmenu "<value>"` | Menu selection only; QUICKMAP takes priority. |

The `map` handler **EXE 0x43000** posts request kind **1** (0x430C1); the
`loadgame` handler **0x44730** posts request kind **4** (0x447F1). A map name
selects a world, while a save name asks the save/profile path to restore state.
The task's reported `loadgame "Checkpoint"` hang at the main menu was not
reproduced here and its exact cause is not established. It does not invalidate
QUICKMAP's separate map-loading branch. Do not describe either early `-map`
or `loadgame` as verified working based solely on their registration strings.

`Content/Worlds/Pa1_Intro.XW` and `Pa1_Arrival.XW` both exist;
`Registry/LevelKeys.xrg` contains their campaign keys, and the normal new-game
menu uses `campaignmap "pa1_intro"` at CubeWnd source line 1477. QUICKMAP uses
the engine's direct `map` path instead of the profile/new-game UI. It does not
promise a clean campaign save or suppress level-authored cinematics. Pa1_Intro
is the opening tutorial/dream; Pa1_Arrival is the subsequent prison arrival.

### Exact commands for the lead (not run during implementation)

From this worktree, with the existing ASI loader installed:

```powershell
.\tools\game_driver.ps1 launch -GameDir 'D:\Games\Riddick' -PluginPath .\build\Release\BigFix.asi -Map Pa1_Intro -Settings 'CaptureBurst=8;CapturePng=1;CaptureKey=F12' -WaitFirstFrame -TimeoutSec 60
# Substitute -Map Pa1_Arrival for the prison arrival map.
# After visually confirming level readiness, one separate action:
.\tools\game_driver.ps1 burst -TimeoutSec 60
# End the run and restore the backed-up environment/plugin/settings:
.\tools\game_driver.ps1 quit
```

`-Map` deliberately writes `QUICKMAP=<name>` into the driver's existing backed-up
Environment.cfg; it does not emit the earlier `-map` switch. Bare map names
accept letters, digits, underscores and hyphens only. Other display settings
remain, and `-ShaderMode` can be combined with `-Map`. No key/menu sequence is
added. Optional extra native arguments are tokens (invoke from PowerShell):

```powershell
.\tools\game_driver.ps1 launch -Map Pa1_Intro -ExtraArgs @('-exec', 'echo "test start"') -WaitFirstFrame
```

For a direct EXE invocation without changing the installed Environment.cfg,
make a copy in this worktree's ignored `local` directory and add
`QUICKMAP=Pa1_Intro` (replace any previous QUICKMAP line). Preserve the current
display settings in that copy. Then the native launch command is:

```powershell
$cfg = (Resolve-Path .\local\fast-start.cfg).Path
Start-Process -FilePath 'D:\Games\Riddick\System\Win64_AMD64\SbzEngine.exe' -WorkingDirectory 'D:\Games\Riddick\System\Win64_AMD64' -ArgumentList ('-env "' + $cfg + '"')
```

`-WaitFirstFrame` is opt-in, errors on timeout/process exit, reads the shared
writer's live log, and cannot accept the previous launch's FirstFrame: launch
rotates BigFix.log to BigFix.prev.log before starting. Without the switch,
launch returns after Start-Process. `burst` reads CaptureBurst (default 1,
valid 1–16) with the INI's section priority and last-valid-value rules; it
requires PNG enabled and CaptureKey=F12, focuses once, presses/releases F12
once (held across the stock TGA stall), then waits for the expected number of
distinct fresh `Capture saved` paths whose PNG files exist. It prints original
PNG paths, reports capture errors/exit/timeouts, and performs no further action.

## Verification

- Configure and Release build with Visual Studio 17 2022, x64: passed.
- CTest: **57/57 passed**, both with RIDDICK_DIR unset and set to the read-only
  reference game. `pytest installer/tests tools/tests`: **106 passed, 1 skipped**.
- `startup_static.py --verify`: all three hashes, 17 instructions and GUI/map
  evidence passed. `git diff --check` passed.
- The logo module harness loads only an artificial data DLL. It exercises the
  real module callback/live patch path in on/off/mismatch scenarios, protects
  adjacent story-video strings, and asserts one skip message per movie.
- Unit tests cover defaults/opt-out/invalid INI values, exact command matching,
  all byte mismatches, and optional raw-file reference bytes.
- Python tests exercise PowerShell 7 and Windows PowerShell 5.1: native argument
  round-trip via CommandLineToArgvW, Map validation/config preservation,
  launch/quit backup restoration with a fake Start-Process, first-frame waiting,
  INI capture count/priority, single F12 press, duplicate/missing PNG rejection,
  capture failures and timeouts. No real input or game launch is used.

Live verification remains: title-only normal startup, unchanged story playback,
both QUICKMAP worlds reaching gameplay, and burst PNG contents. No timings or
successful gameplay are claimed from these static and stub tests.
