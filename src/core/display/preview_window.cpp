#include "preview_window.h"

#include <stdexcept>

#include "d3d_display.h"

namespace yolo_nexus {
namespace {

constexpr wchar_t kPreviewWindowClass[] = L"YoloNexusPreviewWindow";

void RegisterPreviewWindowClass() {
    WNDCLASSEXW window_class{sizeof(window_class)};
    window_class.lpfnWndProc = PreviewWindow::WindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    window_class.lpszClassName = kPreviewWindowClass;

    if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClassExW failed for the preview window.");
    }
}

}

PreviewWindow::~PreviewWindow() {
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    }
}

void PreviewWindow::Create(int client_width, int client_height, const std::wstring& title) {
    RegisterPreviewWindowClass();

    RECT bounds{0, 0, client_width, client_height};
    if (!AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0)) {
        throw std::runtime_error("AdjustWindowRectEx failed for the preview window.");
    }

    hwnd_ = CreateWindowExW(
        0,
        kPreviewWindowClass,
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        this);
    if (hwnd_ == nullptr) {
        throw std::runtime_error("CreateWindowExW failed for the preview window.");
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

void PreviewWindow::AttachDisplay(D3DDisplay& display) {
    display_ = &display;
}

bool PreviewWindow::PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return hwnd_ != nullptr && IsWindow(hwnd_);
}

void PreviewWindow::RequestClose() const {
    if (hwnd_ != nullptr) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

HWND PreviewWindow::GetHandle() const {
    return hwnd_;
}

const std::string& PreviewWindow::GetLastError() const {
    return last_error_;
}

LRESULT CALLBACK PreviewWindow::WindowProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    PreviewWindow* window = reinterpret_cast<PreviewWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        window = static_cast<PreviewWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }

    if (window != nullptr) {
        return window->HandleMessage(hwnd, message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT PreviewWindow::HandleMessage(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    (void)lparam;

    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (display_ != nullptr && wparam != SIZE_MINIMIZED) {
                try {
                    display_->OnResize();
                } catch (const std::exception& error) {
                    last_error_ = error.what();
                }
            }
            return 0;
        case WM_CLOSE:
            return 0;
        case WM_DESTROY:
            hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

}
