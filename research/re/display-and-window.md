# Display modes and window lifecycle — EU 1.1 x64

Static research, 2026-10-29. **The 512-mode enumeration cutoff is confirmed.**
`RndrGL.dll` requests only indices 0–511 through `EnumDisplaySettingsA`.
It builds a dynamic list, adds a separate desktop/windowed entry, and sorts it;
this is a limit on raw enumeration calls, not a 512-element backing allocation.
Duplicates consume the budget and modes below 16 bpp consume indices too.

The fallback needs a correction to the hypothesis in research §B2a: numeric
`VID_MODE` selects the nearest eligible mode using
`4*abs(width difference) + 3*abs(height difference) + abs(refresh difference)`.
Pixel format must match exactly. It does **not** simply choose the largest mode
and its highest refresh. With the recorded prefix ending in 1600×900, a request
for 2560×1440 at 180 Hz is consistent with 1600×900 at 120 Hz. A request at 60 Hz
would prefer 60 Hz if that tuple is present in the prefix. The earlier 60-Hz
observation therefore needs the actual startup request logged, especially since
`SbzEngine.exe` can rewrite `VID_MODE` from separate `VIDEO_DISPLAY_*` options.

The main window is created in `RndrGL.dll`, with visible popup/topmost styles in
fullscreen and ordinary framed styles in windowed mode. There are exactly two
statically identified `ChangeDisplaySettings*` call instructions in the seven
examined modules: mode application and cached-desktop restoration on destruction.
Focus loss explicitly minimises fullscreen windows under **`WM_KILLFOCUS`**, not
an explicit `WM_ACTIVATEAPP` handler. It queues desktop mode 0 for a later
page-flip call; restoration is not synchronous with minimisation. This leaves a
credible timing explanation for §B2b, but cannot establish the event order of
that past run.

**Confidence:** high for the enumerator, scoring algorithm, API sites, styles,
and focus/deferred-mode control flow; medium for explaining the recorded runs;
hook recommendations below are static design candidates, not tested hooks.
No game or game DLL was launched or loaded. No game files were modified.

## Scope, addresses, and reproducibility

Examined read-only under `D:\Games\Riddick`. Addresses below are **RVAs**;
runtime address is the loaded module base plus RVA. Preferred bases are
`0x400000` for `SbzEngine.exe` and `0x10000000` for these DLLs. Names such as
`ModeList_Find` are real exports where stated; descriptive RndrGL names are
inferred from their code and vtable slots.

| Module | SHA-256 |
|---|---|
| System/Win64_AMD64/SbzEngine.exe | `5056ffe030c2ba8687b032f5f2ed95b42c4814db87dec61444a6cf264deaca38` |
| System/Win64_AMD64/MSystem.dll | `b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500` |
| System/Win64_AMD64/RndrGL.dll | `400dc6b50f65c9b884e85daeac7b1b1aaa2f19d2cbf4dbd9e597abae284ea348` |
| System/Win64_AMD64/MXR.dll | `c2d898b4a69dcd362f25f88ba2426ac5fb7efb879bf0f2721d3c8d5b15aec71f` |
| System/Win64_AMD64/MCCDyn.dll | `4ab12ce5f13a0319dff09e43de22f0f787bac4dd75283287f6cbe388f9cb659b` |
| Content/GameClasses_Win64_AMD64.dll | `263c03a430f070ebf5f32c59b598b9e8e98a9d06d07172f76f6786bc0dfc7773` |
| Content/GameWorld_Win64_AMD64.dll | `749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7` |

Tool: [display_static.py](../../tools/re/display_static.py), Python 3 with
Capstone 5.0.9 and pefile 2024.8.26; setup and examples in
[tools/re/README.md](../../tools/re/README.md). All binary-reading Python commands
used `-I`. Full scratch disassembly stays in ignored `local/`, outside git.

Verification: all seven module IAT coverage checks passed; the note's 15 API
call sites matched the inventory. A separate read-only check re-decoded the
126 excerpt instruction addresses/mnemonics, checked seven vtable pointers and
three message-table entries, and matched all seven file hashes against this note.

