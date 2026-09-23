// umbracam-probe: native, non-installing hardware smoke test for the same
// MTA MediaCapture path used by the UmbraCam publisher.

#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Capture.h>
#include <winrt/Windows.Media.Capture.Frames.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.Media.MediaProperties.h>

#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include "ir_frame_policy.h"

using namespace winrt;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::Capture;
using namespace winrt::Windows::Media::Capture::Frames;

namespace {

void AnalyzeFlicker(MediaCapture const& capture, MediaFrameSource const& source) {
    auto controller = capture.VideoDeviceController();
    winrt::Windows::Media::Capture::PowerlineFrequency frequency{};
    try {
        bool supported = controller.TryGetPowerlineFrequency(frequency);
        wprintf(L"Powerline control supported: %d; current: %d\n", supported, static_cast<int>(frequency));
    } catch (hresult_error const& e) {
        wprintf(L"Powerline control unavailable: 0x%08X\n", static_cast<unsigned>(e.code()));
    }
    try {
        auto exposure = controller.ExposureControl();
        wprintf(L"Exposure supported: %d\n", exposure.Supported());
        if (exposure.Supported()) wprintf(L"Exposure auto: %d; value/min/max (100ns): %lld/%lld/%lld\n",
            exposure.Auto(), exposure.Value().count(), exposure.Min().count(), exposure.Max().count());
    } catch (hresult_error const& e) {
        wprintf(L"Exposure unavailable: 0x%08X\n", static_cast<unsigned>(e.code()));
    }
    auto format = source.CurrentFormat();
    wprintf(L"Format: %s, %u/%u fps\n", format.Subtype().c_str(),
            format.FrameRate().Numerator(), format.FrameRate().Denominator());
    auto reader = capture.CreateFrameReaderAsync(source).get();
    if (reader.StartAsync().get() != MediaFrameReaderStartStatus::Success) throw hresult_error(E_FAIL);
    std::vector<double> means;
    std::vector<double> selectedMeans;
    IrFramePolicy policy;
    int illuminated = 0, ambient = 0;
    int64_t lastTime = -1;
    for (int attempt = 0; attempt < 1500 && means.size() < 90; ++attempt) {
        auto frame = reader.TryAcquireLatestFrame();
        if (!frame) { Sleep(10); continue; }
        auto time = frame.SystemRelativeTime();
        if (time && time.Value().count() == lastTime) { frame.Close(); Sleep(10); continue; }
        if (time) lastTime = time.Value().count();
        auto video = frame.VideoMediaFrame();
        auto bitmap = video.SoftwareBitmap();
        if (!bitmap && video.Direct3DSurface()) bitmap = SoftwareBitmap::CreateCopyFromSurfaceAsync(video.Direct3DSurface()).get();
        if (!bitmap) { frame.Close(); continue; }
        auto gray = SoftwareBitmap::Convert(bitmap, BitmapPixelFormat::Gray8);
        auto buffer = gray.LockBuffer(BitmapBufferAccessMode::Read);
        auto plane = buffer.GetPlaneDescription(0);
        auto reference = buffer.CreateReference();
        uint8_t* bytes = nullptr;
        uint32_t capacity = 0;
        check_hresult(reference.as<winrt::impl::IMemoryBufferByteAccess>()->GetBuffer(&bytes, &capacity));
        double sum = 0;
        size_t samples = 0;
        for (int y = 0; y < gray.PixelHeight(); y += 4) {
            for (int x = 0; x < gray.PixelWidth(); x += 4) {
                sum += bytes[plane.StartIndex + y * plane.Stride + x]; ++samples;
            }
        }
        double mean = sum / samples;
        bool lit = video.InfraredMediaFrame() && video.InfraredMediaFrame().IsIlluminated();
        lit ? ++illuminated : ++ambient;
        means.push_back(mean);
        if (policy.Accept(lit, GetTickCount64())) selectedMeans.push_back(mean);
        wprintf(L"Frame %zu: mean=%.3f illuminated=%d time=%lld\n", means.size(), mean, lit, lastTime);
        frame.Close();
        Sleep(10);
    }
    reader.StopAsync().get();
    if (means.size() < 60) throw hresult_error(E_FAIL, L"Too few unique frames for flicker analysis.");
    double adjacent = 0, lag2 = 0;
    for (size_t i = 1; i < means.size(); ++i) adjacent += std::abs(means[i] - means[i - 1]);
    for (size_t i = 2; i < means.size(); ++i) lag2 += std::abs(means[i] - means[i - 2]);
    wprintf(L"Analysis: %zu frames; illuminated=%d ambient=%d; brightness min/max=%.3f/%.3f; mean adjacent delta=%.3f; lag-2 delta=%.3f\n",
        means.size(), illuminated, ambient, *std::min_element(means.begin(), means.end()),
        *std::max_element(means.begin(), means.end()), adjacent / (means.size() - 1), lag2 / (means.size() - 2));
    double selectedDelta = 0;
    for (size_t i = 1; i < selectedMeans.size(); ++i) selectedDelta += std::abs(selectedMeans[i] - selectedMeans[i - 1]);
    if (selectedMeans.size() > 1) wprintf(L"Illuminated-frame policy on SAME input: %zu accepted; mean adjacent brightness delta=%.3f\n",
        selectedMeans.size(), selectedDelta / (selectedMeans.size() - 1));
}

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

int CheckVirtualCameraRegistration() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        fwprintf(stderr, L"Camera enumeration: FAIL (0x%08X)\n", static_cast<unsigned>(hr));
        return 1;
    }
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        CoUninitialize();
        fwprintf(stderr, L"Camera enumeration: FAIL (0x%08X)\n", static_cast<unsigned>(hr));
        return 1;
    }

    Microsoft::WRL::ComPtr<IMFAttributes> attributes;
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    bool found = false;
    if (SUCCEEDED(MFCreateAttributes(&attributes, 1)) &&
        SUCCEEDED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)) &&
        SUCCEEDED(MFEnumDeviceSources(attributes.Get(), &devices, &count))) {
        for (UINT32 index = 0; index < count; ++index) {
            LPWSTR name = nullptr;
            UINT32 length = 0;
            if (SUCCEEDED(devices[index]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length))) {
                wprintf(L"Camera: %s\n", name);
                // Windows appends "Windows Virtual Camera" to the supplied
                // friendly name, so match our stable product-name prefix.
                found |= wcsncmp(name, L"UmbraCam IR Camera", 17) == 0;
                CoTaskMemFree(name);
            }
            devices[index]->Release();
        }
    }
    CoTaskMemFree(devices);
    MFShutdown();
    CoUninitialize();

    wprintf(L"Virtual camera enumeration: %s\n", found ? L"PASS" : L"NOT FOUND");
    return found ? 0 : 1;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && wcscmp(argv[1], L"--check-virtual-camera") == 0) {
        return CheckVirtualCameraRegistration();
    }
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

        if (argc == 2 && wcscmp(argv[1], L"--analyze-flicker") == 0) {
            AnalyzeFlicker(capture, source);
            capture.Close();
            return 0;
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
