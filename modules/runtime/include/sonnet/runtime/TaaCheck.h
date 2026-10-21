#pragma once

#include <sonnet/core/Math.h>
#include <sonnet/renderer/SceneView.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sonnet::runtime::taa {

// The comparison behind the TAA checks of ADR-0034 (which supersedes the gate of ADR-0033's "TAA"), on
// 8-bit display-encoded images: the harness in apps/taa_harness renders the frames and the references,
// and this decides whether a frame is within the budgets (docs/rendering.md, "TAA checks").

// Budgets, in levels out of 255 (ADR-0034). The gate uses the integer-matched reference residual as its
// policy input; it is not a lower bound on reconstruction error (see ReferenceMotionResidual below).
constexpr double RestMeanError = 4.0;
constexpr std::uint32_t RestPercentileError = 40;
constexpr double MeanPerFloor = 2.5;
constexpr double PercentilePerFloor = 1.5;
// The pixels the glass left HistoryFrames frames ago, when there are at least MinVacatedPixels of them, may
// be this many levels worse on average than one unresolved jittered sample of the same frame, which is what
// the content alone leaves on them; a history of another layer that has not been dropped shows as more.
constexpr std::size_t MinVacatedPixels = 100;
constexpr double RecoveredMeanMargin = 3.0;
// Frames of glass coverage before the measured one that stay in the region, and the frames after which a
// pixel the glass left is judged.
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

// A harness may update the simulation several times between renders. Replace the last update's
// previousTransform with the previous rendered pose, matched by nonzero id, mesh and submesh.
// Unmatched/new draws use their current pose. `previous` must be a separate snapshot.
void usePreviousFrameTransforms(std::span<renderer::DrawItem> current, std::span<const renderer::DrawItem> previous);

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

// Reference-to-reference matching residual, not a lower bound on temporal reconstruction error.
// `unshifted` compares the current pixel with the same previous pixel. `integerMatched` reads the
// previous pixel at a whole-pixel displacement within `radius`, chosen by the 3x3 patch's RGB L1
// difference. Fractional motion and motion outside the search window are not tested; even a pure
// half-pixel translation can leave a nonzero residual. Pixels within radius + 2 of the border are
// excluded. This diagnostic does not change the acceptance budgets.
struct ReferenceMotionResidual {
  ErrorStatistics unshifted;
  ErrorStatistics integerMatched;
};

[[nodiscard]] ReferenceMotionResidual referenceMotionResidual(glm::uvec2 size, std::span<const std::byte> image,
                                                              std::span<const std::byte> previous, const Mask &mask,
                                                              int radius);

struct FrameResult {
  ErrorStatistics glass;  // the evaluation region: current and recent coverage, swept areas, dilated
  ErrorStatistics opaque; // everything else, a diagnostic
  // The region split by the frame's own coverage: what the glass covers now, so its details and what is
  // seen through it, which the budgets judge, and the rest of the region, which the glass left or will
  // cross, where the history of another layer shows (a diagnostic).
  ErrorStatistics covered;
  ErrorStatistics uncovered;
  // The pixels the glass left HistoryFrames frames ago and nothing covers now, where the history is another
  // layer's if the resolve does not drop it: the error of the frame, and of one unresolved jittered sample.
  ErrorStatistics vacated;
  ErrorStatistics vacatedSample;
  // The bounded reference residual used as `f` by ADR-0034. The caller sets it from consecutive
  // references because `evaluate` receives per-pixel errors only.
  ReferenceMotionResidual referenceMotion;
  [[nodiscard]] bool pass() const noexcept {
    return covered.pixels > 0 && covered.mean <= RestMeanError + MeanPerFloor * referenceMotion.integerMatched.mean &&
           static_cast<double>(covered.percentile99) <=
               static_cast<double>(RestPercentileError) +
                   PercentilePerFloor * referenceMotion.integerMatched.percentile99 &&
           recovered();
  }
  [[nodiscard]] bool recovered() const noexcept {
    return vacated.pixels < MinVacatedPixels || vacated.mean <= vacatedSample.mean + RecoveredMeanMargin;
  }
};

// Judges the frames from `firstMeasured` on. `glass`, `errors` and `sampleErrors` (the error image of one
// unresolved jittered sample of each frame) hold one entry per frame from the first with a coverage mask
// (the HistoryFrames before the first measured one are context); the errors of the frames before
// `firstMeasured` are not read. The caller sets each result's `referenceMotion` before testing its gate.
[[nodiscard]] std::vector<FrameResult> evaluate(glm::uvec2 size, std::span<const GlassFrame> glass,
                                                std::span<const std::vector<std::uint8_t>> errors,
                                                std::span<const std::vector<std::uint8_t>> sampleErrors,
                                                std::size_t firstMeasured);

} // namespace sonnet::runtime::taa
