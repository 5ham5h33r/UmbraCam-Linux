#pragma once
// Reads the latest frame published by the UmbraCam app through the shared
// memory section described in ir_camera_protocol.h. Runs inside the Frame Server
// service, so THIS side creates the Global\ section (services hold the
// SeCreateGlobalPrivilege; user apps only open the section).

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <vector>

#include "ir_camera_protocol.h"

class FrameSource {
public:
    FrameSource() = default;
    ~FrameSource();

    FrameSource(const FrameSource&) = delete;
    FrameSource& operator=(const FrameSource&) = delete;

    // Fills `dst` (BGRX, top-down, dstWidth*dstHeight*4 bytes) with the most
    // recent app frame, scaled if needed, or with a placeholder pattern when
    // the app is not publishing.
    void Fill(uint8_t* dst, uint32_t dstWidth, uint32_t dstHeight);

    // Same, but writes NV12 (dstWidth*dstHeight*3/2 bytes). Chrome, Teams and
    // most MF consumers negotiate NV12; RGB32-only sources show up green.
    void FillNV12(uint8_t* dst, uint32_t dstWidth, uint32_t dstHeight);

private:
    bool EnsureMapping();
    bool CopyLatest();  // seqlock copy into _frame; false when no valid frame
    void FillBGRALocked(uint8_t* dst, uint32_t dstWidth, uint32_t dstHeight);

    HANDLE _mapping = nullptr;
    volatile uint8_t* _view = nullptr;

    std::mutex _lock;
    std::vector<uint8_t> _frame;    // last good frame, BGRA
    std::vector<uint8_t> _scratch;  // BGRA staging for the NV12 conversion
    uint32_t _frameWidth = 0;
    uint32_t _frameHeight = 0;
    bool _hasFrame = false;
    uint32_t _placeholderTick = 0;
};

