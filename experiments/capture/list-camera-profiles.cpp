// Read-only inventory: no MediaCapture initialization and no video stream.
#include <windows.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Capture.h>
#include <winrt/Windows.Media.Capture.Frames.h>
#include <cstdio>
#include <cwctype>
#include <set>
#include <string>

using namespace winrt;
using namespace Windows::Media::Capture;
using namespace Windows::Media::Capture::Frames;
using namespace Windows::Devices::Enumeration;

static bool IsTarget(const hstring& id) {
    std::wstring text(id);
    for (auto& c : text) c = wchar_t(towlower(c));
    return text.find(L"vid_3277&pid_0060&mi_00") != std::wstring::npos ||
           text.find(L"vid_3277&pid_0060&mi_02") != std::wstring::npos;
}

static void ListProfiles(const hstring& id) {
    wprintf(L"PROFILE_DEVICE id=%s\n", id.c_str());
    const bool supported = MediaCapture::IsVideoProfileSupported(id);
    wprintf(L"PROFILE_SUPPORT supported=%u\n", unsigned(supported));
    if (!supported) return;
    const auto all = MediaCapture::FindAllVideoProfiles(id);
    const auto concurrent = MediaCapture::FindConcurrentProfiles(id);
    wprintf(L"PROFILE_COUNTS all=%u concurrent=%u\n", all.Size(), concurrent.Size());
    for (const auto& profile : all) {
        wprintf(L"PROFILE id=%s device=%s\n", profile.Id().c_str(), profile.VideoDeviceId().c_str());
        auto descriptions = [](const auto& list, const wchar_t* kind) {
            for (const auto& d : list) wprintf(L"DESCRIPTION kind=%s width=%u height=%u fps=%.6f subtype=%s\n",
                kind, d.Width(), d.Height(), d.FrameRate(), d.Subtype().c_str());
        };
        descriptions(profile.SupportedPreviewMediaDescription(), L"preview");
        descriptions(profile.SupportedRecordMediaDescription(), L"record");
        descriptions(profile.SupportedPhotoMediaDescription(), L"photo");
        try {
            for (const auto& peer : profile.GetConcurrency())
                wprintf(L"CONCURRENT profile=%s device=%s\n", peer.Id().c_str(), peer.VideoDeviceId().c_str());
        } catch (const hresult_error& e) {
            // Some FaceAuth profiles enumerate successfully but do not expose
            // peer information. Preserve the remaining profile inventory.
            wprintf(L"CONCURRENCY_UNAVAILABLE profile=%s hr=0x%08x\n", profile.Id().c_str(), unsigned(e.code()));
        }
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || wcscmp(argv[1], L"--inventory")) return 2;
    setvbuf(stdout, nullptr, _IONBF, 0);
    int result = 0;
    init_apartment(apartment_type::multi_threaded);
    try {
        std::set<std::wstring> ids;
        for (const auto& device : DeviceInformation::FindAllAsync(DeviceClass::VideoCapture).get()) {
            if (IsTarget(device.Id())) ids.emplace(device.Id());
        }
        for (const auto& group : MediaFrameSourceGroup::FindAllAsync().get()) {
            bool target = false;
            for (const auto& info : group.SourceInfos()) {
                const auto device = info.DeviceInformation();
                if (device && IsTarget(device.Id())) { target = true; ids.emplace(device.Id()); }
            }
            if (!target) continue;
            ids.emplace(group.Id());
            wprintf(L"GROUP name=%s id=%s\n", group.DisplayName().c_str(), group.Id().c_str());
            for (const auto& info : group.SourceInfos())
                wprintf(L"SOURCE kind=%d stream=%d id=%s\n", int(info.SourceKind()), int(info.MediaStreamType()), info.Id().c_str());
        }
        if (ids.empty()) throw hresult_error(E_FAIL, L"No matching physical camera/group");
        for (const auto& id : ids) {
            try { ListProfiles(hstring(id)); }
            catch (const hresult_error& e) {
                wprintf(L"PROFILE_QUERY_FAILED id=%s hr=0x%08x message=%s\n", id.c_str(), unsigned(e.code()), e.message().c_str());
                result = 1; // An unavailable query is not an empty capability list.
            }
        }
    } catch (const hresult_error& e) {
        wprintf(L"ERROR hr=0x%08x message=%s\n", unsigned(e.code()), e.message().c_str()); result = 1;
    } catch (const std::exception& e) {
        printf("ERROR %s\n", e.what()); result = 1;
    }
    uninit_apartment();
    wprintf(L"EXIT code=%d streaming=0\n", result);
    return result;
}
