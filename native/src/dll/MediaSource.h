#pragma once
// The IMFMediaSourceEx the Frame Server drives. One video stream.

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

class MediaStream;

// ChainInterfaces: the pipeline QIs for IMFMediaSource and
// IMFMediaEventGenerator (bases of IMFMediaSourceEx); WRL only answers for
// explicitly listed IIDs.
class MediaSource
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::ChainInterfaces<IMFMediaSourceEx, IMFMediaSource,
                                          IMFMediaEventGenerator>,
          IMFGetService, IKsControl> {
public:
    HRESULT RuntimeClassInitialize();

    // IMFMediaEventGenerator
    IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    IFACEMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                              const PROPVARIANT* value) override;

    // IMFMediaSource
    IFACEMETHODIMP GetCharacteristics(DWORD* characteristics) override;
    IFACEMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) override;
    IFACEMETHODIMP Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat,
                         const PROPVARIANT* startPosition) override;
    IFACEMETHODIMP Stop() override;
    IFACEMETHODIMP Pause() override;
    IFACEMETHODIMP Shutdown() override;

    // IMFMediaSourceEx
    IFACEMETHODIMP GetSourceAttributes(IMFAttributes** attributes) override;
    IFACEMETHODIMP GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) override;
    IFACEMETHODIMP SetD3DManager(IUnknown* manager) override;

    // IMFGetService
    IFACEMETHODIMP GetService(REFGUID service, REFIID riid, LPVOID* object) override;

    // IKsControl
    IFACEMETHODIMP KsProperty(PKSPROPERTY property, ULONG propertyLength, LPVOID data,
                              ULONG dataLength, ULONG* bytesReturned) override;
    IFACEMETHODIMP KsMethod(PKSMETHOD method, ULONG methodLength, LPVOID data, ULONG dataLength,
                            ULONG* bytesReturned) override;
    IFACEMETHODIMP KsEvent(PKSEVENT event, ULONG eventLength, LPVOID data, ULONG dataLength,
                           ULONG* bytesReturned) override;

private:
    HRESULT CheckShutdown() const { return _shutdown ? MF_E_SHUTDOWN : S_OK; }

    std::mutex _lock;
    bool _shutdown = false;
    bool _streamAnnounced = false;
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> _events;
    Microsoft::WRL::ComPtr<IMFPresentationDescriptor> _descriptor;
    Microsoft::WRL::ComPtr<MediaStream> _stream;
    Microsoft::WRL::ComPtr<IMFAttributes> _attributes;
};