Import/reference scans covered all seven PEs, with an independent executable-byte
sweep checking direct RIP-relative IAT call/jump coverage. Relevant display/window
APIs occur only in RndrGL. No `EnumDisplaySettingsW`, `EnumDisplaySettingsEx*`,
`ChangeDisplaySettingsW/Ex*`, `CreateWindowExW`, or `SetWindowLong[Ptr]*` import or
literal API name was found elsewhere in the seven PEs. This establishes static
sites, not the impossibility of arbitrary computed or dynamically constructed
calls. The plain API-name strings found for enumeration/change are their PE
import names, not extra dynamic lookup sites.

## 1. Enumeration, table, VID_MODE, and fallback

### RndrGL enumerator: RVA 0x4C440; API call 0x4C4AF — high confidence

The primary display-context vtable is at RndrGL RVA `0x952D8`.
Slot `+0xC8` points to `0x4C440`; context initialisation `0x4C190` calls it at
`0x4C2C3`, after snapshotting desktop caps (`0x4C2B8 -> 0x47450`).

```asm
; RndrGL.dll — indexed EnumDisplaySettingsA(NULL, index, &devmode)
4C48E  xor r14d,r14d
4C491  mov r12d,r14d
4C4A2  lea r8,[rsp+3A0h]
4C4AA  mov edx,r12d
4C4AD  xor ecx,ecx
4C4AF  call [rip+42803h]       ; IAT RVA 8ECB8: EnumDisplaySettingsA
4C4B5  test eax,eax
4C4B7  je 4C6B7h
4C4BD  cmp dword ptr [rsp+408h],10h ; DEVMODEA.dmBitsPerPel
4C4C5  jb 4C658h
```

```asm
; RndrGL.dll — same loop, including skipped modes
4C658  inc r12d
4C65B  cmp r12d,200h
4C662  jl 4C4A2h
4C668  jmp 4C6B7h
```

Each accepted mode gets a heap allocation (`0x4C4CB`, size `0xF8`), an image/mode
descriptor, its refresh (`0x4C52B`), and a copied 0x9C-byte DEVMODEA (`0x4C584`–
`0x4C596`). The dynamic array append at `0x4C5DC`–`0x4C5E3` uses the current
count, without a duplicate lookup. Thus at most 512 raw records are accepted,
plus **one synthetic windowed/desktop entry** (up to 513 descriptors total).
There is no second display enumerator in the examined PEs.

The synthetic entry uses `VID_DWIDTH`/`VID_DHEIGHT` (string RVAs `0x95098` /
`0x950A8`), defaulting to 640×480 (`0x4C72C`/`0x4C731`). Its image flags are
`0x82`, including bit `0x80` meaning windowed; its DEVMODE separately carries
the cached desktop dimensions/bpp/refresh. The array is sorted at `0x4C9FA`,
with callback `0x45700 -> 0x65D4A -> MSystem!spCDC_VideoMode::Compare`.

`MSystem.dll` exported comparator `0x1469B0` puts windowed entries before
fullscreen entries, then orders by width, height, pixel format, refresh. This
makes the synthetic entry **mode 0**, not the first raw Windows mode.

```asm
; MSystem.dll — comparator, abbreviated independent excerpts
1469B9 test byte ptr [r8+44h],80h
1469C7 cmovne eax,r10d         ; r10d=-1: windowed before fullscreen
1469CB test byte ptr [rcx+44h],80h
1469CF je 1469D3h
1469D1 inc eax
; after equal flag class: compare fields +2C, +30, +3C, +50
1469DE cmp edx,eax            ; width
146A0F cmp ecx,eax            ; refresh, final tie breaker
```

### VID_MODE consumer and numeric matching — high confidence

Real export `MSystem!CSystemCore::DC_InitVidModeFromEnvironment`, RVA `0x77730`,
reads key `VID_MODE` (`0x1DC068`) with default `640 480 32 85` (`0x1DC078`).
Case-insensitive comparison with `desktop` (`0x1DC040`) takes the mode-0 path
at `0x77889`, bypassing numeric search. Otherwise it parses width, height, bpp,
refresh, converts bpp through exported `CImage::BPP2Format` (`0x5C870`), and calls
display-context slot `+0xD0`. For example, bpp 32 maps to format `0x40` at
`0x5C891`; the selector compares that format, not the raw number 32.

```asm
; MSystem.dll — DC_InitVidModeFromEnvironment
7777D  lea r9,[rip+1648F4h]    ; default "640 480 32 85"
77784  lea r8,[rip+1648DDh]    ; "VID_MODE"
77793  call [r10+300h]         ; environment string getter
; after parsing and BPP2Format:
779E1  mov [rsp+20h],ebp       ; requested Hz, fifth argument
779E5  mov r9d,edi             ; requested pixel format
779E8  mov r8d,r12d            ; requested height
779EB  mov edx,r13d            ; requested width
779EE  call [rax+D0h]
```

