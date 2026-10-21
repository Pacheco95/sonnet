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

// The error image of an unresolved sample that is exact everywhere, for frames that need none.
std::vector<std::vector<std::uint8_t>> exactSamples(std::size_t frames) {
  return std::vector<std::vector<std::uint8_t>>(frames, std::vector<std::uint8_t>(PixelCount, 0));
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
  // The recovery check judges 100 pixels or more, which the small image of the other cases is too small for.
  constexpr glm::uvec2 wide{64, 32};
  constexpr std::size_t wideCount = std::size_t{64} * 32;
  constexpr std::size_t frameCount = 10;
  const auto strip = [&](unsigned first, unsigned last) {
    Mask mask(wideCount, 0);
    for (unsigned y = 0; y < wide.y; ++y) {
      for (unsigned x = first; x <= last; ++x) {
        mask[y * wide.x + x] = 1;
      }
    }
    return mask;
  };
  // A pane four columns wide moving right four columns a frame; the sequence is its pose in 10 frames.
  std::vector<GlassFrame> glass;
  for (unsigned frame = 0; frame < frameCount; ++frame) {
    const unsigned x = 2 + 4 * frame;
    glass.push_back({.coverage = strip(x, x + 3), .swept = strip(frame == 0 ? x : x - 4, x + 3)});
  }
  const auto frames = [&](std::uint8_t level, unsigned column) {
    std::vector<std::vector<std::uint8_t>> errors(frameCount, std::vector<std::uint8_t>(wideCount, 0));
    for (auto &frame : errors) {
      for (unsigned y = 0; y < wide.y; ++y) {
        frame[y * wide.x + column] = level;
      }
    }
    return errors;
  };
  const std::vector<std::vector<std::uint8_t>> none(frameCount, std::vector<std::uint8_t>(wideCount, 0));
  const std::vector<FrameResult> good = evaluate(wide, glass, none, none, 4);
  REQUIRE(good.size() == frameCount - 4);
  for (const FrameResult &result : good) {
    CHECK(result.pass());
  }
  CHECK(good.back().vacated.pixels >= MinVacatedPixels); // columns the pane left more than four frames ago

  // A ghost left on column 2 (the pane's first pose), 20 levels off in every frame.
  const std::vector<std::vector<std::uint8_t>> ghost = frames(20, 2);
  const std::vector<FrameResult> bad = evaluate(wide, glass, ghost, none, 4);
  CHECK(bad[0].uncovered.maximum == 20); // frame 4: column 2 is in the region and no longer covered
  // The pane left column 2 in frame 1; in frame 5 it is still wrong, over a quarter of the 128 pixels it left.
  CHECK(bad[1].vacated.pixels >= MinVacatedPixels);
  CHECK(bad[1].vacated.mean > RecoveredMeanMargin);
  CHECK_FALSE(bad[1].recovered());
  CHECK_FALSE(bad[1].pass());

  // The same errors where an unresolved sample is as far off (the content, not the history, is the cause).
  const std::vector<FrameResult> detailed = evaluate(wide, glass, ghost, frames(20, 2), 4);
  CHECK(detailed[1].recovered());
  CHECK(detailed[1].pass());
}

TEST_CASE("fewer than MinVacatedPixels vacated pixels are too few to judge", "[taa]") {
  std::vector<GlassFrame> glass;
  for (unsigned frame = 0; frame < 10; ++frame) {
    const unsigned x = 2 + frame;
    glass.push_back({.coverage = columns(x, x), .swept = columns(frame == 0 ? x : x - 1, x)});
  }
  std::vector<std::vector<std::uint8_t>> ghost(10, std::vector<std::uint8_t>(PixelCount, 0));
  for (auto &frame : ghost) {
    for (unsigned y = 0; y < Size.y; ++y) {
      frame[y * Size.x + 2] = 60;
    }
  }
  const std::vector<FrameResult> results = evaluate(Size, glass, ghost, exactSamples(10), 4);
  REQUIRE(results[1].vacated.pixels > 0);
  CHECK(results[1].vacated.pixels < MinVacatedPixels);
  CHECK(results[1].recovered());
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
  const std::vector<FrameResult> results = evaluate(Size, glass, errors, exactSamples(5), 4);
  REQUIRE(results.size() == 1);
  CHECK(results[0].pass());
  CHECK(results[0].opaque.maximum == 100);
  CHECK(results[0].glass.maximum == 0);
}

