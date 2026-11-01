# 04 — Community fixes and their known faults

## TCoRFix (Nezarn): the main fix for the 2004 original
- Repo: <https://github.com/Nezarn/TCoRFix>. Description: "OpenGL fixes/workarounds for modern GPUs". 54 stars. Last release v1.4 (2025-03-24). It is an ASI plugin.
- **Install:** extract to `System\Win64_AMD64\` or `System\Win32_x86[_SSE|_SSE2]\`. Add Ultimate ASI Loader's `dinput8.dll` **renamed to `winmm.dll`**. Requires patch 1.1.
- **ini switches** (since v1.3 all are **off by default**): `Enable 2.0++ on any GPU`, `Fix Misc Bugs` (GL_MAX_TEXTURE_UNITS), `Fix Texture Size` (>2048 px), plus the Eyeshine flicker workaround.
- **Release history:**

  | Version | Date | Changes |
  |---|---|---|
  | v1.0 | 2024-04-13 | Extension-list filtering, which fixes the startup crash |
  | v1.1 | 2024-11-23 | Fixed x86 support; first Eyeshine flicker workaround |
  | v1.2 | 2025-03-13 | Partial rewrite; 2.0++ on any GPU incl. Mesa (NV occlusion query → ARB); new flicker workaround |
  | v1.3 | 2025-03-16 | >2048 px fix; defaults changed to off |
  | v1.4 | 2025-03-24 | Typo fix that caused crashes; fix for fast high-DPI mouse |

- **Open issues and faults:**
  - The flicker fix removes some shadows ([#1](https://github.com/Nezarn/TCoRFix/issues/1)).
  - Fullscreen >1080p is still refused on some setups ([#7](https://github.com/Nezarn/TCoRFix/issues/7)).
  - XP is unsupported because of `AcquireSRWLockExclusive` ([#6](https://github.com/Nezarn/TCoRFix/issues/6)).
  - Loader-not-loading reports ([#9](https://github.com/Nezarn/TCoRFix/issues/9)).
  - Does not cover *Dark Athena* ([#8](https://github.com/Nezarn/TCoRFix/issues/8)).
  - Not done from the wishlist in #1: HUD/subtitle aspect fix, FOV, borderless, raw input, persistent AA, SSAA.
- **Linux/Proton:** add the launch option `WINEDLLOVERRIDES="winmm.dll=n" %command%`.

## Ultimate ASI Loader (ThirteenAG)
- <https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases>. Use the x64 or x86 build to match the exe. Rename it to `winmm.dll`.
- A special build is needed for XP (author's remark in [#6](https://github.com/Nezarn/TCoRFix/issues/6)).

## DxWrapper (elishacloud)
- Fixes mouse twitch above 125 Hz with `FixHighFrequencyMouse=1` under a dinput→dinput8 wrapper. `LoadPlugins=1` also loads `.asi` files, so it can replace Ultimate ASI Loader.
- **Works only with the 32-bit exe.** It crashes the 64-bit exe (PCGW; [#1](https://github.com/Nezarn/TCoRFix/issues/1)).

## NVIDIA Profile Inspector
- Set *Extension Limit* = `0x00001B58` (NVIDIA only). This is the older fix for the startup crash, before TCoRFix.

## CRU / SRE (ToastyX)
- Change the display mode list so the game accepts 1440p/4K in fullscreen ([#7](https://github.com/Nezarn/TCoRFix/issues/7)). Results vary: CRU worked for one user, and SRE (deleting most modes) worked for another.

## Executable rename trick
- `SbzEngine.exe` → `quake3.exe` (in `System\Win64_AMD64`). The driver applies its built-in extension-limit profile for quake3.

## For *Dark Athena*
- TAGES update, or the GOG `WIN32_X86` folder swap. Aspect-ratio loaders. DxWnd for borderless. RTSS/Special K for a 60 FPS cap.
  ModDB "OpenGL Crash Fix" (unverified). Console tweaks (`xr_ssao`, `xr_softstencil 1`, `xr_shadowmapmode 0`, `xr_zfog 0`).
  Commentary DLC unlocked by `_profile` keys `*GAME_FINISHED_EFBB 1` and `*GAME_COMMENTARY 1.000000`.

## The fix-stacking problem
A player of the original currently has to combine several pieces: patch 1.1, the 32-bit exe (for correct bloom and DxWrapper), a working way around SecuROM
(the 64-bit exe is the DRM-free one, which conflicts with the first two), TCoRFix with hand-set ini values, an ASI loader or DxWrapper, CRU/SRE for high resolutions, and console commands for AA.
Most user reports in the TCoRFix tracker come from mistakes in this stack (wrong folder, wrong loader bitness, wrong exe), not from bugs in a single fix.
