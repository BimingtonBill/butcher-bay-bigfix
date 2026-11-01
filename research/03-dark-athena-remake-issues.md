# 03 — Issues with EFBB as bundled in *Assault on Dark Athena* (2009)

This build is the remastered EFBB campaign shipped in *Assault on Dark Athena* (AoDA). Most people who bought EFBB digitally have this build.
Main source: PCGamingWiki *Dark Athena* page (Wayback snapshot, 2025) unless noted.

Config: `%LOCALAPPDATA%\Atari\The Chronicles of Riddick - Assault on Dark Athena\Environment.cfg`
Saves: `%APPDATA%\Atari\The Chronicles of Riddick - Assault on Dark Athena\SaveGames\<user-id>\`
Steam Play prefix: `steamapps/compatdata/9860/pfx/`

---

## A. Startup, DRM and crashes

| Issue | Fix / status | Tag |
|---|---|---|
| Crash to desktop on launch | Compatibility mode for Windows XP SP3 (PCGW) | verified |
| TAGES "Unexpected error (13:6)", activation failures on Win10 | Update TAGES; GOG `WIN32_X86` folder swap. See [01](01-versions-and-availability.md#tages-dark-athena-all-builds-except-gog) | verified |
| **`Starbreeze CCException` in `CRenderContextGL::GLSL_LoadSrc`** (`MRndrGL_GLSL.cpp(829)`, "OpenGL: no error") at launch | On an **RTX 5070 Ti laptop (Blackwell)** the crash happens at every shader mode and with every TCoRFix setting. Workaround: run on the Intel iGPU with `XR_SHADERMODE=0` ([TCoRFix #8](https://github.com/Nezarn/TCoRFix/issues/8), 2026-04, open). The same exception was reported years earlier on Win7 and CrossOver ([CodeWeavers forum](https://codeweavers.com/compatibility/crossover/forum/the-chronicles-of-riddick-assault-on-dark-athena?msg=171759), [Steam](https://steamcommunity.com/app/9860/discussions/0/611704531886621248)) | reported; **no fix on Blackwell** |
| GLSL startup crash on modern NVIDIA | A ModDB "Riddick AODA – OpenGL Crash Fix" exists, said to fix a GLSL incompatibility on modern NVIDIA cards. The page could not be opened ([ModDB tag](https://www.moddb.com/tags/riddick)) | unconfirmed |
| Crash with AMD Crimson drivers | Only answer given: use an older driver ([ReShade forum](https://reshade.me/forum/troubleshooting/938-the-chronicles-of-riddick-assault-on-dark-athena)) | reported |
| Linux: crash under Wine/Mesa; broken AO on Mesa | Mesa issues [#5097](https://gitlab.freedesktop.org/mesa/mesa/-/issues/5097) (GL_ATI_shader_texture_lod) and [#11083](https://gitlab.freedesktop.org/mesa/mesa/-/issues/11083) (ambient occlusion broken). The proprietary NVIDIA driver works. Only the titles were read | unconfirmed |

## B. Display

| Issue | Fix / status | Tag |
|---|---|---|
| **Game does not restore the desktop resolution** on exit (it starts at 640×480 fullscreen) | **Unresolved** (PCGW). Workaround: set `VID_MODE=1920 1080 32 60` or `VID_MODE=desktop` in `Environment.cfg` | verified |
| Refresh rate resets to **23 Hz** on each launch | Set correct `VID_DWIDTH`/`VID_DHEIGHT` in `Environment.cfg` | verified |
| Letterboxed on non-16:9, or wrongly on 16:9 | Fix `VID_DWIDTH`/`VID_DHEIGHT`. Per-aspect "loader" executables remove letterboxing (16:10, 5:4, 4:3) | verified |
| Ultrawide: only 2560×1080 works | **Unresolved** | verified |
| Windowed mode is smaller than the desktop (1920×1017) | DxWnd to get borderless (citation needed) | verified |
| Window too large for monitor with high DPI | `DarkAthena.exe` → Compatibility → High DPI → "Application". A user also reported this cures blurriness ([Steam](https://steamcommunity.com/app/9860/discussions/0/1456202492176141984)) | verified |
| AA turns off SSAO and vice versa | Turn on AA, then in the console enter `xr_ssao 1/2/3`. It is not saved | verified |
| Green colour filter, film grain, too much specular shine, fog | Delete `Content\Textures\ColorCorrection.xtc`; NoGrain mod (`System\GL\HL_Shading\XREngine_Final5.fp`); `xr_shaderspecularscale 1 0.2 1`; `xr_zfog 0`. Most are cosmetic preferences and are not saved | verified |

## C. Performance and timing

| Issue | Fix / status | Tag |
|---|---|---|
| Low performance on NVIDIA | Turn off *Threaded optimization* in the NVIDIA control panel (this may hurt some systems) | verified |
| Heavy stutter on NVIDIA | Do not use the "Prefer layered on DXGI Swapchain" Vulkan/OpenGL present method; use Auto or Prefer native | verified |
| **Gravity pull wrong above 60 FPS** (Gravity Generator section) | Cap at 60 FPS with RTSS or Special K. This is in the AoDA campaign, but it shows the engine has frame-rate-dependent physics. Assume EFBB may hide similar bugs | verified |
| Heavy AA cost | Turn AA off | verified |

## D. Saves, input, audio

| Issue | Fix / status | Tag |
|---|---|---|
| **Save files delete themselves if the PC enters screen-saver mode** while the game runs | **Unresolved.** Quit before leaving the PC, or turn off the screen saver (PCGW) | verified — data loss |
| No quick save | Console: `bind f5 savegame('quick')`, `bind f8 loadgame('quick')`. The bindings are lost on exit. F9 loads the latest save | verified |
| Controller must be connected before launch; no button prompts, no rumble | No fix | verified |
| Only the music volume can be set separately; surround is 5.0 and may need Windows set to 7.1 | No fix | verified |
| Multiplayer: official servers shut down; LAN only | n/a | verified |
| macOS build is 32-bit only, so it does not run on Catalina or later | n/a | verified |
