#include "window_capture.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <opencv2/core/mat.hpp>

namespace yolo_nexus {
namespace {

class WindowDc final {
public:
    explicit WindowDc(HWND hwnd)
        : hwnd_(hwnd), dc_(GetDC(hwnd)) {}
    ~WindowDc() {
        if (dc_ != nullptr) {
            ReleaseDC(hwnd_, dc_);
        }
    }
    HDC Get() const { return dc_; }

private:
    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
};

class ScreenDc final {
public:
    ScreenDc()
        : dc_(GetDC(nullptr)) {}
    ~ScreenDc() {
        if (dc_ != nullptr) {
            ReleaseDC(nullptr, dc_);
        }
    }
    HDC Get() const { return dc_; }

private:
    HDC dc_ = nullptr;
};

class MemoryDc final {
public:
    explicit MemoryDc(HDC compatible_dc)
        : dc_(CreateCompatibleDC(compatible_dc)) {}
    ~MemoryDc() {
        if (dc_ != nullptr) {
            DeleteDC(dc_);
        }
    }
    HDC Get() const { return dc_; }

private:
    HDC dc_ = nullptr;
};

class BitmapSelection final {
public:
    BitmapSelection(HDC dc, HBITMAP bitmap)
        : dc_(dc), previous_(SelectObject(dc, bitmap)) {}
    ~BitmapSelection() {
        if (dc_ != nullptr && previous_ != nullptr) {
            SelectObject(dc_, previous_);
        }
    }

private:
    HDC dc_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};

bool HasVisualVariation(const std::uint8_t* pixels, int width, int height) {
    const std::size_t pixel_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixels == nullptr || pixel_count == 0) {
        return false;
    }

    const std::size_t step = (std::max)(pixel_count / 4096, std::size_t{1});
    std::uint8_t minimum = 255;
    std::uint8_t maximum = 0;
    for (std::size_t index = 0; index < pixel_count; index += step) {
        const std::uint8_t* pixel = pixels + index * 4;
        minimum = (std::min)(minimum, (std::min)(pixel[0], (std::min)(pixel[1], pixel[2])));
        maximum = (std::max)(maximum, (std::max)(pixel[0], (std::max)(pixel[1], pixel[2])));
        if (maximum - minimum > 8) {
            return true;
        }
    }
    return false;
}

}

void WindowCapture::Init(const CaptureSourceDescriptor& descriptor) {
    constexpr char prefix[] = "window:";
    if (descriptor.id.rfind(prefix, 0) != 0) {
        throw std::invalid_argument("Invalid window source identifier: " + descriptor.id);
    }
    const auto value = static_cast<std::uintptr_t>(
        std::stoull(descriptor.id.substr(sizeof(prefix) - 1)));
    hwnd_ = reinterpret_cast<HWND>(value);
    if (!IsWindow(hwnd_)) {
        throw std::runtime_error("The selected window no longer exists.");
    }
}

bool WindowCapture::Acquire(FramePacket& frame) {
    const auto start = std::chrono::steady_clock::now();
    if (!IsWindow(hwnd_)) {
        throw std::runtime_error("The captured window was closed.");
    }

    RECT client_bounds{};
    if (!GetClientRect(hwnd_, &client_bounds)) {
        return false;
    }
    const int width = client_bounds.right - client_bounds.left;
    const int height = client_bounds.bottom - client_bounds.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    POINT client_origin{};
    if (!ClientToScreen(hwnd_, &client_origin)) {
        return false;
    }

    WindowDc window_dc(hwnd_);
    MemoryDc memory_dc(window_dc.Get());
    if (window_dc.Get() == nullptr || memory_dc.Get() == nullptr) {
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        window_dc.Get(), &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (bitmap == nullptr || pixels == nullptr) {
        return false;
    }

    std::memset(
        pixels,
        0,
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);

    bool captured = false;
    {
        BitmapSelection selection(memory_dc.Get(), bitmap);
        if (capture_mode_ != CaptureMode::Desktop) {
            captured = PrintWindow(
                hwnd_, memory_dc.Get(), PW_CLIENTONLY | PW_RENDERFULLCONTENT) != FALSE;
            if (captured && HasVisualVariation(
                    static_cast<const std::uint8_t*>(pixels), width, height)) {
                capture_mode_ = CaptureMode::PrintWindow;
            } else {
                captured = false;
            }
        }
        if (!captured && !IsIconic(hwnd_)) {
            ScreenDc screen_dc;
            if (screen_dc.Get() != nullptr) {
                captured = BitBlt(
                    memory_dc.Get(),
                    0,
                    0,
                    width,
                    height,
                    screen_dc.Get(),
                    client_origin.x,
                    client_origin.y,
                    SRCCOPY | CAPTUREBLT) != FALSE;
                if (captured) {
                    capture_mode_ = CaptureMode::Desktop;
                }
            }
        }
        if (captured) {
            frame.bgr = cv::Mat(height, width, CV_8UC4, pixels).clone();
        }
    }
    DeleteObject(bitmap);

    if (!captured || frame.bgr.empty()) {
        return false;
    }

    frame.capture_ts = std::chrono::steady_clock::now();
    latency_.Update(std::chrono::duration<float, std::milli>(frame.capture_ts - start).count());
    return true;
}

float WindowCapture::GetCaptureLatencyMs() const {
    return latency_.Get();
}

}
