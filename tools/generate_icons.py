#!/usr/bin/env python3
"""Cuts the Sonnet icon for every platform from the artwork in assets/branding (docs/branding.md).

Writes, relative to the repository root:
  packaging/linux/icons/hicolor/<n>/apps/sonnet_<app>.png   16, 24, 32, 48, 64, 128, 256 and 512 px, for the
                                                     editor and the player (named after the executables)
  apps/icon.ico                                      16, 24, 32, 48, 64, 128 and 256 px, for apps/icon.rc
  apps/player/android/res/mipmap-<density>/          the adaptive icon's three layers, 108 dp
  apps/player/ios/Assets.xcassets/                   the AppIcon set, the 1024 px icon without alpha
  modules/platform/src/WindowIconData.cpp            the 256 px window icons, embedded in the platform module

Sizes up to 48 px come from icon-small.png, larger ones from icon-desktop.png. Needs Pillow.
Run from anywhere: python3 tools/generate_icons.py
The outputs are committed; re-running overwrites them identically.
"""
import io
import json
import struct
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "assets" / "branding"

HICOLOR_SIZES = [16, 24, 32, 48, 64, 128, 256, 512]
HICOLOR_APPS = ["editor", "player"]
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
SMALL_LIMIT = 48
# mdpi to xxxhdpi: 108 dp at 1, 1.5, 2, 3 and 4 pixels per dp.
ANDROID_DENSITIES = {"mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}
ANDROID_LAYERS = ["foreground", "background", "monochrome"]


def load(name: str) -> Image.Image:
    return Image.open(ART / name).convert("RGBA")


def cut(size: int, small: Image.Image, desktop: Image.Image) -> Image.Image:
    source = small if size <= SMALL_LIMIT else desktop
    return source.resize((size, size), Image.LANCZOS)


def png_bytes(image: Image.Image) -> bytes:
    out = io.BytesIO()
    image.save(out, "PNG", optimize=True)
    return out.getvalue()


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def ico_entry(image: Image.Image) -> bytes:
    """One icon image: PNG at 256 px, an uncompressed 32-bit DIB below it, which every resource
    compiler (rc.exe, llvm-rc) accepts."""
    size = image.width
    if size == 256:
        return png_bytes(image)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = bytearray()
    for y in range(size - 1, -1, -1):  # bottom-up
        for x in range(size):
            r, g, b, a = image.getpixel((x, y))
            pixels += bytes((b, g, r, a))
    mask_row = ((size + 31) // 32) * 4
    return header + bytes(pixels) + bytes(mask_row * size)  # AND mask 0: the alpha channel decides


def ico(images: list[Image.Image]) -> bytes:
    entries = [ico_entry(image) for image in images]
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for image, data in zip(images, entries):
        side = 0 if image.width == 256 else image.width
        out += struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    return bytes(out) + b"".join(entries)


def byte_array(name: str, data: bytes) -> str:
    lines = [f"const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 24):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i : i + 24]) + ",")
    lines.append("};")
    lines.append(f"const std::size_t {name}Size = sizeof({name});")
    return "\n".join(lines)


def main() -> None:
    small = load("icon-small.png")
    desktop = load("icon-desktop.png")

    # System Monitor looks for a theme icon named after the process, so each executable has its own.
    for size in HICOLOR_SIZES:
        data = png_bytes(cut(size, small, desktop))
        for app in HICOLOR_APPS:
            write(ROOT / f"packaging/linux/icons/hicolor/{size}x{size}/apps/sonnet_{app}.png", data)

    write(ROOT / "apps/icon.ico", ico([cut(size, small, desktop) for size in ICO_SIZES]))

    for density, size in ANDROID_DENSITIES.items():
        for layer in ANDROID_LAYERS:
            image = load(f"icon-{layer}.png").resize((size, size), Image.LANCZOS)
            write(ROOT / f"apps/player/android/res/mipmap-{density}/ic_launcher_{layer}.png", png_bytes(image))

    catalog = ROOT / "apps/player/ios/Assets.xcassets"
    icon_set = catalog / "AppIcon.appiconset"
    write(icon_set / "icon.png", png_bytes(load("icon-ios.png").convert("RGB")))  # App Store icons carry no alpha
    contents = {"images": [{"filename": "icon.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}]}
    info = {"info": {"author": "xcode", "version": 1}}
    write(icon_set / "Contents.json", (json.dumps({**contents, **info}, indent=2) + "\n").encode())
    write(catalog / "Contents.json", (json.dumps(info, indent=2) + "\n").encode())

    desktop_icon = png_bytes(desktop.resize((256, 256), Image.LANCZOS))
    macos_icon = png_bytes(load("icon-macos.png").resize((256, 256), Image.LANCZOS))
    source = "\n".join(
        [
            "// Generated by tools/generate_icons.py from assets/branding; do not edit.",
            '#include "WindowIconData.h"',
            "",
            "namespace sonnet::platform {",
            "",
            "// clang-format off",
            byte_array("DesktopWindowIcon", desktop_icon),
            "",
            byte_array("MacWindowIcon", macos_icon),
            "// clang-format on",
            "",
            "} // namespace sonnet::platform",
            "",
        ]
    )
    write(ROOT / "modules/platform/src/WindowIconData.cpp", source.encode())


if __name__ == "__main__":
    main()
