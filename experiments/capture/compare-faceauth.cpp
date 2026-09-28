// Machine-specific, opt-in diagnostic. No frames saved; only documented FaceAuth mode SETs.
#include "../../native/src/probe/privacy-native.cpp"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

class ReaderCallback final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFSourceReaderCallback> {
public:
    winrt::handle ready{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::mutex mutex;
    ComPtr<IMFSample> sample;
    HRESULT result = S_OK;
    DWORD flags = 0;
    HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT hr, DWORD, DWORD streamFlags, LONGLONG, IMFSample* incoming) override {
        std::lock_guard lock(mutex);
        result = hr; flags = streamFlags; sample = incoming;
        SetEvent(ready.get()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnFlush(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
    ComPtr<IMFSample> Read(IMFSourceReader* reader) {
        Check(reader->ReadSample(0, 0, nullptr, nullptr, nullptr, nullptr), L"ReadSample request");
        if (WaitForSingleObject(ready.get(), 5000) != WAIT_OBJECT_0)
            throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"Frame timeout");
        std::lock_guard lock(mutex);
        Check(result, L"ReadSample completion");
        if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))
            throw winrt::hresult_error(E_FAIL, L"Unexpected stream state/type change");
        auto captured = sample; sample.Reset(); return captured;
    }
};

