#include "Activator.h"

#include <wrl/implements.h>
#include <wrl/module.h>

#include "MediaSource.h"
// Complete type required: ~MediaSource releases a ComPtr<MediaStream>.
#include "MediaStream.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;

HRESULT Activator::RuntimeClassInitialize() {
    return MFCreateAttributes(&_attributes, 1);
}

IFACEMETHODIMP Activator::ActivateObject(REFIID riid, void** ppv) {
    if (ppv == nullptr) {
        return E_POINTER;
    }
    *ppv = nullptr;

    std::lock_guard<std::mutex> guard(_lock);
    if (!_source) {
        ComPtr<MediaSource> source;
        HRESULT hr = MakeAndInitialize<MediaSource>(&source);
        if (FAILED(hr)) {
            return hr;
        }
        hr = source.As(&_source);
        if (FAILED(hr)) {
            return hr;
        }
    }
    return _source.CopyTo(riid, ppv);
}

IFACEMETHODIMP Activator::ShutdownObject() {
    std::lock_guard<std::mutex> guard(_lock);
    if (_source) {
        ComPtr<IMFMediaSource> source;
        if (SUCCEEDED(_source.As(&source))) {
            source->Shutdown();
        }
        _source.Reset();
    }
    return S_OK;
}

IFACEMETHODIMP Activator::DetachObject() {
    std::lock_guard<std::mutex> guard(_lock);
    _source.Reset();
    return S_OK;
}

