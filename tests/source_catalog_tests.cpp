#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "core/capture/source_catalog.h"
#include "core/capture/screen_capture.h"
#include "core/capture/window_capture.h"

int main() {
    try {
        const auto sources = yolo_nexus::EnumerateCaptureSources();
        bool has_monitor = false;
        const yolo_nexus::CaptureSourceDescriptor* monitor = nullptr;
        for (const auto& source : sources) {
            if (source.id.empty() || source.name.empty()) {
                throw std::runtime_error("Capture catalog returned an incomplete descriptor");
            }
            has_monitor = has_monitor ||
                source.kind == yolo_nexus::CaptureSourceKind::Monitor;
            if (monitor == nullptr &&
                source.kind == yolo_nexus::CaptureSourceKind::Monitor) {
                monitor = &source;
            }
        }
        if (!has_monitor) {
            throw std::runtime_error("Capture catalog did not find an attached monitor");
        }

        yolo_nexus::ScreenCapture monitor_capture;
        monitor_capture.Configure({120, 320, 180});
        monitor_capture.Init(*monitor);
        yolo_nexus::FramePacket monitor_frame;
        bool monitor_captured = false;
        for (int attempt = 0; attempt < 10 && !monitor_captured; ++attempt) {
            monitor_captured = monitor_capture.Acquire(monitor_frame);
        }
        if (!monitor_captured || monitor_frame.bgr.cols != 320 ||
            monitor_frame.bgr.rows != 180 || monitor_frame.bgr.channels() != 4) {
            throw std::runtime_error("Scaled monitor capture did not return a valid frame");
        }

        HWND test_window = CreateWindowExW(
            0,
            L"STATIC",
            L"YoloNexusCaptureTest",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            320,
            240,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (test_window == nullptr) {
            throw std::runtime_error("Could not create the window capture test fixture");
        }
        UpdateWindow(test_window);

        yolo_nexus::WindowCapture capture;
        const std::string source_id = "window:" + std::to_string(
            reinterpret_cast<std::uintptr_t>(test_window));
        capture.Init({yolo_nexus::CaptureSourceKind::Window, source_id, "Capture test"});
        yolo_nexus::FramePacket frame;
        const bool captured = capture.Acquire(frame);
        DestroyWindow(test_window);
        if (!captured || frame.bgr.empty() || frame.bgr.cols <= 0 || frame.bgr.rows <= 0) {
            throw std::runtime_error("Window capture did not return a valid client frame");
        }

        std::cout << "Capture catalog tests passed: " << sources.size() << " sources\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Capture catalog tests failed: " << error.what() << '\n';
        return 1;
    }
}
