# 01 — PC versions, availability and DRM

## Two different PC builds

| | Original EFBB (2004) | EFBB inside *Assault on Dark Athena* (2009) |
|---|---|---|
| Developer / publisher | Starbreeze + Tigon; Vivendi Universal / Sierra | Starbreeze; Atari |
| Renderer | OpenGL 1.5, shader modes 1.0/1.1/1.4/2.0/2.0++ (stencil soft shadows) | Newer Starbreeze renderer, GLSL shaders (`RndrGL.DLL`) |
| Executables | 32-bit (`System\Win32_x86*`) and 64-bit (`System\Win64_AMD64`) | 32-bit `System\Win32_x86\DarkAthena.exe` |
| Released | 2004-12-03 (Windows) | 2009 |
| DRM | SecuROM 5 on retail; **the 64-bit exe is DRM-free** | TAGES + Solidshield (except the retired GOG build) |
| Sold today | No: retail only (USA: 5 CDs, EU: 1 DVD) | No: delisted from Steam and GOG |

Sources: PCGamingWiki EFBB page (infobox, Availability, API sections); PCGamingWiki *Dark Athena* page (Availability, API).
*Dark Athena* started as a remake of EFBB, and its own campaign was added later
([Wikipedia](https://en.wikipedia.org/wiki/The_Chronicles_of_Riddick:_Assault_on_Dark_Athena), [Destructoid dev diary](https://destructoid.com/?p=41387)).

The two builds have different bugs. TCoRFix supports **only the 2004 original**. Its author said so on
[TCoRFix #8](https://github.com/Nezarn/TCoRFix/issues/8) (2026-04-10): "TCoRFix is for the original game and not for Dark Athena".

## Availability problem: no legal digital copy on sale

- *Dark Athena* was delisted from **Steam between 2013-03-15 and 2013-04-12**, and the delisting may be tied to TAGES activation-server problems.
  It was delisted from **GOG on 2017-03-09**, probably because of Universal licensing ([delistedgames.com](https://delistedgames.com/the-chronicles-of-riddick-assault-on-dark-athena/)). [verified: store record]
- The original EFBB is available only on retail discs. Users of abandonware repacks (e.g. "MagiPack", "Developer's Cut" from MyAbandonware)
  report that fixes behave differently on those copies ([TCoRFix #1 comments](https://github.com/Nezarn/TCoRFix/issues/1), [#9](https://github.com/Nezarn/TCoRFix/issues/9)). [reported]
- Effect for this project: any fix must work on **the retail 1.1 install** and should be tolerant of common repacks.

## DRM problems

### SecuROM (original EFBB, retail)
- Modern Windows (10/11) does not run the SecuROM disc check. PCGamingWiki notes that the **64-bit executable is DRM-free**, which is the usual way around it. [verified: PCGW]
- However, the 64-bit exe has its own bugs (see 02: ship-cutscene bloom; DxWrapper crashes on it).

### TAGES (*Dark Athena*, all builds except GOG)
- Activation limit: 3 activations. After the third, TAGES starts a **30-day timer**, then restores activations up to the original 3 (PCGW *Dark Athena*, Version differences). [verified: PCGW]
- "**Unexpected error (13:6)**": the fix is to update TAGES (PCGW, Issues fixed). [verified: PCGW]
- Steam thread ["(my) TAGES issue, SOLVED"](https://steamcommunity.com/app/9860/discussions/0/1751232561618847165) (2018-11-08 to 2021-04): fails to launch on Windows 10 after working on Windows 7.
  The fix that worked for 5+ users: install a new TAGES build from tagesprotection.com, run Steam as administrator, run the game, enter the serial number, then revert to normal mode.
  It failed for others, who got errors **13:5**, **13:6**, **Error 6**, a hang at "gathering data", or "invalid serial number". Steam support declined the ticket. [reported]
- **"TAGES Fix"**: replacing the `WIN32_X86` folder with the folder from the (DRM-free) GOG release makes Steam and retail copies playable (PCGW, citation [9]). [verified: PCGW]
- **Patch 1.01** (Windows/OS X) tries to install or update the TAGES driver. Do **not** apply it to the GOG version (PCGW). [verified: PCGW]

## Official patch for the original: 1.1

Changelog ([PCGW Community mirror](https://community.pcgamingwiki.com/files/file/679-the-chronicles-of-riddick-escape-from-butcher-bay-patch-11/)): [verified]

- EAX is now disabled by default ("to fix various issues with incompatible drivers").
- Fixed a bug in OpenGL version detection so the game runs on OpenGL 2.0 drivers.
- Auto shader mode no longer chooses 2.0++.
- Refuses to run on Windows XP without SP1.
- Fixed the weapon switch bound to the mouse wheel.
- Updated some outdated model textures.
- Added an option to enable the gamepad, which is now disabled by default.
- Added an option to turn model level of detail off.
- Crash dumps are written on a crash.
- Added benchmark scripts.
- **Install note:** on Vista and later, run the patch installer in Windows XP compatibility mode.

TCoRFix **requires patch 1.1** (PCGW). Without 1.1 the original fails on any OpenGL 2.0+ driver, which in practice means every GPU made since about 2005.
