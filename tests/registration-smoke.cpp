#include <windows.h>

#include <cstdio>

using RegisterServer = HRESULT(STDAPICALLTYPE*)();

int wmain() {
    HMODULE module = LoadLibraryW(L"IRCameraBridgeVCam.dll");
    if (!module) {
        const auto error = GetLastError();
        fwprintf(stderr, L"LoadLibraryW failed: 0x%08lX\n", HRESULT_FROM_WIN32(error));
        return 1;
    }

    auto registerServer = reinterpret_cast<RegisterServer>(GetProcAddress(module, "DllRegisterServer"));
    if (!registerServer) {
        fwprintf(stderr, L"DllRegisterServer export is missing.\n");
        FreeLibrary(module);
        return 1;
    }

    const HRESULT hr = registerServer();
    wprintf(L"DllRegisterServer: 0x%08lX\n", static_cast<unsigned long>(hr));
    FreeLibrary(module);
    return FAILED(hr) ? 1 : 0;
}