RndrGL vtable `0x953A8` (`0x952D8+0xD0`) contains thunk `0x65D68`, which jumps
through IAT `0x8E7A8` to real export `MSystem!CDisplayContext::ModeList_Find`,
RVA **`0x146B30`**. The table is context `+0x28`, with count at table `+8` and
element storage at table `+0x20`. Descriptor fields are width `+0x2C`, height
`+0x30`, format `+0x3C`, flags `+0x44`, Hz `+0x50`, DEVMODEA `+0x58`.

At selector entry the four integer arguments are saved: width at eventual
`rsp+0x78`, height `+0x80`, format `+0x88`, Hz `+0x90`. It scans the whole built
list and minimises the weighted distance. These are normal supported display
sizes; the instructions use signed 32-bit arithmetic, not a general overflow-safe
distance function.

```asm
; MSystem.dll — ModeList_Find score, after loading candidate width/height
146C17 sub ebp,[rsp+80h]      ; height difference
146C1E sub r13d,[rsp+78h]     ; width difference
146C2B mov eax,[rax+50h]
146C2E sub eax,[rsp+90h]      ; refresh difference
; CDQ/XOR/SUB sequences take absolute values
146C43 lea eax,[rax+rax*2]    ; 3*abs(height difference)
146C46 add ecx,eax           ; +abs(refresh difference)
146C50 lea ebp,[rcx+rax*4]    ; +4*abs(width difference)
```

```asm
; MSystem.dll — candidate qualification and strictly better score
146C7B test byte ptr [rax+44h],80h
146C7F jne 146CC0h           ; skip windowed synthetic entry
146CA9 mov ecx,[rsp+88h]
146CB0 cmp [rax+3Ch],ecx     ; exact pixel-format match
146CB3 jne 146CC0h
146CB5 cmp ebp,r12d          ; best score starts INT_MAX (146B78)
146CB8 jge 146CC0h           ; equal scores keep the first sorted candidate
146CBA mov r12d,ebp
146CBD mov r14d,ebx          ; winner index; starts -1 (146B81)
```

Thus an unavailable exact tuple normally gives the nearest fullscreen tuple,
not a failed lookup. Only **no eligible candidate** returns -1. Its caller then
logs `Can't find video mode. Choosing mode 0.` and applies the synthetic windowed
entry. Numeric missing arguments become -1; there is no wildcard branch in the
selector for them.

```asm
; MSystem.dll — both normal selection and the true no-candidate fallback
779F4  mov ebx,eax
779F6  test eax,eax
779F8  jns 77A18h
779FA  lea rdx,[rip+1645FFh]   ; "Can't find video mode. Choosing mode 0."
77A13  mov ebx,r15d           ; r15d=0
; common path:
77AFF  mov rcx,[r14+78h]
77B06  mov edx,ebx
77B08  call [rax+50h]         ; RndrGL SetMode, RVA 4DB20
```

### Startup can replace the environment request — high confidence for the code

`SbzEngine.exe` function starting `0x115D0` reads separate saved options
`VIDEO_DISPLAY_WIDTH`, `VIDEO_DISPLAY_HEIGHT`, `VIDEO_DISPLAY_FULLSCREEN`, and
`VIDEO_DISPLAY_REFRESH` (string RVAs `0xAA978`, `0xAA960`, `0xAA818`, `0xAA948`).
Defaults are 640, 480, true, and 85. The fullscreen branch formats `%d %d 32 %d`
and writes `VID_MODE`, then calls system slot `+0x118` (MSystem vtable
`0x1DB748+0x118 -> 0x77730`). The windowed branch writes `VID_MODE=desktop`.

```asm
; SbzEngine.exe — fullscreen branch, after reading the VIDEO_DISPLAY_* options
11938  mov [rsp+20h],eax      ; refresh
1193C  mov r9d,r14d           ; height
1193F  mov r8d,r13d           ; width
11942  lea rdx,[rip+9A33Fh]   ; "%d %d 32 %d"
1194E  call 64480h            ; string formatting
11956  mov r8,rax
11959  lea rdx,[rip+9A318h]   ; "VID_MODE"
11963  call [r9+248h]         ; environment setter
11971  call [rax+118h]        ; DC_InitVidModeFromEnvironment
```

