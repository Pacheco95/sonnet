#include <sonnet/runtime/Capture.h>
#include <sonnet/runtime/FrameTimes.h>

#include <sonnet/core/Error.h>
#include <sonnet/renderer/RenderGraph.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet;

namespace {

core::Result<runtime::CommandLine> parse(runtime::CaptureApplication application,
                                         std::initializer_list<std::string_view> args) {
  const std::vector<std::string_view> list{args};
  return runtime::parseCommandLine(list, application);
}

core::Result<runtime::CommandLine> player(std::initializer_list<std::string_view> args) {
  return parse(runtime::CaptureApplication::Player, args);
}

} // namespace

TEST_CASE("the player takes the capture flags it has a use for", "[runtime][capture]") {
  SECTION("a game alone, or nothing, is a plain run") {
    const auto line = player({"game.sbundle"});
    REQUIRE(line.has_value());
    REQUIRE(line->content == "game.sbundle");
    REQUIRE(!line->capture.has_value());
    REQUIRE(player({}).has_value());
  }
  SECTION("the five flags, with the game anywhere among them") {
    const auto line = player({"--scene", "scenes/playground.scene.json", "--play", "2.5", "samples/basic",
                              "--shading-term", "albedo", "--settle-frames", "4", "--screenshot", "out/view.png"});
    REQUIRE(line.has_value());
    REQUIRE(line->content == "samples/basic");
    REQUIRE(line->capture.has_value());
    const runtime::CaptureOptions &capture = *line->capture;
    REQUIRE(capture.scene == "scenes/playground.scene.json");
    REQUIRE(capture.playSeconds == 2.5f);
    REQUIRE(capture.shadingTerm == renderer::DebugView::Albedo);
    REQUIRE(capture.settleFrames == 4);
    REQUIRE(capture.viewport == "out/view.png");
    REQUIRE(capture.window.empty());
  }
  SECTION("without a game, a capture runs the one the player ships with") {
    const auto line = player({"--play", "3", "--screenshot", "final.png"});
    REQUIRE(line.has_value());
    REQUIRE(line->content.empty());
    REQUIRE(line->capture.has_value());
  }
  SECTION("--scene alone is a plain run of that scene, for the player only") {
    const auto line = player({"game.sbundle", "--scene", "scenes/playground.scene.json"});
    REQUIRE(line.has_value());
    REQUIRE(!line->capture.has_value());
    REQUIRE(line->scene == "scenes/playground.scene.json");
    // With another flag it is a capture again, which needs its screenshot.
    REQUIRE(!player({"--scene", "scenes/playground.scene.json", "--play", "1"}).has_value());
    REQUIRE(!parse(runtime::CaptureApplication::Editor, {"p", "--scene", "scenes/playground.scene.json"}).has_value());
  }
  SECTION("the editor's own flags are unknown to it, and say where the list is") {
    for (const std::string_view flag : {"--screenshot-window", "--select"}) {
      const auto line = player({flag, "x", "--screenshot", "a.png"});
      REQUIRE(!line.has_value());
      REQUIRE(line.error().message.contains("--help"));
    }
  }
  SECTION("mistakes are errors, not a silently different run") {
    REQUIRE(!player({"--screenshot"}).has_value());                    // no value
    REQUIRE(!player({"--play", "1"}).has_value());                     // nothing to capture
    REQUIRE(!player({"a", "b", "--screenshot", "a.png"}).has_value()); // two games
    REQUIRE(!player({"--shading-term", "sparkle", "--screenshot", "a.png"}).has_value());
  }
}

TEST_CASE("the editor and the player share one table", "[runtime][capture]") {
  const std::string editor = runtime::commandLineUsage(runtime::CaptureApplication::Editor);
  const std::string usage = runtime::commandLineUsage(runtime::CaptureApplication::Player);
  REQUIRE(editor.starts_with("usage: sonnet_editor"));
  REQUIRE(usage.starts_with("usage: sonnet_player"));
  REQUIRE(usage.contains("docs/player.md"));
  int players = 0;
  for (const runtime::CommandLineOption &option : runtime::commandLineOptions()) {
    CAPTURE(option.flag);
    REQUIRE(editor.contains(option.flag));
    REQUIRE(usage.contains(option.flag) == option.player);
    // Every flag the player's usage offers is one its parser takes, with the value it shows.
    const auto line = player({option.flag, option.example, "--screenshot", "a.png"});
    REQUIRE(line.has_value() == option.player);
    players += option.player ? 1 : 0;
  }
  REQUIRE(players == 5);
  // The player's window is its scene, which its line for --screenshot says.
  REQUIRE(usage.contains("the scene, at the window's size"));
  REQUIRE(!usage.contains("viewport"));
}

