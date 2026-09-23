#include "FrameSource.h"

#include <sddl.h>

#include <cstring>

namespace {

// SYSTEM, admins, Local Service (the Frame Server itself) and the interactive
// user get full access; the interactive user is the G14 IR Camera app writing
// frames.
constexpr wchar_t kSectionSddl[] =
    L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;LS)(A;;GA;;;IU)";

uint32_t ReadU32(volatile uint8_t* base, uint32_t offset) {
    uint32_t value;
    memcpy(&value, const_cast<uint8_t*>(base) + offset, sizeof(value));
    return value;
}

}  // namespace

FrameSource::~FrameSource() {
    if (_view != nullptr) {
        UnmapViewOfFile(const_cast<uint8_t*>(_view));
    }
    if (_mapping != nullptr) {
        CloseHandle(_mapping);
    }
}

bool FrameSource::EnsureMapping() {
    if (_view != nullptr) {
        return true;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kSectionSddl, SDDL_REVISION_1, &sd, nullptr)) {
        sa.lpSecurityDescriptor = sd;
    }

    _mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                  sa.lpSecurityDescriptor ? &sa : nullptr,
                                  PAGE_READWRITE, 0, G14IR_SHM_SIZE, G14IR_SHM_NAME);
    if (sd != nullptr) {
        LocalFree(sd);
    }
    if (_mapping == nullptr) {
        // Fall back to opening a section someone else created.
        _mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, G14IR_SHM_NAME);
        if (_mapping == nullptr) {
            return false;
        }
    }

    _view = static_cast<volatile uint8_t*>(
        MapViewOfFile(_mapping, FILE_MAP_ALL_ACCESS, 0, 0, G14IR_SHM_SIZE));
    if (_view == nullptr) {
        _view = static_cast<volatile uint8_t*>(
            MapViewOfFile(_mapping, FILE_MAP_READ, 0, 0, G14IR_SHM_SIZE));
    }
    return _view != nullptr;
}

bool FrameSource::CopyLatest() {
    if (!EnsureMapping()) {
        return false;
    }

    for (int attempt = 0; attempt < 8; ++attempt) {
        uint32_t seq1 = ReadU32(_view, 28);
        if (seq1 & 1u) {
            continue;  // writer mid-update
        }
        MemoryBarrier();

        if (ReadU32(_view, 0) != G14IR_MAGIC || ReadU32(_view, 4) != G14IR_VERSION) {
            return false;  // app has not published yet
        }
        uint32_t width = ReadU32(_view, 8);
        uint32_t height = ReadU32(_view, 12);
        uint32_t format = ReadU32(_view, 24);
        if (width == 0 || height == 0 || width > G14IR_MAX_WIDTH ||
            height > G14IR_MAX_HEIGHT || format != G14IR_FMT_BGRA) {
            return false;
        }

        size_t bytes = static_cast<size_t>(width) * height * 4;
        if (_frame.size() < bytes) {
            _frame.resize(bytes);
        }
        memcpy(_frame.data(), const_cast<uint8_t*>(_view) + G14IR_HEADER_SIZE, bytes);

        MemoryBarrier();
        uint32_t seq2 = ReadU32(_view, 28);
        if (seq1 == seq2) {
            _frameWidth = width;
            _frameHeight = height;
            _hasFrame = true;
            return true;
        }
    }
    // Torn reads 8 times in a row: keep whatever frame we already had.
    return _hasFrame;
}

void FrameSource::Fill(uint8_t* dst, uint32_t dstWidth, uint32_t dstHeight) {
    std::lock_guard<std::mutex> guard(_lock);
    FillBGRALocked(dst, dstWidth, dstHeight);
}