Windowed branch evidence: `0x1197C` loads `desktop` at `0xABC70`, `0x11983` loads
`VID_MODE`, and `0x1198D` calls setter slot `+0x258`. A default fullscreen request
of 640×480 at 85 Hz can therefore select a nearby supported 75-Hz mode. The
presence of `VID_MODE=desktop` in a subsequently saved file does not establish
which request reached the first exclusive mode change. The exact file-save
provenance/event ordering of the earlier runs is not established here.

## 2. Main window and styles — high confidence

Only `CreateWindowExA` is imported. Wrapper **RndrGL RVA `0x46E40`**, sole API
call **`0x46F99`**, receives style in EDX, exstyle in R8D, and a geometry object
in R9. It calls `AdjustWindowRectEx` at `0x46EA7`, optionally centres in
`SPI_GETWORKAREA`, and then creates the window. Class string at `0x94BF8` is
`MOS Win32/OpenGL`; title at `0x95118` is the game's full title.

```asm
; RndrGL.dll — CreateWindowExA wrapper
46E6D  mov esi,r8d            ; exstyle
46E70  mov ebp,edx            ; style
; stack args +20/+28/+30/+38 = x/y/width/height; +40 = parent
46F86  mov r9d,ebp            ; dwStyle
46F89  lea r8,[rip+4E188h]    ; title
46F90  lea rdx,[rip+4DC61h]   ; class
46F97  mov ecx,esi            ; dwExStyle
46F99  call [rip+47D99h]      ; IAT 8ED38: CreateWindowExA
```

Main creation function **`0x4EBD0`**, wrapper call **`0x4EDEA`**, computes
`fullscreen = !(descriptor.flags & 0x80)`, as does exported
`MSystem!CDisplayContext::IsFullScreen` (`0x146A20`, final test `0x146A74`–`0x146A7C`).

```asm
; RndrGL.dll — main window's style and extended style
4ED81  mov eax,80000000h
4ED86  mov r13d,6CF0000h
4ED8C  cmovne r13d,eax       ; fullscreen picks WS_POPUP
4ED90  bts r13d,1Ch          ; WS_VISIBLE
4ED95  movzx eax,r12b        ; fullscreen bool
4ED99  neg al
4ED9B  sbb ebp,ebp
4ED9D  and ebp,8             ; WS_EX_TOPMOST for fullscreen
4EDA0  test r14b,2           ; caller creation flags
4EDA4  jne 4EDAAh
4EDA6  bts ebp,12h           ; WS_EX_APPWINDOW unless flags&2
```

| Path | Style | Exstyle |
|---|---|---|
| Main fullscreen | `0x90000000` = WS_POPUP + WS_VISIBLE | `0x40008` normally; `0x8` if creation flags & 2 |
| Main windowed | `0x16CF0000` = WS_VISIBLE + WS_CLIPSIBLINGS + WS_CLIPCHILDREN + WS_OVERLAPPEDWINDOW | `0x40000` normally; `0` if flags & 2 |

Main caller supplies parent NULL (`0x4EDD7`), calculates client geometry through
`0x46AB0`, and enables centring only for windowed (`0x4EDCD`–`0x4EDD3`).
Fullscreen uses origin zero and chosen mode dimensions (geometry tail
`0x46D8E` onward); the wrapper then performs the Win32 frame adjustment.

A **second wrapper caller**, function `0x4F660`, call `0x4F83A`, accepts its style
from its caller (`EDX -> R14D -> EDX`) and parent from its caller. It uses exstyle
`0x8` or `0x40008` (`0x4F7E2`–`0x4F7F0`) and does not centre. Do not assume
every window from the common wrapper is the main top-level window.

No `SetWindowLong[Ptr]*` style writes were found. The renderer reads styles via
`GetWindowLongA`; a mode transition can destroy/recreate the window instead
(`0x4DD49`, slot `+0x100 -> 0x4F0B0`; `0x4E05E`, slot `+0xF8 -> 0x4EBD0`).
The only `SetWindowPos` site is **`0x4CE7E`**, function **`0x4CD50`**:

