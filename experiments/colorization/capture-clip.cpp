// Offline experiment only: capture IR frames without changing drivers or settings.
// Reuse the native probe's already tested device selection and WinRT includes.
#define wmain UmbraCamProbeMain
#include "../../native/src/probe/main.cpp"
#undef wmain
#include <filesystem>
#include <fstream>
#include <stdexcept>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { fwprintf(stderr, L"Usage: capture-clip NEW_OUTPUT_DIRECTORY\n"); return 2; }
    try {
        const std::filesystem::path output(argv[1]);
        if (std::filesystem::exists(output)) throw std::runtime_error("Output directory already exists.");
        std::filesystem::create_directories(output);
        std::ofstream manifest(output / "frames.csv");
        manifest.exceptions(std::ios::badbit | std::ios::failbit);
        manifest << "file,time_100ns,illuminated\n";
        init_apartment(apartment_type::multi_threaded);
        auto settings = MediaCaptureInitializationSettings();
        settings.SourceGroup(FindInfraredGroup());
        settings.StreamingCaptureMode(StreamingCaptureMode::Video);
        settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
        MediaCapture capture;
        capture.InitializeAsync(settings).get();
        auto source = WaitForInfraredSource(capture);
        if (!source) throw hresult_error(E_FAIL, L"No IR frame source.");
        auto reader = capture.CreateFrameReaderAsync(source).get();
        if (reader.StartAsync().get() != MediaFrameReaderStartStatus::Success) throw hresult_error(E_FAIL);
        const auto deadline = GetTickCount64() + 8000;
        int64_t last = -1;
        size_t count = 0;
        IrFramePolicy policy;
        while (GetTickCount64() < deadline) {
            auto frame = reader.TryAcquireLatestFrame();
            if (!frame) { Sleep(5); continue; }
            auto time = frame.SystemRelativeTime();
            if (!time || time.Value().count() == last) { frame.Close(); Sleep(5); continue; }
            last = time.Value().count();
            auto video = frame.VideoMediaFrame();
            if (!video) { frame.Close(); continue; }
            std::optional<bool> illuminated;
            try { if (auto ir = video.InfraredMediaFrame()) illuminated = ir.IsIlluminated(); } catch (...) {}
            if (!policy.Accept(illuminated, GetTickCount64())) { frame.Close(); continue; }
            auto bitmap = video.SoftwareBitmap();
            if (!bitmap && video.Direct3DSurface()) bitmap = SoftwareBitmap::CreateCopyFromSurfaceAsync(video.Direct3DSurface()).get();
            if (!bitmap) { frame.Close(); continue; }
            auto gray = SoftwareBitmap::Convert(bitmap, BitmapPixelFormat::Gray8);
            auto buffer = gray.LockBuffer(BitmapBufferAccessMode::Read);
            auto plane = buffer.GetPlaneDescription(0);
            auto reference = buffer.CreateReference();
            uint8_t* bytes = nullptr; uint32_t capacity = 0;
            check_hresult(reference.as<winrt::impl::IMemoryBufferByteAccess>()->GetBuffer(&bytes, &capacity));
            if (plane.Stride < gray.PixelWidth() || plane.StartIndex < 0 ||
                uint64_t(plane.StartIndex) + uint64_t(gray.PixelHeight() - 1) * plane.Stride + gray.PixelWidth() > capacity)
                throw std::runtime_error("Invalid bitmap plane bounds.");
            char name[32]; sprintf_s(name, "frame-%04zu.pgm", count);
            std::ofstream image(output / name, std::ios::binary);
            image.exceptions(std::ios::badbit | std::ios::failbit);
            image << "P5\n" << gray.PixelWidth() << ' ' << gray.PixelHeight() << "\n255\n";
            for (int y = 0; y < gray.PixelHeight(); ++y)
                image.write(reinterpret_cast<const char*>(bytes + plane.StartIndex + y * plane.Stride), gray.PixelWidth());
            manifest << name << ',' << last << ',' << (illuminated ? (*illuminated ? 1 : 0) : -1) << '\n';
            ++count;
            frame.Close();
        }
        reader.StopAsync().get();
        capture.Close();
        printf("Captured %zu unique frames in 8 seconds. Camera released.\n", count);
        if (count < 20) throw std::runtime_error("Too few frames; capture is not a useful video sample.");
        return 0;
    } catch (hresult_error const& e) {
        fwprintf(stderr, L"Capture failed: 0x%08X %s\n", unsigned(e.code()), e.message().c_str());
    } catch (std::exception const& e) { fprintf(stderr, "Capture failed: %s\n", e.what()); }
    // Stack unwinding/process exit also releases capture resources on failure.
    return 1;
}
