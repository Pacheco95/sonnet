#include <sonnet/editor/Layout.h>

#include <sonnet/core/File.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace sonnet;
using namespace sonnet::editor;

namespace {

const std::string SampleIni = "[Window][Log]\nPos=0,0\nSize=300,200\n\n"
                              "[SonnetView][View]\nViewport=1\nDockSize=800,600\n\n"
                              "[Docking][Data]\n"
                              "DockSpace ID=0x1 Window=0x2 Pos=0,19 Size=800,581 Split=X\n"
                              "  DockNode ID=0x3 Parent=0x1 SizeRef=144,581 Selected=0x4\n"
                              "  DockNode ID=0x5 Parent=0x1 SizeRef=656,581 Split=Y\n";

} // namespace

TEST_CASE("layout names are sanitized to file names", "[editor][layout]") {
  CHECK(sanitizeLayoutName("My layout") == "My layout");
  CHECK(sanitizeLayoutName("  wide-1_a  ") == "wide-1_a");
  CHECK(sanitizeLayoutName("a/b\\c:d") == "a_b_c_d");
  CHECK(sanitizeLayoutName("../x") == "___x");
  CHECK(sanitizeLayoutName("").empty());
  CHECK(sanitizeLayoutName("   ").empty());
  CHECK(sanitizeLayoutName("CON") == "CON_");
  CHECK(sanitizeLayoutName(std::string(200, 'a')).size() == 64);
}

TEST_CASE("the built-in layout names are reserved whatever their case", "[editor][layout]") {
  CHECK(builtinLayout("Default") == BuiltinLayout::Default);
  CHECK(builtinLayout("play") == BuiltinLayout::Play);
  CHECK(!builtinLayout("Playtest"));
  CHECK(builtinViewState(BuiltinLayout::Default) != builtinViewState(BuiltinLayout::Play));
  CHECK(!builtinViewState(BuiltinLayout::Default).colliders);
  CHECK(builtinViewState(BuiltinLayout::Play).colliders);
  CHECK(!builtinViewState(BuiltinLayout::Default).game);
}

TEST_CASE("only an ini with the view section and a dock tree is a layout", "[editor][layout]") {
  CHECK(isLayoutIni(SampleIni));
  CHECK(!isLayoutIni(""));
  CHECK(!isLayoutIni("garbage \x01\x02 not an ini"));
  CHECK(!isLayoutIni("[Docking][Data]\nDockSpace ID=0x1 Size=1,1\n")); // no view section
  CHECK(!isLayoutIni("[SonnetView][View]\nViewport=1\n"));             // no dock tree
}

namespace {

// SampleIni with `from` replaced by `to` (once, and it must be there).
std::string changed(std::string_view from, std::string_view to) {
  std::string ini = SampleIni;
  const std::size_t at = ini.find(from);
  REQUIRE(at != std::string::npos);
  return ini.replace(at, from.size(), to);
}

// The whole file, read and closed (Windows will not rename over a file that is open).
std::string contentsOf(const std::filesystem::path &file) {
  const auto bytes = core::readFile(file);
  REQUIRE(bytes.has_value());
  return std::string(reinterpret_cast<const char *>(bytes->data()), bytes->size());
}

} // namespace

TEST_CASE("a sound layout passes the check", "[editor][layout]") {
  CHECK(!layoutIniProblem(SampleIni));
  CHECK(!layoutIniProblem(""));
  // An ini from before the view section or the dock tree still loads.
  CHECK(!layoutIniProblem("[Window][Log]\nPos=0,0\nSize=300,200\n\n"));
  CHECK(!layoutIniProblem("[Docking][Data]\nDockSpace ID=0x1 Window=0x2 Pos=0,19 Size=800,581\n"));
  // A file that went through a Windows editor, and a flag a newer editor adds.
  std::string windows;
  for (const char c : SampleIni) {
    windows += c == '\n' ? "\r\n" : std::string(1, c);
  }
  CHECK(!layoutIniProblem(windows));
  CHECK(!layoutIniProblem(changed("Viewport=1\n", "Viewport=1\nFuture=7\n")));
  // Windows docked in nodes the file describes.
  CHECK(!layoutIniProblem(changed("Pos=0,0\nSize=300,200\n", "Pos=0,0\nSize=300,200\nDockId=0x00000003,0\n")));
  CHECK(!layoutIniProblem(changed("Selected=0x4\n", "NoTabBar=1 Selected=0x4 ; in 'x'\n")));
}