TEST_CASE("--play takes what floating-point from_chars took", "[runtime][capture]") {
  using runtime::parseSeconds;
  SECTION("decimal seconds, with a point, an exponent or both") {
    REQUIRE(parseSeconds("0") == 0.0f);
    REQUIRE(parseSeconds("3") == 3.0f);
    REQUIRE(parseSeconds("2.5") == 2.5f);
    REQUIRE(parseSeconds(".5") == 0.5f);
    REQUIRE(parseSeconds("5.") == 5.0f);
    REQUIRE(parseSeconds("00.5") == 0.5f);
    REQUIRE(parseSeconds("0.1") == 0.1f);
    REQUIRE(parseSeconds("0.25") == 0.25f);
    REQUIRE(parseSeconds("10") == 10.0f);
    REQUIRE(parseSeconds("1e3") == 1000.0f);
    REQUIRE(parseSeconds("1E3") == 1000.0f);
    REQUIRE(parseSeconds("1e+3") == 1000.0f);
    REQUIRE(parseSeconds("1.e3") == 1000.0f);
    REQUIRE(parseSeconds("25e-1") == 2.5f);
    REQUIRE(parseSeconds("1e-3") == 0.001f);
    REQUIRE(parseSeconds("0e999999") == 0.0f);
    // The largest float, and a value that rounds to it rather than past it.
    REQUIRE(parseSeconds("3.4028235e38") == std::numeric_limits<float>::max());
    REQUIRE(parseSeconds("123456789012345678901234") == 123456789012345678901234.0f);
  }
  SECTION("a minus zero is zero, as from_chars read it and --play kept it") {
    for (const std::string_view zero : {"-0", "-0.0", "-.0"}) {
      CAPTURE(zero);
      REQUIRE(parseSeconds(zero) == 0.0f);
    }
  }
  SECTION("anything else is refused") {
    for (const std::string_view bad : {"",
                                       "soon",
                                       ".",
                                       "-",
                                       "e3",
                                       ".e3",
                                       "+1",
                                       " 1",
                                       "1 ",
                                       "1e",
                                       "1e+",
                                       "1e-",
                                       "1ex",
                                       "0x1",
                                       "1,5",
                                       "1.2.3",
                                       "2.5f",
                                       "1_000",
                                       "-1",
                                       "-0.5",
                                       "-1e-50",
                                       "inf",
                                       "-inf",
                                       "nan",
                                       "infinity",
                                       "1e39",
                                       "3.4028236e38",
                                       "1e-50",
                                       "1e-99999999999999999999"}) {
      CAPTURE(bad);
      REQUIRE(!parseSeconds(bad).has_value());
    }
  }
  SECTION("the parser says which value it refused") {
    const auto line = player({"--play", "-1", "--screenshot", "a.png"});
    REQUIRE(!line.has_value());
    REQUIRE(line.error().message == "--play takes seconds, not \"-1\"");
  }
}

TEST_CASE("a relative screenshot lands under the preferences directory", "[runtime][capture]") {
  const std::filesystem::path pref = std::filesystem::temp_directory_path() / "sonnet" / "player";
  runtime::CaptureOptions options;
  options.viewport = std::filesystem::path{"shots"} / "final.png";
  runtime::resolveOutputs(options, pref);
  REQUIRE(options.viewport == pref / "shots" / "final.png");
  REQUIRE(options.window.empty()); // none asked for, none made up

  const std::filesystem::path absolute = std::filesystem::temp_directory_path() / "final.png";
  options.viewport = absolute;
  runtime::resolveOutputs(options, pref);
  REQUIRE(options.viewport == absolute);
}

TEST_CASE("frame times are the means of the last hundred frames", "[runtime][capture]") {
  runtime::FrameTimes times;
  REQUIRE(times.summary() == "no frames recorded");
  const auto graph = [](float shadow, float forward) {
    renderer::GraphStatistics statistics;
    statistics.passes = {{.name = "shadow", .cpuMilliseconds = 0.0f, .gpuMilliseconds = shadow},
                         {.name = "forward", .cpuMilliseconds = 0.0f, .gpuMilliseconds = forward}};
    return statistics;
  };
  // Fifty slow frames that the next hundred push out, then a hundred of known times.
  for (int i = 0; i < 50; ++i) {
    times.record(100.0f, 100.0f, graph(100.0f, 100.0f));
  }
  for (int i = 0; i < 100; ++i) {
    times.record(i % 2 == 0 ? 1.0f : 3.0f, 16.0f, graph(0.5f, i % 2 == 0 ? 1.0f : 2.0f));
  }
  REQUIRE(times.count() == runtime::FrameTimes::Capacity);
  REQUIRE(
      times.summary() ==
      "over the last 100 frames: CPU 2.00 ms a frame, 16.00 ms apart; GPU 2.000 ms: shadow 0.500 ms, forward 1.500 ms");
}
