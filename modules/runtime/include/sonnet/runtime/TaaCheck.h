#pragma once

#include <sonnet/core/Math.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sonnet::runtime::taa {

// The comparison behind the TAA checks of ADR-0033 ("TAA"), on 8-bit display-encoded images: the
// harness in apps/taa_harness renders the frames and the references, and this decides whether a
// frame is within the budgets (docs/rendering.md, "TAA checks").

// Budgets, in levels out of 255 (ADR-0033).
constexpr double MaxMeanError = 2.0;
constexpr std::uint32_t MaxPercentileError = 8;
constexpr std::uint32_t MaxRecoveredError = 8;
// Frames of glass coverage before the measured one that stay in the region, and the frames a
// vacated pixel has to return to the reference in.
constexpr std::uint32_t HistoryFrames = 4;
// The region grows by this many pixels, for the reconstruction filters.
constexpr int DilationPixels = 2;

// One byte per pixel, row-major, non-zero inside.
using Mask = std::vector<std::uint8_t>;

// The glass of one frame: where it covers at the frame's pose, and everything it covered on the
// way from the previous frame's pose to this one (the poses in between included).
struct GlassFrame {
  Mask coverage;
  Mask swept;
};

// Per pixel, the largest absolute difference of the red, green and blue channels of two tightly
// packed RGBA images of `size`.
[[nodiscard]] std::vector<std::uint8_t> errorLevels(glm::uvec2 size, std::span<const std::byte> image,
                                                    std::span<const std::byte> reference);

// The mask grown by `radius` pixels in every direction (a square neighbourhood).
[[nodiscard]] Mask dilate(glm::uvec2 size, const Mask &mask, int radius);

struct ErrorStatistics {
  std::size_t pixels{0};
  double mean{0.0};
  std::uint32_t percentile99{0}; // the smallest level that at least 99 % of the pixels are within
  std::uint32_t maximum{0};
};

// The error over the pixels inside the mask, or outside it.
[[nodiscard]] ErrorStatistics statistics(std::span<const std::uint8_t> errors, const Mask &mask, bool inside = true);

struct FrameResult {
  ErrorStatistics glass;      // the evaluation region: current and recent coverage, swept areas, dilated
  ErrorStatistics opaque;     // everything else, a diagnostic
  std::size_t vacated{0};     // pixels the glass left HistoryFrames frames ago and no longer covers
  std::size_t unrecovered{0}; // of those, the ones still more than MaxRecoveredError off
  [[nodiscard]] bool pass() const noexcept {
    return glass.pixels > 0 && glass.mean <= MaxMeanError && glass.percentile99 <= MaxPercentileError &&
           unrecovered == 0;
  }
};

// Judges the frames from `firstMeasured` on. `glass` and `errors` hold one entry per frame from the
// first with a coverage mask (the HistoryFrames before the first measured one are context); the
// errors of the frames before `firstMeasured` are not read.
[[nodiscard]] std::vector<FrameResult> evaluate(glm::uvec2 size, std::span<const GlassFrame> glass,
                                                std::span<const std::vector<std::uint8_t>> errors,
                                                std::size_t firstMeasured);

} // namespace sonnet::runtime::taa
