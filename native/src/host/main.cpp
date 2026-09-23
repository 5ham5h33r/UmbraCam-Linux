// g14ir-host: registers a session-lifetime "G14 IR Camera" with the
// Windows Frame Server and keeps it alive until stopped (Ctrl+C, or the
// named stop event signaled by the app).
//
// Requires Windows 11 and the G14IrBridgeVCam media source DLL to be
// COM-registered - see the project README.

#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>

#include <cstdio>

#include "g14ir_protocol.h"

using Microsoft::WRL::ComPtr;

static HANDLE g_stopEvent = nullptr;

static BOOL WINAPI OnConsoleCtrl(DWORD) {
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
    return TRUE;
}

static void PrintHr(const wchar_t* what, HRESULT hr) {
    fwprintf(stderr, L"%s failed: 0x%08lX\n", what, static_cast<unsigned long>(hr));
    if (hr == E_ACCESSDENIED) {
        fwprintf(stderr,
                 L"  hint: check Settings > Privacy & security > Camera\n"
                 L"  (camera access must be enabled for desktop apps)\n");
    }
    if (hr == REGDB_E_CLASSNOTREG) {
        fwprintf(stderr,
                 L"  hint: the G14IrBridgeVCam media source DLL is not registered.\n"
                 L"  Run from an elevated prompt: regsvr32 G14IrBridgeVCam.dll\n");
    }
}

int wmain() {
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
    {
        ComPtr<IMFVirtualCamera> camera;
        hr = MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource,
                                   MFVirtualCameraLifetime_Session,
                                   MFVirtualCameraAccess_CurrentUser,
                                   G14IR_FRIENDLY_NAME,
                                   G14IR_CLSID_STRING,
                                   nullptr, 0, &camera);
        if (FAILED(hr)) {
            PrintHr(L"MFCreateVirtualCamera", hr);
            exitCode = 1;
        } else {
            hr = camera->Start(nullptr);
            if (FAILED(hr)) {
                PrintHr(L"IMFVirtualCamera::Start", hr);
                exitCode = 1;
            } else {
                g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                HANDLE externalStop =
                    CreateEventW(nullptr, TRUE, FALSE, G14IR_STOP_EVENT_NAME);
                SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);

                wprintf(L"\"%s\" is live - select it in your meeting app.\n",
                        G14IR_FRIENDLY_NAME);
                wprintf(L"Press Ctrl+C to stop.\n");

                HANDLE waits[2] = {g_stopEvent, externalStop};
                DWORD count = externalStop ? 2 : 1;
                WaitForMultipleObjects(count, waits, FALSE, INFINITE);

                camera->Stop();
                if (externalStop) {
                    CloseHandle(externalStop);
                }
                CloseHandle(g_stopEvent);
                g_stopEvent = nullptr;
            }
            camera->Shutdown();
        }
    }

    MFShutdown();
    CoUninitialize();
    return exitCode;
}
