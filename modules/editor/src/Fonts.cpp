#include <sonnet/editor/Fonts.h>
#include <sonnet/editor/Icons.h>

namespace sonnet::editor {

void addFonts(ImGuiIO &io) {
  // A glyph offset needs a reference size to scale from, so both fonts name theirs.
  constexpr float TextSize = 14.0f;
  // Lucide's glyphs sit above Inter's baseline: this centres them on the text (measured on a button at TextSize).
  constexpr float IconOffsetY = 2.0f;

  ImFontConfig text;
  text.FontDataOwnedByAtlas = false;
  io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(InterFontData), static_cast<int>(InterFontSize), TextSize,
                                 &text);

  ImFontConfig icons;
  icons.MergeMode = true;
  icons.FontDataOwnedByAtlas = false;
  icons.PixelSnapH = true;
  icons.GlyphOffset.y = IconOffsetY;
  io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(LucideFontData), static_cast<int>(LucideFontSize),
                                 TextSize, &icons);
}

} // namespace sonnet::editor