```asm
; RndrGL.dll — resize/reposition: RECT differences already in EAX/ECX
4CE68  mov dword ptr [rsp+30h],0 ; uFlags=0; no SWP_FRAMECHANGED
4CE70  mov [rsp+28h],ecx      ; height
4CE74  mov [rsp+20h],eax      ; width
4CE78  xor edx,edx           ; hWndInsertAfter=HWND_TOP
4CE7A  mov rcx,[rbx+28h]     ; hWnd; R8D/R9D hold x/y
4CE7E  call [rip+41E2Ch]      ; IAT 8ECB0: SetWindowPos
```

## 3. Every identified display-change site and lifecycle — high confidence

Both sites are in **RndrGL.dll**, through IAT **`0x8ED68`**, `ChangeDisplaySettingsA`.
Neither passes NULL for the DEVMODE. No Ex/W variants were found.

| Call RVA | Owning routine | Conditions / lifecycle |
|---|---|---|
| `0x4DEBF` | `0x4DB20`, display-context SetMode override, vtable +0x50 | Startup numeric selection; later mode switches; deferred focus-loss mode 0 and focus-regain saved mode. Uses selected descriptor's DEVMODE at +0x58. Flags `CDS_FULLSCREEN=4` for fullscreen, `0` for windowed/desktop. |
| `0x45B87` | `0x45AE0`, desktop-restoration helper | Only if cached width at context +0x708 is nonzero and fullscreen state bit 0x10 at +0x6A0 is set. Called by destructor `0x4FB40` at `0x4FC73`, before destroying windows and unregistering their class. Flags 0; applies cached desktop dimensions/bpp and preserved desktop DEVMODE refresh. |

SetMode skips an unchanged mode unless forced by state bit `0x100`; state bit
`0x8` prevents reentry (`0x4DB66`–`0x4DB8B`). The API call itself is gated by
`0x4DD60`–`0x4DD63`: it runs when the fullscreen state changes **or the new mode
is fullscreen**. Switching between windowed entries need not invoke it.

```asm
; RndrGL.dll — apply selected DEVMODE; r14b=new fullscreen bool
4DEB0  movzx eax,r14b
4DEB4  neg al
4DEB6  sbb edx,edx
4DEB8  and edx,4             ; fullscreen -> CDS_FULLSCREEN, otherwise 0
4DEBB  lea rcx,[r15+58h]
4DEBF  call [rip+40EA3h]     ; ChangeDisplaySettingsA
```

On `DISP_CHANGE_FAILED=-1` it retries at 100-ms intervals. Before the global
success flag (`0xB9598`) is set, its counter begins at 20; after that it begins
at -1, effectively indefinite retries on this specific failure. This matters
for a suppression hook: returning failure can trap the transition in retries.

Cached desktop caps are captured by `0x47450`: GetDesktopWindow/GetDC followed
by GetDeviceCaps HORZRES, VERTRES, BITSPIXEL, VREFRESH, saved at context
`+0x708/+0x70C/+0x710/+0x714`. The synthetic descriptor DEVMODE is refreshed too
(`0x4760B`–`0x47635`). This helper runs during initialisation and when entering
fullscreen from a non-fullscreen state (`0x4DE86`–`0x4DE8E`).

```asm
; RndrGL.dll — restoration helper, separate from ordinary SetMode
45AF9  cmp dword ptr [rcx+708h],0
45B07  je 45B9Ch
45B0D  test byte ptr [rcx+6A0h],10h
45B14  je 45B9Ch
; copy synthetic entry's DEVMODE, overwrite cached width/height/bpp
45B71  lea rcx,[rsp+20h]
45B76  xor edx,edx
45B87  call [rip+491DBh]     ; ChangeDisplaySettingsA
45B8D  and dword ptr [rbx+6A0h],0FFFFFFEFh
```

## 4. Window procedure and focus — high confidence for code; timing unproven

The WndProc is **RndrGL.dll RVA `0x49580`**. Registration function `0x4BEF0`
stores it in WNDCLASSA.lpfnWndProc at `0x4BF21`/`0x4BF28`, and calls
`RegisterClassA` at **`0x4BF86`** (class style `0x23`: CS_OWNDC | CS_HREDRAW |
CS_VREDRAW). Dispatch initially resolves HWND to the renderer's window record.

For messages 2–16 it uses a jump table at **`0x4A898`**, indexed by `(msg-2)`
(`0x4979F`–`0x497BE`). Relevant entries:

