# Butcher Bay BigFix: design

Date: 2026-10-29. Status: approved by the user on 2026-10-29 ("design decisions are good").
Workers: building, reverse engineering and testing are handed to Sol (Codex `gpt-6.1-sol`) and DeepSeek. Claude plans, writes the briefs and reviews.

## 1. Intent

The 2004 PC release of *The Chronicles of Riddick: Escape from Butcher Bay* ("Developer's Cut") is a great game that is hard to run today.
Players have to put together a fragile stack by hand: patch 1.1, the right executable, TCoRFix with hand-edited ini values, an ASI loader or DxWrapper, CRU/SRE display-mode edits, and console commands.
Several problems have no fix at all (see `research/05-open-problems.md`).

**BigFix** is an open-source mod, published on GitHub, that makes the original game (not the 2009 *Dark Athena* remake) run as a solid, seamless modern experience.
It installs like a mod, from a scripted installer, on **Windows, Linux (Wine/Proton) and Steam Deck**.

Success looks like this: someone with a retail copy runs one installer, then launches a game that starts every time and opens at their native resolution.
It aims cleanly with a modern mouse and renders correctly, including 2.0++ shadows without flicker.
When something does go wrong, it leaves a log file that explains why.

## 2. Decisions already made (by the user)

- **Mod-style install, with a scripted installer**, published on GitHub as open source.
- **Build on TCoRFix** (Nezarn, MIT, C++ ASI plugin), improve it, and **credit it** in the README, the licence notice and the source headers where its code is used. Offer fixes upstream where that makes sense.
- **Original 2004 release only.**
- **Standardise on the 64-bit executable** (`System\Win64_AMD64\SbzEngine.exe`). It is DRM-free, so BigFix never touches SecuROM and never ships cracked files.
- **Platforms:** Windows 10/11, Linux via Wine/Proton, and Steam Deck.
- **Reference copy:** EU single-DVD release + official EU patch 1.1. Local paths in this public document use example locations such as `D:\Games\Riddick`. Fingerprints are in `research/fingerprints/`.
- **No spoilers for the user.** From now on, verification must be automated and must not need the user to play or look at the game past the opening dock. Never show the user screenshots from later levels.

## 3. Goals

In priority order. The first two tiers make up the first release.

### Tier 1: starts reliably at the right resolution, and is diagnosable
1. **Hardened plugin base.** Fork TCoRFix into this repo and fix its structural faults (`research/` review):
   - Read the ini from the plugin's own folder, not the current working directory. The CWD lookup made every fix silently turn off when the game was launched through `Riddick.exe` or a shortcut.
   - **Verify bytes before patching.** Every patch site lists its expected original bytes per build. A mismatch skips that patch and logs it, instead of writing to a random address. This makes repacks and other regions safe.
   - Identify the executable by fingerprint (SHA-256 of `RndrGL.dll` and similar), not by matching path strings.
   - Fix the 1024-byte extension buffer, which today copies past the end of the string and may be left unterminated.
   - Move the heavy setup out of `DllMain` (`LoadLibrary` and MinHook init run under the loader lock).
   - Defaults that just work: all known-good fixes on, nothing to hand-edit.
2. **Log file** (`BigFix.log` next to the plugin) recording:
   - plugin version, game build ID, GPU/vendor/renderer/GL version and driver,
   - the display modes the game enumerates and the one it picks,
   - each patch applied or skipped, with the reason,
   - the settings in effect.
3. **Startup crash fix:** keep TCoRFix's extension-list filtering and the `GL_MAX_TEXTURE_UNITS` fix.
4. **Resolutions above 2048:** keep the `RndrGL.dll` 2048-clamp patch, which is verified on all four EU 1.1 builds (a `JLE` after a compare with `0x800`). Add the byte check.
5. **Display-mode table fix (new):** the game appears to keep only the first 512 display modes. A modern monitor lists hundreds (803 on the dev rig), so it falls back to e.g. 1600×900 and overwrites `VID_MODE` (`research/02` §B2a).
   - Hook `EnumDisplaySettingsA/W` and return a short, de-duplicated list in which the native mode is always included.
   - Confirm the 512 limit first. Done when a fresh launch opens at 2560×1440 on the dev rig with no CRU/SRE changes.
