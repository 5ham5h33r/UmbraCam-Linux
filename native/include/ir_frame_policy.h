#pragma once
#include <cstdint>
#include <optional>

// Hello cameras may alternate IR-lit and ambient exposures. Once a camera
// reports illuminated frames, keep that phase instead of flashing between
// both. Unknown metadata and ambient-only devices remain usable.
class IrFramePolicy {
public:
    bool Accept(std::optional<bool> illuminated, uint64_t nowMilliseconds) {
        if (!illuminated.has_value()) return true;
        if (*illuminated) {
            seenIlluminated_ = true;
            lastIlluminated_ = nowMilliseconds;
            return true;
        }
        // Recover if the illuminator stops or the driver changes its metadata.
        return !seenIlluminated_ || nowMilliseconds - lastIlluminated_ >= 1000;
    }

private:
    bool seenIlluminated_ = false;
    uint64_t lastIlluminated_ = 0;
};
