# Optional DSOAL dependency build and verification

The approved behavior is [research/re/audio-eax.md](../research/re/audio-eax.md),
option 1. Source and artifact pins live in `installer/dsoal-release.json`;
`python installer/gen_data.py` exports installer JSON/text component plans.
No game files or dependency DLLs belong in git.

## Packaging

```powershell
pwsh -NoProfile -File tools/package.ps1 -BuildDir build/Release -OutDir dist -IncludeDsoal
```

Downloads are cached by SHA-256 under ignored `local/audio-cache/` and checked
again on every use. The archive is the verified upstream r697 Windows build,
not an independently built BigFix binary. Its Win64 pair has the exact source
revisions recorded below and in its bundled version files. Changed upstream
bytes fail closed, including changes to OpenAL under the same DSOAL revision.
For offline packaging, pass `-DsoalZip PATH` and `-DsoalSourceDir DIR`, with
`dsoal-source.zip` and `openal-soft-source.zip` in that directory. All normal
pins still apply. `-DsoalDataFile` is an offline-test-only override and requires
both local inputs; tests use synthetic PE files, never a network download.

Audio-enabled packages add the DLL pair at package root and source archives,
license/dependency notices and source links under `audio/`. Ordinary packages
contain no audio DLLs and install system audio by default.

## Rebuilding from the pinned source

The complete source archives are distributed beside the optional binaries.
They include CMake build files and vendored dependencies. The upstream
[Windows build workflow](https://github.com/kcat/dsoal/blob/e11128250439d586483ee6598d1670ab52ab5e37/.github/workflows/Untagged.yaml)
records the configuration; its `windows-latest` compiler image is not a
bit-reproducible toolchain pin. To reproduce the configuration using VS 2022
C++ Build Tools and a Windows SDK, unpack the source archives and run:

```powershell
cmake -S dsoal-e11128250439d586483ee6598d1670ab52ab5e37 -B local/dsoal-build -A x64
cmake --build local/dsoal-build --config Release
cmake -S openal-soft-1fbc89b2279280ab89509b5d5adcd43b941ee0a1 -B local/openal-build -A x64 -DALSOFT_EAX=ON -DALSOFT_BUILD_ROUTER=ON -DALSOFT_REQUIRE_WINMM=ON -DALSOFT_REQUIRE_DSOUND=ON -DALSOFT_REQUIRE_WASAPI=ON
cmake --build local/openal-build --config Release
```

Use the resulting `dsound.dll` and rename `soft_oal.dll` to `dsoal-aldrv.dll`.
Locally rebuilt DLLs have different hashes and are intentionally rejected by
the release installer. Updating a release pin requires validating the new
pair, recording all hashes/notices/source provenance, regenerating the data,
and rerunning the fixtures. Keep the DLLs separately replaceable for LGPL
relinking/replacement; BigFix has no runtime hash enforcement.

## No-game verification

```powershell
cmake -S . -B build -A x64 -DBIGFIX_DSOAL_DIR:PATH=C:/path/to/verified/Win64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
python -m pytest -q tools/tests installer/tests
```

Without `BIGFIX_DSOAL_DIR`, four dependency tests report CTest skips. Supplying
it requires the pinned pair's hashes. The fixture copies itself and the pair
to a temporary directory and starts with an unrelated working directory, so
DSOAL's application-directory renderer search matches deployment. It checks
the real loaded paths, factory, hardware 3D voice capacity (at least 64;
recorded capacity 128), all six property-set support gates, slot Set/Get
fallback, 28/16/112/32-byte traffic, changed room packets and source routing.
It also checks EAX disabled in OpenAL and a missing backend. No buffers are
played to an output device.

The separate OpenAL loopback path renders a synthetic impulse into memory:
dry routing and two room decays must produce finite samples and distinct,
nonzero late wet energy. Slot zero is a locked legacy slot in this pair;
its configuration Set fails and the existing-reverb Get succeeds, exactly
the fallback retained in the engine. Room/source setters must still succeed.
Do not equate this with a real game's own resource-pool requirement or mix.

The fake-DS tests exercise BigFix's real hooks independently of the dependency
tests. They model the researched selection algorithm and verify negative
gates, payload sizes, COM identity/refcounts, duplicates, repeated restarts,
two vtables, focus flags and bounded/disabled tracing. They do not prove that
game control flow ran. Installer/package tests use temporary dummy layouts
and injected hashes only.

Still required separately: opening-dock game selection/room changes,
streaming/notifications, listening/spatial orientation, performance, speakers
and headphones, and Wine/Proton/Deck behavior. Never infer those results from
the offline fixtures. No game was launched during this implementation.
