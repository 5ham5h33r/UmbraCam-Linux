#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

struct FrameStatistics {
    unsigned minimum = 255, maximum = 0;
    double mean = 0, deviation = 0;
};

// Inspect visible Gray8 pixels only; padding must not change the result.
inline FrameStatistics AnalyzeGray8(const uint8_t* data, size_t length,
                                    size_t width, size_t height, size_t stride) {
    if (!data || !width || !height || stride < width || length < width ||
        height - 1 > (length - width) / stride)
        throw std::invalid_argument("Invalid Gray8 buffer bounds");
    FrameStatistics result;
    double sum = 0, squares = 0;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            const unsigned value = data[y * stride + x];
            result.minimum = std::min(result.minimum, value);
            result.maximum = std::max(result.maximum, value);
            sum += value;
            squares += value * value;
        }
    }
    const double pixels = double(width) * double(height);
    result.mean = sum / pixels;
    result.deviation = std::sqrt(std::max(0.0, squares / pixels - result.mean * result.mean));
    return result;
}
