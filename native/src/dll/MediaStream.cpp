#include <windows.h>

// GUID definitions for the kernel-streaming pin categories used in the
// stream attributes must materialize in exactly one translation unit.
// Order matters: windows.h, then initguid.h, then ks*.h.
#include <initguid.h>

#include <ks.h>

#include <ksmedia.h>

#include "MediaStream.h"

#include <mferror.h>

#include "MediaSource.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT32 kFps = 30;
constexpr LONGLONG kFrameDuration = 10'000'000LL / kFps;

struct Resolution {
    UINT32 width;
    UINT32 height;
};
constexpr Resolution kResolutions[] = {{1280, 720}, {1920, 1080}};

DWORD SampleBytes(REFGUID subtype, UINT32 width, UINT32 height) {
    if (subtype == MFVideoFormat_NV12) {
        return width * height * 3 / 2;
    }
    return width * height * 4;
}

HRESULT CreateFormat(REFGUID subtype, UINT32 width, UINT32 height, IMFMediaType** type) {
    ComPtr<IMFMediaType> mediaType;
    HRESULT hr = MFCreateMediaType(&mediaType);
    if (FAILED(hr)) {
        return hr;
    }
    const DWORD sampleBytes = SampleBytes(subtype, width, height);
    mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mediaType->SetGUID(MF_MT_SUBTYPE, subtype);
    MFSetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(mediaType.Get(), MF_MT_FRAME_RATE, kFps, 1);
    MFSetAttributeRatio(mediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    mediaType->SetUINT32(MF_MT_DEFAULT_STRIDE,
                         subtype == MFVideoFormat_NV12 ? width : width * 4);
    mediaType->SetUINT32(MF_MT_SAMPLE_SIZE, sampleBytes);
    mediaType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    mediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    mediaType->SetUINT32(MF_MT_AVG_BITRATE, sampleBytes * 8 * kFps);
    *type = mediaType.Detach();
    return S_OK;
}

}  // namespace

HRESULT MediaStream::RuntimeClassInitialize(MediaSource* parent, DWORD streamId) {
    _parent = parent;
    _streamId = streamId;

    HRESULT hr = MFCreateEventQueue(&_events);
    if (FAILED(hr)) {
        return hr;
    }

    // NV12 first: Chrome/Teams and the Windows capture pipeline prefer it
    // (RGB32-only virtual cameras render green there). RGB32 kept for
    // consumers that want it.
    const GUID subtypes[] = {MFVideoFormat_NV12, MFVideoFormat_RGB32};
    constexpr size_t kTypeCount = ARRAYSIZE(subtypes) * ARRAYSIZE(kResolutions);
    ComPtr<IMFMediaType> types[kTypeCount];
    IMFMediaType* rawTypes[kTypeCount];
    size_t index = 0;
    for (const GUID& subtype : subtypes) {
        for (const Resolution& res : kResolutions) {
            hr = CreateFormat(subtype, res.width, res.height, &types[index]);
            if (FAILED(hr)) {
                return hr;
            }
            rawTypes[index] = types[index].Get();
            ++index;
        }
    }

    hr = MFCreateStreamDescriptor(_streamId, ARRAYSIZE(rawTypes), rawTypes, &_descriptor);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IMFMediaTypeHandler> handler;
    hr = _descriptor->GetMediaTypeHandler(&handler);
    if (FAILED(hr)) {
        return hr;
    }
    hr = handler->SetCurrentMediaType(rawTypes[0]);
    if (FAILED(hr)) {
        return hr;
    }

    hr = MFCreateAttributes(&_attributes, 4);
    if (FAILED(hr)) {
        return hr;
    }
    _attributes->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
    _attributes->SetUINT32(MF_DEVICESTREAM_STREAM_ID, _streamId);
    _attributes->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
    _attributes->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES,
                           MFFrameSourceTypes_Color);
    return S_OK;
}

IFACEMETHODIMP MediaStream::GetEvent(DWORD flags, IMFMediaEvent** event) {
    ComPtr<IMFMediaEventQueue> events;
    {
        std::lock_guard<std::mutex> guard(_lock);
        HRESULT hr = CheckShutdown();
        if (FAILED(hr)) {
            return hr;
        }
        events = _events;
    }
    return events->GetEvent(flags, event);
}

IFACEMETHODIMP MediaStream::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    return _events->BeginGetEvent(callback, state);
}

IFACEMETHODIMP MediaStream::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    return _events->EndGetEvent(result, event);
}

IFACEMETHODIMP MediaStream::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                                       const PROPVARIANT* value) {
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    return _events->QueueEventParamVar(type, extendedType, status, value);
}

