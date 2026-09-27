// Lower-level diagnostic paths: KSProxy and direct Kernel Streaming.
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <setupapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <winrt/base.h>
#include <cstdio>
#include <cwctype>
#include <mutex>
#include <string>
#include <vector>
#include "frame-statistics.h"

using Microsoft::WRL::ComPtr;

// qedit.h is no longer shipped in modern SDKs. These are the documented
// Sample Grabber COM interfaces; no third-party filter is installed.
struct __declspec(uuid("0579154A-2B53-4994-B0D0-E773148EFF85")) IGrabberCallback : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample*) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double, BYTE*, long) = 0;
};
struct __declspec(uuid("6B652FFF-11FE-4FCE-92AD-0266B5D7C78F")) IGrabber : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long*, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(IGrabberCallback*, long) = 0;
};
static const CLSID GrabberClass = {0xc1f400a0,0x3f08,0x11d3,{0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
static const CLSID NullRendererClass = {0xc1f400a4,0x3f08,0x11d3,{0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
static const GUID CameraCategory = {0xe5323777,0xf976,0x4f5b,{0x9b,0x55,0xb9,0x46,0x99,0xc4,0x6e,0x44}};
static const GUID SensorCategory = {0x24e552d7,0x6523,0x47f7,{0xa6,0x47,0xd3,0x46,0x5b,0xf1,0xf5,0xca}};
static const GUID CaptureCategory = {0x65e8773d,0x8f56,0x11d0,{0xa3,0xb9,0x00,0xa0,0xc9,0x22,0x31,0x96}};

static void Check(HRESULT hr, const wchar_t* stage) {
    if (FAILED(hr)) throw winrt::hresult_error(hr, stage);
}
static std::wstring GuidText(const GUID& guid) {
    wchar_t value[40]{}; StringFromGUID2(guid, value, 40); return value;
}
static void FreeType(AM_MEDIA_TYPE& type) {
    CoTaskMemFree(type.pbFormat);
    if (type.pUnk) type.pUnk->Release();
    type = {};
}
struct MediaType {
    AM_MEDIA_TYPE value{};
    ~MediaType() { FreeType(value); }
};
static const BITMAPINFOHEADER* BitmapHeader(const AM_MEDIA_TYPE& type) {
    if (type.formattype == FORMAT_VideoInfo && type.cbFormat >= sizeof(VIDEOINFOHEADER))
        return &reinterpret_cast<const VIDEOINFOHEADER*>(type.pbFormat)->bmiHeader;
    if (type.formattype == FORMAT_VideoInfo2 && type.cbFormat >= sizeof(VIDEOINFOHEADER2))
        return &reinterpret_cast<const VIDEOINFOHEADER2*>(type.pbFormat)->bmiHeader;
    return nullptr;
}
static bool IsGraySubtype(const GUID& subtype) {
    if (subtype == KSDATAFORMAT_SUBTYPE_L8_IR) return true;
    const auto code = subtype.Data1;
    const GUID base{code,0,0x0010,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
    return subtype == base && (code == 50 || code == MAKEFOURCC('Y','8','0','0') ||
        code == MAKEFOURCC('Y','8',' ',' ') || code == MAKEFOURCC('G','R','E','Y'));
}
static bool IsGray8(const AM_MEDIA_TYPE& type) {
    const auto* header = BitmapHeader(type);
    // L8 (D3DFMT_L8), Y800, Y8, or GREY, not arbitrary 8-bit compressed data.
    return header && header->biBitCount == 8 && header->biWidth > 0 &&
        header->biWidth <= 8192 && header->biHeight != 0 && header->biHeight >= -8192 && header->biHeight <= 8192 &&
        IsGraySubtype(type.subtype);
}

static std::wstring FindCategoryPath(const std::wstring& match, const GUID& category) {
    HDEVINFO devices = SetupDiGetClassDevsW(&category, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) winrt::throw_last_error();
    std::vector<std::wstring> matches;
    try {
        for (DWORD index = 0;; ++index) {
            SP_DEVICE_INTERFACE_DATA info{sizeof(info)};
            if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &category, index, &info)) {
                if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
                winrt::throw_last_error();
            }
            DWORD size = 0;
            SetupDiGetDeviceInterfaceDetailW(devices, &info, nullptr, 0, &size, nullptr);
            if (!size) winrt::throw_last_error();
            std::vector<BYTE> storage(size);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(devices, &info, detail, size, nullptr, nullptr)) winrt::throw_last_error();
            std::wstring path = detail->DevicePath, lower = path;
            for (auto& ch : lower) ch = wchar_t(towlower(ch));
            if (lower.find(match) != std::wstring::npos) matches.push_back(path);
        }
    } catch (...) { SetupDiDestroyDeviceInfoList(devices); throw; }
    SetupDiDestroyDeviceInfoList(devices);
    wprintf(L"INTERFACE category=%s matches=%zu\n", GuidText(category).c_str(), matches.size());
    if (matches.size() > 1) throw winrt::hresult_error(E_FAIL, L"Ambiguous camera interface match.");
    return matches.empty() ? std::wstring{} : matches.front();
}
static std::wstring FindPath(const std::wstring& match) {
    // Hello sensors need not advertise the ordinary webcam category.
    for (const auto& category : {CaptureCategory, SensorCategory, CameraCategory}) {
        auto path = FindCategoryPath(match, category);
        if (!path.empty()) return path;
    }
    throw winrt::hresult_error(E_FAIL, L"No matching present KS camera interface.");
}

static DWORD KernelIoctl(HANDLE device, DWORD code, void* input, DWORD inputSize,
                         void* output, DWORD outputSize, DWORD& returned) {
    winrt::handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event) winrt::throw_last_error();
    OVERLAPPED operation{}; operation.hEvent = event.get();
    if (DeviceIoControl(device, code, input, inputSize, output, outputSize, &returned, &operation)) return ERROR_SUCCESS;
    DWORD error = GetLastError();
    if (error != ERROR_IO_PENDING) return error;
    if (WaitForSingleObject(event.get(), 3000) != WAIT_OBJECT_0) {
        CancelIoEx(device, &operation);
        // Keep OVERLAPPED and its buffers alive until cancellation completes.
        // The process runner provides a second, independent watchdog.
        GetOverlappedResult(device, &operation, &returned, TRUE);
        return ERROR_TIMEOUT;
    }
    return GetOverlappedResult(device, &operation, &returned, FALSE) ? ERROR_SUCCESS : GetLastError();
}

static winrt::handle OpenKernelCamera(const std::wstring& match) {
    const auto path = FindPath(match);
    wprintf(L"KERNEL device=%s\n", path.c_str());
    HANDLE value = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (value == INVALID_HANDLE_VALUE)
        throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()), L"Open kernel camera interface (no permission bypass attempted)");
    return winrt::handle{value};
}