TEST_CASE("integer reference matching recognizes an in-range translation and retains a content residual", "[taa]") {
  constexpr glm::uvec2 big{32, 16};
  constexpr std::size_t count = std::size_t{32} * 16;
  // A ramp that does not repeat within the search window, then the same ramp moved two pixels right.
  const auto ramp = [&](int shift, int tint) {
    std::vector<std::byte> pixels(count * 4, std::byte{255});
    for (unsigned y = 0; y < big.y; ++y) {
      for (unsigned x = 0; x < big.x; ++x) {
        const auto level =
            static_cast<unsigned>((static_cast<int>(x) - shift) * 7 % 150 + static_cast<int>(y) * 2 + tint);
        for (std::size_t channel = 0; channel < 3; ++channel) {
          pixels[(std::size_t{y} * big.x + x) * 4 + channel] = static_cast<std::byte>(level);
        }
      }
    }
    return pixels;
  };
  const Mask everything(count, 1);
  const ReferenceMotionResidual moved = referenceMotionResidual(big, ramp(2, 0), ramp(0, 0), everything, 4);
  REQUIRE(moved.unshifted.pixels > 0);
  CHECK(moved.unshifted.mean > 5.0);
  CHECK(moved.integerMatched.maximum == 0);
  // The same translation with the content brightened by 50 levels: no displacement within the window
  // explains that, where it explained all of the translation.
  const ReferenceMotionResidual changed = referenceMotionResidual(big, ramp(2, 50), ramp(0, 0), everything, 4);
  CHECK(changed.integerMatched.mean > 5.0);
}

TEST_CASE("the region's error is split by what the glass covers now", "[taa]") {
  // The pane covers columns 4 and 5 in every frame and swept column 3 on the way.
  std::vector<GlassFrame> glass(5, {.coverage = columns(4, 5), .swept = columns(3, 5)});
  std::vector<std::vector<std::uint8_t>> errors(5, std::vector<std::uint8_t>(PixelCount, 0));
  for (auto &frame : errors) {
    for (unsigned y = 0; y < Size.y; ++y) {
      frame[y * Size.x + 3] = 30; // where the pane was, in the region and uncovered
    }
  }
  const std::vector<FrameResult> results = evaluate(Size, glass, errors, exactSamples(5), 4);
  REQUIRE(results.size() == 1);
  CHECK(results[0].covered.pixels == std::size_t{2} * Size.y);
  CHECK(results[0].covered.maximum == 0);
  // Columns 1 to 7 are the region once dilated by two; the covered 4 and 5 are not in the rest.
  CHECK(results[0].uncovered.pixels == std::size_t{5} * Size.y);
  CHECK(results[0].uncovered.maximum == 30);
}

TEST_CASE("capture motion spans all updates since the previous rendered pose", "[taa]") {
  const renderer::MeshHandle mesh{1, 1};
  const std::vector previous{renderer::DrawItem{.mesh = mesh, .id = 7}};
  std::vector current = previous;
  // Four simulation updates of one quarter metre overwrite the world's previous transform.
  for (int substep = 0; substep < 4; ++substep) {
    current[0].previousTransform = current[0].transform;
    current[0].transform[3].x += 0.25f;
  }
  REQUIRE((*current[0].previousTransform)[3].x == Catch::Approx(0.75f));
  usePreviousFrameTransforms(current, previous);
  REQUIRE(current[0].transform[3].x == Catch::Approx(1.0f));
  REQUIRE(current[0].previousTransform == previous[0].transform);
  // The next rendered frame, not the first frame forever, is the next snapshot.
  const auto nextPrevious = current;
  current[0].transform[3].x = 2.0f;
  usePreviousFrameTransforms(current, nextPrevious);
  REQUIRE(current[0].previousTransform == nextPrevious[0].transform);
}

