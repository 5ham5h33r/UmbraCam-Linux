#include "../src/probe/frame-statistics.h"
#include <cstdio>
#include <limits>

int main() {
    const uint8_t padded[]{144,144,0,255,144,144};
    const auto flat = AnalyzeGray8(padded, sizeof(padded), 2, 2, 4);
    if (flat.minimum != 144 || flat.maximum != 144 || flat.mean != 144 || flat.deviation != 0) return 1;
    const uint8_t varied[]{0,255};
    const auto detail = AnalyzeGray8(varied, 2, 2, 1, 2);
    if (detail.minimum != 0 || detail.maximum != 255 || detail.mean != 127.5 || detail.deviation != 127.5) return 1;
    unsigned rejected = 0;
    auto invalid = [&](const uint8_t* data, size_t len, size_t w, size_t h, size_t stride) {
        try { AnalyzeGray8(data, len, w, h, stride); } catch (const std::invalid_argument&) { ++rejected; }
    };
    invalid(nullptr, 2, 2, 1, 2);
    invalid(varied, 2, 0, 1, 2);
    invalid(varied, 2, 2, 0, 2);
    invalid(varied, 2, 2, 1, 1);
    invalid(varied, 1, 2, 1, 2);
    invalid(varied, 2, 2, 2, 2);
    invalid(varied, 2, 2, std::numeric_limits<size_t>::max(), 2);
    if (rejected != 7) return 1;
    puts("PASS: flat, varied, padded frames and seven invalid bounds cases");
    return 0;
}
