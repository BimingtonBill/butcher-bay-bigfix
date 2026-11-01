# Research: PC issues with *The Chronicles of Riddick: Escape from Butcher Bay*

Collected 2026-10-28. This folder lists the problems people report when they play
*Escape from Butcher Bay* (EFBB) on modern Windows PCs, what fixes exist, and what is still unsolved.

The game is playable on PC in two forms, and their problems differ, so the issues are split by build:

| File | Contents |
|---|---|
| [01-versions-and-availability.md](01-versions-and-availability.md) | The two PC builds (2004 original and 2009 *Dark Athena* remake), DRM, delistings, the official patch 1.1 |
| [02-original-2004-issues.md](02-original-2004-issues.md) | Issues with the original OpenGL 1.5 Starbreeze-engine release, and their fixes |
| [03-dark-athena-remake-issues.md](03-dark-athena-remake-issues.md) | Issues with the EFBB campaign as bundled in *Assault on Dark Athena* |
| [04-community-fixes.md](04-community-fixes.md) | The tools people use (TCoRFix, DxWrapper, Ultimate ASI Loader, CRU/SRE, Proton overrides), with their known faults |
| [05-open-problems.md](05-open-problems.md) | Ranked list of problems with no complete fix: candidate targets for this project |
| [fingerprints/efbb-eu-dvd-1.0.sha256](fingerprints/efbb-eu-dvd-1.0.sha256) | SHA-256 of all 2,673 files of a fresh, unpatched EU single-DVD install (v1.0), relative to the game folder. In this install the 32-bit `SbzEngine.exe` files carry SecuROM (extra `.tcrb`/`.zrixzb` sections); `System/Win64_AMD64/SbzEngine.exe` has none |
| [fingerprints/efbb-eu-dvd-1.1.sha256](fingerprints/efbb-eu-dvd-1.1.sha256) | The same install after the official EU updater `riddick_update_eu_10_11.exe`: 2,786 files. 37 changed (all engine DLLs/EXEs, `Riddick.exe`, `GUI.xtc`, `Models.xtc`, dialogue/wave banks, `CubeWnd.xrg`), 113 added (`Benchmark\`, `Content\Demos\*.XDM`, `dbghelp.dll` ×4), none removed. SecuROM is still on the 32-bit exes. TCoRFix's `Fix Texture Size` byte matches in all four `RndrGL.dll` builds: a `JLE` (`7E`) after a compare with `0x800` (2048) |

## Sources and how far to trust them

- **PCGamingWiki** pages for [EFBB](https://www.pcgamingwiki.com/wiki/The_Chronicles_of_Riddick:_Escape_from_Butcher_Bay)
  (last edited 2025-10-16) and [Assault on Dark Athena](https://www.pcgamingwiki.com/wiki/The_Chronicles_of_Riddick:_Assault_on_Dark_Athena).
  The live site blocks scripted access (Cloudflare), so the text was read from Wayback Machine
  snapshots taken in 2025. Some wiki entries are marked "citation needed". This file keeps that mark.
- **[Nezarn/TCoRFix](https://github.com/Nezarn/TCoRFix)**: the source code, README, release notes (v1.0–v1.4) and issue tracker
  (#1–#9, read through the GitHub API). This is the primary source for the modern-GPU fixes.
- **Official patch 1.1 changelog**, as mirrored on the
  [PCGW Community files page](https://community.pcgamingwiki.com/files/file/679-the-chronicles-of-riddick-escape-from-butcher-bay-patch-11/) (Wayback snapshot).
- **Steam Community threads** for *Dark Athena* (app 9860): user reports. Each claim from them is marked with its date and the number of people who confirmed it.
- **[delistedgames.com](https://delistedgames.com/the-chronicles-of-riddick-assault-on-dark-athena/)** for store delisting dates.

Not read (blocked to scripted access): the Mesa issue tracker
([#5097](https://gitlab.freedesktop.org/mesa/mesa/-/issues/5097), [#11083](https://gitlab.freedesktop.org/mesa/mesa/-/issues/11083)), the ModDB file pages, and the WSGF page.
Only their titles or search-result summaries were seen, and the files say so wherever they are cited.

Confidence tags used in the files:
- **[verified]**: a primary source (changelog, fix source/README, or a wiki entry marked as verified) states it.
- **[reported]**: one or more users report it and no primary source confirms it.
- **[unconfirmed]**: seen only in a search summary or title.
