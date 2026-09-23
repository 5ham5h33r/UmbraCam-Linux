// umbracam: a consent-owning desktop companion for a Windows Hello IR camera.
//
// The Windows Frame Server hosts the virtual-camera DLL in a service process,
// which cannot display privacy prompts. This process owns MediaCapture
// initialization (and its first-run consent flow), then publishes IR frames
// over shared memory. The system-lifetime virtual device is installed once by
// the elevated installer helper.

#include <windows.h>
#include <shellapi.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Capture.h>
#include <winrt/Windows.Media.Capture.Frames.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

#include "ir_camera_protocol.h"

using namespace winrt;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Capture;
using namespace Windows::Media::Capture::Frames;

namespace {

std::atomic_bool g_running{true};
bool g_diagnosticMode = false;

BOOL WINAPI OnConsoleControl(DWORD) {
    g_running = false;
    return TRUE;
}

class SharedFrameWriter {
public:
    ~SharedFrameWriter() {
        if (view_) {
            UnmapViewOfFile(view_);
        }
        if (mapping_) {
            CloseHandle(mapping_);
        }
    }

    void Publish(SoftwareBitmap const& bitmap) {
        if (!EnsureMapping()) {
            return;
        }

        SoftwareBitmap bgra = bitmap;
        if (bitmap.BitmapPixelFormat() != BitmapPixelFormat::Bgra8 ||
            bitmap.BitmapAlphaMode() != BitmapAlphaMode::Premultiplied) {
            bgra = SoftwareBitmap::Convert(bitmap, BitmapPixelFormat::Bgra8,
                                            BitmapAlphaMode::Premultiplied);
        }

        const uint32_t width = static_cast<uint32_t>(bgra.PixelWidth());
        const uint32_t height = static_cast<uint32_t>(bgra.PixelHeight());
        if (width == 0 || height == 0 || width > IRCAM_MAX_WIDTH || height > IRCAM_MAX_HEIGHT) {
            return;
        }

        auto buffer = bgra.LockBuffer(BitmapBufferAccessMode::Read);
        const auto plane = buffer.GetPlaneDescription(0);
        const auto reference = buffer.CreateReference();
        uint8_t* data = nullptr;
        uint32_t capacity = 0;
        check_hresult(reference.as<winrt::impl::IMemoryBufferByteAccess>()->GetBuffer(&data, &capacity));
        const uint8_t* source = data + plane.StartIndex;

        auto* header = reinterpret_cast<ir_camera_header*>(view_);
        auto* destination = view_ + IRCAM_HEADER_SIZE;
        auto sequence = InterlockedIncrement(reinterpret_cast<volatile LONG*>(&header->seq));
        if ((sequence & 1) == 0) {
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(&header->seq));
        }

        header->magic = IRCAM_MAGIC;
        header->version = IRCAM_VERSION;
        header->width = width;
        header->height = height;
        header->fps_num = 30;
        header->fps_den = 1;
        header->format = IRCAM_FMT_BGRA;

        const uint32_t rowBytes = width * 4;
        for (uint32_t row = 0; row < height; ++row) {
            CopyMemory(destination + static_cast<size_t>(row) * rowBytes,
                       source + static_cast<size_t>(row) * plane.Stride, rowBytes);
        }

        header->frame_id++;
        header->qpc_100ns = GetTickCount64() * 10'000;
        MemoryBarrier();
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&header->seq));
    }

private:
    bool EnsureMapping() {
        if (view_) {
            return true;
        }

        mapping_ = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, IRCAM_SHM_NAME);
        if (!mapping_) {
            return false; // The Frame Server creates it once a client opens the virtual camera.
        }
        view_ = static_cast<uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ | FILE_MAP_WRITE,
                                                     0, 0, IRCAM_SHM_SIZE));
        if (!view_) {
            CloseHandle(mapping_);
            mapping_ = nullptr;
            return false;
        }
        return true;
    }

    HANDLE mapping_ = nullptr;
    uint8_t* view_ = nullptr;
};

MediaFrameSourceGroup FindInfraredGroup() {
    const auto groups = MediaFrameSourceGroup::FindAllAsync().get();

    // Drivers such as Realtek's Windows Hello stack can expose a composite
    // Color+IR group before the direct IR UVC group. The former may advertise
    // IR but not surface frames to standard MediaCapture clients. Prefer a
    // group consisting solely of IR sources, then retain the generic fallback.
    for (const auto& group : groups) {
        bool hasInfrared = false;
        bool hasNonInfrared = false;
        for (const auto& source : group.SourceInfos()) {
            if (source.SourceKind() == MediaFrameSourceKind::Infrared) {
                hasInfrared = true;
            } else {
                hasNonInfrared = true;
            }
        }
        if (hasInfrared && !hasNonInfrared) {
            return group;
        }
    }

    for (const auto& group : groups) {
        for (const auto& source : group.SourceInfos()) {
            if (source.SourceKind() == MediaFrameSourceKind::Infrared) {
                return group;
            }
        }
    }
    throw hresult_error(HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
                        L"No Windows Hello infrared camera was found.");
}

winrt::hstring FindInfraredDeviceId(MediaFrameSourceGroup const& group) {
    for (const auto& source : group.SourceInfos()) {
        if (source.SourceKind() == MediaFrameSourceKind::Infrared) {
            return source.DeviceInformation().Id();
        }
    }
    throw hresult_error(HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
                        L"The selected camera group has no infrared device.");
}

