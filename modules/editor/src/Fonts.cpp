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

  // The second font of the atlas, after the text font the icons merged into (monoFont).
  ImFontConfig mono;
  mono.FontDataOwnedByAtlas = false;
  io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(JetBrainsMonoFontData),
                                 static_cast<int>(JetBrainsMonoFontSize), TextSize, &mono);
}

ImFont *monoFont() {
  constexpr int MonoIndex = 1;
  const ImVector<ImFont *> &fonts = ImGui::GetIO().Fonts->Fonts;
  return fonts.Size > MonoIndex ? fonts[MonoIndex] : nullptr;
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
