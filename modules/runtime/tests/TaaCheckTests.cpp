#include <sonnet/runtime/TaaCheck.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

using namespace sonnet;
using namespace sonnet::runtime::taa;

namespace {

constexpr glm::uvec2 Size{16, 8};
constexpr std::size_t PixelCount = std::size_t{16} * 8;

std::vector<std::byte> grey(unsigned level) {
  return std::vector<std::byte>(PixelCount * 4, static_cast<std::byte>(level));
}

// A mask with the columns [first, last] set in every row.
Mask columns(unsigned first, unsigned last) {
  Mask mask(PixelCount, 0);
  for (unsigned y = 0; y < Size.y; ++y) {
    for (unsigned x = first; x <= last; ++x) {
      mask[y * Size.x + x] = 1;
    }
  }
  return mask;
}

} // namespace

TEST_CASE("the error is the largest channel difference", "[taa]") {
  std::vector<std::byte> image = grey(100);
  image[0] = std::byte{110};
  image[1] = std::byte{60}; // 40 off, the largest
  image[2] = std::byte{101};
  image[3] = std::byte{0}; // alpha is not compared
  const std::vector<std::uint8_t> errors = errorLevels(Size, image, grey(100));
  REQUIRE(errors.size() == PixelCount);
  CHECK(errors[0] == 40);
  CHECK(errors[1] == 0);
}

TEST_CASE("dilation grows a mask by a square neighbourhood and stops at the border", "[taa]") {
  Mask mask(PixelCount, 0);
  mask[3 * Size.x + 8] = 1;
  const Mask grown = dilate(Size, mask, 2);
  std::size_t count = 0;
  for (unsigned y = 0; y < Size.y; ++y) {
    for (unsigned x = 0; x < Size.x; ++x) {
      const bool expected = x >= 6 && x <= 10 && y >= 1 && y <= 5;
      CHECK((grown[y * Size.x + x] != 0) == expected);
      count += grown[y * Size.x + x] != 0 ? 1U : 0U;
    }
  }
  CHECK(count == 25U);
  Mask corner(PixelCount, 0);
  corner[0] = 1;
  const Mask clipped = dilate(Size, corner, 2);
  CHECK(std::ranges::count(clipped, std::uint8_t{1}) == 9);
}

TEST_CASE("statistics split the image by the mask and find the 99th percentile", "[taa]") {
  // 100 pixels inside with errors 0..99, the rest outside with error 200.
  std::vector<std::uint8_t> errors(PixelCount, 200);
  Mask inside(PixelCount, 0);
  for (std::size_t i = 0; i < 100; ++i) {
    errors[i] = static_cast<std::uint8_t>(i);
    inside[i] = 1;
  }
  const ErrorStatistics in = statistics(errors, inside);
  CHECK(in.pixels == 100);
  CHECK(in.mean == Catch::Approx(49.5));
  CHECK(in.percentile99 == 98); // 99 of the 100 pixels are within 98
  CHECK(in.maximum == 99);
  const ErrorStatistics out = statistics(errors, inside, false);
  CHECK(out.pixels == PixelCount - 100);
  CHECK(out.mean == Catch::Approx(200.0));
}

TEST_CASE("a pane that leaves a ghost trail fails the recovery check, one that is clean passes", "[taa]") {
  // A one-column pane moving right one column a frame; the sequence is the pane's pose in 10 frames.
  constexpr std::size_t frameCount = 10;
  std::vector<GlassFrame> glass;
  for (unsigned frame = 0; frame < frameCount; ++frame) {
    const unsigned x = 2 + frame;
    glass.push_back({.coverage = columns(x, x), .swept = columns(frame == 0 ? x : x - 1, x)});
  }
  std::vector<std::vector<std::uint8_t>> clean(frameCount, std::vector<std::uint8_t>(PixelCount, 0));
  const std::vector<FrameResult> good = evaluate(Size, glass, clean, 4);
  REQUIRE(good.size() == frameCount - 4);
  for (const FrameResult &result : good) {
    CHECK(result.pass());
  }
  CHECK(good.back().vacated > 0); // columns the pane left more than four frames ago

  // A ghost left on column 2 (the pane's first pose) that is still there in every frame.
  std::vector<std::vector<std::uint8_t>> ghost = clean;
  for (std::size_t frame = 0; frame < frameCount; ++frame) {
    for (unsigned y = 0; y < Size.y; ++y) {
      ghost[frame][y * Size.x + 2] = 20;
    }
  }
  const std::vector<FrameResult> bad = evaluate(Size, glass, ghost, 4);
  CHECK_FALSE(bad[0].pass()); // frame 4: column 2 is in the region, 20 levels off over a column in a small region
  // The pane left column 2 in frame 1; four frames later, in frame 5, it is still wrong: unrecovered.
  CHECK(bad[1].unrecovered > 0);
  CHECK_FALSE(bad[1].pass());
}

TEST_CASE("the mean and percentile budgets are checked on the region only", "[taa]") {
  std::vector<GlassFrame> glass(5, {.coverage = columns(4, 5), .swept = columns(4, 5)});
  std::vector<std::vector<std::uint8_t>> errors(5, std::vector<std::uint8_t>(PixelCount, 0));
  // Wrong far from the pane: a diagnostic, not a failure.
  for (auto &frame : errors) {
    for (unsigned y = 0; y < Size.y; ++y) {
      frame[y * Size.x + 15] = 100;
    }
  }
  const std::vector<FrameResult> results = evaluate(Size, glass, errors, 4);
  REQUIRE(results.size() == 1);
  CHECK(results[0].pass());
  CHECK(results[0].opaque.maximum == 100);
  CHECK(results[0].glass.maximum == 0);
}
