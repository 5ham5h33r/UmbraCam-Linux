#include "ir_frame_policy.h"
#include <cstdio>
#include <cstdlib>

void Require(bool result, const char* description) {
    if (!result) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}

int main() {
    IrFramePolicy ambientOnly;
    for (uint64_t time = 0; time < 2000; time += 67)
        Require(ambientOnly.Accept(false, time), "ambient-only devices must not freeze");
    IrFramePolicy alternating;
    unsigned accepted = 0;
    for (uint64_t i = 0; i < 90; ++i) accepted += alternating.Accept(i % 2 == 0, i * 67);
    Require(accepted == 45, "alternating stream must retain only illuminated phase");
    IrFramePolicy fallback;
    Require(fallback.Accept(true, 100), "first illuminated frame");
    Require(!fallback.Accept(false, 1099), "hold illuminated frame briefly");
    Require(fallback.Accept(false, 1100), "recover after illuminator stops");
    Require(fallback.Accept(true, 1200), "resume illuminated phase");
    Require(!fallback.Accept(false, 1267), "suppress ambient after resuming");
    Require(fallback.Accept(std::nullopt, 1300), "unknown metadata must pass through");
    std::puts("IR frame policy tests: PASS");
}