| Message | Table entry RVA | Handler RVA | Effect |
|---|---|---|---|
| WM_ACTIVATE `0x6` | `0x4A8A8` | `0x4A693` | No explicit game activation action; forwards to DefWindowProcA. |
| WM_SETFOCUS `0x7` | `0x4A8AC` | `0x49831` | Clears input state; if suspended and HWND matches, queues saved previous mode. Otherwise can minimise the newly focused window. |
| WM_KILLFOCUS `0x8` | `0x4A8B0` | `0x497C0` | Releases capture; if fullscreen and not already guarded, minimises, saves current mode, queues mode 0. |

`WM_ACTIVATEAPP=0x1C` misses the small-message table (`0x497A6 -> 0x4A822`) and
also forwards to **DefWindowProcA at `0x4A841`**. No explicit renderer branch
handles it. An early fallback DefWindowProcA call also exists at `0x49780` when
the game/system context is unavailable.

```asm
; RndrGL.dll — WM_KILLFOCUS, fullscreen/guard tests precede the minimise
497C0  xor ecx,ecx
497C2  call [rip+45508h]     ; SetCapture(NULL)
497CE  call [r11+60h]        ; IsFullScreen
497D2  test al,al
497D4  je 4A811h
497DA  cmp qword ptr [rbx+6A8h],0
497E2  jne 4A800h
497E8  mov [rbx+6A8h],r14    ; guard HWND
497EF  mov edx,6             ; SW_MINIMIZE
497F7  call [rip+454DBh]     ; ShowWindow
```

```asm
; RndrGL.dll — remaining loss state, then separate focus-regain excerpt
497FD  or dword ptr [rbx+6A0h],40h ; suspended flag
4980A  call [rax+58h]             ; GetMode
4980D  mov [rbx+6B0h],eax        ; remember previous mode
49816  mov [rbx+718h],r12d       ; pending mode=0, r12d=0
; WM_SETFOCUS, suspended flag and matching HWND already checked:
49981  and eax,0FFFFFFBFh
49984  mov [rbx+6A0h],eax
4998A  mov eax,[rbx+6B0h]
49990  mov [rbx+718h],eax        ; pending previous mode
49996  mov qword ptr [rbx+6A8h],0
```

The second ShowWindow site **`0x499B0`** also uses `SW_MINIMIZE=6`, when focus
lands on a different HWND while the renderer's suspended flag is set. There is
no explicit `ShowWindow(SW_RESTORE)` call: regain queues a mode restoration.

The page-flip override **`0x4E520`** (vtable +0xC0) applies pending changes,
**after** the swap/foreground check. It sleeps 100 ms if the foreground process
is not this process, then still reaches the pending-mode logic:

```asm
; RndrGL.dll — deferred mode application in page flip
4E7C2  mov edx,[rsi+718h]
4E7CA  test edx,edx
4E7CC  js 4E8ECh
4E7D2  mov rax,[rsi]
4E7D5  mov rcx,rsi
4E7D8  call [rax+50h]            ; SetMode -> same ChangeDisplaySettingsA site
4E7DB  mov dword ptr [rsi+718h],0FFFFFFFFh
```

Therefore it is inaccurate to say the game has no desktop-restore path on loss.
It has a **deferred** one. If page flip is not reached, is delayed during startup,
or the pending value is replaced before consumption, a minimised exclusive
window can continue holding the display mode. Static analysis cannot decide
which happened in the recorded background-start run. OS restoration on process
termination is separate from proving this destructor was executed.

## Complete relevant API-site inventory

All rows below are **RndrGL.dll RVAs**. Other six modules have no statically
identified references to these imported APIs. This includes destroy/recreate
and geometry helpers needed to avoid missing a later window reset.

| API | IAT RVA | All call RVAs |
|---|---|---|
| EnumDisplaySettingsA | `0x8ECB8` | `0x4C4AF` |
| CreateWindowExA | `0x8ED38` | `0x46F99` |
| ChangeDisplaySettingsA | `0x8ED68` | `0x45B87`, `0x4DEBF` |
| ShowWindow | `0x8ECD8` | `0x497F7`, `0x499B0` |
| SetWindowPos | `0x8ECB0` | `0x4CE7E` |
| GetWindowLongA (reads only) | `0x8ED28` | `0x488D3`, `0x488E1` |
| RegisterClassA | `0x8ECC0` | `0x4BF86` |
| DefWindowProcA | `0x8ECC8` | `0x49780`, `0x4A841` |
| AdjustWindowRectEx | `0x8ED30` | `0x46EA7`, `0x488F4` |
| DestroyWindow | `0x8ECA8` | `0x4F243` |
| SetWindowLong[Ptr]A/W | absent | none found |