static ComPtr<IMFExtendedCameraControl> ReadMode(IMFExtendedCameraController* controller, const wchar_t* stage) {
    ComPtr<IMFExtendedCameraControl> value;
    Check(controller->GetExtendedCameraControl(0, KSPROPERTY_CAMERACONTROL_EXTENDED_FACEAUTH_MODE, &value), L"Get FaceAuth");
    wprintf(L"MODE stage=%s flags=0x%llx capabilities=0x%llx\n", stage, value->GetFlags(), value->GetCapabilities());
    fflush(stdout); return value;
}
static void SetMode(IMFExtendedCameraController* controller, ULONGLONG mode, const wchar_t* stage) {
    auto value = ReadMode(controller, stage);
    if (!(value->GetCapabilities() & mode)) throw winrt::hresult_error(E_FAIL, L"Mode not advertised");
    Check(value->SetFlags(mode), L"SetFlags FaceAuth");
    Check(value->CommitSettings(), L"Commit FaceAuth");
    if (ReadMode(controller, stage)->GetFlags() != mode) throw winrt::hresult_error(E_FAIL, L"Mode readback mismatch");
}
static void Measure(IMFSourceReader* reader, ReaderCallback* callback, IMFExtendedCameraController* controller,
                    IKsControl* privacy, UINT32 width, UINT32 height, LONG stride, const wchar_t* phase,
                    ULONGLONG durationMs = 8000) {
    const auto start = GetTickCount64();
    ULONGLONG report = 0;
    unsigned frames = 0, flat = 0, gray144 = 0;
    while (GetTickCount64() - start < durationMs) {
        auto sample = callback->Read(reader);
        if (!sample) continue;
        ComPtr<IMFMediaBuffer> buffer;
        Check(sample->ConvertToContiguousBuffer(&buffer), L"Sample buffer");
        BYTE* bytes = nullptr; DWORD length = 0;
        Check(buffer->Lock(&bytes, nullptr, &length), L"Lock buffer");
        FrameStatistics stats{};
        try { stats = AnalyzeGray8(bytes, length, width, height, size_t(stride)); }
        catch (...) { buffer->Unlock(); throw; }
        Check(buffer->Unlock(), L"Unlock buffer");
        ++frames;
        if (stats.minimum == stats.maximum) { ++flat; if (stats.minimum == 144) ++gray144; }
        const auto elapsed = GetTickCount64() - start;
        if (elapsed >= report) {
            wprintf(L"FRAME phase=%s t=%.2f frames=%u flat=%u min=%u max=%u mean=%.3f sd=%.3f\n",
                phase, elapsed/1000.0, frames, flat, stats.minimum, stats.maximum, stats.mean, stats.deviation);
            ReadMode(controller, phase);
            PrintPrivacy(privacy, elapsed/1000.0);
            fflush(stdout); report = elapsed + 1000;
        }
    }
    wprintf(L"SUMMARY phase=%s frames=%u flat=%u gray144=%u nonuniform=%u\n", phase, frames, flat, gray144, frames-flat);
    fflush(stdout);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || (wcscmp(argv[1], L"--compare") && wcscmp(argv[1], L"--baseline") && wcscmp(argv[1], L"--prestart") && wcscmp(argv[1], L"--query") && wcscmp(argv[1], L"--restore-general"))) return 2;
    const bool prestart = !wcscmp(argv[1], L"--prestart");
    setvbuf(stdout, nullptr, _IONBF, 0);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFExtendedCameraController> controller;
    ComPtr<IMFSourceReader> reader;
    ULONGLONG original = 0;
    bool restoreNeeded = false;
    bool mfStarted = false;
    int result = 0;
    try {
        Check(MFStartup(MF_VERSION), L"MFStartup"); mfStarted = true;
        ComPtr<IMFAttributes> attrs;
        Check(MFCreateAttributes(&attrs, 3), L"Source attributes");
        Check(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID), L"Source type");
        auto path = FindPath(L"vid_3277&pid_0060&mi_02");
        Check(attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, path.c_str()), L"IR path");
        Check(MFCreateDeviceSource(attrs.Get(), &source), L"Open IR");
        ComPtr<IMFGetService> service;
        Check(source.As(&service), L"GetService interface");
        Check(service->GetService(GUID_NULL, IID_PPV_ARGS(&controller)), L"Extended controller");
        auto initial = ReadMode(controller.Get(), L"initial"); original = initial->GetFlags();
        if (original != 1 && original != 2) throw winrt::hresult_error(E_FAIL, L"Unexpected initial mode; refusing test");
        if (!wcscmp(argv[1], L"--restore-general")) {
            SetMode(controller.Get(), 1, L"recovery");
        } else if (!wcscmp(argv[1], L"--compare") || !wcscmp(argv[1], L"--baseline") || prestart) {
            if ((initial->GetCapabilities() & 3) != 3) throw winrt::hresult_error(E_FAIL, L"Both test modes must be advertised");
            // Arm cleanup before any mutation, including partial SET failures.
            restoreNeeded = true;
            if (prestart) SetMode(controller.Get(), 2, L"before-reader-creation");
            auto callback = Microsoft::WRL::Make<ReaderCallback>();
            if (!callback || !callback->ready) throw winrt::hresult_error(E_OUTOFMEMORY);
            ComPtr<IMFAttributes> readerAttrs;
            Check(MFCreateAttributes(&readerAttrs, 2), L"Reader attributes");
            Check(readerAttrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.Get()), L"Reader callback");
            Check(readerAttrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE), L"Disable converters");
            Check(MFCreateSourceReaderFromMediaSource(source.Get(), readerAttrs.Get(), &reader), L"IR reader");
            ReadMode(controller.Get(), L"after-reader-creation");
            Check(reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE), L"Deselect streams");
            ComPtr<IMFMediaType> selected;
            UINT32 width = 0, height = 0;
            for (DWORD index = 0; index < 128; ++index) {
                ComPtr<IMFMediaType> type;
                auto hr = reader->GetNativeMediaType(0, index, &type);
                if (hr == MF_E_NO_MORE_TYPES) break;
                Check(hr, L"Native IR format");
                GUID subtype{}; Check(type->GetGUID(MF_MT_SUBTYPE, &subtype), L"Subtype");
                Check(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height), L"Frame size");
                if (IsGraySubtype(subtype) && width == 640 && height == 400) { selected = type; break; }
            }
            if (!selected) throw winrt::hresult_error(E_FAIL, L"Expected native Gray8 format not found");
            UINT32 rawStride = width;
            selected->GetUINT32(MF_MT_DEFAULT_STRIDE, &rawStride);
            const LONG stride = LONG(rawStride);
            if (stride < LONG(width) || stride > 8192) throw winrt::hresult_error(E_FAIL, L"Unexpected native stride");
            Check(reader->SetCurrentMediaType(0, nullptr, selected.Get()), L"Select native IR format");
            ReadMode(controller.Get(), L"after-format-selection");
            Check(reader->SetStreamSelection(0, TRUE), L"Select IR stream");
            ReadMode(controller.Get(), L"before-first-read");
            ComPtr<IKsControl> privacy; Check(source.As(&privacy), L"Privacy interface");
            wprintf(L"CAPTURE native=Gray8 width=%u height=%u stride=%ld seconds=%u prestart=%u\n",
                width, height, stride, prestart ? 60u : 8u, unsigned(prestart));
            Measure(reader.Get(), callback.Get(), controller.Get(), privacy.Get(), width, height, stride,
                prestart ? L"prestart-alternating" : L"baseline", prestart ? 60000 : 8000);
            if (!wcscmp(argv[1], L"--compare")) {
            SetMode(controller.Get(), 2, L"set-alternating");
            Measure(reader.Get(), callback.Get(), controller.Get(), privacy.Get(), width, height, stride, L"alternating");
            SetMode(controller.Get(), original, L"restore-for-comparison");
            Measure(reader.Get(), callback.Get(), controller.Get(), privacy.Get(), width, height, stride, L"restored");
            }
        }
    } catch (const winrt::hresult_error& e) {
        wprintf(L"ERROR hr=0x%08lx message=%s\n", ULONG(e.code().value), e.message().c_str()); result = 1;
    } catch (const std::exception& e) {
        printf("ERROR %s\n", e.what()); result = 1;
    }
    if (reader) { auto hr = reader->Flush(DWORD(MF_SOURCE_READER_ALL_STREAMS)); wprintf(L"FLUSH hr=0x%08lx\n", hr); }
    if (restoreNeeded && controller) {
        try { SetMode(controller.Get(), original, L"cleanup-restore"); }
        catch (const winrt::hresult_error& e) { wprintf(L"RESTORE_FAILED hr=0x%08lx\n", ULONG(e.code().value)); result = 3; }
    }
    if (source) { auto hr = source->Shutdown(); wprintf(L"SHUTDOWN hr=0x%08lx\n", hr); if (FAILED(hr)) result = 4; }
    reader.Reset(); controller.Reset(); source.Reset();
    if (mfStarted) MFShutdown();
    winrt::uninit_apartment();
    wprintf(L"EXIT code=%d\n", result);
    return result;
}
