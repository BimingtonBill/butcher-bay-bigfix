"""Decoder for the game's ScreenNN.TGA screenshots.

The game writes ``ScreenNN.TGA`` (type 10, RLE truecolor, 24 bpp, top-left
origin) and ``ScreenNN_z.TGA`` (type 11, RLE grayscale, 8 bpp depth). Pillow
refuses them with "buffer overrun when reading image file" because the RLE
packets run across scanline boundaries. The TGA spec forbids that, the game
does it anyway, so this decoder reads the pixel stream as one flat run of
pixels and lets packets cross rows freely.

Usage::

    python tools/tga2png.py Screen00.TGA
    python tools/tga2png.py Screen00.TGA out.png
    python tools/tga2png.py ScreenShots/               # every *.tga/*.TGA
    python tools/tga2png.py Screen00_z.TGA --autocontrast

Output PNGs are written directly with zlib (filter type 0), so the tool has no
dependencies beyond the standard library. Images that cannot be decoded
completely are reported and no PNG is written.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

# idLength, colourMapType, imageType, colourMapSpec(5), xOrigin, yOrigin,
# width, height, pixelDepth, imageDescriptor -- 18 bytes, little endian.
HEADER = struct.Struct("<BBBHHBHHHHBB")

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

_TOP_LEFT = 0x20  # descriptor bit 5: rows stored top to bottom
_RIGHT_LEFT = 0x10  # descriptor bit 4: pixels stored right to left
_RLE_PACKET = 0x80

_UNCOMPRESSED_TYPES = (2, 3)  # truecolor, grayscale
_RLE_TYPES = (10, 11)  # RLE truecolor, RLE grayscale
_GRAY_TYPES = (3, 11)


class TgaError(ValueError):
    """The TGA data is invalid or truncated."""


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    channels: int  # 1 = grayscale, 3 = RGB, rows top to bottom
    pixels: bytes


def decode(data: bytes) -> Image:
    """Decode TGA bytes into a top-down ``Image``."""
    if len(data) < HEADER.size:
        raise TgaError(f"truncated header: {len(data)} of {HEADER.size} bytes")

    (id_length, colour_map, image_type, _colour_map_first, _colour_map_length,
     _colour_map_entry, _x_origin, _y_origin, width, height, depth,
     descriptor) = HEADER.unpack_from(data)

    if colour_map:
        raise TgaError("colour-mapped TGA files are not supported")
    if image_type not in _UNCOMPRESSED_TYPES + _RLE_TYPES:
        raise TgaError(f"unsupported image type {image_type} (supported: 2, 3, 10, 11)")
    if width == 0 or height == 0:
        raise TgaError(f"empty image: {width}x{height}")

    gray = image_type in _GRAY_TYPES
    allowed_depths = (8,) if gray else (24, 32)
    if depth not in allowed_depths:
        kind = "grayscale" if gray else "truecolor"
        raise TgaError(
            f"unsupported pixel depth {depth} for {kind} image type {image_type}"
            f" (supported: {', '.join(map(str, allowed_depths))})"
        )
    bytes_per_pixel = depth // 8

    start = HEADER.size + id_length
    if start > len(data):
        raise TgaError(
            f"truncated image ID field: header asks for {id_length} bytes,"
            f" file ends after {len(data) - HEADER.size}"
        )

    pixel_bytes = width * height * bytes_per_pixel
    if image_type in _RLE_TYPES:
        pixels = _decode_rle(data, start, bytes_per_pixel, pixel_bytes)
    else:
        available = len(data) - start
        if available < pixel_bytes:
            raise TgaError(
                f"truncated pixel data: {available} of {pixel_bytes} bytes"
                f" ({pixel_bytes - available} missing)"
            )
        pixels = data[start:start + pixel_bytes]

    if not gray:
        pixels = _bgr_to_rgb(pixels, bytes_per_pixel)
    pixels = _orient(pixels, width, height, 1 if gray else 3, descriptor)
    return Image(width, height, 1 if gray else 3, pixels)


def _decode_rle(data: bytes, position: int, bytes_per_pixel: int, pixel_bytes: int) -> bytes:
    """Expand the RLE stream, allowing packets to cross scanlines."""
    out = bytearray()
    end = len(data)
    while len(out) < pixel_bytes:
        if position >= end:
            raise TgaError(
                f"truncated RLE data: {len(out)} of {pixel_bytes} pixel bytes"
                f" ({pixel_bytes - len(out)} missing)"
            )
        packet = data[position]
        position += 1
        length = (packet & 0x7F) + 1
        if packet & _RLE_PACKET:
            if position + bytes_per_pixel > end:
                raise TgaError(
                    f"truncated RLE run packet: {len(out)} of {pixel_bytes} pixel bytes"
                    f" ({pixel_bytes - len(out)} missing)"
                )
            out += data[position:position + bytes_per_pixel] * length
            position += bytes_per_pixel
        else:
            size = length * bytes_per_pixel
            if position + size > end:
                raise TgaError(
                    f"truncated RLE raw packet: {len(out)} of {pixel_bytes} pixel bytes"
                    f" ({pixel_bytes - len(out)} missing)"
                )
            out += data[position:position + size]
            position += size
        if len(out) > pixel_bytes:
            raise TgaError(
                f"RLE data overruns the image: {len(out)} pixel bytes"
                f" for a {pixel_bytes}-byte image"
            )
    return bytes(out)


def _bgr_to_rgb(pixels: bytes, bytes_per_pixel: int) -> bytes:
    out = bytearray((len(pixels) // bytes_per_pixel) * 3)
    out[0::3] = pixels[2::bytes_per_pixel]
    out[1::3] = pixels[1::bytes_per_pixel]
    out[2::3] = pixels[0::bytes_per_pixel]
    return bytes(out)


def _orient(pixels: bytes, width: int, height: int, channels: int, descriptor: int) -> bytes:
    """Reorder rows/pixels from the TGA origin to top-down, left-right."""
    row_length = width * channels
    if not descriptor & _TOP_LEFT:
        rows = [pixels[y * row_length:(y + 1) * row_length] for y in range(height)]
        pixels = b"".join(reversed(rows))
    if descriptor & _RIGHT_LEFT:
        out = bytearray(len(pixels))
        for y in range(height):
            start = y * row_length
            row = pixels[start:start + row_length]
            for channel in range(channels):
                out[start + channel:start + row_length:channels] = row[channel::channels][::-1]
        pixels = bytes(out)
    return pixels


def apply_autocontrast(image: Image) -> Image:
    """Stretch each channel's min..max range to 0..255 (for depth captures)."""
    out = bytearray(len(image.pixels))
    for channel in range(image.channels):
        plane = image.pixels[channel::image.channels]
        low, high = min(plane), max(plane)
        if high == low:
            out[channel::image.channels] = plane
        else:
            scale = 255 / (high - low)
            table = bytes(
                max(0, min(255, round((value - low) * scale))) for value in range(256)
            )
            out[channel::image.channels] = plane.translate(table)
    return Image(image.width, image.height, image.channels, bytes(out))


