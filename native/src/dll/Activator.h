#pragma once
// The COM class registered under G14IR_CLSID_STRING. The Frame Server
// CoCreates it and calls ActivateObject to obtain the media source.

#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <wrl/implements.h>

#include <mutex>

#include "AttributesForward.h"

class MediaSource;

// ChainInterfaces: WRL only answers QueryInterface for the exact IIDs listed,
// not for base interfaces. The Frame Server QIs the activator for
// IMFAttributes, so the IMFActivate -> IMFAttributes chain must be explicit.
class __declspec(uuid("19E90705-FD06-4443-9F30-68F97F09FC9E")) Activator
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::ChainInterfaces<IMFActivate, IMFAttributes>> {
public:
    HRESULT RuntimeClassInitialize();

    // IMFActivate
    IFACEMETHODIMP ActivateObject(REFIID riid, void** ppv) override;
    IFACEMETHODIMP ShutdownObject() override;
    IFACEMETHODIMP DetachObject() override;

    // IMFAttributes
    G14IR_FORWARD_IMFATTRIBUTES(_attributes)

private:
    std::mutex _lock;
    Microsoft::WRL::ComPtr<IMFAttributes> _attributes;
    Microsoft::WRL::ComPtr<IMFMediaSourceEx> _source;
};
