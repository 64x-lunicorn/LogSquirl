#!/usr/bin/env python3
"""Derives the platform files of the document icon from its SVGs (#726).

The master, src/app/images/hicolor/scalable/logsquirl-document.svg, is the
sheet with the squirrel and serves 64 px and up. Below that the squirrel can
no longer be made out, so 48, 32 (and 24) and 16 px have their own sheets
without it in src/app/images/document-icon/.

Writes:
  src/app/images/hicolor/<n>x<n>/logsquirl-document.png   16 to 512 px
  Resources/logsquirl-document.ico                          16 to 256 px
  Resources/logsquirl-document.icns                         16 to 1024 px, with @2x

Needs rsvg-convert and Pillow; the .icns needs macOS's iconutil and is
skipped elsewhere.
"""

import io
import pathlib
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
IMAGES = ROOT / "src" / "app" / "images"
MASTER = IMAGES / "hicolor" / "scalable" / "logsquirl-document.svg"
SMALL = IMAGES / "document-icon"
RESOURCES = ROOT / "Resources"

HICOLOR_SIZES = [16, 32, 48, 64, 128, 256, 512]
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
# iconutil's names: icon_<n>x<n>.png and icon_<n>x<n>@2x.png
ICNS_SIZES = [16, 32, 128, 256, 512]


def source_for(size: int) -> pathlib.Path:
    if size <= 16:
        return SMALL / "logsquirl-document-16.svg"
    if size <= 32:
        return SMALL / "logsquirl-document-32.svg"
    if size <= 48:
        return SMALL / "logsquirl-document-48.svg"
    return MASTER


def render(size: int) -> Image.Image:
    png = subprocess.run(
        ["rsvg-convert", "-w", str(size), "-h", str(size), str(source_for(size))],
        check=True,
        capture_output=True,
    ).stdout
    return Image.open(io.BytesIO(png)).convert("RGBA")


def main() -> int:
    if shutil.which("rsvg-convert") is None:
        print("rsvg-convert is needed (librsvg)", file=sys.stderr)
        return 1

    for size in HICOLOR_SIZES:
        out = IMAGES / "hicolor" / f"{size}x{size}" / "logsquirl-document.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        render(size).save(out, optimize=True)

    images = [render(size) for size in ICO_SIZES]
    images[-1].save(
        RESOURCES / "logsquirl-document.ico",
        sizes=[(s, s) for s in ICO_SIZES],
        append_images=images[:-1],
    )

    if shutil.which("iconutil") is None:
        print("iconutil not found: logsquirl-document.icns left as it is")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        iconset = pathlib.Path(tmp) / "logsquirl-document.iconset"
        iconset.mkdir()
        for size in ICNS_SIZES:
            render(size).save(iconset / f"icon_{size}x{size}.png")
            render(2 * size).save(iconset / f"icon_{size}x{size}@2x.png")
        subprocess.run(
            [
                "iconutil",
                "-c",
                "icns",
                str(iconset),
                "-o",
                str(RESOURCES / "logsquirl-document.icns"),
            ],
            check=True,
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
