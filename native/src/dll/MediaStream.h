#pragma once
// The single video stream of the virtual camera. Serves RGB32 samples filled
// from the shared-memory frame source.

#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>

// Kernel-streaming headers must come after the MF/OLE headers.
#include <ks.h>
#include <ksproxy.h>

#include <wrl/implements.h>

#include <mutex>

#include "FrameSource.h"

class MediaSource;

// ChainInterfaces: consumers QI for IMFMediaStream and
// IMFMediaEventGenerator (bases of IMFMediaStream2).
class MediaStream
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::ChainInterfaces<IMFMediaStream2, IMFMediaStream,
                                          IMFMediaEventGenerator>,
          IKsControl> {
public:
    HRESULT RuntimeClassInitialize(MediaSource* parent, DWORD streamId);

    // IMFMediaEventGenerator
    IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    IFACEMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                              const PROPVARIANT* value) override;

    // IMFMediaStream
    IFACEMETHODIMP GetMediaSource(IMFMediaSource** source) override;
    IFACEMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override;
    IFACEMETHODIMP RequestSample(IUnknown* token) override;

    // IMFMediaStream2
    IFACEMETHODIMP SetStreamState(MF_STREAM_STATE state) override;
    IFACEMETHODIMP GetStreamState(MF_STREAM_STATE* state) override;

    // IKsControl
    IFACEMETHODIMP KsProperty(PKSPROPERTY property, ULONG propertyLength, LPVOID data,
                              ULONG dataLength, ULONG* bytesReturned) override;
    IFACEMETHODIMP KsMethod(PKSMETHOD method, ULONG methodLength, LPVOID data, ULONG dataLength,
                            ULONG* bytesReturned) override;
    IFACEMETHODIMP KsEvent(PKSEVENT event, ULONG eventLength, LPVOID data, ULONG dataLength,
                           ULONG* bytesReturned) override;

    // Called by MediaSource (which holds its own lock; these only touch
    // stream state).
    HRESULT Start();
    HRESULT Stop();
    HRESULT Shutdown();
    IMFStreamDescriptor* Descriptor() { return _descriptor.Get(); }
    IMFAttributes* Attributes() { return _attributes.Get(); }

private:
    HRESULT CheckShutdown() const { return _shutdown ? MF_E_SHUTDOWN : S_OK; }
    HRESULT DeliverSample(IUnknown* token);

    std::mutex _lock;
    bool _shutdown = false;
    MF_STREAM_STATE _state = MF_STREAM_STATE_STOPPED;
    DWORD _streamId = 0;

    Microsoft::WRL::ComPtr<MediaSource> _parent;
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> _events;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> _descriptor;
    Microsoft::WRL::ComPtr<IMFAttributes> _attributes;
    FrameSource _frames;
};

