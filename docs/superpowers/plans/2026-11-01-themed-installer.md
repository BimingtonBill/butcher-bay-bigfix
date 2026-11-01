# Themed installer implementation plan

**Goal:** Deliver roadmap item 18 as a Windows PowerShell 5.1 / PowerShell 7 WinForms front end.

**Architecture:** `install.ps1` remains the sole owner of detection, fingerprints, settings writes, registry repair, backup, install and uninstall. Add read-only `-Inspect` JSON and validated `-IniOverrides` / `-ShaderMode` inputs there. `BigFixSetup.ps1` builds arguments, runs that backend in a hidden child process, polls output asynchronously, and presents controls and errors.

**Spec:** `docs/superpowers/specs/2026-10-29-butcher-bay-bigfix-design.md`, sections 3 and 6, plus the implementation brief.

**Constraints:** No external dependencies or copied game artwork. No real game writes. Preserve CLI defaults, unknown-build support, EU10 refusal, explicit registry-repair consent and reversible uninstall. Name staged paths; commit only, no push or merge. Work inline as the implementation sub-agent.

**Design:** Coal #141618, steel #222629, bone #E7E4DD, amber #FFBA63, muted text #C3BAB0. Condensed system-font title over a drawn industrial gradient; Segoe UI controls; left-aligned options, a stable action row and a growing scrolling log. Native focus cues, mnemonics, Enter/Esc, DPI scaling.

## Tasks

- [x] Add failing pytest coverage: both PowerShell parsers, headless option plan, delegation boundary, read-only inspection for known/unknown/EU10, settings validation and install/uninstall round-trip.
- [x] Extend backend at the existing detection/config-writing layers; implement GUI, `.cmd` launcher, package entries and README usage.
- [x] Run `python -m pytest -q tools/tests installer/tests`; render off-screen with DrawToBitmap and inspect it; review safety and compatibility; commit named paths.

## Review focus

- Paths with Unicode, quotes and spaces must survive process argument transport.
- EU10 and invalid settings must fail before any mutation.
- Registry repair must retain consent even when setup is already elevated.
- UI must remain responsive while both output streams are drained; closing cannot abandon an active install.
- Preview/headless inspection must never launch the game or change its files.

## Verification and review

- Final required suite: **141 passed**. Both installed PowerShell hosts exercised syntax, headless planning, encoded process transport, off-screen rendering and the real preview message loop.
- Regression checks prove invalid settings/display input fail before file or registry writes, selected options survive preview, small windows scroll, and uninstall restores the original sandbox files.
- Rendered and inspected `local/installer-gui.png` using a synthetic game tree. Text palette contrast is at least 9.48:1. No real game writes; live UAC and real-game launch were not exercised.
- Ruling: review inline because this is the implementation sub-agent; no further delegation. Keep defaults at shader mode 5, and offer mode 6 explicitly, matching the existing documented visual limitations.
