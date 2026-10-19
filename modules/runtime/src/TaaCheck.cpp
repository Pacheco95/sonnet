#include <sonnet/runtime/TaaCheck.h>

#include <algorithm>
#include <array>

namespace sonnet::runtime::taa {

std::vector<std::uint8_t> errorLevels(glm::uvec2 size, std::span<const std::byte> image,
                                      std::span<const std::byte> reference) {
  const std::size_t count = std::size_t{size.x} * size.y;
  std::vector<std::uint8_t> errors(count, 0);
  if (image.size() < count * 4 || reference.size() < count * 4) {
    return errors;
  }
  for (std::size_t i = 0; i < count; ++i) {
    std::uint8_t largest = 0;
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const int a = std::to_integer<int>(image[i * 4 + channel]);
      const int b = std::to_integer<int>(reference[i * 4 + channel]);
      largest = std::max(largest, static_cast<std::uint8_t>(a > b ? a - b : b - a));
    }
    errors[i] = largest;
  }
  return errors;
}

Mask dilate(glm::uvec2 size, const Mask &mask, int radius) {
  const auto width = static_cast<std::ptrdiff_t>(size.x);
  const auto height = static_cast<std::ptrdiff_t>(size.y);
  const auto radiusPixels = static_cast<std::ptrdiff_t>(radius);
  // A square neighbourhood is two passes: along the rows, then down the columns.
  Mask rows(mask.size(), 0);
  for (std::ptrdiff_t y = 0; y < height; ++y) {
    for (std::ptrdiff_t x = 0; x < width; ++x) {
      if (mask[static_cast<std::size_t>(y * width + x)] == 0) {
        continue;
      }
      for (std::ptrdiff_t dx = std::max<std::ptrdiff_t>(x - radiusPixels, 0);
           dx <= std::min(x + radiusPixels, width - 1); ++dx) {
        rows[static_cast<std::size_t>(y * width + dx)] = 1;
      }
    }
  }
  Mask grown(mask.size(), 0);
  for (std::ptrdiff_t y = 0; y < height; ++y) {
    for (std::ptrdiff_t x = 0; x < width; ++x) {
      if (rows[static_cast<std::size_t>(y * width + x)] == 0) {
        continue;
      }
      for (std::ptrdiff_t dy = std::max<std::ptrdiff_t>(y - radiusPixels, 0);
           dy <= std::min(y + radiusPixels, height - 1); ++dy) {
        grown[static_cast<std::size_t>(dy * width + x)] = 1;
      }
    }
  }
  return grown;
}

ErrorStatistics statistics(std::span<const std::uint8_t> errors, const Mask &mask, bool inside) {
  ErrorStatistics result;
  std::array<std::size_t, 256> histogram{};
  std::size_t sum = 0;
  for (std::size_t i = 0; i < errors.size(); ++i) {
    if ((mask[i] != 0) != inside) {
      continue;
    }
    ++histogram[errors[i]];
    sum += errors[i];
    ++result.pixels;
    result.maximum = std::max<std::uint32_t>(result.maximum, errors[i]);
  }
  if (result.pixels == 0) {
    return result;
  }
  result.mean = static_cast<double>(sum) / static_cast<double>(result.pixels);
  // The smallest level with at least 99 % of the pixels at or below it, in integers.
  const std::size_t needed = (result.pixels * 99 + 99) / 100;
  std::size_t seen = 0;
  for (std::uint32_t level = 0; level < histogram.size(); ++level) {
    seen += histogram[level];
    if (seen >= needed) {
      result.percentile99 = level;
      break;
    }
  }
  return result;
}

std::vector<FrameResult> evaluate(glm::uvec2 size, std::span<const GlassFrame> glass,
                                  std::span<const std::vector<std::uint8_t>> errors, std::size_t firstMeasured) {
  std::vector<FrameResult> results;
  const std::size_t pixels = std::size_t{size.x} * size.y;
  for (std::size_t frame = firstMeasured; frame < glass.size(); ++frame) {
    // Everything the glass covered or swept over in this frame and the HistoryFrames before it.
    Mask region(pixels, 0);
    for (std::size_t other = frame >= HistoryFrames ? frame - HistoryFrames : 0; other <= frame; ++other) {
      for (std::size_t i = 0; i < pixels; ++i) {
        region[i] = (region[i] != 0 || glass[other].coverage[i] != 0 || glass[other].swept[i] != 0) ? 1 : 0;
      }
    }
    region = dilate(size, region, DilationPixels);
    FrameResult result;
    result.glass = statistics(errors[frame], region);
    result.opaque = statistics(errors[frame], region, false);
    // The pixels the glass left HistoryFrames frames ago: swept then, uncovered then, and not
    // covered or swept again by now.
    if (frame >= HistoryFrames) {
      const GlassFrame &then = glass[frame - HistoryFrames];
      const GlassFrame &now = glass[frame];
      for (std::size_t i = 0; i < pixels; ++i) {
        if (then.swept[i] != 0 && then.coverage[i] == 0 && now.coverage[i] == 0 && now.swept[i] == 0) {
          ++result.vacated;
          if (errors[frame][i] > MaxRecoveredError) {
            ++result.unrecovered;
          }
        }
      }
    }
    results.push_back(result);
  }
  return results;
}

} // namespace sonnet::runtime::taa
