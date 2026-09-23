// umbracam: a consent-owning desktop companion for a Windows Hello IR camera.
//
// The Windows Frame Server hosts the virtual-camera DLL in a service process,
// which cannot display privacy prompts. This process therefore owns
// MediaCapture initialization (and its first-run consent flow), then publishes
// the IR frames over the shared-memory protocol consumed by that DLL.

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

#include "ir_camera_protocol.h"

using namespace winrt;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Capture;
using namespace Windows::Media::Capture::Frames;

namespace {

std::atomic_bool g_running{true};

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

PROCESS_INFORMATION StartVirtualCameraHost() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    wchar_t* fileName = wcsrchr(path, L'\\');
    if (!fileName) {
        throw hresult_error(E_FAIL, L"Unable to locate the bridge executable.");
    }
    ++fileName;
    wcscpy_s(fileName, MAX_PATH - static_cast<size_t>(fileName - path), L"umbracam-host.exe");

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path, nullptr, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
        throw hresult_error(HRESULT_FROM_WIN32(GetLastError()), L"Could not start umbracam-host.exe.");
    }
    CloseHandle(process.hThread);
    return process;
}

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
    for (const auto& item : capture.FrameSources()) {
        if (item.Value().Info().SourceKind() == MediaFrameSourceKind::Infrared) {
            return item.Value();
        }
    }
    return nullptr;
}

void ShowCameraAccessError(hresult_error const& error) {
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

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int) {
    if (commandLine && wcscmp(commandLine, L"--stop") == 0) {
        HANDLE stopEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, IRCAM_STOP_EVENT_NAME);
        if (stopEvent) {
            SetEvent(stopEvent);
            CloseHandle(stopEvent);
        }
        return 0;
    }

    SetConsoleCtrlHandler(OnConsoleControl, TRUE);
    PROCESS_INFORMATION host{};
    HANDLE stopEvent = nullptr;

    try {
        init_apartment(apartment_type::single_threaded);
        host = StartVirtualCameraHost();
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, IRCAM_STOP_EVENT_NAME);

        const auto infraredGroup = FindInfraredGroup();
        auto settings = MediaCaptureInitializationSettings();
        settings.SourceGroup(infraredGroup);
        settings.StreamingCaptureMode(StreamingCaptureMode::Video);
        settings.MemoryPreference(MediaCaptureMemoryPreference::Cpu);
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
            deviceSettings.MemoryPreference(MediaCaptureMemoryPreference::Cpu);
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
            if (auto frame = reader.TryAcquireLatestFrame()) {
                if (auto video = frame.VideoMediaFrame()) {
                    if (auto bitmap = video.SoftwareBitmap()) {
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
    if (host.hProcess) {
        WaitForSingleObject(host.hProcess, 5'000);
        CloseHandle(host.hProcess);
    }
    return 0;
}
