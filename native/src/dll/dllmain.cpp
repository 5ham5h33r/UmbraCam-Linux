// COM entry points and self-registration for the G14IrBridgeVCam media source.
//
// Registration must live under HKLM (not HKCU): the Frame Server service
// runs as a different user and resolves the CLSID through the machine hive.

#include <windows.h>

#include <wrl/implements.h>
#include <wrl/module.h>

#include <cstring>

#include "Activator.h"
#include "g14ir_protocol.h"

using namespace Microsoft::WRL;

CoCreatableClass(Activator);

namespace {

HINSTANCE g_instance = nullptr;

constexpr wchar_t kClsidRoot[] = L"Software\\Classes\\CLSID\\" G14IR_CLSID_STRING;
constexpr wchar_t kDescription[] = L"G14 IR Camera Virtual Camera Media Source";

HRESULT SetRegistryValue(HKEY root, const wchar_t* subkey, const wchar_t* name,
                         const wchar_t* value) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                     KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(status);
    }
    status = RegSetValueExW(key, name, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value),
                            static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(status);
}

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
    return Module<InProc>::GetModule().GetClassObject(clsid, riid, ppv);
}

STDAPI DllCanUnloadNow() {
    return Module<InProc>::GetModule().GetObjectCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(g_instance, path, ARRAYSIZE(path)) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HRESULT hr = SetRegistryValue(HKEY_LOCAL_MACHINE, kClsidRoot, nullptr, kDescription);
    if (FAILED(hr)) {
        return hr;
    }

    wchar_t inprocKey[MAX_PATH] = {};
    swprintf_s(inprocKey, L"%s\\InprocServer32", kClsidRoot);
    hr = SetRegistryValue(HKEY_LOCAL_MACHINE, inprocKey, nullptr, path);
    if (FAILED(hr)) {
        return hr;
    }
    return SetRegistryValue(HKEY_LOCAL_MACHINE, inprocKey, L"ThreadingModel", L"Both");
}

STDAPI DllUnregisterServer() {
    LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, kClsidRoot);
    if (status == ERROR_FILE_NOT_FOUND) {
        return S_OK;
    }
    return HRESULT_FROM_WIN32(status);
}