IFACEMETHODIMP MediaStream::GetMediaSource(IMFMediaSource** source) {
    if (source == nullptr) {
        return E_POINTER;
    }
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    if (!_parent) {
        return MF_E_SHUTDOWN;
    }
    return _parent->QueryInterface(IID_PPV_ARGS(source));
}

IFACEMETHODIMP MediaStream::GetStreamDescriptor(IMFStreamDescriptor** descriptor) {
    if (descriptor == nullptr) {
        return E_POINTER;
    }
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    return _descriptor.CopyTo(descriptor);
}

IFACEMETHODIMP MediaStream::RequestSample(IUnknown* token) {
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    if (_state != MF_STREAM_STATE_RUNNING) {
        return MF_E_INVALIDREQUEST;
    }
    return DeliverSample(token);
}

HRESULT MediaStream::DeliverSample(IUnknown* token) {
    // Resolve the negotiated frame size for every sample: the consumer can
    // change the current media type between requests.
    ComPtr<IMFMediaTypeHandler> handler;
    HRESULT hr = _descriptor->GetMediaTypeHandler(&handler);
    if (FAILED(hr)) {
        return hr;
    }
    ComPtr<IMFMediaType> mediaType;
    hr = handler->GetCurrentMediaType(&mediaType);
    if (FAILED(hr)) {
        return hr;
    }
    UINT32 width = 0;
    UINT32 height = 0;
    hr = MFGetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr)) {
        return hr;
    }
    GUID subtype = MFVideoFormat_NV12;
    mediaType->GetGUID(MF_MT_SUBTYPE, &subtype);

    const DWORD bytes = SampleBytes(subtype, width, height);
    ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateMemoryBuffer(bytes, &buffer);
    if (FAILED(hr)) {
        return hr;
    }

    BYTE* data = nullptr;
    DWORD maxLength = 0;
    hr = buffer->Lock(&data, &maxLength, nullptr);
    if (FAILED(hr)) {
        return hr;
    }
    if (subtype == MFVideoFormat_NV12) {
        _frames.FillNV12(data, width, height);
    } else {
        _frames.Fill(data, width, height);
    }
    buffer->Unlock();
    buffer->SetCurrentLength(bytes);

    ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) {
        return hr;
    }
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(MFGetSystemTime());
    sample->SetSampleDuration(kFrameDuration);
    if (token != nullptr) {
        sample->SetUnknown(MFSampleExtension_Token, token);
    }

    return _events->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
}

IFACEMETHODIMP MediaStream::SetStreamState(MF_STREAM_STATE state) {
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    if (state == _state) {
        return S_OK;
    }
    switch (state) {
        case MF_STREAM_STATE_RUNNING:
            _state = state;
            return _events->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
        case MF_STREAM_STATE_STOPPED:
        case MF_STREAM_STATE_PAUSED:
            _state = MF_STREAM_STATE_STOPPED;
            return _events->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
        default:
            return MF_E_INVALID_STATE_TRANSITION;
    }
}

IFACEMETHODIMP MediaStream::GetStreamState(MF_STREAM_STATE* state) {
    if (state == nullptr) {
        return E_POINTER;
    }
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    *state = _state;
    return S_OK;
}

HRESULT MediaStream::Start() {
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    _state = MF_STREAM_STATE_RUNNING;
    return _events->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
}

HRESULT MediaStream::Stop() {
    std::lock_guard<std::mutex> guard(_lock);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) {
        return hr;
    }
    _state = MF_STREAM_STATE_STOPPED;
    return _events->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
}

HRESULT MediaStream::Shutdown() {
    std::lock_guard<std::mutex> guard(_lock);
    if (_shutdown) {
        return S_OK;
    }
    _shutdown = true;
    if (_events) {
        _events->Shutdown();
    }
    _descriptor.Reset();
    _attributes.Reset();
    _parent.Reset();
    return S_OK;
}

IFACEMETHODIMP MediaStream::KsProperty(PKSPROPERTY /*property*/, ULONG /*propertyLength*/,
                                       LPVOID /*data*/, ULONG /*dataLength*/,
                                       ULONG* bytesReturned) {
    if (bytesReturned != nullptr) {
        *bytesReturned = 0;
    }
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP MediaStream::KsMethod(PKSMETHOD /*method*/, ULONG /*methodLength*/, LPVOID /*data*/,
                                     ULONG /*dataLength*/, ULONG* bytesReturned) {
    if (bytesReturned != nullptr) {
        *bytesReturned = 0;
    }
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP MediaStream::KsEvent(PKSEVENT /*event*/, ULONG /*eventLength*/, LPVOID /*data*/,
                                    ULONG /*dataLength*/, ULONG* bytesReturned) {
    if (bytesReturned != nullptr) {
        *bytesReturned = 0;
    }
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

