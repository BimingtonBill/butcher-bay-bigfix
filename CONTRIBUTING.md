# Contributing to BigFix

Start with the [README](README.md), [volunteer test script](docs/VERIFY.md) and
[compatibility ledger](docs/COMPAT.md). Hardware reports are useful without any
code changes. Use the bug or hardware verification issue form and attach logs.

## Build and test

The plugin targets **Windows x64, C++17**. Install Visual Studio 2022 Build Tools
with Desktop development with C++, a Windows SDK, CMake 3.20+, Git, Python 3,
and PowerShell 7 (`pwsh`). Git Bash runs the POSIX installer tests on Windows.
CMake fetches the pinned MinHook commit and doctest tag on first configure;
internet access is required then. Use a short checkout path on Windows, or
enable Git long-path support (as the CI workflow does) to avoid dependency
checkout failures. From the repository root:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
python -m pip install pytest pyyaml
python -m pytest -q tools/tests installer/tests
```

The plugin is `build/Release/BigFix.asi`; ship it with
`installer/BigFix.default.ini` renamed to `BigFix.ini` and an x64 Ultimate ASI
Loader renamed to `winmm.dll`. `tools/package.ps1` creates the complete release:

```powershell
pwsh -NoProfile -File tools/package.ps1 -BuildDir build/Release -OutDir dist
```

Packaging downloads the pinned Ultimate ASI Loader archive and verifies its
SHA-256. Offline packaging tests use a dummy loader, not an unverified download.
The default CMake path matches [.github/workflows/build.yml](.github/workflows/build.yml).
There is no Linux-native ASI build target; Wine/Proton runs the Windows plugin.

Most native tests use synthetic modules/GL fixtures, without launching the game.
Reference-binary and shader checks report skips when `RIDDICK_DIR` is unset.
Optionally set it to your own legally installed game root before `ctest`:

```powershell
$env:RIDDICK_DIR = 'D:\Games\Riddick'
ctest --test-dir build -C Release --output-on-failure
```

Keep the game outside the checkout. Installer tests operate on disposable dummy
trees and do not touch the real registry or game installation. `pwsh` is needed
by their shared fixtures; `bash` is needed for `test_install_sh.py`. Without the
required tools tests may skip: report skips, not just the pass count. On a Linux
or WSL machine with Python, pytest, PowerShell 7 and bash available, run
`python -m pytest -q installer/tests/test_install_sh.py` too. This checks the
installer, not Wine/Proton rendering.

## Changes and evidence

- Keep patches scoped to their owning module. Preserve byte verification and
  log a skipped patch with its reason. Do not guess offsets on unknown builds.
- Add regression coverage for behavior changes; run native tests for plugin
  changes and the Python suites for installers/tools. Include exact commands,
  failures/skips, and test hardware in your pull request.
- Run the [short verification script](docs/VERIFY.md) for a relevant visual
  change when hardware is available. Mark screenshots from later levels as
  spoilers; opening-dock evidence is preferred. Never claim AMD/Intel/Deck
  support from simulated or NVIDIA-only tests.
- Credit derived work and retain license notices. Update docs and the
  compatibility matrix only with linked evidence.

## Files that must never be committed or released

No game executables, DLLs, assets, saves, disc images, cracks, no-CD files, or
official patch installers. **Fingerprints only** for identifying game builds;
store SHA-256 metadata under `research/fingerprints/`, never the corresponding
game bytes. Keep local binaries, traces and captures outside the repository or
in ignored folders. Review logs and documentation for personal paths before
posting. BigFix's own ASI and licensed third-party loader are release artifacts;
they are not game files.
