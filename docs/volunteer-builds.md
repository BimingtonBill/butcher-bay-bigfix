# Volunteer build recognition

US 5-CD 1.1, Russian, Polish and repack builds need volunteer evidence. No new
regional build is enabled by this workflow alone. EU DVD 1.1 remains the only
verified plugin layout; EU 1.0 still needs the official 1.1 patch.

## Collect a report

Install Python 3.9 or newer. From outside the game folder, run either command
from a source checkout, replacing the game path:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\fingerprint.ps1 -GameDir 'D:\Games\Riddick' -Output 'bigfix-fingerprint.json'
```

```sh
sh tools/fingerprint.sh --game-dir '/path/to/Riddick' --output 'bigfix-fingerprint.json'
```

Release zips put these tools next to the installers: use `./fingerprint.ps1` or
`fingerprint.sh` there. Both wrappers use the same Python collector and bundled
`bigfix-data.json`. Output must be outside the game folder and must not already
exist. The tools do not load or execute game binaries, change the game or registry,
or upload anything. Review the JSON, then attach it to a new issue using
[new-build.yml](../.github/ISSUE_TEMPLATE/new-build.yml). Do not submit game files,
cracks, keys, discs or patch installers.

The report lists each file in `reportFiles`, the union of the generated build
file sets: x64 System modules, the two x64 Content DLLs and all three x86 engine
variants. Missing/unreadable files are explicit. Each readable file gets its
SHA-256, size and PE timestamp (null for invalid PE). No absolute installation
path is included. On Windows, an inexpensive resource query also records the
numeric file version when available; it is null on POSIX or if absent/too large.
Version resources are hints, not build identity.
Language hints contain names only: `Content_*`, `ExtraContent_*`, localized
`Registry/StringTable_*.txt`, root readme and licence files. Presence does not
prove the selected language. These patterns come from a read-only inspection
of the EU install (`Content_Fre`, `Content_Ger`, `StringTable_Eng.txt`).

Each fixed plugin verification site records its source, reference build, RVA,
expected bytes, raw file offset, actual bytes and match/mismatch/unmapped status.
The collector maps RVAs through validated PE section headers; it does not treat
an RVA as a raw offset or scan for similar bytes. Probes are at most 96 bytes.
The report includes diagnostic/test-only sites and semantic caller checks.
Export, IAT and vtable hooks resolve symbols/pointers at runtime and have no
fixed on-disk probe.

## Installer safety policy

`installer/gen_data.py` owns the policy shared by both installers. It exports
the plugin's existing C++ verification tables into `patchSites` and derives
`patchedFiles` from their modules. The generated JSON and POSIX fallback text
carry the same required paths and supported build IDs. Neither installer needs
Python when using its native data format.

- Exact match: every listed file matches a known, supported build.
- Partial match: every patched file matches **one** supported build; other files
  differ or are absent. Every difference is printed, and installation is allowed.
  Examples include replaced `dbghelp.dll`, x86 no-CD executables or an x64 engine
  replacement with unchanged patched DLLs. This recognizes compatible modules;
  it does not establish a repack's origin or endorse its executable.
- Unknown: a patched file differs, is unreadable/missing, or no supported build
  supplies the complete required set. Refuse before changing files or registry;
  print the collector command and issue template path. Missing policy data also
  fails closed. Individually known DLLs from different builds cannot be combined.

The plugin retains its live expected-byte checks, fingerprint gates and safe
skips. The installer does not relax them or patch game files on disk.

## Import and review

From a clean checkout, import a volunteer report with one command:

```sh
python installer/gen_data.py --import-report /path/to/bigfix-fingerprint.json --build-id US11 --label 'US 5-CD 1.1'
```

This adds a hash-only candidate to `installer/volunteer-builds.json` and regenerates
both data files. It validates known paths, digests, sizes, schema and unique build
IDs; arbitrary report content is discarded. All patched files must be present
to import a complete candidate. Imported candidates always have `supported: false`,
even if every probe matches. Keep the original issue/report as review evidence;
do not copy game files into this repository.

Review the diff before admitting a candidate. If all patched module hashes equal
one existing verified build, the installer can already accept it as a partial
match; retaining its label/hash set is optional. For changed patched modules:

1. Review every report entry with `reviewRequired`, using the named C++ source.
   A matching short prologue alone does not establish function semantics. Verify
   instructions, semantic callers, internal data/layout offsets and referenced
   helpers, including `widescreen_hooks.cpp`, `video_trace.cpp` and
   `eyeshine_variants.cpp`. Mismatches/unmapped sites require verified new RVAs.
2. Extend the responsible plugin tables/build identification and any explicit
   module hash gates (`fingerprint.cpp`, `window_mode.cpp`,
   `eyeshine_variants.cpp`) with verified build-specific data. Do not replace EU
   entries, reuse EU offsets on assumption, or bypass expected-byte checks.
   The exporter in `gen_data.py` currently labels the EU tables as EU11; extend
   it alongside any future build-specific table format.
3. Add binary-site evidence/tests and perform the relevant game checks with the
   volunteer. Only then set the candidate's `supported` field to `true` and run
   `python installer/gen_data.py` again. Review and commit the candidate, plugin
   changes, evidence and generated files together.

Run `python -m pytest -q tools/tests installer/tests`. Inspect deterministic
regeneration with `git diff` and retain the plugin's version-matched native tests
when changing C++. This workflow collects evidence; it does not automatically
turn an unowned build into a verified one.
