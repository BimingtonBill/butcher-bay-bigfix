"""Read-only EU 1.1 startup evidence; never loads a PE or launches the game.

Run with the project's Python -I (pefile and Capstone), --root GAME --verify.
"""
import argparse
import hashlib
from pathlib import Path
import re
import runpy

Image = runpy.run_path(str(Path(__file__).with_name("display_static.py")))["Image"]
MODULES = {
    "exe": ("System/Win64_AMD64/SbzEngine.exe", "5056ffe030c2ba8687b032f5f2ed95b42c4814db87dec61444a6cf264deaca38"),
    "world": ("Content/GameWorld_Win64_AMD64.dll", "749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7"),
    "system": ("System/Win64_AMD64/MSystem.dll", "b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500"),
}
EVIDENCE = {
    "exe": {
        0x11101: ("lea", "rdx, [rip + 0x9a88c]"),  # -MAP
        0x1114E: ("lea", "rdx, [rip + 0x9a833]"),  # map "%s"
        0x1116D: ("call", "0x70370"),             # ExecuteString
        0x11242: ("lea", "rdx, [rip + 0x9a6bb]"),  # -EXEC
        0x1127F: ("lea", "rcx, [rbp + 0x968]"),    # retain exec
        0x15DE0: ("call", "0x70370"),             # consume exec in early loop
        0x430C1: ("mov", "dword ptr [rsp + 0x58], 1"),  # map request
        0x447F1: ("mov", "dword ptr [rsp + 0x58], 4"),  # loadgame request
    },
    "world": {
        0x46F1D: ("lea", "r8, [rip + 0x1a11fc]"),  # QUICKMAP
        0x46F47: ("je", "0x46fd4"),               # empty -> QUICKMENU
        0x46F9E: ("call", "0x1898c2"),           # map after frontend init
        0x46FCF: ("jmp", "0x470c6"),            # skip default intro
        0x46FD7: ("lea", "r8, [rip + 0x1a112a]"),  # QUICKMENU
        0x470AA: ("lea", "rdx, [rip + 0x1a102f]"), # default startup command
        0x470C1: ("call", "0x1898c2"),
    },
    "system": {
        0x756FE: ("lea", "rdx, [rip + 0x1666ef]"), # -env
        0x75A5D: ("call", "qword ptr [r9 + 0x3e8]"), # environment file read
    },
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(root):
    images = {}
    for key, (relative, digest) in MODULES.items():
        im = images[key] = Image(root / relative)
        require(hashlib.sha256(im.raw).hexdigest() == digest, f"unrecognized {relative}")
        for at, expected in EVIDENCE[key].items():
            instruction = next(im.md.disasm(im.data[at:at + 15], at))
            require((instruction.mnemonic, instruction.op_str) == expected, f"{key}+{at:x} changed")
        print(f"{relative}: hash and {len(EVIDENCE[key])} instructions PASS")
    world = images["world"]
    require(world.data.startswith(b'cg_rootmenu "intro"\0', 0x1E80E0), "startup literal changed")
    require(world.data[0x1E8120:0x1E8129] == b'QUICKMAP\0', "QUICKMAP changed")
    require(world.data[0x1E8108:0x1E8112] == b'QUICKMENU\0', "QUICKMENU changed")
    refs = [i.address for i in world.instructions() if 0x1E80E0 in world.references(i)]
    require(refs == [0x470AA], f"unexpected startup literal references: {refs}")
    source = (root / "Content/Gui/CubeWnd.xrg").read_text(encoding="cp1252")
    windows = {}
    for block in re.split(r'(?m)^\*WINDOW ', source)[1:]:
        name = re.match(r'"([^"]+)"', block)
        if name:
            windows[name[1]] = block
    chain = ["intro", "legal", "kiosk_fork", "main_blank_logos", "esrb", "vug_logo",
             "universal_logo", "tigon_logo", "mixed_logo", "sbz_logo", "main"]
    compiled = (root / "Content/Gui/CubeWnd.xcr").read_bytes()
    for name, target in zip(chain, chain[1:]):
        # Drop comments so dormant script alternatives cannot prove a link.
        block = re.sub(r'(?m)//.*$', '', windows[name])
        require(f'\\"{target}\\"' in block, f"missing {name} -> {target}")
        require(f'"{target}"'.encode() in compiled, f"compiled target {target} missing")
    for name, texture in [("vug_logo", "VUG_NEW"), ("universal_logo", "UNIVERSAL_NTSC"),
                          ("tigon_logo", "TIGON"), ("mixed_logo", "LOGO_MIXED"), ("sbz_logo", "SBZ")]:
        require(f'*TEXTURE_NAME "*VIDEO_{texture}"' in windows[name], f"unexpected {name} video")
        require(f'*VIDEO_{texture}'.encode() in compiled, f"compiled video {texture} missing")
    require('*SHOW_PRESSSTART bottom' in windows["main"], "main is no longer title")
    require('*TEXTURE_NAME "*VIDEO_INTRO"' in windows["video_intro"], "story window changed")
    for name in ("Pa1_Intro", "Pa1_Arrival"):
        require((root / f"Content/Worlds/{name}.XW").is_file(), f"map {name} absent")
    print("Unique startup command, GUI/XCR chain, story window and opening maps PASS")
    for name in ("CubeWnd.xrg", "CubeWnd.xcr"):
        path = root / "Content/Gui" / name
        print(f"{name}: SHA-256 {hashlib.sha256(path.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--verify", action="store_true", required=True)
    args = parser.parse_args()
    verify(args.root)
