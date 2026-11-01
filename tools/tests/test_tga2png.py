"""Tests for tools/tga2png.py.

Every TGA here is built in the test: the real screenshots live under C:\\Games
and are never read. Decoded PNGs are checked with the small zlib reader below,
so the tests need no Pillow or numpy.
"""

from __future__ import annotations

import io
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pytest

TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

import tga2png  # noqa: E402  (import needs the sys.path entry above)

SCRIPT = TOOLS_DIR / "tga2png.py"
TOP_LEFT = 0x20


def tga_header(width, height, image_type, depth, descriptor=TOP_LEFT, id_length=0):
    return struct.pack(
        "<BBBHHBHHHHBB",
        id_length,
        0,  # colour map type
        image_type,
        0,
        0,
        0,  # colour map spec
        0,
        0,  # x/y origin
        width,
        height,
        depth,
        descriptor,
    )


def rgb(*pixels):
    return b"".join(bytes(pixel) for pixel in pixels)


def decode_png(data: bytes):
    """Minimal PNG reader: (width, height, channels, top-down pixel bytes)."""
    assert data.startswith(b"\x89PNG\r\n\x1a\n"), "missing PNG signature"
    position = 8
    header = None
    compressed = bytearray()
    while position < len(data):
        (length,) = struct.unpack_from(">I", data, position)
        tag = data[position + 4:position + 8]
        body = data[position + 8:position + 8 + length]
        (crc,) = struct.unpack_from(">I", data, position + 8 + length)
        assert crc == zlib.crc32(tag + body) & 0xFFFFFFFF, f"bad CRC in {tag!r}"
        position += 12 + length
        if tag == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif tag == b"IDAT":
            compressed += body
        elif tag == b"IEND":
            break
    assert header is not None, "no IHDR chunk"
    width, height, depth, colour_type, compression, filtering, interlace = header
    assert (depth, compression, filtering, interlace) == (8, 0, 0, 0)
    channels = {0: 1, 2: 3}[colour_type]
    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    assert len(raw) == height * (stride + 1)
    rows = []
    for y in range(height):
        offset = y * (stride + 1)
        assert raw[offset] == 0, "only filter type 0 is expected"
        rows.append(raw[offset + 1:offset + 1 + stride])
    return width, height, channels, b"".join(rows)


def convert(tmp_path, data: bytes, autocontrast=False):
    source = tmp_path / "test.tga"
    source.write_bytes(data)
    destination = tga2png.convert_file(source, autocontrast=autocontrast)
    assert destination == source.with_suffix(".png")
    return destination.read_bytes()


