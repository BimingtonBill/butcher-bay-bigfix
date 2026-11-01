# 05 — Open problems (no complete fix as of 2026-10-28)

Ranked by impact × number of reports. "Build" means O = 2004 original, DA = *Dark Athena* EFBB campaign.

| # | Problem | Build | Why it matters | Evidence |
|---|---|---|---|---|
| 1 | **Saves deleted when the screen saver starts** | DA | Silent data loss | PCGW "Issues unresolved" |
| 2 | **GLSL_LoadSrc crash on NVIDIA Blackwell (RTX 50)**; older GLSL crashes on some drivers | DA | Game will not start on current NVIDIA hardware | [TCoRFix #8](https://github.com/Nezarn/TCoRFix/issues/8); Steam/CodeWeavers reports |
| 3 | **Fullscreen refuses native 1440p/4K** even with TCoRFix; needs CRU/SRE | O | Most modern monitors are affected; the workaround needs system-wide display changes. Reproduced on our rig; probable cause is a 512-entry mode table, see 02 §B2a | [#7](https://github.com/Nezarn/TCoRFix/issues/7) (3+ users) |
| 4 | **The fix stack is fragile**: 32-bit vs 64-bit exe conflicts (SecuROM-free x64 vs correct bloom and DxWrapper on x86), wrong loader name or folder | O | Most failed installs | [04](04-community-fixes.md#the-fix-stacking-problem) |
| 5 | **Eyeshine flicker fix removes character shadows** in 2.0++ | O | The best-looking mode is still broken one way or the other | PCGW; [#1](https://github.com/Nezarn/TCoRFix/issues/1) |
| 6 | **Bloom bug in the ship cutscene on the x64 exe** | O | Visual bug; pushes players onto the x86 exe | PCGW; [#1](https://github.com/Nezarn/TCoRFix/issues/1) |
| 7 | **HUD and subtitles stretched** in widescreen | O | Visible all the time | PCGW; [#1](https://github.com/Nezarn/TCoRFix/issues/1) |
| 8 | **Mouse: acceleration, no raw input**; the >125 Hz fix depends on DxWrapper (x86 only) or TCoRFix 1.4 | O | Feel of aiming | PCGW; [#1](https://github.com/Nezarn/TCoRFix/issues/1), [#5](https://github.com/Nezarn/TCoRFix/issues/5) |
| 9 | **No FOV option** | O | Comfort on wide screens | PCGW; [#1](https://github.com/Nezarn/TCoRFix/issues/1) |
| 10 | **AA only through the console, not saved, broken on AMD**; no SSAA | O | Image quality | PCGW; [#1](https://github.com/Nezarn/TCoRFix/issues/1) |
| 11 | **No borderless windowed** | O, DA | Alt-tab and multi-monitor use | PCGW (both) |
| 12 | **Desktop resolution not restored**; refresh rate resets to 23 Hz; ultrawide limited to 2560×1080 | DA | Annoying; config workarounds exist | PCGW |
| 13 | **Frame-rate-dependent physics** (gravity bug >60 FPS) | DA (AoDA part) | May hide other bugs; no built-in cap | PCGW |
| 14 | **Hardware EAX lost** on modern Windows; mutes when it loses focus | O | Audio quality and alt-tab | 1.1 changelog; PCGW; [#6](https://github.com/Nezarn/TCoRFix/issues/6) |
| 15 | **XP not supported by TCoRFix** | O | A small group of players (retro/EAX builds) | [#6](https://github.com/Nezarn/TCoRFix/issues/6) |
| 16 | **Cannot be bought**: delisted from Steam (2013) and GOG (2017); TAGES activation unreliable | O, DA | Licensing; out of scope for code, but an installer must handle retail and GOG layouts | [delistedgames](https://delistedgames.com/the-chronicles-of-riddick-assault-on-dark-athena/), [Steam](https://steamcommunity.com/app/9860/discussions/0/1751232561618847165) |

## Gaps in this research
- The live PCGW site, ModDB file pages, WSGF and the Mesa tracker blocked scripted access. Their content here comes from 2025 Wayback snapshots or search summaries.
  Before building on items tagged [unconfirmed], read them in a browser.
- Reddit was not searched, and only some Steam threads were read. The 3DMGame and Chinese/Russian forums were not checked; the EFBB community there is large.
- No issue in this research was reproduced on real hardware.
