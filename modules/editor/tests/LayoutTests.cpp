#include <sonnet/editor/Layout.h>

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