def run_tool(*arguments):
    return subprocess.run(
        [sys.executable, str(SCRIPT), *map(str, arguments)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def test_rle_crossing_rows(tmp_path):
    red, green, blue, cyan, white = (
        b"\x00\x00\xff",  # BGR for RGB red
        b"\x00\xff\x00",  # green
        b"\xff\x00\x00",  # blue
        b"\xff\xff\x00",  # cyan
        b"\xff\xff\xff",  # white
    )
    # A 3x2 image whose first raw RLE packet holds 4 pixels: it starts on the
    # first row and ends on the second. The TGA spec forbids this; the game does it.
    data = (
        tga_header(3, 2, 10, 24)
        + bytes([0x03])  # raw packet, 4 pixels
        + red + green + blue + cyan
        + bytes([0x80 | 1])  # run packet, 2 pixels
        + white
    )
    assert decode_png(convert(tmp_path, data)) == (
        3,
        2,
        3,
        rgb((255, 0, 0), (0, 255, 0), (0, 0, 255), (0, 255, 255), (255, 255, 255), (255, 255, 255)),
    )


def test_gray_rle(tmp_path):
    data = (
        tga_header(4, 2, 11, 8)
        + b"\x81\x0a"  # run of 2 pixels of 10
        + b"\x81\x14"  # 2 x 20
        + b"\x81\x1e"  # 2 x 30
        + b"\x81\x28"  # 2 x 40
    )
    assert decode_png(convert(tmp_path, data)) == (4, 2, 1, bytes([10, 10, 20, 20, 30, 30, 40, 40]))


@pytest.mark.parametrize("depth", [24, 32])
def test_uncompressed_24_and_32(tmp_path, depth):
    first = b"\x01\x02\x03"
    second = b"\x10\x20\x30"
    if depth == 32:  # alpha is dropped
        first += b"\x7f"
        second += b"\x80"
    data = tga_header(2, 1, 2, depth, id_length=6) + b"gameid" + first + second
    assert decode_png(convert(tmp_path, data)) == (2, 1, 3, rgb((3, 2, 1), (0x30, 0x20, 0x10)))


def test_bottom_left_origin_flipped(tmp_path):
    bottom, top = b"\x01\x02\x03", b"\x04\x05\x06"
    bottom_right, top_right = b"\x07\x08\x09", b"\x0a\x0b\x0c"
    # descriptor 0: bit 5 clear, so the first stored row is the bottom row.
    data = tga_header(2, 2, 2, 24, descriptor=0) + bottom + bottom_right + top + top_right
    assert decode_png(convert(tmp_path, data)) == (
        2,
        2,
        3,
        rgb((6, 5, 4), (12, 11, 10), (3, 2, 1), (9, 8, 7)),
    )


def test_truncated_reports_error(tmp_path):
    # Truecolor header promises 2x2x3 = 12 pixel bytes but only 5 are present.
    source = tmp_path / "short.tga"
    source.write_bytes(tga_header(2, 2, 2, 24) + b"\x01" * 5)
    destination = tmp_path / "short.png"
    result = run_tool(source, destination)
    assert result.returncode != 0
    assert "truncated pixel data: 5 of 12 bytes (7 missing)" in result.stderr
    assert not destination.exists(), "a failed decode must not leave a PNG"

    # RLE stream ends after 4 of 16 pixels (12 of 48 bytes).
    source = tmp_path / "short-rle.tga"
    source.write_bytes(tga_header(4, 4, 10, 24) + b"\x03" + b"\x00\x00\xff" * 4)
    destination = tmp_path / "short-rle.png"
    result = run_tool(source, destination)
    assert result.returncode != 0
    assert "truncated RLE data: 12 of 48 pixel bytes (36 missing)" in result.stderr
    assert not destination.exists()

    # Too short even for the 18-byte header.
    source = tmp_path / "header.tga"
    source.write_bytes(b"\x00" * 10)
    result = run_tool(source, tmp_path / "header.png")
    assert result.returncode != 0
    assert "truncated header" in result.stderr


def test_folder_mode(tmp_path):
    folder = tmp_path / "ScreenShots"
    folder.mkdir()
    (folder / "Screen00.TGA").write_bytes(tga_header(1, 1, 2, 24) + b"\x00\x00\xff")
    (folder / "screen01_z.tga").write_bytes(tga_header(1, 1, 3, 8) + b"\x40")
    (folder / "notes.txt").write_text("keep me", encoding="utf-8")

    result = run_tool(folder)
    assert result.returncode == 0, result.stderr
    assert {path.name for path in folder.iterdir()} == {
        "Screen00.TGA",
        "Screen00.png",
        "screen01_z.tga",
        "screen01_z.png",
        "notes.txt",
    }
    assert decode_png((folder / "Screen00.png").read_bytes()) == (1, 1, 3, rgb((255, 0, 0)))
    assert decode_png((folder / "screen01_z.png").read_bytes()) == (1, 1, 1, b"\x40")


def test_autocontrast_stretches_depth_image(tmp_path):
    data = tga_header(3, 1, 3, 8) + bytes([50, 60, 100])
    assert decode_png(convert(tmp_path, data, autocontrast=True)) == (
        3,
        1,
        1,
        bytes([0, 51, 255]),
    )


def test_png_opens_in_pillow(tmp_path):
    """Optional cross-check of the hand-written PNG against a real decoder."""
    pillow = pytest.importorskip("PIL.Image")
    png = convert(tmp_path, tga_header(2, 1, 2, 24) + b"\x01\x02\x03\x04\x05\x06")
    with pillow.open(io.BytesIO(png)) as image:
        assert image.mode == "RGB"
        assert image.tobytes() == rgb((3, 2, 1), (6, 5, 4))
