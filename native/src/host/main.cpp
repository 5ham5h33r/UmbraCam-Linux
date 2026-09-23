// umbracam-host: elevated installer helper for the Windows virtual camera.
//
// Windows can deny session-scoped virtual-camera registration to an unpackaged
// desktop publisher despite allowing that publisher to capture physical frames.
// The installer therefore creates a system-lifetime, all-users device once;
// umbracam.exe remains an ordinary user process that only captures and
// publishes Windows Hello IR frames.

#include <windows.h>

#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>

#include <cstdio>

#include "ir_camera_protocol.h"

using Microsoft::WRL::ComPtr;

namespace {

void PrintHr(const wchar_t* operation, HRESULT hr) {
    fwprintf(stderr, L"%s failed: 0x%08lX\n", operation,
             static_cast<unsigned long>(hr));
    if (hr == E_ACCESSDENIED) {
        fwprintf(stderr, L"  hint: run the UmbraCam installer from an Administrator prompt.\n");
    }
    if (hr == REGDB_E_CLASSNOTREG) {
        fwprintf(stderr,
                 L"  hint: the IRCameraBridgeVCam media source DLL is not registered.\n");
    }
}

HRESULT OpenInstalledCamera(ComPtr<IMFVirtualCamera>& camera) {
    return MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource,
                                 MFVirtualCameraLifetime_System,
                                 MFVirtualCameraAccess_AllUsers,
                                 IRCAM_FRIENDLY_NAME,
                                 IRCAM_CLSID_STRING,
                                 nullptr, 0, &camera);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || (wcscmp(argv[1], L"--install") != 0 &&
                      wcscmp(argv[1], L"--uninstall") != 0)) {
        fwprintf(stderr, L"Usage: umbracam-host.exe --install | --uninstall\n");
        return 2;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        PrintHr(L"CoInitializeEx", hr);
        return 1;
    }

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        PrintHr(L"MFStartup", hr);
        CoUninitialize();
        return 1;
    }

    int exitCode = 0;
    ComPtr<IMFVirtualCamera> camera;
    hr = OpenInstalledCamera(camera);
    if (FAILED(hr)) {
        PrintHr(L"MFCreateVirtualCamera", hr);
        exitCode = 1;
    } else if (wcscmp(argv[1], L"--install") == 0) {
        hr = camera->Start(nullptr);
        if (FAILED(hr)) {
            PrintHr(L"IMFVirtualCamera::Start", hr);
            exitCode = 1;
        } else {
            wprintf(L"Installed \"%s\".\n", IRCAM_FRIENDLY_NAME);
        }
    } else {
        hr = camera->Remove();
        if (FAILED(hr)) {
            PrintHr(L"IMFVirtualCamera::Remove", hr);
            exitCode = 1;
        } else {
            wprintf(L"Removed \"%s\".\n", IRCAM_FRIENDLY_NAME);
        }
    }

    if (camera) {
        camera->Shutdown();
        // Release COM objects before shutting down their runtime/apartment.
        camera.Reset();
    }
    MFShutdown();
    CoUninitialize();
    return exitCode;
}
