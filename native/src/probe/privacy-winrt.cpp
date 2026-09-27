// Non-installing IR diagnostics. No privacy SET operations or frame recording.
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Capture.h>
#include <winrt/Windows.Media.Capture.Frames.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.Media.MediaProperties.h>
#include <cstdio>
#include <cwctype>
#include <string>
#include "frame-statistics.h"

using namespace winrt;
using namespace Windows::Media::Capture;
using namespace Windows::Media::Capture::Frames;
using namespace Windows::Media::Devices;
using namespace Windows::Graphics::Imaging;

int RunDirectShow(const std::wstring& match, unsigned seconds);
int RunKernelProperties(const std::wstring& match);
int RunKernelCapture(const std::wstring& match, unsigned seconds);

static std::wstring Lower(std::wstring value) {
    for (auto& ch : value) ch = wchar_t(towlower(ch));
    return value;
}

static void PrintShutter(const CameraOcclusionInfo& info, double elapsed) {
    if (!info) return;
    try {
        const auto state = info.GetState();
        wprintf(L"SHUTTER t=%.2f occluded=%d hardware=%d lid=%d\n", elapsed,
            state.IsOccluded(), state.IsOcclusionKind(CameraOcclusionKind::CameraHardware),
            state.IsOcclusionKind(CameraOcclusionKind::Lid));
    } catch (hresult_error const& e) {
        wprintf(L"SHUTTER t=%.2f unavailable HRESULT=0x%08X\n", elapsed, unsigned(e.code()));
    }
}

