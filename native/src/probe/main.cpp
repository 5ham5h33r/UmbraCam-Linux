// umbracam-probe: native, non-installing hardware smoke test for the same
// MTA MediaCapture path used by the UmbraCam publisher.

#include <windows.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Capture.h>
#include <winrt/Windows.Media.Capture.Frames.h>

#include <cstdio>

using namespace winrt;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Capture;
using namespace Windows::Media::Capture::Frames;

namespace {

MediaFrameSourceGroup FindInfraredGroup() {
    const auto groups = MediaFrameSourceGroup::FindAllAsync().get();
    for (const auto& group : groups) {
        bool infrared = false;
        bool other = false;
        for (const auto& info : group.SourceInfos()) {
            infrared |= info.SourceKind() == MediaFrameSourceKind::Infrared;
            other |= info.SourceKind() != MediaFrameSourceKind::Infrared;
        }
        if (infrared && !other) {
            return group;
        }
    }
    for (const auto& group : groups) {
        for (const auto& info : group.SourceInfos()) {
            if (info.SourceKind() == MediaFrameSourceKind::Infrared) {
                return group;
            }
        }
    }
    throw hresult_error(HRESULT_FROM_WIN32(ERROR_NOT_FOUND), L"No infrared camera group was found.");
}

MediaFrameSource WaitForInfraredSource(MediaCapture const& capture) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        for (const auto& item : capture.FrameSources()) {
            if (item.Value().Info().SourceKind() == MediaFrameSourceKind::Infrared) {
                return item.Value();
            }
        }
        Sleep(100);
    }
    return nullptr;
}

} // namespace

int wmain() {
    try {
        init_apartment(apartment_type::multi_threaded);
        const auto group = FindInfraredGroup();

        auto settings = MediaCaptureInitializationSettings();
        settings.SourceGroup(group);
        settings.StreamingCaptureMode(StreamingCaptureMode::Video);
        settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);

        MediaCapture capture;
        const wchar_t* sharing = L"ExclusiveControl";
        try {
            capture.InitializeAsync(settings).get();
        } catch (hresult_error const&) {
            capture.Close();
            capture = MediaCapture();
            settings.SharingMode(MediaCaptureSharingMode::SharedReadOnly);
            sharing = L"SharedReadOnly";
            capture.InitializeAsync(settings).get();
        }

        const auto source = WaitForInfraredSource(capture);
        if (!source) {
            throw hresult_error(HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
                                L"The IR group opened but exposed no infrared frame source.");
        }

        const auto reader = capture.CreateFrameReaderAsync(source).get();
        if (reader.StartAsync().get() != MediaFrameReaderStartStatus::Success) {
            throw hresult_error(E_FAIL, L"The infrared frame reader did not start.");
        }

        for (int attempt = 0; attempt < 30; ++attempt) {
            if (const auto frame = reader.TryAcquireLatestFrame()) {
                if (const auto video = frame.VideoMediaFrame()) {
                    auto bitmap = video.SoftwareBitmap();
                    if (!bitmap && video.Direct3DSurface()) {
                        bitmap = SoftwareBitmap::CreateCopyFromSurfaceAsync(
                            video.Direct3DSurface()).get();
                    }
                    if (bitmap) {
                        wprintf(L"Probe: PASS\nGroup: %s\nSharing: %s\nSize: %d x %d\n",
                                group.DisplayName().c_str(), sharing,
                                bitmap.PixelWidth(), bitmap.PixelHeight());
                        reader.StopAsync().get();
                        capture.Close();
                        return 0;
                    }
                }
            }
            Sleep(100);
        }

        throw hresult_error(E_FAIL, L"The IR reader started but delivered no CPU-readable frame.");
    } catch (hresult_error const& error) {
        fwprintf(stderr, L"Probe: FAIL (0x%08X): %s\n",
                 static_cast<unsigned>(error.code()), error.message().c_str());
        return 1;
    }
}
