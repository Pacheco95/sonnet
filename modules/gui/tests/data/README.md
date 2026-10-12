# Test data

Fonts the `gui` tests read from the source tree, found by walking up from the test binary.

| File | What it is | Source | Licence |
|---|---|---|---|
| `TestFont.ttf` | Five glyphs (`.notdef`, space, `A`, `V`, `i`) as plain polygons on a 1000-unit em, with ascent 800, descent -200, line gap 100, and a legacy `kern` table (`A` then `V` is -100, `V` then `A` is -50) | Made for this repository by `make_test_font.py` | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) |

The numbers are round on purpose, so `GuiTextTests.cpp` computes every metric by hand: advances 600 (`A`, `V`), 300 (`i`), 250 (space) and 500 (`.notdef`), and a left side bearing of 50 (100 for `i`). The font is committed; running the script writes the same glyphs with new timestamps in `head`.