void FrameSource::FillNV12(uint8_t* dst, uint32_t dstWidth, uint32_t dstHeight) {
    std::lock_guard<std::mutex> guard(_lock);

    const size_t bgraBytes = static_cast<size_t>(dstWidth) * dstHeight * 4;
    if (_scratch.size() < bgraBytes) {
        _scratch.resize(bgraBytes);
    }
    FillBGRALocked(_scratch.data(), dstWidth, dstHeight);

    // BGRA -> NV12, integer BT.601 studio swing. Y plane first, then
    // interleaved UV at quarter resolution (2x2 average).
    uint8_t* yPlane = dst;
    uint8_t* uvPlane = dst + static_cast<size_t>(dstWidth) * dstHeight;
    const auto lumaOf = [](const uint8_t* px) -> int {
        return ((66 * px[2] + 129 * px[1] + 25 * px[0] + 128) >> 8) + 16;
    };
    for (uint32_t y = 0; y < dstHeight; y += 2) {
        const uint8_t* row0 = _scratch.data() + static_cast<size_t>(y) * dstWidth * 4;
        const uint8_t* row1 = row0 + static_cast<size_t>(dstWidth) * 4;
        uint8_t* y0 = yPlane + static_cast<size_t>(y) * dstWidth;
        uint8_t* y1 = y0 + dstWidth;
        uint8_t* uv = uvPlane + static_cast<size_t>(y / 2) * dstWidth;
        for (uint32_t x = 0; x < dstWidth; x += 2) {
            const uint8_t* p00 = row0 + x * 4;
            const uint8_t* p01 = p00 + 4;
            const uint8_t* p10 = row1 + x * 4;
            const uint8_t* p11 = p10 + 4;

            y0[x] = static_cast<uint8_t>(lumaOf(p00));
            y0[x + 1] = static_cast<uint8_t>(lumaOf(p01));
            y1[x] = static_cast<uint8_t>(lumaOf(p10));
            y1[x + 1] = static_cast<uint8_t>(lumaOf(p11));

            const int b = (p00[0] + p01[0] + p10[0] + p11[0] + 2) >> 2;
            const int g = (p00[1] + p01[1] + p10[1] + p11[1] + 2) >> 2;
            const int r = (p00[2] + p01[2] + p10[2] + p11[2] + 2) >> 2;
            uv[x] = static_cast<uint8_t>(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            uv[x + 1] = static_cast<uint8_t>(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }
}

void FrameSource::FillBGRALocked(uint8_t* dst, uint32_t dstWidth, uint32_t dstHeight) {
    if (!CopyLatest()) {
        // Placeholder: animated diagonal gradient so users can tell the
        // camera is alive but the app is not sending frames yet.
        ++_placeholderTick;
        for (uint32_t y = 0; y < dstHeight; ++y) {
            uint8_t* row = dst + static_cast<size_t>(y) * dstWidth * 4;
            for (uint32_t x = 0; x < dstWidth; ++x) {
                uint8_t v = static_cast<uint8_t>(
                    (x * 160 / dstWidth + y * 64 / dstHeight + _placeholderTick) & 0xFF);
                row[x * 4 + 0] = static_cast<uint8_t>(48 + (v >> 2));  // B
                row[x * 4 + 1] = 32;                                    // G
                row[x * 4 + 2] = static_cast<uint8_t>(24 + (v >> 3));  // R
                row[x * 4 + 3] = 0xFF;
            }
        }
        return;
    }

    if (_frameWidth == dstWidth && _frameHeight == dstHeight) {
        memcpy(dst, _frame.data(), static_cast<size_t>(dstWidth) * dstHeight * 4);
        return;
    }

    // Resolution mismatch between the app and what the consumer negotiated:
    // nearest-neighbor scale. Cheap and good enough for a fallback path.
    for (uint32_t y = 0; y < dstHeight; ++y) {
        uint32_t srcY = static_cast<uint32_t>(
            static_cast<uint64_t>(y) * _frameHeight / dstHeight);
        const uint8_t* srcRow =
            _frame.data() + static_cast<size_t>(srcY) * _frameWidth * 4;
        uint8_t* dstRow = dst + static_cast<size_t>(y) * dstWidth * 4;
        for (uint32_t x = 0; x < dstWidth; ++x) {
            uint32_t srcX = static_cast<uint32_t>(
                static_cast<uint64_t>(x) * _frameWidth / dstWidth);
            memcpy(dstRow + x * 4, srcRow + srcX * 4, 4);
        }
    }
}

