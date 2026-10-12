#!/usr/bin/env python3
"""Writes TestFont.ttf: five glyphs with round metrics, so a test can compute them by hand.

Needs fontTools (pip install fonttools). The output is committed; this script only documents
where its numbers come from. CC0, see README.md.
"""
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import newTable

UNITS = 1000


def polygon(points):
    pen = TTGlyphPen(None)
    pen.moveTo(points[0])
    for point in points[1:]:
        pen.lineTo(point)
    pen.closePath()
    return pen.glyph()


# Clockwise outlines, y up. Each shape runs from x = left side bearing to its advance - bearing.
glyphs = {
    ".notdef": (500, polygon([(50, 0), (50, 700), (450, 700), (450, 0)])),
    "space": (250, TTGlyphPen(None).glyph()),
    "A": (600, polygon([(50, 0), (50, 700), (550, 700), (550, 0)])),
    "V": (600, polygon([(50, 700), (550, 700), (300, 0)])),
    "i": (300, polygon([(100, 0), (100, 700), (200, 700), (200, 0)])),
}

builder = FontBuilder(UNITS, isTTF=True)
builder.setupGlyphOrder(list(glyphs))
builder.setupCharacterMap({0x20: "space", 0x41: "A", 0x56: "V", 0x69: "i"})
builder.setupGlyf({name: glyph for name, (_, glyph) in glyphs.items()})
builder.setupHorizontalMetrics({name: (advance, glyph.xMin if hasattr(glyph, "xMin") else 0)
                                for name, (advance, glyph) in glyphs.items()})
builder.setupHorizontalHeader(ascent=800, descent=-200, lineGap=100)
builder.setupNameTable({"familyName": "Sonnet Test", "styleName": "Regular"})
builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, sTypoLineGap=100, usWinAscent=800, usWinDescent=200)
builder.setupPost()

# The legacy kern table that stb_truetype reads: A before V pulls 100 units in, V before A 50.
kern = newTable("kern")
kern.version = 0

from fontTools.ttLib.tables._k_e_r_n import KernTable_format_0

table = KernTable_format_0()
table.coverage = 1
table.format = 0
table.kernTable = {("A", "V"): -100, ("V", "A"): -50}
kern.kernTables = [table]
builder.font["kern"] = kern

builder.save(sys.argv[1] if len(sys.argv) > 1 else "TestFont.ttf")
