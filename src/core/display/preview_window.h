#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace yolo_nexus {

class D3DDisplay;

class PreviewWindow {
public:
    PreviewWindow() = default;
    ~PreviewWindow();

    PreviewWindow(const PreviewWindow&) = delete;
    PreviewWindow& operator=(const PreviewWindow&) = delete;

    void Create(int client_width, int client_height, const std::wstring& title);
    void AttachDisplay(D3DDisplay& display);
    bool PumpMessages();
    void RequestClose() const;

    HWND GetHandle() const;
    const std::string& GetLastError() const;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

private:
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    HWND hwnd_ = nullptr;
    D3DDisplay* display_ = nullptr;
    std::string last_error_;
};

}