TEST_CASE("a layout that would trouble Dear ImGui fails the check with the line", "[editor][layout]") {
  const auto problem = [](const std::string &ini) { return layoutIniProblem(ini).value_or(""); };
  // A dock node of zero size.
  CHECK(problem(changed("Size=800,581", "Size=0,0")).starts_with("line 10:"));
  CHECK(problem(changed("Size=800,581", "Size=800,0")).find("no size") != std::string::npos);
  CHECK(problem(changed("SizeRef=144,581", "SizeRef=0,581")).starts_with("line 11:"));
  CHECK(!problem(changed("Size=800,581", "Size=-5,581")).empty());
  CHECK(!problem(changed("Size=800,581", "Size=70000,581")).empty()); // beyond the 16 bits ImGui keeps
  // A parent that is missing, comes later, is the node itself or has no room for the node.
  CHECK(problem(changed("Parent=0x1 SizeRef=144", "Parent=0x9 SizeRef=144")).find("parent") != std::string::npos);
  CHECK(!problem(changed("ID=0x3 Parent=0x1", "ID=0x3 Parent=0x3")).empty());
  CHECK(!problem(changed("ID=0x1 Window", "ID=0x7 Window")).empty());
  CHECK(!problem(SampleIni + "  DockNode ID=0x6 Parent=0x1 SizeRef=1,1\n").empty()); // a third child
  CHECK(!problem(changed("Split=Y", "") + "  DockNode ID=0x6 Parent=0x5 SizeRef=1,1\n").empty());
  CHECK(!problem(changed("ID=0x3", "ID=0x1")).empty()); // an id twice
  CHECK(!problem(changed("ID=0x3", "ID=0x0")).empty());
  // A window docked in a node the file lacks.
  CHECK(!problem(changed("Pos=0,0\nSize=300,200\n", "Pos=0,0\nSize=300,200\nDockId=0x00000042,0\n")).empty());
  // Lines Dear ImGui would skip or read as something else.
  CHECK(!problem(changed("ID=0x3 Parent=0x1 SizeRef=144,581", "Parent=0x1 ID=0x3 SizeRef=144,581")).empty());
  CHECK(!problem(changed("SizeRef=144,581", "Pos=0,0 Size=144,581")).empty());
  CHECK(!problem(changed("Window=0x2 Pos=0,19 Size=800,581", "Window=0x2 SizeRef=800,581")).empty());
  CHECK(!problem(changed("Split=X", "Split=Z")).empty());
  CHECK(!problem(changed("Selected=0x4", "Selected=zz")).empty());
  CHECK(!problem(changed("Selected=0x4", "Selected=0x4 Mystery=1")).empty());
  CHECK(!problem(changed("DockNode ID=0x3", "Dock ID=0x3")).empty());
  CHECK(!problem(changed("ID=0x1", "ID=0x123456789")).empty());
  // A view section that is not one.
  CHECK(!problem(changed("Viewport=1", "Viewport=2")).empty());
  CHECK(!problem(changed("Viewport=1", "Viewport")).empty());
  CHECK(!problem(changed("DockSize=800,600", "DockSize=800")).empty());
  CHECK(!problem(changed("DockSize=800,600", "DockSize=800,600,5")).empty());
}

TEST_CASE("a layout cut anywhere fails the check unless it was cut at a line", "[editor][layout]") {
  // Every prefix that ends inside a line is refused: a cut file never loads.
  for (std::size_t length = 1; length < SampleIni.size(); ++length) {
    const std::string cut = SampleIni.substr(0, length);
    if (SampleIni[length - 1] != '\n') {
      INFO("cut after " << length << ": " << cut);
      CHECK(layoutIniProblem(cut).has_value());
    }
  }
}

TEST_CASE("setting aside a bad layout keeps the file under another name", "[editor][layout]") {
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "aside";
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  const std::filesystem::path file = directory / "layout.ini";
  CHECK(setAsideBadLayout(file)); // absent
  std::ofstream{file} << SampleIni;
  CHECK(setAsideBadLayout(file));
  CHECK(std::filesystem::exists(file));
  const std::string bad = changed("Size=800,581", "Size=0,0");
  std::ofstream{file, std::ios::trunc} << bad;
  CHECK(setAsideBadLayout(file));
  CHECK(!std::filesystem::exists(file));
  CHECK(contentsOf(directory / "layout.ini.rejected") == bad);
  // A second bad file replaces the first one.
  std::ofstream{file} << "[Docking][Data]\nDockSpace ID=0x1 trunc";
  CHECK(setAsideBadLayout(file));
  CHECK(contentsOf(directory / "layout.ini.rejected").starts_with("[Docking]"));
}

TEST_CASE("scaling a layout multiplies the dock sizes and nothing else", "[editor][layout]") {
  const std::string doubled = scaleLayoutIni(SampleIni, 1600.0f, 1200.0f);
  CHECK(doubled.find("Size=1600,1162") != std::string::npos);
  CHECK(doubled.find("SizeRef=288,1162") != std::string::npos);
  CHECK(doubled.find("SizeRef=1312,1162") != std::string::npos);
  CHECK(doubled.find("Selected=0x4") != std::string::npos);
  // A floating window's own size is not a dock size.
  CHECK(doubled.find("Pos=0,0\nSize=300,200") != std::string::npos);
  CHECK(scaleLayoutIni(SampleIni, 800.0f, 600.0f) == SampleIni);
  const std::string unrecorded = "[Docking][Data]\nDockSpace ID=0x1 Size=800,581\n";
  CHECK(scaleLayoutIni(unrecorded, 1600.0f, 1200.0f) == unrecorded);
}

TEST_CASE("saved layouts are the ini files of a directory", "[editor][layout]") {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "layout_list";
  std::filesystem::remove_all(directory);
  CHECK(listLayouts(directory).empty());
  std::filesystem::create_directories(directory);
  std::ofstream{directory / "zeta.ini"} << "x";
  std::ofstream{directory / "Alpha.ini"} << "x";
  std::ofstream{directory / "notes.txt"} << "x";
  CHECK(listLayouts(directory) == std::vector<std::string>{"Alpha", "zeta"});
}