6. **Borderless windowed by default, and a sane first run:** the plugin turns the game's window into a borderless window at the desktop resolution (exclusive fullscreen stays an option). The installer writes `Environment.cfg` with the native resolution and the desktop refresh rate. The game no longer starts at 640×480 or takes over the desktop mode while minimised.
7. **Screenshot capture:** F12 also saves a PNG named with the resolution and settings, alongside the game's own RLE TGA that common tools fail to read. This is our main test instrument.
8. **Installer** (see §6).
8a. **Videos keep their own aspect ratio** (added by the user on 2026-10-29 after the first in-game run, where the intro was stretched to 16:9).
   - 4:3 videos play at 4:3, centred, with black bars. The intro files are 640×480, and the NTSC Universal logo is 720×486, which displays as 4:3.
   - 16:9 videos (the Starbreeze logo, 640×360) fill a 16:9 screen, and are letterboxed on narrower screens.
   - Menus, HUD and 3D rendering are unaffected.
   - Setting `VideoAspectFix=1` (default on).

### Tier 2: plays and looks right
9. **Mouse:** support polling rates above 125 Hz on the x64 exe. DxWrapper's fix only works on x86, and TCoRFix's DPI-aware change may only partly cover it. Add raw input with no acceleration, as an option that is on by default.
10. **2.0++ shadows and Eyeshine flicker:** find and fix the root cause. TCoRFix's workaround comments out `MUL r0.rgb, r0, ShadowMaskTexel.a` in one fragment program, which removes every character shadow from the main light. This was reproduced on the dev rig with F12 captures (`research/02` §C2).
    - The goal is 2.0++ with full shadows and no flicker, on NVIDIA, AMD and Intel/Mesa.
    - Use apitrace (it supports legacy GL; RenderDoc does not) to capture a flickering frame.
