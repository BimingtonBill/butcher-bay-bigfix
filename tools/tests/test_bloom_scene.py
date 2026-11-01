"""Exercise launch planning only: no test starts a game or sends input."""
import ctypes
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "bloom_scene.ps1"


@unittest.skipUnless(shutil.which("pwsh"), "PowerShell 7 required")
class BloomSceneTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="bloom plan ")
        self.root = Path(self.tmp.name)
        system = self.root / "System/Win64_AMD64"
        system.mkdir(parents=True)
        self.exe = system / "SbzEngine.exe"
        self.pe = bytearray(256)
        self.pe[:2] = b"MZ"
        struct.pack_into("<I", self.pe, 0x3C, 128)
        struct.pack_into("<IH", self.pe, 128, 0x4550, 0x8664)
        self.exe.write_bytes(self.pe)
        (self.root / "Content/Demos").mkdir(parents=True)
        (self.root / "Content/Demos/panoptical01.XDM").touch()
        (self.root / "Content/Worlds").mkdir(parents=True)
        (self.root / "Content/Worlds/Pa1_Intro.XW").touch()
        self.config = self.root / "Environment.cfg"
        self.config.write_bytes(b"VID_MODE=1280 720 32 60\r\n")

    def tearDown(self):
        self.tmp.cleanup()

    def run_plan(self, *args):
        return subprocess.run(["pwsh", "-NoProfile", "-File", str(SCRIPT),
                               "-GameDir", str(self.root), "-DryRun", *args],
                              capture_output=True, text=True, timeout=20)

    def test_default_plan_is_x64_read_only_and_quotes_nested_exec(self):
        before = {str(p): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        result = self.run_plan()
        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads(result.stdout)
        self.assertEqual(Path(plan["Executable"]), self.exe)
        self.assertEqual(plan["ShaderMode"], 5)
        self.assertIn('-exec "map \\"Pa1_Intro\\""', plan["Arguments"])
        self.assertEqual(before, {str(p): p.read_bytes() for p in self.root.rglob("*") if p.is_file()})
        self.assertFalse(Path(plan["OutputDir"]).exists())
        # Check the actual Windows argument decoder, including a path with spaces.
        if hasattr(ctypes, "windll"):
            count = ctypes.c_int()
            parse = ctypes.windll.shell32.CommandLineToArgvW
            parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
            parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
            argv = parse("dummy " + plan["Arguments"], ctypes.byref(count))
            try:
                values = [argv[i] for i in range(count.value)]
                self.assertEqual(values[-2:], ["-exec", 'map "Pa1_Intro"'])
                self.assertEqual(values[1], "-env")
                self.assertEqual(Path(values[2]).name, "run.cfg")
            finally:
                ctypes.windll.kernel32.LocalFree(ctypes.cast(argv, ctypes.c_void_p))

    def test_map_and_shader_mode_are_explicit(self):
        result = self.run_plan("-Action", "Map", "-Scene", "Pa1_Intro", "-ShaderMode", "6")
        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads(result.stdout)
        self.assertIn('map \\"Pa1_Intro\\"', plan["Arguments"])
        self.assertEqual(plan["ShaderMode"], 6)

    def test_rejects_x86_even_in_x64_directory(self):
        struct.pack_into("<H", self.pe, 132, 0x14C)
        self.exe.write_bytes(self.pe)
        result = self.run_plan()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("non-AMD64", result.stderr)

    def test_rejects_missing_scene(self):
        result = self.run_plan("-Action", "Play", "-Scene", "absent")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing demo", result.stderr)


if __name__ == "__main__":
    unittest.main()
