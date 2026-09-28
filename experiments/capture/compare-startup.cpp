// Opt-in, machine-specific RGB/IR coordination. Statistics only; no control SETs.
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
#include "../../native/src/probe/frame-statistics.h"

using namespace winrt;
using namespace Windows::Media::Capture;
using namespace Windows::Media::Capture::Frames;
using namespace Windows::Media::Devices;
using namespace Windows::Graphics::Imaging;
static constexpr auto FaceAuthId = L"{81361B22-700B-4546-A2D4-C52E907BFC27},0";

static bool Matches(const hstring& id, const wchar_t* needle) {
    std::wstring value(id);
    for (auto& c : value) c = wchar_t(towlower(c));
    return value.find(needle) != std::wstring::npos;
}

static MediaFrameSourceGroup PhysicalGroup(bool infrared) {
    const wchar_t* needle = infrared ? L"vid_3277&pid_0060&mi_02" : L"vid_3277&pid_0060&mi_00";
    MediaFrameSourceGroup selected{nullptr};
    unsigned matches = 0;
    for (const auto& group : MediaFrameSourceGroup::FindAllAsync().get()) {
        if (!Matches(group.Id(), needle)) continue; // Never select the aggregate or a virtual camera.
        bool valid = group.SourceInfos().Size() > 0;
        for (const auto& info : group.SourceInfos()) {
            auto device = info.DeviceInformation();
            valid &= device && Matches(device.Id(), needle) && info.SourceKind() ==
                (infrared ? MediaFrameSourceKind::Infrared : MediaFrameSourceKind::Color);
        }
        if (valid) { selected = group; ++matches; }
    }
    if (matches != 1) throw hresult_error(E_FAIL, L"Need exactly one physical group for the requested interface");
    return selected;
}

static void OpenFaceAuthGroup(MediaCapture& capture) {
    MediaFrameSourceGroup selected{nullptr};
    unsigned matches = 0;
    for (const auto& group : MediaFrameSourceGroup::FindAllAsync().get()) {
        bool rgb = false, ir = false, unrelated = false;
        for (const auto& info : group.SourceInfos()) {
            const auto device = info.DeviceInformation();
            const bool isRgb = device && Matches(device.Id(), L"vid_3277&pid_0060&mi_00") && info.SourceKind() == MediaFrameSourceKind::Color;
            const bool isIr = device && Matches(device.Id(), L"vid_3277&pid_0060&mi_02") && info.SourceKind() == MediaFrameSourceKind::Infrared;
            rgb |= isRgb; ir |= isIr; unrelated |= !isRgb && !isIr;
        }
        if (rgb && ir && !unrelated) { selected = group; ++matches; }
    }
    if (matches != 1) throw hresult_error(E_FAIL, L"Need exactly one group containing only the target RGB and IR interfaces");
    if (!MediaCapture::IsVideoProfileSupported(selected.Id())) throw hresult_error(E_FAIL, L"Group profiles unsupported");
    MediaCaptureVideoProfile profile{nullptr};
    for (const auto& candidate : MediaCapture::FindAllVideoProfiles(selected.Id()))
        if (!_wcsicmp(candidate.Id().c_str(), FaceAuthId)) profile = candidate;
    if (!profile || profile.SupportedRecordMediaDescription().Size() != 1)
        throw hresult_error(E_FAIL, L"Expected group FaceAuth profile/description unavailable");
    MediaCaptureInitializationSettings settings;
    settings.SourceGroup(selected);
    settings.StreamingCaptureMode(StreamingCaptureMode::Video);
    settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
    settings.MemoryPreference(MediaCaptureMemoryPreference::Cpu);
    settings.VideoProfile(profile);
    settings.RecordMediaDescription(profile.SupportedRecordMediaDescription().GetAt(0));
    wprintf(L"OPEN_GROUP name=%s id=%s requestedProfile=%s\n", selected.DisplayName().c_str(), selected.Id().c_str(), profile.Id().c_str());
    // Assign before InitializeAsync so the caller can close on partial failure.
    capture = MediaCapture();
    capture.InitializeAsync(settings).get();
}