def encode_png(image: Image) -> bytes:
    """Encode a top-down image as PNG (8-bit, filter type 0)."""
    stride = image.width * image.channels
    scanlines = bytearray()
    for y in range(image.height):
        scanlines.append(0)  # filter type 0: None
        scanlines += image.pixels[y * stride:(y + 1) * stride]
    colour_type = 0 if image.channels == 1 else 2
    ihdr = struct.pack(">IIBBBBB", image.width, image.height, 8, colour_type, 0, 0, 0)
    return (
        PNG_SIGNATURE
        + _chunk(b"IHDR", ihdr)
        + _chunk(b"IDAT", zlib.compress(bytes(scanlines)))
        + _chunk(b"IEND", b"")
    )


def _chunk(tag: bytes, body: bytes) -> bytes:
    return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))


def convert_file(source: Path, destination: Path | None = None, *, autocontrast: bool = False) -> Path:
    """Convert one TGA file; returns the PNG path. Writes nothing on failure."""
    image = decode(source.read_bytes())
    if autocontrast:
        image = apply_autocontrast(image)
    png = encode_png(image)
    destination = destination if destination is not None else source.with_suffix(".png")
    destination.write_bytes(png)
    return destination


def _convert_folder(folder: Path, autocontrast: bool) -> int:
    sources = sorted(
        path for path in folder.iterdir() if path.is_file() and path.suffix.lower() == ".tga"
    )
    if not sources:
        print(f"error: no .tga files in {folder}", file=sys.stderr)
        return 1
    failures = 0
    for source in sources:
        try:
            destination = convert_file(source, autocontrast=autocontrast)
        except (OSError, TgaError) as error:
            failures += 1
            print(f"error: {source.name}: {error}", file=sys.stderr)
        else:
            print(f"{source.name} -> {destination.name}")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Convert the game's screenshot TGAs to PNG (standard library only).",
    )
    parser.add_argument("input", type=Path, help="a .tga file, or a folder of them")
    parser.add_argument("output", type=Path, nargs="?", help="output .png (file input only)")
    parser.add_argument(
        "--autocontrast",
        action="store_true",
        help="stretch each channel's min..max to 0..255 (useful for _z depth captures)",
    )
    arguments = parser.parse_args(argv)

    if arguments.input.is_dir():
        if arguments.output is not None:
            parser.error("an output path is not accepted when converting a folder")
        try:
            return _convert_folder(arguments.input, arguments.autocontrast)
        except OSError as error:
            print(f"error: {arguments.input}: {error}", file=sys.stderr)
            return 1

    try:
        destination = convert_file(
            arguments.input, arguments.output, autocontrast=arguments.autocontrast
        )
    except (OSError, TgaError) as error:
        print(f"error: {arguments.input}: {error}", file=sys.stderr)
        return 1
    print(f"{arguments.input} -> {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
