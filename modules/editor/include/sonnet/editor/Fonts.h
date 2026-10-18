#pragma once

#include <imgui.h>

namespace sonnet::editor {

// The editor's fonts (docs/editor.md, "Fonts"): Inter for text, cut out by tools/generate_ui_fonts.py, with the Lucide
// icons of Icons.h merged into it. Embedded, so every platform draws the same glyphs.
extern const unsigned char InterFontData[];
extern const unsigned int InterFontSize;

// Adds the fonts to the atlas. Once, before the first frame.
void addFonts(ImGuiIO &io);

} // namespace sonnet::editor