class Stream {
    bool ir, startAttempted = false, ownsCapture = true;
    MediaCapture capture{nullptr};
    MediaFrameReader reader{nullptr};
    CameraOcclusionInfo occlusion{nullptr};
    int64_t timestamp = -1;
    ULONGLONG lastFrame = 0;
public:
    unsigned frames = 0, flat = 0, gray144 = 0;
    FrameStatistics last{};
    explicit Stream(bool infrared) : ir(infrared) {}
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    ~Stream() { Close(); }
    const wchar_t* Name() const { return ir ? L"IR" : L"RGB"; }
    bool Active() const { return bool(reader); }
    void Open(bool faceauth, MediaCapture groupOwner = nullptr) {
        if (groupOwner) {
            capture = groupOwner; ownsCapture = false;
            wprintf(L"OPEN stream=%s sharedGroup=1 profile=FaceAuth\n", Name());
        } else {
            const auto group = PhysicalGroup(ir);
            MediaCaptureInitializationSettings settings;
            settings.SourceGroup(group);
            settings.StreamingCaptureMode(StreamingCaptureMode::Video);
            settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
            settings.MemoryPreference(MediaCaptureMemoryPreference::Cpu);
            if (faceauth) {
                if (!MediaCapture::IsVideoProfileSupported(group.Id())) throw hresult_error(E_FAIL, L"Profiles unsupported");
                MediaCaptureVideoProfile selected{nullptr};
                for (const auto& p : MediaCapture::FindAllVideoProfiles(group.Id()))
                    if (!_wcsicmp(p.Id().c_str(), FaceAuthId)) selected = p;
                if (!selected) throw hresult_error(E_FAIL, L"FaceAuth profile not advertised; refusing to invent one");
                const auto descriptions = selected.SupportedRecordMediaDescription();
                if (descriptions.Size() != 1) throw hresult_error(E_FAIL, L"Expected one FaceAuth record description");
                settings.VideoProfile(selected);
                settings.RecordMediaDescription(descriptions.GetAt(0));
                wprintf(L"REQUEST stream=%s profile=%s width=%u height=%u fps=%.3f\n", Name(), selected.Id().c_str(),
                    descriptions.GetAt(0).Width(), descriptions.GetAt(0).Height(), descriptions.GetAt(0).FrameRate());
            }
            wprintf(L"OPEN stream=%s group=%s profile=%s\n", Name(), group.Id().c_str(), faceauth ? L"FaceAuth" : L"default");
            capture = MediaCapture();
            capture.InitializeAsync(settings).get();
        }
        MediaFrameSource source{nullptr};
        for (const auto& pair : capture.FrameSources()) {
            const auto candidate = pair.Value();
            if (candidate.Info().SourceKind() == (ir ? MediaFrameSourceKind::Infrared : MediaFrameSourceKind::Color)) {
                if (source) throw hresult_error(E_FAIL, L"Ambiguous frame source");
                source = candidate;
            }
        }
        if (!source) throw hresult_error(E_FAIL, L"Physical group exposed no requested source");
        if (!faceauth) {
            MediaFrameFormat selected{nullptr};
            for (const auto& format : source.SupportedFormats()) {
                const auto video = format.VideoFormat();
                const auto rate = format.FrameRate();
                const auto subtype = format.Subtype();
                const bool typeOk = ir ? (!_wcsicmp(subtype.c_str(), L"L8") || !_wcsicmp(subtype.c_str(), L"Gray8")) :
                    (!_wcsicmp(subtype.c_str(), L"NV12") || !_wcsicmp(subtype.c_str(), L"YUY2"));
                if (video && video.Width() == 640 && video.Height() == (ir ? 400u : 480u) &&
                    rate.Denominator() && rate.Numerator() == 15 * rate.Denominator() && typeOk) {
                    selected = format; break;
                }
            }
            if (!selected) throw hresult_error(E_FAIL, L"Expected bounded native 15fps format unavailable");
            source.SetFormatAsync(selected).get();
        }
        const auto format = source.CurrentFormat();
        const auto video = format.VideoFormat();
        wprintf(L"FORMAT stream=%s subtype=%s width=%u height=%u rate=%u/%u\n", Name(), format.Subtype().c_str(),
            video.Width(), video.Height(), format.FrameRate().Numerator(), format.FrameRate().Denominator());
        if (ir && (video.Width() != 640 || video.Height() != 400)) throw hresult_error(E_FAIL, L"Unexpected IR geometry");
        try {
            occlusion = capture.VideoDeviceController().CameraOcclusionInfo();
            wprintf(L"OCCLUSION_SUPPORT stream=%s hardware=%d\n", Name(), occlusion ? int(occlusion.IsOcclusionKindSupported(CameraOcclusionKind::CameraHardware)) : -1);
        } catch (const hresult_error& e) { wprintf(L"OCCLUSION_UNAVAILABLE stream=%s hr=0x%08x\n", Name(), unsigned(e.code())); }
        reader = capture.CreateFrameReaderAsync(source).get();
        startAttempted = true;
        const auto status = reader.StartAsync().get();
        if (status != MediaFrameReaderStartStatus::Success) throw hresult_error(E_FAIL, L"Frame reader start failed");
        lastFrame = GetTickCount64();
        wprintf(L"STARTED stream=%s\n", Name());
    }
    void Poll() {
        if (!reader) return;
        auto frame = reader.TryAcquireLatestFrame();
        if (frame) {
            // Close the frame even if pixel validation throws.
            try {
                const auto time = frame.SystemRelativeTime();
                if (time && time.Value().count() != timestamp) {
                    timestamp = time.Value().count();
                    if (ir) {
                        const auto video = frame.VideoMediaFrame();
                        const auto bitmap = video ? video.SoftwareBitmap() : SoftwareBitmap{nullptr};
                        if (!bitmap || bitmap.BitmapPixelFormat() != BitmapPixelFormat::Gray8)
                            throw hresult_error(E_FAIL, L"Expected CPU Gray8 IR frame; refusing conversion");
                        auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Read);
                        const auto plane = buffer.GetPlaneDescription(0);
                        auto reference = buffer.CreateReference();
                        uint8_t* bytes = nullptr; uint32_t capacity = 0;
                        check_hresult(reference.as<winrt::impl::IMemoryBufferByteAccess>()->GetBuffer(&bytes, &capacity));
                        if (plane.StartIndex < 0 || unsigned(plane.StartIndex) > capacity || plane.Stride < 0)
                            throw hresult_error(E_FAIL, L"Invalid bitmap plane");
                        last = AnalyzeGray8(bytes + plane.StartIndex, capacity - plane.StartIndex,
                            bitmap.PixelWidth(), bitmap.PixelHeight(), plane.Stride);
                        if (last.minimum == last.maximum) { ++flat; if (last.minimum == 144) ++gray144; }
                    }
                    ++frames; lastFrame = GetTickCount64();
                }
                frame.Close();
            } catch (...) { frame.Close(); throw; }
        }
        if (GetTickCount64() - lastFrame > 5000) throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"No fresh camera frames for five seconds");
    }
    void Report(double elapsed, const wchar_t* phase) {
        if (!reader) return;
        wprintf(L"FRAME phase=%s stream=%s t=%.2f frames=%u flat=%u gray144=%u min=%u max=%u sd=%.3f\n",
            phase, Name(), elapsed, frames, flat, gray144, ir ? last.minimum : 0, ir ? last.maximum : 0, ir ? last.deviation : 0);
        if (occlusion) {
            try {
                const auto state = occlusion.GetState();
                wprintf(L"SHUTTER phase=%s stream=%s occluded=%u hardware=%u\n", phase, Name(),
                    unsigned(state.IsOccluded()), unsigned(state.IsOcclusionKind(CameraOcclusionKind::CameraHardware)));
            } catch (const hresult_error& e) { wprintf(L"SHUTTER_QUERY_FAILED stream=%s hr=0x%08x\n", Name(), unsigned(e.code())); }
        }
    }
    bool Close() noexcept {
        bool ok = true;
        if (reader) {
            if (startAttempted) {
                try { reader.StopAsync().get(); wprintf(L"STOP stream=%s ok=1\n", Name()); }
                catch (...) { ok = false; wprintf(L"STOP stream=%s ok=0\n", Name()); }
            }
            try { reader.Close(); } catch (...) { ok = false; }
            reader = nullptr; startAttempted = false;
        }
        occlusion = nullptr;
        if (capture) {
            if (ownsCapture) {
                try { capture.Close(); wprintf(L"CLOSE stream=%s ok=1\n", Name()); }
                catch (...) { ok = false; wprintf(L"CLOSE stream=%s ok=0\n", Name()); }
            } else { wprintf(L"DETACH stream=%s groupOwnerClosesAfterBothReaders=1\n", Name()); }
            capture = nullptr;
        }
        return ok;
    }
};