## 5. Recommended hook points — medium confidence until runtime validation

1. **Filtered mode list:** intercept `EnumDisplaySettingsA` for calls from
   RndrGL, especially return RVA `0x4C4B5` after its call `0x4C4AF` (IAT
   `0x8ECB8`). Build a stable deduplicated cache from the real API, include native
   mode early, and serve a contiguous indexed list of **at most 512 real modes**.
   The synthetic windowed entry is added by the engine outside that budget.
   Preserve real DEVMODE fields, bpp distinctions, and special current/registry
   settings queries for other callers. This binary uses A; W coverage is useful
   for other builds, not required by an observed W call here. Replacing the
   `0x200` immediate alone would leave duplicates and ordering issues intact.
2. **Borderless at desktop size:** prefer the existing internal windowed route:
   make `VID_MODE=desktop` at consumption and `VID_DWIDTH/VID_DHEIGHT` equal the
   physical monitor size **before list construction**. Account for the separate
   SbzEngine VIDEO_DISPLAY_* rewrite. Then intercept main-window
   `CreateWindowExA` (`0x46F99`; wrapper `0x46E40`) to use a visible popup without
   frame/topmost and the monitor's physical origin/extent. Identify the actual
   top-level game HWND, not every use of the shared class/wrapper. Maintain
   geometry across `SetWindowPos` (`0x4CE7E`) and destruction/recreation. If
   altering styles after creation, the plugin itself needs SetWindowLongPtr plus
   SWP_FRAMECHANGED; the original game does not supply that step. API geometry
   alone does not change the engine's internal fullscreen flag or render size.
3. **Suppress mode changes:** intercept `ChangeDisplaySettingsA` for the owned
   renderer calls (`0x4DEBF` and `0x45B87`) in borderless policy and report
   `DISP_CHANGE_SUCCESSFUL=0` without changing the desktop. Preserve normal
   behavior for unrelated callers/policies. Cover teardown as well as startup.
   Retaining an internal fullscreen mode still permits focus-loss minimisation
   and queued mode/window recreation; use the internal windowed route, or
   explicitly handle that lifecycle too. Merely suppressing the API is not a
   complete borderless implementation.

These are candidates for Tasks 7/8; no hooks or binary patches are implemented
by this research. Any internal hook/patch must fingerprint this build and verify
expected bytes independently before modification.

## Exactly what runtime evidence remains necessary (not performed)

The static questions above are answered without a game run. The following
historical/runtime claims remain separate:

| Unsettled claim | Single log that would settle it |
|---|---|
| Exact 1600×900@120 selection and the earlier 60-Hz mismatch, including VID_MODE overwrite provenance | One timestamped **startup selection trace**: at each DC_InitVidModeFromEnvironment call, record the consumed VID_MODE string; at ModeList_Find entry/return record requested w/h/format/Hz, list tuples and winner; record the SbzEngine setters at 0x11963/0x1198D with source VIDEO_DISPLAY_* values and each actual Environment.cfg write's VID_MODE. In the same log, capture actual **A** enumerator indices/returned tuples to confirm the W-derived 803-mode ordering applies to this path. |
| Why a background-start minimised game retained 640×480 | One timestamped **window/mode lifecycle trace** for that startup: WndProc message, HWND, current mode, flags +0x6A0, guard +0x6A8 and pending +0x718 before/after; page-flip entry and pending-mode consumption; both ChangeDisplaySettingsA sites' DEVMODE/flags/return and current desktop mode immediately afterward. It distinguishes delayed/absent page flip, overwritten pending mode, and API failure. |
| Why the menu could offer/apply 1440p despite the startup cutoff | One **menu-apply trace** recording list count/tuples before opening and on apply, selected index, and the DEVMODE passed at 0x4DEBF. It distinguishes a rebuilt/augmented internal list from a different selection route. A second enumerator is not evidenced by these binaries. |

No smoke run was scheduled or performed: the user's no-run instruction takes
precedence over the plan's optional Task 6 run. Lead review of these notes remains
the parent task's integration step; this research sub-agent did not spawn a reviewer.