static void KernelPrivacy(HANDLE filter, double elapsed, bool streaming) {
    KSPROPERTY_CAMERACONTROL_S request{}, result{};
    request.Property.Set = PROPSETID_VIDCAP_CAMERACONTROL;
    request.Property.Id = KSPROPERTY_CAMERACONTROL_PRIVACY;
    request.Property.Flags = KSPROPERTY_TYPE_GET;
    DWORD returned = 0;
    const auto error = KernelIoctl(filter, IOCTL_KS_PROPERTY, &request, sizeof(request), &result, sizeof(result), returned);
    wprintf(L"KERNEL_PRIVACY t=%.2f streaming=%d error=%lu bytes=%lu value=%ld (value valid only on success)\n",
        elapsed, streaming, error, returned, result.Value);
}

// A read-only property probe below KSProxy. No stream is started in this mode,
// so a successful privacy value is not authoritative for active capture.
int RunKernelProperties(const std::wstring& match) {
    auto filter = OpenKernelCamera(match);
    KernelPrivacy(filter.get(), 0, false);
    KSPROPERTY pins{KSPROPSETID_Pin, KSPROPERTY_PIN_CTYPES, KSPROPERTY_TYPE_GET};
    ULONG count = 0; DWORD returned = 0;
    const auto error = KernelIoctl(filter.get(), IOCTL_KS_PROPERTY, &pins, sizeof(pins), &count, sizeof(count), returned);
    wprintf(L"KERNEL_PINS error=%lu count=%lu bytes=%lu\n", error, count, returned);
    return error == ERROR_SUCCESS ? 0 : 1;
}

