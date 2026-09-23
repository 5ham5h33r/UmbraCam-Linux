// Run via scripts/test-camera-streams.ps1, which enforces a process deadline.
// No image data is saved; only dimensions, timestamps and checksums are logged.
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <cwchar>
#include <cstdint>

using Microsoft::WRL::ComPtr;

int Run(bool normalCamera, bool componentOnly) {
    ComPtr<IMFActivate> selected;
    if (componentOnly) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (!slash) return 1;
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"IRCameraBridgeVCam.dll");
        // Keep the module loaded until process teardown, after all COM refs.
        HMODULE module = LoadLibraryW(path);
        if (!module) return 1;
        using GetFactory = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
        auto getFactory = reinterpret_cast<GetFactory>(GetProcAddress(module, "DllGetClassObject"));
        CLSID clsid;
        CLSIDFromString(L"{19E90705-FD06-4443-9F30-68F97F09FC9E}", &clsid);
        ComPtr<IClassFactory> factory;
        HRESULT creation = getFactory ? getFactory(clsid, IID_PPV_ARGS(&factory)) : E_FAIL;
        if (SUCCEEDED(creation)) creation = factory->CreateInstance(nullptr, IID_PPV_ARGS(&selected));
        if (FAILED(creation)) {
            fwprintf(stderr, L"Component creation failed: 0x%08X\n", static_cast<unsigned>(creation));
            return 1;
        }
        UINT32 attributeCount = 0;
        creation = selected->GetCount(&attributeCount);
        if (FAILED(creation)) return 1;
        wprintf(L"Selected: UmbraCam DLL directly (placeholder stream, no hardware)\n");
    }
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) return 1;
    attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attributes.Get(), &devices, &count);
    if (FAILED(hr)) return 1;
    for (UINT32 i = 0; i < count; ++i) {
        wchar_t* name = nullptr;
        UINT32 length = 0;
        if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                                    &name, &length))) {
            const bool matches = normalCamera ? wcscmp(name, L"USB2.0 FHD UVC WebCam") == 0
                : wcsncmp(name, L"UmbraCam IR Camera", 17) == 0;
            if (matches && !selected) {
                selected = devices[i];
                wprintf(L"Selected: %s\n", name);
            }
            CoTaskMemFree(name);
        }
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    if (!selected) { fwprintf(stderr, L"Requested camera not found.\n"); return 1; }

    ComPtr<IMFMediaSource> source;
    hr = selected->ActivateObject(IID_PPV_ARGS(&source));
    if (FAILED(hr)) {
        fwprintf(stderr, L"ActivateObject failed: 0x%08X\n", static_cast<unsigned>(hr));
        return 1;
    }
    int result = 1;
    {
        ComPtr<IMFSourceReader> reader;
        hr = MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader);
        if (SUCCEEDED(hr)) {
            unsigned frames = 0, changed = 0, increasing = 0;
            uint64_t previousHash = 0;
            LONGLONG previousTime = 0;
            for (unsigned attempt = 0; attempt < 60 && frames < 15; ++attempt) {
                DWORD flags = 0;
                LONGLONG timestamp = 0;
                ComPtr<IMFSample> sample;
                hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                        nullptr, &flags, &timestamp, &sample);
                if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) break;
                if (!sample) continue;
                ComPtr<IMFMediaBuffer> buffer;
                hr = sample->ConvertToContiguousBuffer(&buffer);
                if (FAILED(hr)) break;
                BYTE* bytes = nullptr;
                DWORD length = 0;
                hr = buffer->Lock(&bytes, nullptr, &length);
                if (FAILED(hr)) break;
                uint64_t hash = 14695981039346656037ull;
                for (DWORD i = 0; i < length; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
                buffer->Unlock();
                if (!length) { hr = E_FAIL; break; }
                if (frames && hash != previousHash) ++changed;
                if (frames && timestamp > previousTime) ++increasing;
                previousHash = hash;
                previousTime = timestamp;
                ++frames;
                Sleep(100);
            }
            ComPtr<IMFMediaType> type;
            UINT32 width = 0, height = 0;
            if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type))) {
                MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
            }
            wprintf(L"Frames: %u; changed: %u; advancing timestamps: %u; size: %u x %u\n",
                    frames, changed, increasing, width, height);
            if (SUCCEEDED(hr) && frames == 15 && changed > 0 && increasing == 14 && width && height) result = 0;
            reader->Flush(MF_SOURCE_READER_ALL_STREAMS);
        }
    }
    source->Shutdown();
    source.Reset();
    selected->ShutdownObject();
    wprintf(L"Stream test: %s (0x%08X)\n", result == 0 ? L"PASS" : L"FAIL", static_cast<unsigned>(hr));
    return result;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || (wcscmp(argv[1], L"--normal") && wcscmp(argv[1], L"--virtual") && wcscmp(argv[1], L"--component"))) return 2;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    if (FAILED(MFStartup(MF_VERSION))) { CoUninitialize(); return 1; }
    int result = Run(wcscmp(argv[1], L"--normal") == 0, wcscmp(argv[1], L"--component") == 0);
    MFShutdown();
    CoUninitialize();
    return result;
}