TEST_CASE("capture motion matches draw identity across reorder, replacement and appearance", "[taa]") {
  const renderer::MeshHandle mesh{1, 1};
  std::vector previous{renderer::DrawItem{.mesh = mesh, .submesh = 1, .id = 7},
                       renderer::DrawItem{.mesh = mesh, .submesh = 0, .id = 7}};
  previous[0].transform[3].x = 3.0f;
  previous[1].transform[3].x = 4.0f;
  std::vector current{previous[1], previous[0]};
  for (auto &draw : current) {
    draw.transform[3].x += 1.0f;
  }
  usePreviousFrameTransforms(current, previous);
  REQUIRE(current[0].previousTransform == previous[1].transform);
  REQUIRE(current[1].previousTransform == previous[0].transform);
  for (const renderer::DrawItem &fresh :
       {renderer::DrawItem{.mesh = mesh, .id = 8}, renderer::DrawItem{.mesh = mesh, .submesh = 2, .id = 7},
        renderer::DrawItem{.mesh = {1, 2}, .id = 7}, renderer::DrawItem{.mesh = mesh, .id = 0}}) {
    current = {fresh};
    usePreviousFrameTransforms(current, previous);
    REQUIRE(current[0].previousTransform == current[0].transform);
  }
  usePreviousFrameTransforms(current, {});
  REQUIRE(current[0].previousTransform == current[0].transform);
}

TEST_CASE("integer reference residual is not a floor for fractional motion", "[taa]") {
  constexpr glm::uvec2 size{32, 32};
  constexpr std::size_t count = std::size_t{size.x} * size.y;
  std::vector<std::byte> previous(count * 4, std::byte{255});
  auto current = previous;
  auto reconstructed = previous;
  Mask interior(count, 0);
  for (unsigned y = 0; y < size.y; ++y) {
    for (unsigned x = 0; x < size.x; ++x) {
      const std::size_t pixel = std::size_t{y} * size.x + x;
      for (std::size_t channel = 0; channel < 3; ++channel) {
        previous[pixel * 4 + channel] = static_cast<std::byte>(10 + 6 * x);
        current[pixel * 4 + channel] = static_cast<std::byte>(7 + 6 * x);
      }
      if (x >= 8 && x < 24 && y >= 8 && y < 24) {
        interior[pixel] = 1;
      }
    }
  }
  // A half-pixel shift of this ramp is exactly a bilinear read between the previous samples.
  for (std::size_t pixel = 0; pixel < count; ++pixel) {
    if (interior[pixel] != 0) {
      for (std::size_t channel = 0; channel < 3; ++channel) {
        const int left = std::to_integer<int>(previous[(pixel - 1) * 4 + channel]);
        const int right = std::to_integer<int>(previous[pixel * 4 + channel]);
        reconstructed[pixel * 4 + channel] = static_cast<std::byte>((left + right) / 2);
      }
    }
  }
  const auto residual = referenceMotionResidual(size, current, previous, interior, 6);
  REQUIRE(residual.integerMatched.pixels == std::size_t{16} * 16);
  CHECK(residual.integerMatched.mean == 3.0);
  CHECK(residual.integerMatched.percentile99 == 3);
  CHECK(statistics(errorLevels(size, current, reconstructed), interior).maximum == 0);
}

TEST_CASE("the covered error may exceed the rest budgets by a multiple of the reference residual", "[taa]") {
  FrameResult result;
  result.covered = {.pixels = 100, .mean = RestMeanError + 1.0, .percentile99 = RestPercentileError + 5, .maximum = 90};
  CHECK_FALSE(result.pass()); // content that does not change is held to the rest budgets
  result.referenceMotion.integerMatched = {.pixels = 100, .mean = 0.5, .percentile99 = 4, .maximum = 20};
  CHECK(result.pass()); // 0.5 of residual allows 2.0 more mean, and 4 allows 8 more of the percentile
  result.referenceMotion.integerMatched.mean = 0.2;
  CHECK_FALSE(result.pass()); // 0.8 more is under the 1.0 the mean exceeds by
  result.referenceMotion.integerMatched.mean = 0.5;
  result.vacated = {.pixels = MinVacatedPixels, .mean = 9.0};
  result.vacatedSample = {.pixels = MinVacatedPixels, .mean = 5.0};
  CHECK_FALSE(result.pass()); // vacated pixels 4 levels worse than a sample, over the margin
  result.vacatedSample.mean = 6.5;
  CHECK(result.pass());
  result.covered.pixels = 0; // nothing covered is nothing measured
  CHECK_FALSE(result.pass());
}