int RunKernelCapture(const std::wstring& match, unsigned seconds) {
    auto filter = OpenKernelCamera(match);
    KSPROPERTY pinCount{KSPROPSETID_Pin, KSPROPERTY_PIN_CTYPES, KSPROPERTY_TYPE_GET};
    ULONG count = 0; DWORD returned = 0;
    Check(HRESULT_FROM_WIN32(KernelIoctl(filter.get(), IOCTL_KS_PROPERTY, &pinCount, sizeof(pinCount), &count, sizeof(count), returned)), L"Kernel pin count");
    if (returned != sizeof(count) || count > 64) throw winrt::hresult_error(E_FAIL, L"Invalid kernel pin count.");
    std::vector<BYTE> chosenFormat;
    ULONG chosenPin = 0;
    for (ULONG id = 0; id < count && chosenFormat.empty(); ++id) {
        KSP_PIN request{};
        request.Property = {KSPROPSETID_Pin, KSPROPERTY_PIN_DATAFLOW, KSPROPERTY_TYPE_GET};
        request.PinId = id;
        KSPIN_DATAFLOW flow{};
        auto error = KernelIoctl(filter.get(), IOCTL_KS_PROPERTY, &request, sizeof(request), &flow, sizeof(flow), returned);
        wprintf(L"PIN id=%lu dataflow=%d error=%lu\n", id, int(flow), error);
        if (error || returned != sizeof(flow) || flow != KSPIN_DATAFLOW_OUT) continue;
        request.Property.Id = KSPROPERTY_PIN_DATARANGES;
        std::vector<BYTE> ranges(65536);
        error = KernelIoctl(filter.get(), IOCTL_KS_PROPERTY, &request, sizeof(request), ranges.data(), DWORD(ranges.size()), returned);
        if (error || returned < sizeof(KSMULTIPLE_ITEM) || returned > ranges.size()) {
            wprintf(L"PIN ranges unavailable error=%lu bytes=%lu\n", error, returned); continue;
        }
        const auto* items = reinterpret_cast<KSMULTIPLE_ITEM*>(ranges.data());
        size_t offset = sizeof(KSMULTIPLE_ITEM);
        for (ULONG item = 0; item < items->Count && offset <= returned && returned - offset >= sizeof(KSDATARANGE); ++item) {
            auto* range = reinterpret_cast<KSDATARANGE*>(ranges.data() + offset);
            if (range->FormatSize < sizeof(KSDATARANGE) || range->FormatSize > returned - offset) break;
            wprintf(L"RANGE pin=%lu subtype=%s specifier=%s size=%lu\n", id,
                GuidText(range->SubFormat).c_str(), GuidText(range->Specifier).c_str(), range->FormatSize);
            if (range->MajorFormat == KSDATAFORMAT_TYPE_VIDEO && IsGraySubtype(range->SubFormat)) {
                const size_t requestSize = sizeof(KSP_PIN) + sizeof(KSMULTIPLE_ITEM) + range->FormatSize;
                std::vector<BYTE> intersection(requestSize);
                auto* property = reinterpret_cast<KSP_PIN*>(intersection.data());
                property->Property = {KSPROPSETID_Pin, KSPROPERTY_PIN_DATAINTERSECTION, KSPROPERTY_TYPE_GET};
                property->PinId = id;
                auto* list = reinterpret_cast<KSMULTIPLE_ITEM*>(intersection.data() + sizeof(KSP_PIN));
                list->Size = DWORD(sizeof(KSMULTIPLE_ITEM) + range->FormatSize); list->Count = 1;
                memcpy(intersection.data() + sizeof(KSP_PIN) + sizeof(KSMULTIPLE_ITEM), range, range->FormatSize);
                std::vector<BYTE> format(65536); DWORD formatBytes = 0;
                const auto formatError = KernelIoctl(filter.get(), IOCTL_KS_PROPERTY, intersection.data(), DWORD(intersection.size()), format.data(), DWORD(format.size()), formatBytes);
                wprintf(L"INTERSECTION error=%lu bytes=%lu\n", formatError, formatBytes);
                if (!formatError && formatBytes >= sizeof(KSDATAFORMAT) && formatBytes <= format.size()) {
                    format.resize(formatBytes); chosenFormat = std::move(format); chosenPin = id; break;
                }
            }
            offset += (size_t(range->FormatSize) + 7) & ~size_t(7);
        }
    }
    if (chosenFormat.empty()) throw winrt::hresult_error(E_FAIL, L"No supported native Gray8 kernel capture format.");
    const auto* format = reinterpret_cast<const KSDATAFORMAT*>(chosenFormat.data());
    const KS_BITMAPINFOHEADER* bitmap = nullptr;
    if (format->FormatSize < sizeof(KSDATAFORMAT) || format->FormatSize > chosenFormat.size() ||
        format->MajorFormat != KSDATAFORMAT_TYPE_VIDEO || !IsGraySubtype(format->SubFormat)) throw winrt::hresult_error(E_FAIL);
    if (format->Specifier == KSDATAFORMAT_SPECIFIER_VIDEOINFO && format->FormatSize >= sizeof(KS_DATAFORMAT_VIDEOINFOHEADER))
        bitmap = &reinterpret_cast<const KS_DATAFORMAT_VIDEOINFOHEADER*>(format)->VideoInfoHeader.bmiHeader;
    if (format->Specifier == KSDATAFORMAT_SPECIFIER_VIDEOINFO2 && format->FormatSize >= sizeof(KS_DATAFORMAT_VIDEOINFOHEADER2))
        bitmap = &reinterpret_cast<const KS_DATAFORMAT_VIDEOINFOHEADER2*>(format)->VideoInfoHeader2.bmiHeader;
    if (!bitmap || bitmap->biBitCount != 8 || bitmap->biWidth <= 0 || bitmap->biWidth > 8192 ||
        bitmap->biHeight == 0 || bitmap->biHeight < -8192 || bitmap->biHeight > 8192)
        throw winrt::hresult_error(E_FAIL, L"Refusing unsupported kernel frame layout.");
    const size_t width = bitmap->biWidth, height = abs(bitmap->biHeight);
    size_t stride = (width + 3) & ~size_t(3);
    if (bitmap->biSizeImage && bitmap->biSizeImage % height == 0 && bitmap->biSizeImage / height >= width)
        stride = bitmap->biSizeImage / height;
    if (stride > 65536) throw winrt::hresult_error(E_FAIL, L"Unreasonable frame stride.");
    std::vector<BYTE> connectStorage(sizeof(KSPIN_CONNECT) + format->FormatSize);
    auto* connect = reinterpret_cast<KSPIN_CONNECT*>(connectStorage.data());
    connect->Interface = {KSINTERFACESETID_Standard, KSINTERFACE_STANDARD_STREAMING, 0};
    connect->Medium = {KSMEDIUMSETID_Standard, KSMEDIUM_TYPE_ANYINSTANCE, 0};
    connect->PinId = chosenPin;
    connect->Priority = {KSPRIORITY_NORMAL, 1};
    memcpy(connectStorage.data() + sizeof(KSPIN_CONNECT), format, format->FormatSize);
    HANDLE pinHandle = nullptr;
    const auto createError = KsCreatePin(filter.get(), connect, GENERIC_READ, &pinHandle);
    Check(HRESULT_FROM_WIN32(createError), L"Create native kernel capture pin");
    winrt::handle pin{pinHandle};
    wprintf(L"NATIVE_KERNEL pin=%lu width=%zu height=%zu stride=%zu; no MediaCapture, Frame Server or KSProxy\n", chosenPin, width, height, stride);
    auto setState = [&](KSSTATE state) {
        KSPROPERTY property{KSPROPSETID_Connection, KSPROPERTY_CONNECTION_STATE, KSPROPERTY_TYPE_SET};
        DWORD bytes = 0;
        return KernelIoctl(pin.get(), IOCTL_KS_PROPERTY, &property, sizeof(property), &state, sizeof(state), bytes);
    };
    unsigned frames = 0, flat = 0;
    try {
        Check(HRESULT_FROM_WIN32(setState(KSSTATE_ACQUIRE)), L"Acquire kernel pin");
        Check(HRESULT_FROM_WIN32(setState(KSSTATE_PAUSE)), L"Pause kernel pin");
        Check(HRESULT_FROM_WIN32(setState(KSSTATE_RUN)), L"Run kernel pin");
        std::vector<BYTE> pixels(stride * height);
        const auto start = GetTickCount64(); ULONGLONG nextReport = 0;
        while (GetTickCount64() - start < seconds * 1000ULL) {
            struct Packet { KSSTREAM_HEADER header; KS_FRAME_INFO info; } packet{};
            packet.header.Size = sizeof(packet);
            packet.header.FrameExtent = DWORD(pixels.size());
            packet.header.Data = pixels.data();
            packet.header.PresentationTime.Numerator = packet.header.PresentationTime.Denominator = 1;
            packet.info.ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
            DWORD bytes = 0;
            const auto error = KernelIoctl(pin.get(), IOCTL_KS_READ_STREAM, nullptr, 0, &packet, sizeof(packet), bytes);
            Check(HRESULT_FROM_WIN32(error), L"Read kernel video sample");
            if (!packet.header.DataUsed) continue;
            if (packet.header.DataUsed > pixels.size()) throw winrt::hresult_error(E_FAIL, L"Invalid kernel sample length.");
            const auto stats = AnalyzeGray8(pixels.data(), packet.header.DataUsed, width, height, stride);
            ++frames; flat += stats.minimum == stats.maximum;
            const auto elapsed = GetTickCount64() - start;
            if (frames <= 10 || elapsed >= nextReport) {
                wprintf(L"FRAME t=%.2f number=%u min=%u max=%u mean=%.3f std=%.3f timestamp=%lld flags=%lu\n",
                    elapsed / 1000.0, frames, stats.minimum, stats.maximum, stats.mean, stats.deviation,
                    packet.header.PresentationTime.Time, packet.header.OptionsFlags);
                KernelPrivacy(filter.get(), elapsed / 1000.0, true);
                nextReport = elapsed + 1000;
            }
        }
        Check(HRESULT_FROM_WIN32(setState(KSSTATE_PAUSE)), L"Pause kernel capture for shutdown");
        Check(HRESULT_FROM_WIN32(setState(KSSTATE_ACQUIRE)), L"Release kernel capture resources");
        Check(HRESULT_FROM_WIN32(setState(KSSTATE_STOP)), L"Stop kernel capture");
    } catch (...) { setState(KSSTATE_STOP); throw; }
    wprintf(L"DONE path=kernel frames=%u uniform=%u detailed=%u; pin stopped; no images saved\n", frames, flat, frames - flat);
    return frames ? 0 : 1;
}

