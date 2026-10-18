#include <sonnet/editor/Fonts.h>
#include <sonnet/editor/Icons.h>

namespace sonnet::editor {

void addFonts(ImGuiIO &io) {
  // A glyph offset needs a reference size to scale from, so both fonts name theirs.
  constexpr float textSize = 14.0f;
  // Lucide's glyphs sit above Inter's baseline: this centres them on the text (measured on a button at textSize).
  constexpr float iconOffsetY = 2.0f;

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