static void Measure(Stream& ir, Stream& rgb, const wchar_t* phase, unsigned seconds) {
    const auto start = GetTickCount64();
    ULONGLONG report = 0;
    const unsigned irStart = ir.frames, rgbStart = rgb.frames, flatStart = ir.flat, grayStart = ir.gray144;
    while (GetTickCount64() - start < seconds * 1000ULL) {
        ir.Poll(); rgb.Poll();
        const auto elapsed = GetTickCount64() - start;
        if (elapsed >= report) {
            ir.Report(elapsed / 1000.0, phase); rgb.Report(elapsed / 1000.0, phase);
            report = elapsed + 1000;
        }
        Sleep(2);
    }
    wprintf(L"SUMMARY phase=%s seconds=%u irFrames=%u rgbFrames=%u irFlat=%u irGray144=%u irNonuniform=%u\n",
        phase, seconds, ir.frames-irStart, rgb.frames-rgbStart, ir.flat-flatStart,
        ir.gray144-grayStart, ir.frames-irStart-(ir.flat-flatStart));
    if ((ir.Active() && ir.frames == irStart) || (rgb.Active() && rgb.frames == rgbStart))
        throw hresult_error(E_FAIL, L"Phase produced no frames on an active stream");
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const std::wstring mode(argv[1]);
    if (mode != L"--ir-only" && mode != L"--rgb-first" && mode != L"--ir-first" &&
        mode != L"--rgb-release" && mode != L"--faceauth-ir" && mode != L"--faceauth-pair" && mode != L"--faceauth-group") return 2;
    if (!*argv[2]) return 2;
    for (auto p = argv[2]; *p; ++p) if (*p < L'0' || *p > L'9') return 2;
    wchar_t* end = nullptr;
    const auto seconds = wcstoul(argv[2], &end, 10);
    if (*end || seconds < 1 || seconds > 60) return 2;
    setvbuf(stdout, nullptr, _IONBF, 0);
    init_apartment(apartment_type::multi_threaded);
    int result = 0;
    {
        Stream ir(true), rgb(false);
        MediaCapture groupOwner{nullptr};
        try {
            const bool faceauth = mode == L"--faceauth-ir" || mode == L"--faceauth-pair";
            wprintf(L"RUN mode=%s seconds=%lu framesSaved=0 explicitControlSets=0\n", mode.c_str(), seconds);
            if (mode == L"--faceauth-group") {
                OpenFaceAuthGroup(groupOwner);
                rgb.Open(true, groupOwner); ir.Open(true, groupOwner);
            } else if (mode == L"--ir-only" || mode == L"--faceauth-ir") {
                ir.Open(faceauth);
            } else if (mode == L"--ir-first") {
                ir.Open(false); Measure(ir, rgb, L"ir-warmup", 8); rgb.Open(false);
            } else {
                rgb.Open(faceauth); Measure(ir, rgb, L"rgb-warmup", 3); ir.Open(faceauth);
            }
            if (mode == L"--rgb-release") {
                Measure(ir, rgb, L"both-before-release", 8);
                if (!rgb.Close()) throw hresult_error(E_FAIL, L"RGB cleanup failed");
            }
            Measure(ir, rgb, mode.c_str(), unsigned(seconds));
        } catch (const hresult_error& e) {
            wprintf(L"ERROR hr=0x%08x message=%s\n", unsigned(e.code()), e.message().c_str()); result = 1;
        } catch (const std::exception& e) { printf("ERROR %s\n", e.what()); result = 1; }
        const bool rgbClosed = rgb.Close(), irClosed = ir.Close();
        if (!rgbClosed || !irClosed) result = 3;
        if (groupOwner) {
            try { groupOwner.Close(); wprintf(L"CLOSE_GROUP ok=1\n"); }
            catch (...) { wprintf(L"CLOSE_GROUP ok=0\n"); result = 3; }
            groupOwner = nullptr;
        }
    }
    uninit_apartment();
    wprintf(L"EXIT code=%d\n", result);
    return result;
}
