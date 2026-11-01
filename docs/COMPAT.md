# Hardware compatibility evidence

This is an evidence ledger, not a support guarantee. Only Windows 11 / RTX 3090
is available to the developer (EU DVD 1.1 x64, 2560×1440 @ 180 Hz). “Observed”
means the existing README reports a result on that machine; it does not certify
every driver or the current build. “Untested” requires a volunteer game run.
Installer fixtures and fake-GL tests do not certify Linux or another GPU vendor.

| GPU / OS or runtime | Starts | Native / borderless | 2.0++ shadows / Eyeshine | Ship bloom | AA | HUD 4:3 | FOV / hands | Raw mouse | Audio |
|---|---|---|---|---|---|---|---|---|---|
| NVIDIA RTX 3090 / Windows 11 | observed [^1] | observed [^1] | untested (visual confirmation pending) [^1] | known issue [^1] | observed [^1][^2] | untested [^1] | untested (hands coupled) [^1] | untested | untested |
| NVIDIA / Windows 10 | untested | untested | untested | untested | untested | untested | untested | untested | untested |
| AMD / Windows 10–11 | untested | untested | untested | untested | untested | untested | untested | untested | untested |
| Intel / Windows 10–11 | untested | untested | untested | untested | untested | untested | untested | untested | untested |
| NVIDIA / Linux Wine | untested | untested | untested | untested | untested | untested | untested | untested | untested |
| NVIDIA / Linux Proton | untested | untested | untested | untested | untested | untested | untested | untested | untested |
| AMD / Linux Wine or Proton (Mesa) | untested | untested | untested | untested | untested | untested | untested | untested | untested |
| Intel / Linux Wine or Proton (Mesa) | untested | untested | untested | untested | untested | untested | untested | untested | untested |
| Steam Deck LCD / SteamOS Proton (AMD) | untested | untested | untested | untested | untested | untested | untested | untested | untested |
| Steam Deck OLED / SteamOS Proton (AMD) | untested | untested | untested | untested | untested | untested | untested | untested | untested |

[^1]: [README status, feature notes and known issues](../README.md).
    Startup, 1440p borderless and NVIDIA AA are reported there; Eyeshine and HUD
    visual acceptance remain pending, bloom remains unresolved, and independent
    hands FOV is not implemented. The available evidence does not establish manual
    raw-mouse/audio acceptance, so those cells remain untested.

[^2]: [AMD/Intel/Mesa AA research](../research/re/amd-aa.md).
    RGB/alpha translation and depth24/no-accumulation retries have game-free
    coverage, with native chooser tests on NVIDIA. Missing AMD/Intel/Mesa runtime
    evidence remains explicit. Fallback is fewer MSAA samples, then the original
    no-AA path; there is **no automatic supersampling**. Driver DSR/VSR or external
    downscaling are separate opt-in workarounds, not verified plugin features.

Use [VERIFY.md](VERIFY.md) and the hardware report issue form. Each new accepted
result should link its issue and name GPU, driver, OS/runtime, BigFix version,
scene and per-item outcome. Keep failures and untested items visible. Separate
driver/runtime versions and Deck Gaming/Desktop Mode when results differ.