static int RunWinRT(const std::wstring& match, unsigned seconds) {
    MediaFrameSourceGroup selected{nullptr};
    unsigned matches = 0;
    for (const auto& group : MediaFrameSourceGroup::FindAllAsync().get()) {
        bool found = false;
        bool infraredOnly = true;
        wprintf(L"GROUP name=%s id=%s\n", group.DisplayName().c_str(), group.Id().c_str());
        for (const auto& info : group.SourceInfos()) {
            const auto device = info.DeviceInformation();
            infraredOnly &= info.SourceKind() == MediaFrameSourceKind::Infrared;
            wprintf(L"SOURCE kind=%d id=%s device=%s\n", int(info.SourceKind()), info.Id().c_str(), device ? device.Id().c_str() : L"<none>");
            found |= info.SourceKind() == MediaFrameSourceKind::Infrared &&
                ((device && Lower(device.Id().c_str()).find(match) != std::wstring::npos) ||
                 Lower(group.Id().c_str()).find(match) != std::wstring::npos ||
                 Lower(info.Id().c_str()).find(match) != std::wstring::npos);
        }
        // A Realtek grouped RGB+IR source may advertise the same IR device.
        // Compare the physical IR-only group, not that aggregate processing group.
        if (found && infraredOnly) { selected = group; ++matches; }
    }
    if (matches != 1) throw hresult_error(E_FAIL, L"Need exactly one matching IR group; use a more specific device match.");
    MediaCaptureInitializationSettings settings;
    settings.SourceGroup(selected);
    settings.StreamingCaptureMode(StreamingCaptureMode::Video);
    settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
    MediaCapture capture;
    MediaFrameReader reader{nullptr};
    bool started = false;
    try {
        capture.InitializeAsync(settings).get();
        MediaFrameSource source{nullptr};
        for (unsigned retry = 0; retry < 20 && !source; ++retry) {
            for (const auto& item : capture.FrameSources()) {
                if (item.Value().Info().SourceKind() == MediaFrameSourceKind::Infrared) source = item.Value();
            }
            if (!source) Sleep(100);
        }
        if (!source) throw hresult_error(E_FAIL, L"Matching camera exposed no IR source.");
        wprintf(L"WINRT device=%s subtype=%s\n", selected.DisplayName().c_str(), source.CurrentFormat().Subtype().c_str());
        CameraOcclusionInfo occlusion{nullptr};
        try {
            occlusion = capture.VideoDeviceController().CameraOcclusionInfo();
            if (occlusion) wprintf(L"SHUTTER support hardware=%d lid=%d (unsupported is not proof of an open shutter)\n",
                occlusion.IsOcclusionKindSupported(CameraOcclusionKind::CameraHardware),
                occlusion.IsOcclusionKindSupported(CameraOcclusionKind::Lid));
            else wprintf(L"SHUTTER API returned null\n");
        } catch (hresult_error const& e) {
            wprintf(L"SHUTTER API unavailable HRESULT=0x%08X\n", unsigned(e.code()));
        }
        reader = capture.CreateFrameReaderAsync(source).get();
        if (reader.StartAsync().get() != MediaFrameReaderStartStatus::Success) throw hresult_error(E_FAIL, L"Reader start failed.");
        started = true;
        const auto start = GetTickCount64();
        ULONGLONG nextReport = 0;
        int64_t lastTimestamp = -1;
        unsigned frames = 0, flat = 0;
        while (GetTickCount64() - start < seconds * 1000ULL) {
            const auto elapsed = GetTickCount64() - start;
            if (elapsed >= nextReport) {
                PrintShutter(occlusion, elapsed / 1000.0);
                nextReport = elapsed + 1000;
            }
            auto frame = reader.TryAcquireLatestFrame();
            if (!frame) { Sleep(5); continue; }
            const auto timestamp = frame.SystemRelativeTime();
            if (!timestamp || timestamp.Value().count() == lastTimestamp) { frame.Close(); Sleep(5); continue; }
            lastTimestamp = timestamp.Value().count();
            const auto video = frame.VideoMediaFrame();
            if (!video) { frame.Close(); continue; }
            auto bitmap = video.SoftwareBitmap();
            if (!bitmap && video.Direct3DSurface()) bitmap = SoftwareBitmap::CreateCopyFromSurfaceAsync(video.Direct3DSurface()).get();
            if (!bitmap) { frame.Close(); continue; }
            if (bitmap.BitmapPixelFormat() != BitmapPixelFormat::Gray8) throw hresult_error(E_FAIL, L"Refusing non-Gray8 conversion.");
            FrameStatistics stats;
            {
                auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Read);
                const auto plane = buffer.GetPlaneDescription(0);
                auto reference = buffer.CreateReference();
                uint8_t* bytes = nullptr; uint32_t capacity = 0;
                check_hresult(reference.as<winrt::impl::IMemoryBufferByteAccess>()->GetBuffer(&bytes, &capacity));
                if (plane.StartIndex < 0 || unsigned(plane.StartIndex) > capacity || plane.Stride < 0)
                    throw hresult_error(E_FAIL, L"Invalid bitmap plane.");
                stats = AnalyzeGray8(bytes + plane.StartIndex, capacity - plane.StartIndex,
                    bitmap.PixelWidth(), bitmap.PixelHeight(), plane.Stride);
            }
            ++frames;
            flat += stats.minimum == stats.maximum;
            if (frames <= 10 || frames % 15 == 0) wprintf(L"FRAME t=%.2f number=%u min=%u max=%u mean=%.3f std=%.3f timestamp=%lld\n",
                elapsed / 1000.0, frames, stats.minimum, stats.maximum, stats.mean, stats.deviation, lastTimestamp);
            frame.Close();
        }
        reader.StopAsync().get(); started = false;
        reader.Close(); capture.Close();
        wprintf(L"DONE path=winrt frames=%u uniform=%u detailed=%u; camera released; no images saved\n", frames, flat, frames - flat);
        return frames ? 0 : 1;
    } catch (...) {
        if (started) { try { reader.StopAsync().get(); } catch (...) {} }
        if (reader) { try { reader.Close(); } catch (...) {} }
        capture.Close();
        throw;
    }
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 5 || (wcscmp(argv[1], L"--winrt") && wcscmp(argv[1], L"--directshow") && wcscmp(argv[1], L"--ks-properties") && wcscmp(argv[1], L"--kernel")) || wcscmp(argv[2], L"--device-match")) {
        fwprintf(stderr, L"Usage: umbracam-privacy-probe (--winrt|--directshow|--ks-properties|--kernel) --device-match <IR device-ID substring> <seconds 1..60>\n");
        return 2;
    }
    wchar_t* end = nullptr;
    const auto seconds = wcstoul(argv[4], &end, 10);
    if (!*argv[3] || !*argv[4] || *end || seconds < 1 || seconds > 60) return 2;
    try {
        init_apartment(apartment_type::multi_threaded);
        wprintf(L"Requested device match: %s\n", argv[3]);
        if (wcscmp(argv[1], L"--ks-properties") == 0) return RunKernelProperties(Lower(argv[3]));
        if (wcscmp(argv[1], L"--kernel") == 0) return RunKernelCapture(Lower(argv[3]), unsigned(seconds));
        return wcscmp(argv[1], L"--winrt") == 0 ? RunWinRT(Lower(argv[3]), unsigned(seconds)) : RunDirectShow(Lower(argv[3]), unsigned(seconds));
    } catch (hresult_error const& e) {
        fwprintf(stderr, L"FAILED HRESULT=0x%08X %s\n", unsigned(e.code()), e.message().c_str());
    } catch (std::exception const& e) { fprintf(stderr, "FAILED: %s\n", e.what()); }
    return 1;
}