MediaFrameSource FindInfraredSource(MediaCapture const& capture) {
    // Realtek's camera pipeline can complete initialization slightly before
    // it publishes FrameSources. Wait a short bounded interval for the map.
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

void ShowCameraAccessError(hresult_error const& error) {
    wchar_t executable[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    wchar_t* fileName = wcsrchr(executable, L'\\');
    if (fileName) {
        ++fileName;
        wcscpy_s(fileName, MAX_PATH - static_cast<size_t>(fileName - executable),
                 L"umbracam-diagnostic.log");
        wchar_t diagnostic[1024] = {};
        wsprintfW(diagnostic, L"UmbraCam failed: 0x%08X %s\r\n",
                  static_cast<unsigned>(error.code()), error.message().c_str());
        HANDLE log = CreateFileW(executable, GENERIC_WRITE, FILE_SHARE_READ,
                                 nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(log, diagnostic, static_cast<DWORD>(wcslen(diagnostic) * sizeof(wchar_t)),
                      &written, nullptr);
            CloseHandle(log);
        }
    }

    if (g_diagnosticMode) {
        return;
    }
    const bool denied = error.code() == E_ACCESSDENIED;
    const wchar_t* text = denied
        ? L"Windows blocked camera access. Select OK to open Camera privacy settings, enable camera access and Let desktop apps access your camera, then launch UmbraCam again."
        : L"UmbraCam could not start the infrared capture stream. Close other camera apps and try again.";
    MessageBoxW(nullptr, text, L"UmbraCam", MB_OK | MB_ICONWARNING);
    if (denied) {
        ShellExecuteW(nullptr, L"open", L"ms-settings:privacy-webcam", nullptr, nullptr, SW_SHOWNORMAL);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    const bool stopRequested = arguments && argumentCount == 2 &&
        wcscmp(arguments[1], L"--stop") == 0;
    g_diagnosticMode = arguments && argumentCount == 2 &&
        wcscmp(arguments[1], L"--diagnose") == 0;
    if (arguments) {
        LocalFree(arguments);
    }
    if (stopRequested) {
        HANDLE stopEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, IRCAM_STOP_EVENT_NAME);
        if (stopEvent) {
            SetEvent(stopEvent);
            CloseHandle(stopEvent);
        }
        return 0;
    }

    SetConsoleCtrlHandler(OnConsoleControl, TRUE);
    HANDLE stopEvent = nullptr;

    try {
        // MediaCapture's frame pipeline on the Realtek Windows Hello stack
        // is delivered from a worker/MTA context. The app still owns the
        // privacy error UI below, but capture itself must not block an STA.
        init_apartment(apartment_type::multi_threaded);
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, IRCAM_STOP_EVENT_NAME);

        const auto infraredGroup = FindInfraredGroup();
        auto settings = MediaCaptureInitializationSettings();
        settings.SourceGroup(infraredGroup);
        settings.StreamingCaptureMode(StreamingCaptureMode::Video);
        MediaCapture capture;
        // Windows displays its first-use consent dialog from this STA UI thread.
        settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
        try {
            capture.InitializeAsync(settings).get();
        } catch (hresult_error const&) {
            capture.Close();
            capture = MediaCapture();
            settings.SharingMode(MediaCaptureSharingMode::SharedReadOnly);
            capture.InitializeAsync(settings).get();
        }
        auto source = FindInfraredSource(capture);
        if (!source) {
            // Certain Windows Hello stacks enumerate their IR source group but
            // leave FrameSources empty. Retry with the physical IR device ID;
            // never substitute the ordinary colour webcam.
            capture.Close();
            auto deviceSettings = MediaCaptureInitializationSettings();
            deviceSettings.VideoDeviceId(FindInfraredDeviceId(infraredGroup));
            deviceSettings.StreamingCaptureMode(StreamingCaptureMode::Video);
            capture = MediaCapture();
            deviceSettings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
            try {
                capture.InitializeAsync(deviceSettings).get();
            } catch (hresult_error const&) {
                capture.Close();
                capture = MediaCapture();
                deviceSettings.SharingMode(MediaCaptureSharingMode::SharedReadOnly);
                capture.InitializeAsync(deviceSettings).get();
            }
            source = FindInfraredSource(capture);
        }
        if (!source) {
            throw hresult_error(HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
                                L"The Windows Hello IR device exposed no infrared frame source.");
        }
        auto reader = capture.CreateFrameReaderAsync(source).get();
        if (reader.StartAsync().get() != MediaFrameReaderStartStatus::Success) {
            throw hresult_error(E_FAIL, L"The infrared frame reader did not start.");
        }

        SharedFrameWriter writer;
        while (g_running) {
            if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
                break;
            }
            if (auto frame = reader.TryAcquireLatestFrame()) {
                if (auto video = frame.VideoMediaFrame()) {
                    auto bitmap = video.SoftwareBitmap();
                    if (!bitmap && video.Direct3DSurface()) {
                        bitmap = SoftwareBitmap::CreateCopyFromSurfaceAsync(
                            video.Direct3DSurface()).get();
                    }
                    if (bitmap) {
                        writer.Publish(bitmap);
                    }
                }
            }
            Sleep(10);
        }

        reader.StopAsync().get();
        capture.Close();
    } catch (hresult_error const& error) {
        ShowCameraAccessError(error);
    } catch (...) {
        MessageBoxW(nullptr, L"UmbraCam stopped unexpectedly.", L"UmbraCam",
                    MB_OK | MB_ICONERROR);
    }

    if (stopEvent) {
        SetEvent(stopEvent);
        CloseHandle(stopEvent);
    }
    return 0;
}
