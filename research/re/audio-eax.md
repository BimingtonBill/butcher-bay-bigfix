# EAX audio on the EU 1.1 x64 build

2026-11-01. Static RE only: no game process, target DLL loading, game-folder
writes, or listening tests. Scope: design tier 3 item 17 / v0.5, following
[the design](../../docs/superpowers/specs/2026-10-29-butcher-bay-bigfix-design.md),
[the earlier DirectSound investigation](v0.2-input-timing-audio-aa.md#3-audio-focus),
[original issues §E](../02-original-2004-issues.md#e-audio), and
[open problem 14](../05-open-problems.md).

## Decision

**Recommend an optional, installer-managed x64 DSOAL + OpenAL Soft pair.**
The game already implements EAX 4 room reverb on x64. Missing `eax.dll` does
**not** disable its EAX property path: it falls back to ordinary
`dsound.dll!DirectSoundCreate8`, then probes the returned buffers for EAX 4.
DSOAL implements those property sets on its ordinary DirectSound objects.
No `eax.dll`, invented reverb presets, binary patch, or EAX factory shim is
needed for the first implementation.

This establishes architectural feasibility, not runtime compatibility or
audible equivalence to a Creative card. A matched dependency pair still needs
the offline API tests below before bundling. Keep the feature opt-in until
opening-dock verification is separately authorized and completed.

## Reproducibility and binary identity

All instruction addresses below are **module-relative RVAs**, hexadecimal.
x86 operands such as `0x101488d0` retain their preferred virtual address;
subtract preferred image base `0x10000000` to obtain their data RVA.
These are the unmodified installed files; every hash agrees with the existing
[EU 1.1 manifest](../fingerprints/efbb-eu-dvd-1.1.sha256).

| File relative to game root | SHA-256 |
| --- | --- |
| `System/Win64_AMD64/MSystem.dll` | `b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500` |
| `System/Win32_x86/MSystem.dll` | `17508fd36c1ff7c6d1ba223c91592aba8156e2e034606f6b1d172228b047e7de` |
| `System/Win32_x86_SSE/MSystem.dll` | `e6e0d355b9aee787c4dbb30ffeec3274ff9c00d0cb8c064846826d3082ba280a` |
| `System/Win32_x86_SSE2/MSystem.dll` | `80a1a95c69e266f2d4d7381286262602328891359449a83b6b2f649252698981` |
| `System/Win32_x86/eax.dll` | `70c38f00d9fb722bcee85e4de6b30edbe580adefb49925c641978deb01a2601b` |
| `System/Win32_x86_SSE/eax.dll` | `70c38f00d9fb722bcee85e4de6b30edbe580adefb49925c641978deb01a2601b` |
| `System/Win32_x86_SSE2/eax.dll` | `70c38f00d9fb722bcee85e4de6b30edbe580adefb49925c641978deb01a2601b` |
| `Content/GameClasses_Win64_AMD64.dll` | `263c03a430f070ebf5f32c59b598b9e8e98a9d06d07172f76f6786bc0dfc7773` |
| `Content/GameWorld_Win64_AMD64.dll` | `749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7` |

Use Python with `capstone==5.0.9` and `pefile==2024.8.26`, as in
[tools/re](../../tools/re/README.md). The new
[audio_static.py](../../tools/re/audio_static.py) supports both PE architectures;
the existing display decoder deliberately accepts only AMD64. Examples:

```powershell
./local/venv/Scripts/python.exe -I tools/re/audio_static.py --verify-note "$RIDDICK_DIR"
./local/venv/Scripts/python.exe -I tools/re/audio_static.py "$RIDDICK_DIR"/System/Win64_AMD64/MSystem.dll --range 0xaf020 0xaf478
./local/venv/Scripts/python.exe -I tools/re/audio_static.py "$RIDDICK_DIR"/System/Win32_x86/MSystem.dll --strings --exports
```

`--verify-note` checks the nine hashes against both this note and the manifest,
re-decodes every quoted instruction, checks the EAX 4 GUID table, and checks
absence of the four EAX 2/3 GUIDs in all four MSystem images. Full exploration
output belongs in ignored `local/`. Xrefs are candidates until their containing
control flow and object types are traced; a shared COM slot offset alone is
not a method identification.

## 1. Exact backend and EAX selection

### x64: two factories, independent property-capability detection

The sound-context constructor `MSystem+0xAFFF0` unconditionally attempts
`LoadLibraryA("eax.dll")`, storing the handle at sound context `+0x8B0`.
If loaded, `GetProcAddress(...,"EAXDirectSoundCreate8")` is stored at `+0x8B8`.
It separately loads `dsound.dll` and resolves `DirectSoundCreate8` into
`+0x6E0` (module handle `+0x6D8`). Both are dynamic loads, not an OpenAL backend.
Data strings are `+0x1DFB38`, `+0x1DFB20`, `+0x1E13B8`, `+0x1E13A0` respectively.
The IAT entries at `+0x1CE178` and `+0x1CE180` are LoadLibraryA and GetProcAddress.

```asm
; System/Win64_AMD64/MSystem.dll
B00DD lea rcx, [rip + 0x12fa54] ; eax.dll
B00E4 call qword ptr [rip + 0x11e08e]
B00F6 lea rdx, [rip + 0x12fa23] ; EAXDirectSoundCreate8
B0100 call qword ptr [rip + 0x11e07a]
B0106 mov qword ptr [rdi + 0x8b8], rax
B015F lea rcx, [rip + 0x131252] ; dsound.dll
B0178 lea rdx, [rip + 0x131221] ; DirectSoundCreate8
B0182 call qword ptr [rip + 0x11dff8]
B0188 mov qword ptr [rdi + 0x6e0], rax
```

`+0xAF020` initializes the sound system for the window. It obtains the engine
environment registry from the system object and reads integer keys
**`Snd_Use3D` with default 1** and **`Snd_UseEAX` with default 0**.
The registry getter is virtual `+0x320`; these are engine `Environment.cfg`
keys, not BigFix INI keys. It stores their boolean values at `+0x7F4` and
`+0x7F8`, then sets effective EAX `+0x7FC = use3D && useEAX`.

```asm
; System/Win64_AMD64/MSystem.dll
AF105 mov r8d, esi ; esi was set to 1
AF108 lea rdx, [rip + 0x132201] ; Snd_Use3D
AF112 call qword ptr [r9 + 0x320]
AF120 mov dword ptr [rbx + 0x7f4], ecx
AF136 xor r8d, r8d ; EAX default 0
AF139 lea rdx, [rip + 0x1321c0] ; Snd_UseEAX
AF143 call qword ptr [r9 + 0x320]
AF151 mov dword ptr [rbx + 0x7f8], ecx
AF157 cmp dword ptr [rbx + 0x7f8], 0
AF160 cmp dword ptr [rbx + 0x7f4], 0
AF16B mov dword ptr [rbx + 0x7fc], esi
```

The parser registers `snd_use3d <i_expr>` and `snd_useeax <i_expr>` at
`+0xA2C95` / `+0xA2CB3`. Their handlers are `+0xAFCB0` / `+0xAFE00`;
the latter writes/deletes `Snd_UseEAX` in the registry and restarts sound when
3D is active (`+0xAFF0C`–`+0xAFF20`). A restart is the safe user-facing
requirement when changing the backend or effect settings. `SND_MIX_2DWHENIN3DMODE`
(default 1.0, `+0xAF206`–`+0xAF223`) is a mixing setting, not an EAX gate.

Factory selection in `+0xAE740`:

```asm
; System/Win64_AMD64/MSystem.dll
AE7D3 mov rax, qword ptr [rsi + 0x8b8]
AE7DA test rax, rax
AE7DD je 0xae8a1
AE7E3 cmp dword ptr [rsi + 0x7fc], ebp ; ebp=0
AE7E9 je 0xae8a1
AE7FB call rax ; EAX factory, null device GUID and outer unknown
AE801 je 0xae9be ; exact zero HRESULT succeeds
AE8A1 mov rax, qword ptr [rsi + 0x6e0]
AE918 call rax ; ordinary DirectSoundCreate8
```

Thus missing `eax.dll` or missing export selects ordinary DS8 **without
clearing `+0x7FC`**. The EAX factory's nonzero return takes the engine's
error-report path (`+0xAE807`–`+0xAE89B`); do not promise a graceful fallback
for a present-but-failing third-party EAX factory. A missing normal DS factory
also enters an engine error path. Supplying a wrongly named/bitness DLL is
unnecessary and introduces another failure mode.

After factory creation, `+0xAF2C8` sets `DSSCL_PRIORITY`, `+0xACFC0` creates
primary/listener resources, and effective EAX triggers two further gates:

1. `+0x9FEF0` measures hardware-capable voices. It repeatedly creates 64-byte
   mono PCM 44.1 kHz/16-bit buffers with flags `0x14` (`LOCHARDWARE|CTRL3D`),
   obtains DS3DBuffer and IKsPropertySet, holds successful objects until counting
   finishes, then releases them. The loop caps at 256. `+0xAF351` compares this
   count against the engine voice-pool length (`[context+0x770]+8`). Insufficient
   voices clear effective EAX and destroy/recreate audio through the plain path
   (`+0xAF383`–`+0xAF3C0`). Do not bypass this resource check.
2. `+0xA0B30` creates a small hardware 3D probe buffer, obtains IKsPropertySet,
   checks EAX 4 support, and finds/loads a reverb effect slot. The result replaces
   effective EAX. Failure logs `Failed to initialize EAX`; success logs
   `EAX successfully initialized` and submits the default reverb descriptor.

```asm
; System/Win64_AMD64/MSystem.dll
A0001 mov dword ptr [rsp + 0x1844], 0x14
A0081 call qword ptr [rax] ; IKsPropertySet QI on the 3D buffer
A008F cmp ebx, 0x100
AF336 call 0x9fef0
AF351 cmp eax, ecx
AF383 mov dword ptr [rbx + 0x7fc], edi ; edi=0
AF419 call 0xa0b30
AF422 mov dword ptr [rbx + 0x7fc], r11d
AF42B je 0xaf45f
```

The constructor need not load an EAX factory for either gate to run. This is
the critical reason DSOAL's ordinary DS8 export can restore the existing path.

### x86 counterpart

Baseline `Win32_x86/MSystem.dll` has the same decision structure: constructor
loads `eax.dll` at `+0x74A6F`, resolves EAXDirectSoundCreate8 at `+0x74A85`,
then loads dsound at `+0x74AE4` and resolves DirectSoundCreate8 at `+0x74AFA`.
Factory pointers are `+0x658` / `+0x4C0`; requested flags `+0x59C` / `+0x5A0`;
effective EAX `+0x5A4`. Initializer reads Use3D / UseEAX at `+0x74103` /
`+0x7412B` (defaults 1 / 0), compares hardware voices at `+0x742FB`, and calls
the EAX initializer `+0x6A580` from `+0x7438E`.

```asm
; System/Win32_x86/MSystem.dll
74A4C push 0x101488d0 ; eax.dll
74A6F call dword ptr [0x101400b8] ; LoadLibraryA
74A7F push 0x101488b8 ; EAXDirectSoundCreate8
74A85 call dword ptr [0x101400bc] ; GetProcAddress
74AD8 push 0x101497a8 ; dsound.dll
74AF4 push 0x10149794 ; DirectSoundCreate8
74AFA call dword ptr [0x101400bc]
73BE8 mov eax, dword ptr [esi + 0x658]
73BF0 je 0x73c99 ; absent EAX factory -> plain
73BF6 cmp dword ptr [esi + 0x5a4], 0
73BFD je 0x73c99
73C0E call eax
73C99 mov eax, dword ptr [esi + 0x4c0]
74121 push 0 ; UseEAX default
7412B push 0x1014977c ; Snd_UseEAX
74156 mov dword ptr [esi + 0x5a4], eax
```

The SSE and SSE2 variants were inventoried for fingerprints, EAX GUIDs and
shipped DLL identity; the quoted x86 control-flow addresses are for baseline
x86 only. BigFix should continue supporting x64 only.

## 2. Property sets and actual room-data source

### EAX 4, not a direct EAX 2/3 fallback

The game constants match [DSOAL's EAX GUID definitions](https://github.com/kcat/dsoal/blob/e11128250439d586483ee6598d1670ab52ab5e37/src/eax.h).
All four MSystem images contain the EAX 4 Context, four slots, and Source GUIDs.
The full little-endian EAX 2 listener/buffer GUIDs
`0306a6a8/0306a6a7-b224-11d2-99e5-0000e8d8c722` and EAX 3 listener/buffer
GUIDs `a8fa6882/a8fa6881-b476-11d3-bdb9-00c0f02ddf87` are **absent** in all four.
The traced engine path queries EAX 4; it does not negotiate down to those sets.
This does not exclude compatibility translation inside Creative's x86 DLL.

| Module | RVA | GUID |
| --- | --- | --- |
| `System/Win64_AMD64/MSystem.dll` | `0x1fd690` | `1d4870ad-0def-43c0-a40c-523632296342` |
| `System/Win64_AMD64/MSystem.dll` | `0x1fd6d0` | `c4d79f1e-f1ac-436b-a81d-a738e7045469` |
| `System/Win64_AMD64/MSystem.dll` | `0x1fd6c0` | `08c00e96-74be-4491-93aa-e8ad35a49117` |
| `System/Win64_AMD64/MSystem.dll` | `0x1fd6b0` | `1d433b88-f0f6-4637-919f-60e7e06b5edd` |
| `System/Win64_AMD64/MSystem.dll` | `0x1fd6a0` | `efff08ea-c7d8-44ab-93ad-6dbd5f910064` |
| `System/Win64_AMD64/MSystem.dll` | `0x1fd6e0` | `1b86b823-22df-4eae-8b3c-1278ce544227` |

Rows are Context, FXSlot0, FXSlot1, FXSlot2, FXSlot3, Source. x86 uses the
same order at `+0x1602B0`, `+0x1602F0`, `+0x1602E0`, `+0x1602D0`,
`+0x1602C0`, `+0x160300`; SSE at `+0x1662A0`, `+0x1662E0`,
`+0x1662D0`, `+0x1662C0`, `+0x1662B0`, `+0x1662F0`; SSE2 at
`+0x16B2F8`, `+0x16B338`, `+0x16B328`, `+0x16B318`, `+0x16B308`, `+0x16B348`.
IKsPropertySet IID is `31efac30-515c-11d0-a9aa-00aa0061be93`, x64 data
`+0x219EC0`; QI sites include `+0xA0077`, `+0xA0C32`, `+0xA5D80`.

COM slots are QueryInterface `+0`, Get `+0x18`, Set `+0x20`, QuerySupport
`+0x28` on x64; x86 Get/Set/QuerySupport are `+0xC/+0x10/+0x14`.
Initializer `+0xA0B30` requires both GET and SET (`support & 3 == 3`) for
Context property **1**, every FXSlot property **0x10001**, and Source property
**1**. It tries slots 0, 1, 2, 3 in order until a reverb effect can be used;
it requires the support probes for all four even though room reverb uses one.

```asm
; System/Win64_AMD64/MSystem.dll
A0C5D lea rdx, [rip + 0x15ca2c] ; Context
A0C67 call qword ptr [rax + 0x28]
A0C76 and al, 3
A0C78 cmp al, 3
A0C9C lea rdx, [rip + 0x15ca2d] ; FXSlot0
A0CA3 mov r8d, 0x10001
A0CA9 call qword ptr [rax + 0x28]
A0D90 lea rdx, [rip + 0x15c949] ; Source
A0D9A call qword ptr [rax + 0x28]
```

Identified property traffic (instance pointer/size are null/zero):

| Purpose | x64 site | Set / Get data |
| --- | --- | --- |
| Install reverb in candidate FXSlot | `A0140`, Set `A01D3` | FXSlot GUID, `0x10001` ALLPARAMETERS, 28 bytes: effect GUID, volume 0, lock 1, flags 0 |
| If install fails, accept existing reverb | Get `A0240` | Same FXSlot, `0x10002` LOADEFFECT, 16-byte effect GUID; compare with reverb GUID |
| Change room parameters | `A02A0`, Set `A0440` | Selected FXSlot GUID, property 1 EAXREVERB_ALLPARAMETERS, 112 bytes |
| Connect a voice to the selected slot | Set `A5ECC` | EAX40_Source, property `0x1B` ACTIVEFXSLOTID, 32 bytes: null GUID and selected slot GUID |

Reverb effect GUID `0cf95c8f-a3cc-4849-b0b6-832ecc1822df` is at
`MSystem+0x1FD700`. Candidate slot GUID is kept at sound context `+0x8E4`;
the IKsPropertySet pointer is `+0x8D8`. These offsets belong to the existing
engine implementation, not proposed new fields. Property names/layouts match
[OpenAL Soft's EAX definitions](https://github.com/kcat/openal-soft/blob/1fbc89b2279280ab89509b5d5adcd43b941ee0a1/al/eax/api.h).

```asm
; System/Win64_AMD64/MSystem.dll
A01B3 mov dword ptr [rsp + 0x30], 0x1c
A01C2 lea rdx, [rdi + 0x34] ; candidate slot GUID in EAX helper
A01C9 mov r8d, 0x10001
A01D3 call qword ptr [rax + 0x20]
A0236 mov r8d, 0x10002
A0240 call qword ptr [rax + 0x18]
A02D1 mov dword ptr [rsp + 0x50], 0x1a ; user-defined environment 26
A0420 mov dword ptr [rsp + 0x30], 0x70
A042D lea r8d, [r9 + 1] ; property 1, r9=0
A0431 add rdx, 0x8e4 ; selected slot GUID
A0440 call qword ptr [rax + 0x20]
A5EA1 mov dword ptr [rsp + 0x30], 0x20
A5EC1 lea r8d, [r9 + 0x1b] ; ACTIVEFXSLOTID
A5EC5 lea rdx, [rip + 0x157814] ; EAX40_Source
A5ECC call qword ptr [rax + 0x20]
```

Baseline x86 equivalents: slot setup `+0x69C00` (Set `+0x69C6C`), room
conversion `+0x69DE0` (Set `+0x69EF5`), voice routing `+0x6E3EF` through
wrapper `+0x69530` (Set `+0x6955F`). Their packet sizes/property IDs match.

```asm
; System/Win32_x86/MSystem.dll
69C55 push 0x1c
69C5F push 0x10001
69C69 mov eax, dword ptr [ecx + 0x10]
69C6C call eax
69DFE mov dword ptr [ebp - 0x7c], 0x1a
69EDE push 0x70
69EE8 push 1
69EF2 mov eax, dword ptr [ecx + 0x10]
69EF5 call eax
6E3DD push 0x20
6E3E3 push 0x1b
6E3E5 push 0x10160300 ; EAX40_Source
6E3EF call 0x69530
6955C mov eax, dword ptr [ecx + 0x10]
6955F call eax
```

No per-voice obstruction/occlusion property setter was established in this
trace. DSOAL supporting those features does not establish that this game
authors or uses them. Preserve DS3D position/velocity/distance updates too;
the EAX property packets alone are not the full spatial audio API.

### Authored sound volumes supply the reverb

`GameClasses` RTTI names `CWObject_SoundVolume` (vtable `+0x2ABD28`) and
`CClientData` for that class. Source filename at `+0x2ABA00` identifies
`WObj_SoundVolume.cpp`. Its registry parser recognizes `REVERB`, `REVERBDELAY`
at `+0x52236` / `+0x522CE`, alongside decay/room/diffusion/echo-related keys.
It stores parameters in client data and marks them changed. Its message
handler at `+0x52FF0` answers message **0x29A** only for a **0x54-byte** buffer,
copying all 21 floats from its authored descriptor to the caller.

GameWorld's audio update builds that message (`+0x13C6B8`), attaches a 0x54-byte
output (`+0x13C6EE`/`+0x13C6F6`), sends it to a spatially selected object,
accumulates returned room descriptors with weights (`+0x13C72A`–`+0x13C73D`,
helper `+0x13A8F0`), and normalizes the result (`+0x13BF80`). The update then
calls the sound context's virtual **+0x198** at `+0x13D09B`. That slot maps to
MSystem `+0xA0450`: the constructor installs vtable `+0x1E1468`, whose
`+0x198` pointer at `+0x1E1600` is `+0xA0450`. No eligible volume after a
previous active one invokes the default descriptor path at `+0x13D0B4`–`D0D9`.

```asm
; Content/GameClasses_Win64_AMD64.dll
52FF6 cmp dword ptr [r8], 0x29a
53002 cmp dword ptr [r8 + 0x40], 0x54
53027 mov rcx, qword ptr [rbx + 0x38] ; output payload
530AF mov dword ptr [rcx + 0x4c], eax
530B6 mov dword ptr [rcx + 0x50], eax
; Content/GameWorld_Win64_AMD64.dll
13C6B8 mov edx, 0x29a
13C6F6 mov dword ptr [rsp + 0x170], 0x54
13C701 call qword ptr [rax + 0x100] ; object message dispatch
13C73D call 0x13a8f0 ; accumulate weighted room data
13D084 call 0x13bf80 ; normalize
13D089 mov rcx, qword ptr [r13 + 0x4c0] ; sound context
13D09B call qword ptr [rax + 0x198]
; System/Win64_AMD64/MSystem.dll
A0476 mov r9d, 4
A0488 lea r8d, [r9 + 0x50] ; compare 0x54-byte descriptor
A0494 call 0x1a1010
A049B je 0xa04bd ; unchanged descriptor: no update
A04B8 call 0xa02a0 ; convert and submit EAX reverb
```

MSystem converts the descriptor into EAX's 0x70-byte reverb structure, including
integer room/HF/LF/reflection/reverb levels, decay times/ratios, diffusion,
environment size, echo/modulation and frequency references. It uses environment
26 (user-defined), zero pan vectors and flags `0x3F`, rather than choosing one
hard-coded room enum. The changed-descriptor gate also respects EAX off and
the reverb-console override (`context+0x800`). Debug `sr_*` commands can change
parameters, but are not the normal source of room data. No level archive was
unpacked, so exact named-room values and the complete spatial weighting rules
remain unverified; the producer-to-consumer path above is established.

## 3. What x86 ships, and modern Windows

All three x86 directories ship the **same 192,512-byte PE32 Creative EAX
Unified DLL**, string FileVersion/ProductVersion **4.00**, numeric file version
**4.0.0.0**, company Creative Technology Ltd. Its exports include
`EAXDirectSoundCreate` at RVA `0x1000` and `EAXDirectSoundCreate8` at `0x1140`.
The latter calls CoInitialize and CoCreateInstance, then initializes and wraps
the resulting DirectSound8 object; its COM IAT entries are `+0x24124` and
`+0x2411C`. It is not evidence of an OpenAL backend or software EAX renderer.
No `eax.dll`, `dsound.dll` or `OpenAL32.dll` was present in the x64 folder.
Copying this PE32 DLL into x64 cannot work and must never be an installer step.

Microsoft documents Vista's changed DirectSound behavior: hardware buffers
requested with DSBCAPS_LOCHARDWARE fail
([What's New in DirectSound, Windows Vista](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee419022(v=vs.85))).
Consequently, the game's hardware-buffer count cannot pass through the native
Vista-and-later DS path just because an EAX setting or old Unified DLL exists.
Ordinary software sound can still work. A Creative card alone does not restore
the removed DirectSound HAL; it needs an appropriate translation path such as
ALchemy or DSOAL. This is an OS/API consequence, not a new runtime observation.

## 4. DSOAL / OpenAL Soft facts checked against upstream

Sources were fetched via web search/fetch and inspected locally as untrusted
reference text; no upstream instructions were executed. Source revisions
inspected: **DSOAL `e11128250439d586483ee6598d1670ab52ab5e37`**, **OpenAL Soft
`1fbc89b2279280ab89509b5d5adcd43b941ee0a1`**. These are research snapshots,
not a tested/pinned BigFix runtime dependency selection.

- **DSOAL replaces dsound.dll**, implements DS/DS3D and EAX through OpenAL;
  its documented EAX limit is version 4. Deployment is an application-local
  `dsound.dll` plus an OpenAL DLL named `dsoal-aldrv.dll`.
  [Upstream README](https://github.com/kcat/dsoal/blob/e11128250439d586483ee6598d1670ab52ab5e37/README.md).
- **x64 exists upstream:** the build workflow has Win32 and Win64/x64 jobs and
  packages both architecture directories. The upstream
  [latest-master prerelease](https://github.com/kcat/dsoal/releases/tag/latest-master)
  provides builds; it is mutable, so never use that tag as the installer's
  integrity pin. [Build and packaging source](https://github.com/kcat/dsoal/blob/e11128250439d586483ee6598d1670ab52ab5e37/.github/workflows/Untagged.yaml).
- **No EAXDirectSoundCreate8 export.** The
  [x64 export definition](https://github.com/kcat/dsoal/blob/e11128250439d586483ee6598d1670ab52ab5e37/msvc64.def)
  exposes DirectSoundCreate8 and normal DS exports. This is sufficient here
  because the game has the ordinary-factory fallback above. Renaming DSOAL to
  `eax.dll` does not manufacture the missing export and is not the design.
- **The needed property sets are implemented:** `Buffer::Prop::Set/Get/QuerySupport`
  recognizes EAX40 Context, all four slots and Source. Queries for Context 1,
  FXSlot 0x10001, Source 1 and ACTIVEFXSLOTID report GET|SET. Sets forward the
  GUID/property/payload into OpenAL EAX, returning failure on a renderer error.
  [Buffer implementation](https://github.com/kcat/dsoal/blob/e11128250439d586483ee6598d1670ab52ab5e37/src/buffer.cpp),
  [property query switches](https://github.com/kcat/dsoal/blob/e11128250439d586483ee6598d1670ab52ab5e37/src/eax.cpp).
  DSOAL reports emulated hardware resources and accepts hardware buffers, so
  this can satisfy the game's real allocation test without lying in BigFix.
  Its inspected defaults request 1024 OpenAL sources and expose at most 128
  hardware sources (64 with fewer available sources); measure the resulting
  capacity against the game's requested pool.
  [Device implementation](https://github.com/kcat/dsoal/blob/e11128250439d586483ee6598d1670ab52ab5e37/src/dsoundoal.cpp).
- **Current OpenAL Soft supports legacy EAX.** The
  [changelog](https://github.com/kcat/openal-soft/blob/1fbc89b2279280ab89509b5d5adcd43b941ee0a1/ChangeLog)
  dates initial legacy EAX implementation to 1.22.0. `ALSOFT_EAX` defaults on
  for Windows, and capable contexts expose EAX through EAX5.0, including 4.0.
  This does not raise DSOAL's advertised DS-facing limit above EAX4.
  [Build option](https://github.com/kcat/openal-soft/blob/1fbc89b2279280ab89509b5d5adcd43b941ee0a1/CMakeLists.txt),
  [extension/capability initialization](https://github.com/kcat/openal-soft/blob/1fbc89b2279280ab89509b5d5adcd43b941ee0a1/alc/context.cpp).
  EAX can be disabled in configuration and needs enough auxiliary sends.
  The inspected DSOAL specifically detects **EAX5.0** to enable its EAX path
  and requires **ALC_EXT_direct_context** plus functions including
  **EAXSetDirect/EAXGetDirect**. Therefore “OpenAL Soft >=1.22” is insufficient
  as a compatibility requirement for a current DSOAL build: use a verified,
  matched pair, with EAX and required extensions enabled.
  [DSOAL loader](https://github.com/kcat/dsoal/blob/e11128250439d586483ee6598d1670ab52ab5e37/src/dsoal.cpp).
- **Licenses differ from BigFix:** DSOAL carries
  [LGPL 2.1](https://github.com/kcat/dsoal/blob/e11128250439d586483ee6598d1670ab52ab5e37/LICENSE);
  OpenAL Soft states
  [LGPL 2 or later](https://github.com/kcat/openal-soft/blob/1fbc89b2279280ab89509b5d5adcd43b941ee0a1/README.md).
  Keep them separately replaceable DLLs, with matching source/build material
  distributed alongside the optional package, copyright/license texts and
  dependency notices (including bundled fmt and HRTF/data licenses where used).
  Do not describe their binaries as MIT or copy their implementation into
  BigFix's MIT ASI. Audit the exact packaged artifact's notices, not just its
  two top-level license files.

## 5. Ranked designs and concrete implementation plan

### 1 — Optional DSOAL installation: recommended

**Installer:** add an explicit audio-backend choice, defaulting to the existing
system path (`-AudioBackend dsoal` / `--audio-backend dsoal`, names proposed).
Pin both source revisions and exact x64 artifact SHA-256s in release metadata;
preflight PE machine, expected exports and complete paired payload before any
write. Extend `installer/gen_data.py` / generated file-plan data and both
installers with an optional component, rather than unconditionally adding
audio DLLs to every install. Place `dsound.dll` and `dsoal-aldrv.dll` next to
`System/Win64_AMD64/SbzEngine.exe`, with licenses/source material in the mod
distribution. Do not touch any x86 folders or ship Creative's/game DLLs.

For this option only, extend the existing backed-up `Environment.cfg` merge
to set `SND_USE3D=1` and `SND_USEEAX=1`. Preserve their original values through
the install manifest/backup mechanism. Dry run must show the optional DLLs
and config keys. Handle existing third-party dsound/aldrv/config files as an
explicit conflict with a reversible backup, never a silent overwrite. Component
disable/uninstall must restore replaced files and keys, preserve later user
changes according to the installer's ownership rules, and remain interoperable
between PowerShell and shell uninstallers.

Wine/Proton additionally needs the application-local **native dsound** override
(alongside winmm), proposed launch string
`WINEDLLOVERRIDES="winmm=n,b;dsound=n,b" %command%`. Apply only through the
existing explicit-prefix/launch-option workflow. No global registry/COM
registration or system DLL replacement. Compatibility on Wine/Deck remains
unverified; a native Windows x64 OpenAL DLL supplies its own Windows EAX build
configuration rather than depending on a distro OpenAL compiled without EAX.

**Plugin:** no new audio synthesis or engine byte patch. Reuse
`src/module_watch.cpp`'s dsound handler and `src/background_audio.cpp`'s
DirectSoundCreate8 interception. Log the actual module paths/versions, selected
backend and effective EAX success/failure. Extend diagnostics in that module
to observe buffer QI and IKsPropertySet QuerySupport/Get/Set, with object IDs,
GUID, property, payload size, HRESULT and selected-slot/room changes. Preserve
COM identity/refcounts, calling convention, HRESULT, caller payload and thread
safety. Bound/summarize logging; do not use a shared single saved trampoline
for different implementations' vtables. Test simultaneous native/wrapper
objects before broadening hooks.

**INI:** existing `BackgroundAudio` and `TraceAudio` keep their meanings. Add
only optional diagnostic `TraceEax=0` if needed; the first implementation needs
no duplicate `EAX=1` BigFix key because the installer owns backend deployment
and the engine owns `SND_USEEAX`. Changing `BackgroundAudio` must not enable
EAX, and setting engine EAX off leaves DSOAL installed but disables the engine's
EAX effects. To return to system DS, change the installed component and restart;
an INI switch after DLL loading cannot unload an in-use COM backend safely.

**Risks:** incorrect DLL pair/bitness can remove sound entirely; DSOAL cannot
fall through to system DS merely because its backend DLL is missing. Unknown
builds, upstream runtime behavior, resource limits, buffer duplication/lifetime,
streaming notifications, focus handling, latency, speaker layouts and Wine
loading need coverage. HRTF is a separate user output preference, not a
requirement for EAX; do not force headphone processing on speaker users.

### 2 — Plugin-side shim: feasible but unjustified initially

A narrow factory adapter could supply EAXDirectSoundCreate8 semantics by
forwarding to a verified DSOAL DirectSoundCreate8. The traced x64 fallback
already does this job, so such an adapter adds loader/lifetime/hook failure
modes without enabling extra game properties. Consider a scoped loader-route
hook only if the fixture proves application-local dsound is not selected in a
supported launch environment; never globally redirect every DLL load.

A complete ASI-side DS/DS3D-to-OpenAL/EAX implementation would need streaming,
buffers/duplicates, cursors/notifications, 3D transforms, COM identities,
resource accounting and EAX effect translation. It duplicates DSOAL and expands
testing/licensing burden. Intercepting just IKsPropertySet while native DS
mixes the audio cannot attach OpenAL reverb to those native buffers. Installer
would still need a matching OpenAL backend, plugin settings would need one
authoritative backend selection plus diagnostics, and tests would have to
cover the entire DS lifetime contract. Do not choose this for v0.5.

### 3 — Declare infeasible / postpone: fallback only if evidence fails

“x64 has no EAX DLL, so EAX is impossible” is contradicted by the retained
property path. If the matched DSOAL pair fails the required compatibility
tests, leave system audio and existing settings unchanged and document the
specific unsupported operation. An aesthetic, fixed global reverb could be
implemented elsewhere, but would not restore this game's authored EAX audio
and should not be presented as the requested feature.

## 6. Verification plan without listening

The following implementation tests are **proposed, not run in this research**.
Reuse the existing `tests/plugin/dsound_stub.cpp`, `dsound_stub.def`,
`test_background_audio.cpp` and CMake's `bigfix_background_audio_*` targets.
There is already a fake-DS harness as well as fake GL; do not build a separate
test framework or load MSystem as a way of obtaining its audio classes.

1. **Static gate (now):** run `audio_static.py --verify-note` against the nine
   real files, plus `git diff --check`. Retain expected-byte/fingerprint checks
   before any future internal engine hook; the recommended normal path needs
   no game patch. Installer tests use temporary dummy layouts only.
2. **Fake DS selection/trace tests:** extend the stub with correctly laid out
   DS8, buffer/3D and IKsPropertySet interfaces. Replay the traced call sequence
   in independent C++ fixture code: EAX DLL absent -> ordinary factory;
   Use3D off, UseEAX off/on, exact-zero/error factory results, insufficient
   hardware allocations, missing QI, missing GET or SET bits, candidate slot
   fallthrough, reverb Get fallback and invalid property returns. Verify that
   negative paths are reported and effects are not falsely marked active.
   Keep selection-model tests distinct from proof that real game control flow
   ran; the static evidence establishes the latter's structure.
3. **Exact payload/lifetime tests:** assert 28-byte slot configuration,
   16-byte effect Get, 112-byte reverb and two-GUID/32-byte ACTIVEFXSLOTID
   packets; verify all four slot GUIDs, flags, zero instance fields and passed
   HRESULTs. Feed two different valid room descriptors and a default reset;
   prove changed parameters reach the selected slot and source sends match it.
   QuerySupport alone is insufficient. Exercise QI/AddRef/Release, duplicates,
   more than one device/vtable, repeated restarts and failure cleanup. Run with
   existing BackgroundAudio off/on, preserving primary flags and applying
   GLOBALFOCUS only to secondary buffers. Verify bounded trace content and
   disabled diagnostics without changing payloads.
4. **Real dependency API fixture, no game:** load the pinned DSOAL x64 pair in
   a temporary harness directory (also test an unrelated CWD), create an actual
   DS8 device/window and replay hardware-count/QI/support/slot/room/source calls.
   Require successful real setters/gets and available voice capacity, inspect
   the loaded DLL paths, and repeat without EAX enabled in OpenAL. Also test
   missing backend and wrong-architecture payload rejection. This is where
   source-level compatibility becomes tested artifact compatibility.
5. **Renderer proof without ears:** an OpenAL Soft loopback fixture can render
   a synthetic impulse with EAX disabled/enabled and two valid room packets.
   Measure finite samples and different/nonzero late reverb energy after the
   dry impulse ends; avoid fragile full-waveform hashes across library updates.
   This proves the renderer produces wet audio. The DS API fixture/log proves
   forwarding; neither alone establishes the game's audible mix. DSOAL API
   trace logging is enabled through its documented DSOAL_LOGLEVEL/LOGFILE
   controls; OpenAL has additional logging and EAX commit tracing in its
   [configuration sample](https://github.com/kcat/openal-soft/blob/1fbc89b2279280ab89509b5d5adcd43b941ee0a1/alsoftrc.sample).
6. **Installer gate:** extend `installer/tests/test_install.py` and
   `test_install_sh.py` with component off/on, dry run, source/artifact hash and
   PE rejection, existing wrapper conflicts, config preservation, partial
   install failure rollback, cross-uninstaller round trips and no x86/system
   changes. Check generated JSON/text file-plan parity. Verify disabled EAX
   and wrapper removal have distinct, documented behavior.

Only after these gates, a separately authorized opening-dock smoke could
establish that the real game selects DSOAL, passes its own voice/property
checks, submits changing room data and continues streaming. No game run was
performed here. Windows audible quality, spatial orientation, performance and
Wine/Proton/Deck behavior remain runtime validation items, not static findings.