static ComPtr<IPin> GetPin(IBaseFilter* filter, PIN_DIRECTION direction) {
    ComPtr<IEnumPins> pins; Check(filter->EnumPins(&pins), L"Enumerate pins");
    ComPtr<IPin> pin;
    while (pins->Next(1, &pin, nullptr) == S_OK) {
        PIN_DIRECTION current{};
        if (SUCCEEDED(pin->QueryDirection(&current)) && current == direction) return pin;
        pin.Reset();
    }
    throw winrt::hresult_error(E_FAIL, L"Expected graph pin missing.");
}

class StatsCallback final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IGrabberCallback> {
public:
    size_t width = 0, height = 0, stride = 0;
    std::mutex mutex;
    unsigned frames = 0, flat = 0, invalid = 0;
    FrameStatistics last;
    double timestamp = 0;
    HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BufferCB(double time, BYTE* bytes, long length) override {
        std::lock_guard lock(mutex);
        try {
            if (length < 0) throw std::invalid_argument("Negative sample length");
            last = AnalyzeGray8(bytes, size_t(length), width, height, stride);
            ++frames; flat += last.minimum == last.maximum; timestamp = time;
        } catch (...) { ++invalid; }
        return S_OK;
    }
};

static void PrintPrivacy(IKsControl* control, double elapsed) {
    if (!control) return;
    KSPROPERTY_CAMERACONTROL_S request{};
    request.Property.Set = PROPSETID_VIDCAP_CAMERACONTROL;
    request.Property.Id = KSPROPERTY_CAMERACONTROL_PRIVACY;
    request.Property.Flags = KSPROPERTY_TYPE_GET; // Never write privacy state.
    KSPROPERTY_CAMERACONTROL_S result{};
    ULONG returned = 0;
    const auto hr = control->KsProperty(&request.Property, sizeof(request), &result, sizeof(result), &returned);
    if (SUCCEEDED(hr) && returned >= sizeof(result))
        wprintf(L"KS_PRIVACY t=%.2f value=%ld flags=%lu\n", elapsed, result.Value, result.Flags);
    else wprintf(L"KS_PRIVACY t=%.2f unavailable HRESULT=0x%08X bytes=%lu\n", elapsed, unsigned(hr), returned);
}

