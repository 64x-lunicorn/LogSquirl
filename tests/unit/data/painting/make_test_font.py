#!/usr/bin/env python3
#
# Copyright (C) 2026 LogSquirl Contributors
#
# This file is part of LogSquirl.
#
# LogSquirl is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# LogSquirl is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with LogSquirl.  If not, see <http://www.gnu.org/licenses/>.

"""Generates logsquirl-painting-test.ttf, the font the painting test draws with.

The painting test compares the log view against golden images, pixel for
pixel, on Linux, macOS and Windows. A real font cannot do that: each
platform rasterises glyph outlines its own way, and the host may not have
the font at all. This font sidesteps both. At 16 px every edge of every
glyph falls exactly on a pixel boundary, so any rasteriser, hinting or not,
produces the same pixels -- and the test ships the font itself.

Every printable ASCII character is an 8 x 16 px cell (ascent 12, descent 4)
holding seven one-pixel bars, one per bit of the character code: a bar on
the left half for a 1, on the right half for a 0. Different characters
therefore draw different pixels, so a test that drew the wrong text would
see it. The space is empty, and anything without a glyph gets a solid box.

Run it again only to change the font; the output is byte-for-byte
reproducible. Needs fontTools (pip install fonttools).
"""

import os

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables.O_S_2f_2 import Panose

FAMILY_NAME = "LogSquirl Painting Test"
OUTPUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logsquirl-painting-test.ttf")

UNITS_PER_EM = 1024
# Font units per pixel when the font is used at a pixel size of 16.
PIXEL = UNITS_PER_EM // 16
CELL_WIDTH_PX = 8
ASCENT_PX = 12
DESCENT_PX = 4
BITS = 7

# A fixed timestamp keeps the generated file reproducible.
TIMESTAMP = 0


def add_rect(pen, x0, y0, x1, y1):
    """A filled rectangle in pixels: x to the right, y down from the top of the cell."""
    left, right = x0 * PIXEL, x1 * PIXEL
    top, bottom = (ASCENT_PX - y0) * PIXEL, (ASCENT_PX - y1) * PIXEL
    # Clockwise, as TrueType expects for a filled contour.
    pen.moveTo((left, bottom))
    pen.lineTo((left, top))
    pen.lineTo((right, top))
    pen.lineTo((right, bottom))
    pen.closePath()


def character_glyph(code):
    pen = TTGlyphPen(None)
    for bit in range(BITS):
        row = 1 + 2 * bit
        if code & (1 << bit):
            add_rect(pen, 1, row, 4, row + 1)
        else:
            add_rect(pen, 4, row, 7, row + 1)
    return pen.glyph()


def box_glyph():
    pen = TTGlyphPen(None)
    add_rect(pen, 1, 1, 7, 15)
    return pen.glyph()


def empty_glyph():
    return TTGlyphPen(None).glyph()


def main():
    glyph_order = [".notdef", "space"]
    glyphs = {".notdef": box_glyph(), "space": empty_glyph()}
    cmap = {0x20: "space"}
    for code in range(0x21, 0x7F):
        name = "c%02X" % code
        glyph_order.append(name)
        glyphs[name] = character_glyph(code)
        cmap[code] = name

    fb = FontBuilder(UNITS_PER_EM, isTTF=True)
    fb.font.recalcTimestamp = False
    fb.updateHead(created=TIMESTAMP, modified=TIMESTAMP)
    fb.setupGlyphOrder(glyph_order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)

    glyf = fb.font["glyf"]
    metrics = {}
    for name in glyph_order:
        glyph = glyf[name]
        glyph.recalcBounds(glyf)
        metrics[name] = (CELL_WIDTH_PX * PIXEL, getattr(glyph, "xMin", 0))
    fb.setupHorizontalMetrics(metrics)

    fb.setupHorizontalHeader(ascent=ASCENT_PX * PIXEL, descent=-DESCENT_PX * PIXEL, lineGap=0)
    fb.setupNameTable({"familyName": FAMILY_NAME, "styleName": "Regular"})

    panose = Panose()
    panose.bFamilyType = 2
    panose.bProportion = 9  # monospaced
    fb.setupOS2(
        version=4,
        sTypoAscender=ASCENT_PX * PIXEL,
        sTypoDescender=-DESCENT_PX * PIXEL,
        sTypoLineGap=0,
        usWinAscent=ASCENT_PX * PIXEL,
        usWinDescent=DESCENT_PX * PIXEL,
        xAvgCharWidth=CELL_WIDTH_PX * PIXEL,
        sxHeight=8 * PIXEL,
        sCapHeight=ASCENT_PX * PIXEL,
        fsSelection=0x40 | 0x80,  # REGULAR | USE_TYPO_METRICS
        panose=panose,
    )
    fb.setupPost(isFixedPitch=1)
    fb.save(OUTPUT)
    print("wrote", OUTPUT)


if __name__ == "__main__":
    main()