11. **x64 bloom bug** in the ship sequence (x86 renders it correctly). Find what differs in the x64 path and fix it.
12. **Persistent anti-aliasing:** MSAA chosen in the plugin settings, without console commands. Investigate why it fails on AMD, and fall back to supersampling if MSAA can't work there.
13. *(Borderless windowed moved to Tier 1; see §9.)*
14. **Frame-rate safety:** check whether gameplay or physics misbehave above 60 FPS (*Dark Athena*'s engine does). If so, add a frame cap, defaulting to the safe value.

### Tier 3: polish (after the first release)
15. Correct HUD/subtitle aspect ratio in widescreen.
16. FOV option that doesn't break the viewmodel.
17. Audio: option to keep playing when unfocused; investigate EAX/OpenAL for x64 (the x64 build ships no `eax.dll`).
18. A themed installer window in the spirit of the original Starbreeze autorun (the user liked it).
19. More regions and builds: US 5-CD 1.1, Russian and Polish releases, recognising common repacks.

## 3a. Roadmap (approved by the user, 2026-10-29)

The user plans to play the game themselves once **v0.4** ships. That makes v0.2–v0.4 the priority path, and it is why spoiler-free verification matters.

| Version | Theme | Contents |
|---|---|---|
| **v0.1** | Starts right, looks right | Tier 1 (§3, items 1–8a): startup crash, >2048 px, mode table, borderless, installers, log, PNG capture, video aspect |
| **v0.2** | Feels right | Raw mouse input / no acceleration / >125 Hz fix; frame-rate safety check + cap if needed; play audio when unfocused (option); AA from `BigFix.ini` (NVIDIA at least) |
| **v0.3** | Renders right | 2.0++ shadows without Eyeshine flicker; x64 bloom in the ship sequence; AA on AMD or supersampling fallback (traces from the shipped benchmark demos, announced first) |
| **v0.4** | Widescreen right | HUD/subtitle aspect ratio; FOV option without viewmodel distortion |
| v0.5 | Everyone's copy | US/RU/PL builds and repack recognition; EAX-style audio via OpenAL; themed installer |
| v1.0 | Public | Volunteer-verified AMD/Intel/Linux/Proton/Deck. Repo goes public when the user decides (suggested at v0.1–v0.2, so testers can report) |

If a v0.3 investigation stalls, v0.4 may ship ahead of it.

## 4. Non-goals

- *Assault on Dark Athena*: different engine generation, and TAGES DRM.
- The 32-bit executables, beyond not breaking them. The plugin may load on x86, but it is only tested and supported on x64.
- Windows XP.
- Multiplayer: the original has none.
- New content, gameplay changes, texture packs.

## 5. Anti-goals (would be failure even if it technically works)

- Shipping any game files, cracks, no-CD executables, or the copyrighted patch in the repo or releases. Fingerprints only.
- Changing bytes we haven't verified, or silently doing nothing (a fix that fails must say so in the log).
- Changes to the user's system outside the game folder. The only exceptions are the Starbreeze registry `InstallPath` repair and Steam launch options, and both need consent. No CRU/SRE, driver profile or system-wide edits.
- A fix that trades one visible bug for another, as TCoRFix's flicker workaround does, presented as "fixed".
- Spoilers for the user during development.

## 6. Installer

A scripted installer that:
1. Finds the game: the registry `InstallPath`, a path the user passes, or a prompt.
2. **Repairs the truncated `InstallPath`** written by the original installer (observed: `...\Riddic\`; `research/02` §E2.1).
3. Identifies the build from the fingerprints. If it is 1.0, it tells the user patch 1.1 is required, where to get it, and how to run it (XP compatibility mode, Wine on Linux). On a build it doesn't recognise, it refuses to patch binaries and explains why.
4. Copies the loader (Ultimate ASI Loader as `winmm.dll`; MIT, credited), the plugin and its ini into `System\Win64_AMD64`.
5. Writes a sensible `Environment.cfg`: native resolution, desktop refresh rate, shader mode 2.0++ once tier 2 lands, vsync on.
6. Creates a shortcut that launches the x64 exe with the right working directory.
7. **Linux/Proton/Steam Deck:** sets the `winmm` DLL override (`WINEDLLOVERRIDES="winmm=n,b"`), and prints or applies the Steam launch option for a non-Steam shortcut.
8. Uninstalls cleanly: it removes only what it added and keeps a backup of every file it replaced.

Form: **PowerShell on Windows** and a **POSIX shell script on Linux/Deck**, sharing one data file (fingerprints and file list).
The themed GUI is tier 3.

## 7. Verification without the user playing

**Decided by the user (2026-10-29):**
- There is **one test machine**: the dev PC (Windows 11, RTX 3090, 2560×1440 @ 180 Hz). There is no second monitor, AMD/Intel GPU, Linux box or Steam Deck.
- **Code must be simulated, tested and reviewed as much as possible without running the game.** Automated game runs are allowed, but **sparingly**.

That gives a testing pyramid, from most used to least used:

1. **Offline tests, no game process** (the bulk of the work):
   - **Patch-table tests** against the real binaries on disk. Read the installed `RndrGL.dll`, `SbzEngine.exe` and others. For every patch site, check that the expected bytes are at that file offset on each known build and absent on the wrong builds. Also cover mismatch handling with mutated copies.
   - **Unit tests** for the pure logic: building the extension list (buffer bounds and termination), the fragment-program text rewrites (using the real `.fp` sources the game ships under `System\GL`), the display-mode filter (fed the 803-mode list captured from this machine), the ini and config writer, fingerprint matching, and the installer's file plan.
   - **Static reverse engineering** with headless disassembly (e.g. Ghidra headless, or capstone scripts) to confirm hypotheses before any game run. Examples: the 512-mode table limit, and the x86 versus x64 bloom path.
   - **A fake-GL harness:** a test executable that loads `BigFix.asi` against a stub `opengl32` and checks the hooks' behaviour (extension filtering, query translation, `glProgramStringARB` rewriting) without the game.
   - **Installer tests** in a temporary copy of the game folder's layout (fingerprinted dummy files plus real config files), including uninstall restoring everything byte for byte.
   - **Linux path:** test the shell installer under WSL2 against the same temp layout. Whether Wine can run the game under WSL2's GPU passthrough (WSLg, Mesa d3d12) is worth one experiment; if not, Linux and Deck runtime stay volunteer-verified.
2. **Short game smoke runs** (sparing): launch the x64 game in a borderless window, wait until `BigFix.log` reports rendering at the requested mode, take one capture in the menu or opening dock, then close. Used once per milestone or per risky change.
3. **Benchmark-demo replays** (rare): only for rendering fixes that can't be confirmed any other way (shadows, flicker, bloom). They show later levels, so **announce them first** and run them only when the user is away or agrees. Captures are judged by the agent and **never shown to the user**.

Existing tools these tiers build on:

- **Shipped benchmark demos:** patch 1.1 installs five demos (`Content\Demos\panoptical01–03.XDM`, `sodini01–02.XDM`) and a harness: `Riddick.exe -env <cfg> -exec "benchmark \"<demo>\""`, with results written to `BenchResults.txt`.
  These give deterministic replays across several levels, so they're the regression suite. Run each config (shader mode, fix on/off) on the same demo, capture at fixed frame numbers with the plugin's capture hook, and compare the images. Only the agent looks at these images.
- **Automated launch smoke test:** start the game, check through the log that it reached rendering at the requested resolution, then close it.
- **Frame-time logging** from the plugin, for performance regressions and to measure behaviour above 60 FPS.
- **apitrace** for single-frame GPU debugging (flicker, bloom).
- **Linux:** run the same smoke test and demos under Wine on a Linux machine or VM. Steam Deck is checked by hand when a Deck is available.
- **Upstream:** compare behaviour against TCoRFix 1.4 to show each change is an improvement.

## 8. Constraints

- **Toolchain on the dev machine:**
  - Visual Studio 2022 Build Tools (MSVC x64), CMake, MinGW-w64 g++/ninja, git, gh and Python.
  - MinHook via CMake FetchContent, as TCoRFix does, pinned to a commit.
- **Licences:** TCoRFix is MIT, Ultimate ASI Loader is MIT, MinHook is BSD-2. BigFix will be MIT.
- **Target binaries:**
  - EU 1.1 x64 at first: `SbzEngine.exe` `5056ffe0…`, `RndrGL.dll` `400dc6b5…`, `MSystem.dll` `b59eb2cc…`. The full list is in `research/fingerprints/efbb-eu-dvd-1.1.sha256`.
  - Patch sites need per-build offsets plus expected bytes.
- **Repo:** `BimingtonBill/butcher-bay-bigfix` (public, published 2026-11-01 as a fresh history; the private dev repo keeps the full history).
- **Game data stays outside the repo.** Captures stay in `D:\Captures\Riddick` or another ignored folder.

## 9. Calls made by the designer (the user can overrule them)

- **Borderless windowed is the default display mode, and moves to Tier 1.** It avoids the exclusive-fullscreen problems at once (640×480 takeover, minimise-on-focus-loss, mode restore), suits alt-tab and Wine, and makes automated test runs less intrusive. Exclusive fullscreen stays available as an option, with the 512-mode fix.
- **Releases are staged.** v0.1 ships Tier 1 alone. Tier 2 items ship as each is genuinely fixed.
  - Until the 2.0++ shadow/flicker fix lands, the installer defaults to **shader mode 5 (2.0)**.
  - 2.0++ remains selectable, and the README states the known trade-off. TCoRFix's workaround is never presented as a fix.
- **Unknown builds:** BigFix never patches files on disk, so the installer installs on any build that looks like 1.1, and warns if the fingerprints aren't recognised.
  - The plugin then applies only the patches whose expected bytes match, and logs the rest.
  - The installer refuses only when it detects 1.0, because patch 1.1 is required.
- **No 32-bit runs for research.** The x64 bloom bug is investigated with apitrace on x64, plus a static comparison of the x86 and x64 binaries and shader paths. No SecuROM circumvention, not even locally.
- **Hardware coverage:** each Tier 2 item is "done" when it is verified on the hardware we actually have. AMD, Intel/Mesa and Steam Deck results come from volunteer testers using the log and capture tools, and are tracked as open issues until confirmed.

## 10. Left to the builder

- Code layout, module split, and how patch sites and expected bytes are described. A data table is fine.
- Whether to keep using Ultimate ASI Loader or ship our own `winmm.dll` proxy. Keep UAL unless it causes problems under Wine.
- The ini format and option names (keep TCoRFix's keys readable as aliases).
- Log format, and the capture filename scheme.
- The order of work within each tier, and when to open upstream PRs to TCoRFix.