int RunDirectShow(const std::wstring& match, unsigned seconds) {
    const auto path = FindPath(match);
    wprintf(L"DIRECTSHOW device=%s\n", path.c_str());
    ComPtr<IBindCtx> context; Check(CreateBindCtx(0, &context), L"CreateBindCtx");
    ComPtr<IMoniker> moniker;
    auto name = std::wstring(L"@device:pnp:") + path;
    ULONG eaten = 0;
    Check(MkParseDisplayName(context.Get(), name.data(), &eaten, &moniker), L"Parse camera moniker");
    ComPtr<IBaseFilter> camera;
    Check(moniker->BindToObject(context.Get(), nullptr, IID_PPV_ARGS(&camera)), L"Bind camera KSProxy filter");
    CLSID cameraClass{};
    Check(camera->GetClassID(&cameraClass), L"Camera class ID");
    wprintf(L"Camera filter CLSID=%s\n", GuidText(cameraClass).c_str());
    ComPtr<IKsControl> privacy;
    const auto privacyHr = camera.As(&privacy);
    wprintf(L"IKsControl HRESULT=0x%08X\n", unsigned(privacyHr));
    ComPtr<IFilterGraph2> graph;
    Check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)), L"Create filter graph");
    Check(graph->AddFilter(camera.Get(), L"Physical IR (KSProxy)"), L"Add physical IR");
    ComPtr<IPin> output;
    MediaType selected;
    ComPtr<IEnumPins> pins; Check(camera->EnumPins(&pins), L"Camera pins");
    ComPtr<IPin> candidate;
    while (pins->Next(1, &candidate, nullptr) == S_OK && !output) {
        PIN_DIRECTION direction{};
        if (SUCCEEDED(candidate->QueryDirection(&direction)) && direction == PINDIR_OUTPUT) {
            ComPtr<IEnumMediaTypes> types;
            if (SUCCEEDED(candidate->EnumMediaTypes(&types))) {
                AM_MEDIA_TYPE* type = nullptr;
                while (types->Next(1, &type, nullptr) == S_OK) {
                    const auto* header = BitmapHeader(*type);
                    wprintf(L"TYPE subtype=%s width=%ld height=%ld bits=%u\n", GuidText(type->subtype).c_str(),
                        header ? header->biWidth : 0, header ? header->biHeight : 0, header ? header->biBitCount : 0);
                    if (!output && IsGray8(*type)) { selected.value = *type; *type = {}; output = candidate; }
                    FreeType(*type); CoTaskMemFree(type);
                    if (output) break;
                }
            }
        }
        candidate.Reset();
    }
    if (!output) throw winrt::hresult_error(E_FAIL, L"KSProxy exposes no native Gray8 output; refusing conversion.");
    ComPtr<IBaseFilter> grabberFilter, sink;
    Check(CoCreateInstance(GrabberClass, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&grabberFilter)), L"Create inbox Sample Grabber");
    Check(CoCreateInstance(NullRendererClass, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sink)), L"Create Null Renderer");
    ComPtr<IGrabber> grabber; Check(grabberFilter.As(&grabber), L"Sample Grabber interface");
    Check(grabber->SetMediaType(&selected.value), L"Set native Gray8 type");
    Check(graph->AddFilter(grabberFilter.Get(), L"Statistics only"), L"Add grabber");
    Check(graph->AddFilter(sink.Get(), L"Discard frames"), L"Add sink");
    // Direct connections cannot silently insert decoders or processing filters.
    Check(graph->ConnectDirect(output.Get(), GetPin(grabberFilter.Get(), PINDIR_INPUT).Get(), &selected.value), L"Direct camera-to-grabber connection");
    Check(graph->ConnectDirect(GetPin(grabberFilter.Get(), PINDIR_OUTPUT).Get(), GetPin(sink.Get(), PINDIR_INPUT).Get(), nullptr), L"Direct grabber-to-sink connection");
    MediaType connected; Check(grabber->GetConnectedMediaType(&connected.value), L"Read negotiated type");
    if (!IsGray8(connected.value)) throw winrt::hresult_error(E_FAIL, L"Negotiated format is not native Gray8.");
    const auto* bitmap = BitmapHeader(connected.value);
    auto callback = Microsoft::WRL::Make<StatsCallback>();
    callback->width = bitmap->biWidth; callback->height = abs(bitmap->biHeight);
    callback->stride = (callback->width + 3) & ~size_t(3);
    if (bitmap->biSizeImage && bitmap->biSizeImage % callback->height == 0 &&
        bitmap->biSizeImage / callback->height >= callback->width)
        callback->stride = bitmap->biSizeImage / callback->height;
    wprintf(L"NATIVE Gray8 width=%zu height=%zu stride=%zu; graph=KSProxy->SampleGrabber->NullRenderer\n",
        callback->width, callback->height, callback->stride);
    Check(grabber->SetOneShot(FALSE), L"Disable single shot");
    Check(grabber->SetBufferSamples(FALSE), L"Disable retained buffer");
    Check(grabber->SetCallback(callback.Get(), 1), L"Set statistics callback");
    ComPtr<IMediaControl> control; Check(graph.As(&control), L"Graph media control");
    try {
        Check(control->Run(), L"Run native IR graph");
        const auto start = GetTickCount64();
        while (GetTickCount64() - start < seconds * 1000ULL) {
            const double elapsed = (GetTickCount64() - start) / 1000.0;
            PrintPrivacy(privacy.Get(), elapsed);
            {
                std::lock_guard lock(callback->mutex);
                wprintf(L"FRAME t=%.2f count=%u uniform=%u invalid=%u min=%u max=%u mean=%.3f std=%.3f sampleTime=%.3f\n",
                    elapsed, callback->frames, callback->flat, callback->invalid, callback->last.minimum,
                    callback->last.maximum, callback->last.mean, callback->last.deviation, callback->timestamp);
            }
            Sleep(1000);
        }
        Check(control->Stop(), L"Stop native IR graph");
        Check(grabber->SetCallback(nullptr, 1), L"Clear callback");
    } catch (...) { control->Stop(); grabber->SetCallback(nullptr, 1); throw; }
    std::lock_guard lock(callback->mutex);
    wprintf(L"DONE path=directshow frames=%u uniform=%u detailed=%u invalid=%u; graph stopped; no images saved\n",
        callback->frames, callback->flat, callback->frames - callback->flat, callback->invalid);
    return callback->frames && !callback->invalid ? 0 : 1;
}
