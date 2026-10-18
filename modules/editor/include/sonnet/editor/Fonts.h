#pragma once

#include <imgui.h>

namespace sonnet::editor {

// The editor's fonts (docs/editor.md, "Fonts"): Inter for text, with the Lucide icons of Icons.h merged into it, and
// JetBrains Mono for what is read character by character. Both are cut out by tools/generate_ui_fonts.py and embedded,
// so every platform draws the same glyphs.
extern const unsigned char InterFontData[];
extern const unsigned int InterFontSize;
extern const unsigned char JetBrainsMonoFontData[];
extern const unsigned int JetBrainsMonoFontSize;

// Adds the fonts to the atlas. Once, before the first frame.
void addFonts(ImGuiIO &io);

// The monospace font of the current context's atlas, or null when addFonts has not run on it.
[[nodiscard]] ImFont *monoFont();

// Draws what is in its scope in the monospace font, at the size around it: UUIDs, paths and log lines. Without the
// fonts added it changes nothing.
class ScopedMono {
public:
  ScopedMono();
  ~ScopedMono();
  ScopedMono(const ScopedMono &) = delete;
  ScopedMono &operator=(const ScopedMono &) = delete;

private:
  bool m_pushed;
};

} // namespace sonnet::editor
