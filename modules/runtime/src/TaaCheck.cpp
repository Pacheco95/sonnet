#include <sonnet/runtime/TaaCheck.h>

#include <algorithm>
#include <array>
#include <cstdlib>

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

TemporalFloor temporalFloor(glm::uvec2 size, std::span<const std::byte> image, std::span<const std::byte> previous,
                            const Mask &mask, int radius) {
  const std::size_t count = std::size_t{size.x} * size.y;
  if (image.size() < count * 4 || previous.size() < count * 4) {
    return {};
  }
  const auto width = static_cast<int>(size.x);
  const auto height = static_cast<int>(size.y);
  const int margin = radius + 2;
  const auto at = [&](std::span<const std::byte> pixels, int x, int y, std::size_t channel) {
    return std::to_integer<int>(
        pixels[(static_cast<std::size_t>(y) * size.x + static_cast<std::size_t>(x)) * 4 + channel]);
  };
  // The 3x3 neighbourhood of (x, y) now, against the one at (x - dx, y - dy) before: summed over channels.
  const auto patch = [&](int x, int y, int dx, int dy) {
    int sum = 0;
    for (int v = -1; v <= 1; ++v) {
      for (int u = -1; u <= 1; ++u) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
          sum += std::abs(at(image, x + u, y + v, channel) - at(previous, x + u - dx, y + v - dy, channel));
        }
      }
    }
    return sum;
  };
  const auto center = [&](int x, int y, int dx, int dy) {
    int largest = 0;
    for (std::size_t channel = 0; channel < 3; ++channel) {
      largest = std::max(largest, std::abs(at(image, x, y, channel) - at(previous, x - dx, y - dy, channel)));
    }
    return static_cast<std::uint8_t>(largest);
  };
  std::vector<std::uint8_t> still(count, 0);
  std::vector<std::uint8_t> best(count, 0);
  Mask inside(count, 0);
  for (int y = margin; y < height - margin; ++y) {
    for (int x = margin; x < width - margin; ++x) {
      const std::size_t index = static_cast<std::size_t>(y) * size.x + static_cast<std::size_t>(x);
      if (mask[index] == 0) {
        continue;
      }
      inside[index] = 1;
      still[index] = center(x, y, 0, 0);
      int bestSum = patch(x, y, 0, 0);
      int bestX = 0;
      int bestY = 0;
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          if (const int sum = patch(x, y, dx, dy); sum < bestSum) {
            bestSum = sum;
            bestX = dx;
            bestY = dy;
          }
        }
      }
      best[index] = center(x, y, bestX, bestY);
    }
  }
  return {.still = statistics(still, inside), .best = statistics(best, inside)};
}

std::vector<FrameResult> evaluate(glm::uvec2 size, std::span<const GlassFrame> glass,
                                  std::span<const std::vector<std::uint8_t>> errors,
                                  std::span<const std::vector<std::uint8_t>> sampleErrors, std::size_t firstMeasured) {
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
    result.covered = statistics(errors[frame], glass[frame].coverage);
    Mask rest(pixels, 0);
    for (std::size_t i = 0; i < pixels; ++i) {
      rest[i] = region[i] != 0 && glass[frame].coverage[i] == 0 ? 1 : 0;
    }
    result.uncovered = statistics(errors[frame], rest);
    // The pixels the glass left HistoryFrames frames ago: swept then, uncovered then, and not
    // covered or swept again by now.
    if (frame >= HistoryFrames) {
      const GlassFrame &then = glass[frame - HistoryFrames];
      const GlassFrame &now = glass[frame];
      Mask vacated(pixels, 0);
      for (std::size_t i = 0; i < pixels; ++i) {
        vacated[i] = then.swept[i] != 0 && then.coverage[i] == 0 && now.coverage[i] == 0 && now.swept[i] == 0 ? 1 : 0;
      }
      result.vacated = statistics(errors[frame], vacated);
      result.vacatedSample = statistics(sampleErrors[frame], vacated);
    }
    results.push_back(result);
  }
  return results;
}

} // namespace sonnet::runtime::taa
