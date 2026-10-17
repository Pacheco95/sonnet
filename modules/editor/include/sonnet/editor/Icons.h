#pragma once

#include <imgui.h>

#include <string_view>

namespace sonnet::editor {

// The icon font (docs/editor.md, "Icons"): sixteen glyphs of Lucide, cut out by tools/generate_lucide_font.py and
// merged into the default font by addIconFont, so a label can start with one: `ImGui::Text("%s Folder", IconFolder)`.
// Each is the glyph's UTF-8 encoding, in the private use area.
extern const unsigned char LucideFontData[];
extern const unsigned int LucideFontSize;

inline constexpr const char *IconFolder = "\xee\x83\x97";  // U+E0D7
inline constexpr const char *IconFile = "\xee\x83\x80";    // U+E0C0
inline constexpr const char *IconImage = "\xee\x83\xb6";   // U+E0F6
inline constexpr const char *IconGlobe = "\xee\x83\xa8";   // U+E0E8
inline constexpr const char *IconPalette = "\xee\x87\x9d"; // U+E1DD
inline constexpr const char *IconBox = "\xee\x81\xa1";     // U+E061
inline constexpr const char *IconPackage = "\xee\x84\xa9"; // U+E129
inline constexpr const char *IconBlocks = "\xee\x93\xba";  // U+E4FA
inline constexpr const char *IconScript = "\xee\x83\x83";  // U+E0C3
inline constexpr const char *IconSound = "\xee\x86\xab";   // U+E1AB
inline constexpr const char *IconFont = "\xee\x86\x98";    // U+E198
inline constexpr const char *IconPlay = "\xee\x84\xbc";    // U+E13C
inline constexpr const char *IconBone = "\xee\x8d\x98";    // U+E358
inline constexpr const char *IconFilm = "\xee\x83\x90";    // U+E0D0
inline constexpr const char *IconGrid = "\xee\x83\xbf";    // U+E0FF
inline constexpr const char *IconList = "\xee\x84\x86";    // U+E106

// Every icon above, for the test that holds them against the embedded font.
inline constexpr const char *AllIcons[] = {IconFolder,  IconFile,   IconImage,  IconGlobe, IconPalette, IconBox,
                                           IconPackage, IconBlocks, IconScript, IconSound, IconFont,    IconPlay,
                                           IconBone,    IconFilm,   IconGrid,   IconList};

// Adds the default font and merges the icons into it. Once, before the first frame.
void addIconFont(ImGuiIO &io);

// The icon of an asset row's type name ("Texture", "Folder", "Scene", "File" and the other types of the browser).
[[nodiscard]] const char *iconForType(std::string_view type) noexcept;

} // namespace sonnet::editor
