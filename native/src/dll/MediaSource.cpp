#include "MediaSource.h"

#include <mferror.h>
#include <wrl/module.h>

#include "MediaStream.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;

HRESULT MediaSource::RuntimeClassInitialize() {
    HRESULT hr = MFCreateEventQueue(&_events);
    if (FAILED(hr)) {
        return hr;
    }
    hr = MFCreateAttributes(&_attributes, 1);
    if (FAILED(hr)) {
        return hr;
    }
    hr = MakeAndInitialize<MediaStream>(&_stream, this, /*streamId=*/0);
    if (FAILED(hr)) {
        return hr;
    }

    IMFStreamDescriptor* descriptors[] = {_stream->Descriptor()};
    hr = MFCreatePresentationDescriptor(1, descriptors, &_descriptor);
    if (FAILED(hr)) {
        return hr;
    }
    return _descriptor->SelectStream(0);
}

IFACEMETHODIMP MediaSource::GetEvent(DWORD flags, IMFMediaEvent** event) {
    ComPtr<IMFMediaEventQueue> events;
    {
        std::lock_guard<std::mutex> guard(_lock);
        HRESULT hr = CheckShutdown();
        if (FAILED(hr)) {
            return hr;
        }
        events = _events;
    }
    // GetEvent can block; never hold the lock across it.
    return events->GetEvent(flags, event);
}

IFACEMETHODIMP MediaSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    return _events->BeginGetEvent(callback, state);
}

IFACEMETHODIMP MediaSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    return _events->EndGetEvent(result, event);
}

IFACEMETHODIMP MediaSource::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                                       const PROPVARIANT* value) {
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    return _events->QueueEventParamVar(type, extendedType, status, value);
}

IFACEMETHODIMP MediaSource::GetCharacteristics(DWORD* characteristics) {
    if (characteristics == nullptr) {
        return E_POINTER;
    }
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    *characteristics = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

IFACEMETHODIMP MediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) {
    if (descriptor == nullptr) {
        return E_POINTER;
    }
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    return _descriptor->Clone(descriptor);
}

IFACEMETHODIMP MediaSource::Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat,
                                  const PROPVARIANT* startPosition) {
    if (descriptor == nullptr) {
        return E_INVALIDARG;
    }
    if (timeFormat != nullptr && *timeFormat != GUID_NULL) {
        return MF_E_UNSUPPORTED_TIME_FORMAT;
    }

    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }

    // Announce the stream, then the started events. MENewStream only on the
    // first start; MEUpdatedStream afterwards.
    ComPtr<IUnknown> streamUnknown;
    hr = _stream.As(&streamUnknown);
    if (FAILED(hr)) {
        return hr;
    }
    hr = _events->QueueEventParamUnk(
        _streamAnnounced ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK,
        streamUnknown.Get());
    if (FAILED(hr)) {
        return hr;
    }
    _streamAnnounced = true;

    hr = _stream->Start();
    if (FAILED(hr)) {
        return hr;
    }

    return _events->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, startPosition);
}

IFACEMETHODIMP MediaSource::Stop() {
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    hr = _stream->Stop();
    if (FAILED(hr)) {
        return hr;
    }
    return _events->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
}

IFACEMETHODIMP MediaSource::Pause() {
    return MF_E_INVALID_STATE_TRANSITION;  // live camera: no pause
}

IFACEMETHODIMP MediaSource::Shutdown() {
    std::lock_guard<std::mutex> guard(_lock);
    if (_shutdown) {
        return MF_E_SHUTDOWN;
    }
    _shutdown = true;

    if (_stream) {
        _stream->Shutdown();
        _stream.Reset();
    }
    if (_events) {
        _events->Shutdown();
    }
    _descriptor.Reset();
    _attributes.Reset();
    return S_OK;
}

IFACEMETHODIMP MediaSource::GetSourceAttributes(IMFAttributes** attributes) {
    if (attributes == nullptr) {
        return E_POINTER;
    }
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    return _attributes.CopyTo(attributes);
}

IFACEMETHODIMP MediaSource::GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) {
    if (attributes == nullptr) {
        return E_POINTER;
    }
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    if (streamId != 0 || !_stream) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    return _stream->Attributes()->QueryInterface(IID_PPV_ARGS(attributes));
}

IFACEMETHODIMP MediaSource::SetD3DManager(IUnknown* /*manager*/) {
    // CPU buffers only; accepting and ignoring the manager is valid.
    return S_OK;
}

IFACEMETHODIMP MediaSource::GetService(REFGUID /*service*/, REFIID /*riid*/, LPVOID* object) {
    if (object != nullptr) {
        *object = nullptr;
    }
    return MF_E_UNSUPPORTED_SERVICE;
}

IFACEMETHODIMP MediaSource::KsProperty(PKSPROPERTY /*property*/, ULONG /*propertyLength*/,
                                       LPVOID /*data*/, ULONG /*dataLength*/,
                                       ULONG* bytesReturned) {
    if (bytesReturned != nullptr) {
        *bytesReturned = 0;
    }
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP MediaSource::KsMethod(PKSMETHOD /*method*/, ULONG /*methodLength*/, LPVOID /*data*/,
                                     ULONG /*dataLength*/, ULONG* bytesReturned) {
    if (bytesReturned != nullptr) {
        *bytesReturned = 0;
    }
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP MediaSource::KsEvent(PKSEVENT /*event*/, ULONG /*eventLength*/, LPVOID /*data*/,
                                    ULONG /*dataLength*/, ULONG* bytesReturned) {
    if (bytesReturned != nullptr) {
        *bytesReturned = 0;
    }
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

