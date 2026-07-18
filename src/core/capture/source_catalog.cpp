#include "source_catalog.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string_view>

#include "camera_capture.h"
#include "screen_capture.h"
#include "window_capture.h"

namespace yolo_nexus {
namespace {

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
    return result;
}

std::vector<CaptureSourceDescriptor> EnumerateMonitors() {
    std::vector<CaptureSourceDescriptor> result;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return result;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT adapter_index = 0;
         factory->EnumAdapters1(adapter_index, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++adapter_index) {
        DXGI_ADAPTER_DESC1 adapter_desc{};
        if (FAILED(adapter->GetDesc1(&adapter_desc)) ||
            (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            adapter.Reset();
            continue;
        }

        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        for (UINT output_index = 0;
             adapter->EnumOutputs(output_index, &output) != DXGI_ERROR_NOT_FOUND;
             ++output_index) {
            DXGI_OUTPUT_DESC output_desc{};
            if (SUCCEEDED(output->GetDesc(&output_desc)) && output_desc.AttachedToDesktop) {
                const int width = output_desc.DesktopCoordinates.right -
                    output_desc.DesktopCoordinates.left;
                const int height = output_desc.DesktopCoordinates.bottom -
                    output_desc.DesktopCoordinates.top;

                std::ostringstream id;
                id << "monitor:" << adapter_desc.AdapterLuid.HighPart << ':'
                   << adapter_desc.AdapterLuid.LowPart << ':' << output_index;

                std::ostringstream name;
                name << WideToUtf8(output_desc.DeviceName) << " ("
                     << width << "×" << height << ')';

                result.push_back({CaptureSourceKind::Monitor, id.str(), name.str()});
            }
            output.Reset();
        }
        adapter.Reset();
    }
    return result;
}

BOOL CALLBACK CollectWindow(HWND hwnd, LPARAM parameter) {
    auto& result = *reinterpret_cast<std::vector<CaptureSourceDescriptor>*>(parameter);
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id == GetCurrentProcessId()) {
        return TRUE;
    }

    const int title_length = GetWindowTextLengthW(hwnd);
    if (title_length <= 0) {
        return TRUE;
    }
    std::wstring title(static_cast<std::size_t>(title_length + 1), L'\0');
    const int copied = GetWindowTextW(hwnd, title.data(), title_length + 1);
    if (copied <= 0) {
        return TRUE;
    }
    title.resize(static_cast<std::size_t>(copied));

    const auto handle_value = reinterpret_cast<std::uintptr_t>(hwnd);
    result.push_back({
        CaptureSourceKind::Window,
        "window:" + std::to_string(handle_value),
        WideToUtf8(title)});
    return TRUE;
}

std::vector<CaptureSourceDescriptor> EnumerateWindows() {
    std::vector<CaptureSourceDescriptor> result;
    EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&result));
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return result;
}

std::vector<CaptureSourceDescriptor> EnumerateCameras() {
    std::vector<CaptureSourceDescriptor> result;
    const HRESULT init_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool should_uninitialize = SUCCEEDED(init_result);

    {
        Microsoft::WRL::ComPtr<ICreateDevEnum> device_enumerator;
        Microsoft::WRL::ComPtr<IEnumMoniker> class_enumerator;
        if (SUCCEEDED(CoCreateInstance(
                CLSID_SystemDeviceEnum,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&device_enumerator))) &&
            device_enumerator &&
            device_enumerator->CreateClassEnumerator(
                CLSID_VideoInputDeviceCategory, &class_enumerator, 0) == S_OK) {
            Microsoft::WRL::ComPtr<IMoniker> moniker;
            ULONG fetched = 0;
            int camera_index = 0;
            while (class_enumerator->Next(1, &moniker, &fetched) == S_OK) {
                Microsoft::WRL::ComPtr<IPropertyBag> properties;
                std::string friendly_name = "Camera " + std::to_string(camera_index + 1);
                if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&properties)))) {
                    VARIANT value{};
                    VariantInit(&value);
                    if (SUCCEEDED(properties->Read(L"FriendlyName", &value, nullptr)) &&
                        value.vt == VT_BSTR && value.bstrVal != nullptr) {
                        friendly_name = WideToUtf8(value.bstrVal);
                    }
                    VariantClear(&value);
                }
                result.push_back({
                    CaptureSourceKind::Camera,
                    "camera:" + std::to_string(camera_index),
                    friendly_name});
                ++camera_index;
                moniker.Reset();
            }
        }
    }

    if (should_uninitialize) {
        CoUninitialize();
    }
    return result;
}

}

std::vector<CaptureSourceDescriptor> EnumerateCaptureSources() {
    std::vector<CaptureSourceDescriptor> result = EnumerateMonitors();
    auto windows = EnumerateWindows();
    auto cameras = EnumerateCameras();
    result.insert(result.end(), windows.begin(), windows.end());
    result.insert(result.end(), cameras.begin(), cameras.end());
    return result;
}

std::unique_ptr<ICaptureSource> CreateCaptureSource(CaptureSourceKind kind) {
    switch (kind) {
        case CaptureSourceKind::Monitor:
            return std::make_unique<ScreenCapture>();
        case CaptureSourceKind::Window:
            return std::make_unique<WindowCapture>();
        case CaptureSourceKind::Camera:
            return std::make_unique<CameraCapture>();
    }
    return std::make_unique<ScreenCapture>();
}

}
