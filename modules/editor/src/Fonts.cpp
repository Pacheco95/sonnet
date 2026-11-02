#include <sonnet/editor/Fonts.h>
#include <sonnet/editor/Icons.h>
#include <sonnet/ui/Theme.h>

namespace sonnet::editor {

void addFonts(ImGuiIO &io) {
  // A glyph offset needs a reference size to scale from, so both fonts name theirs.
  const float textSize = ui::currentTheme().textSize;
  // Lucide's glyphs sit above Inter's baseline: this centres them on the text. Dear ImGui scales it by the baked size
  // over textSize and rounds it to whole pixels, and the glyph boxes move in whole-pixel steps that are not linear in
  // the size, so no whole number centres every size (2 leaves the icons a pixel high from scale 1.5). 2.45 centres
  // every quarter step of the display scale from 1 to 2 and is within a pixel from 0.5 to 4 (docs/editor.md, Fonts).
  constexpr float iconOffsetY = 2.45f;

  ImFontConfig text;
  text.FontDataOwnedByAtlas = false;
  io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(InterFontData), static_cast<int>(InterFontSize), textSize,
                                 &text);

  ImFontConfig icons;
  icons.MergeMode = true;
  icons.FontDataOwnedByAtlas = false;
  icons.PixelSnapH = true;
  icons.GlyphOffset.y = iconOffsetY;
  io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(LucideFontData), static_cast<int>(LucideFontSize),
                                 textSize, &icons);

  // The second font of the atlas, after the text font the icons merged into (monoFont).
  ImFontConfig mono;
  mono.FontDataOwnedByAtlas = false;
  io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(JetBrainsMonoFontData),
                                 static_cast<int>(JetBrainsMonoFontSize), textSize, &mono);
}

ImFont *monoFont() {
  constexpr int monoIndex = 1;
  const ImVector<ImFont *> &fonts = ImGui::GetIO().Fonts->Fonts;
  return fonts.Size > monoIndex ? fonts[monoIndex] : nullptr;
}

ScopedMono::ScopedMono() : m_pushed(monoFont() != nullptr) {
  if (m_pushed) {
    ImGui::PushFont(monoFont(), 0.0f); // 0 keeps the size around it
  }
}

ScopedMono::~ScopedMono() {
  if (m_pushed) {
    ImGui::PopFont();
  }
}

} // namespace sonnet::editor
