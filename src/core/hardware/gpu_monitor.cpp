#include "gpu_monitor.h"

#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <vector>

namespace yolo_nexus {
namespace {

std::string WideToUtf8(const wchar_t* value) {
    if (value == nullptr || value[0] == L'\0') {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string GetHardwareGpuName(std::optional<UINT> vendor_id) {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(
            __uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(factory.GetAddressOf())))) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            (!vendor_id || desc.VendorId == *vendor_id)) {
            return WideToUtf8(desc.Description);
        }
        adapter.Reset();
    }

    return {};
}

}

GpuMonitor::GpuMonitor()
    : process_id_(GetCurrentProcessId()) {}

GpuMonitor::~GpuMonitor() {
    if (query_ != nullptr) {
        PdhCloseQuery(query_);
    }
}

GpuSample GpuMonitor::Sample() {
    if (!initialized_) {
        InitializeCounters();
    }

    GpuSample sample;

    if (query_ == nullptr || counters_.empty()) {
        return sample;
    }

    if (PdhCollectQueryData(query_) != ERROR_SUCCESS) {
        return sample;
    }

    double total_usage = 0.0;
    for (PDH_HCOUNTER counter : counters_) {
        PDH_FMT_COUNTERVALUE value{};
        if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS &&
            value.CStatus == PDH_CSTATUS_VALID_DATA) {
            total_usage += value.doubleValue;
        }
    }

    sample.process_load_percent = static_cast<float>((std::min)(total_usage, 100.0));
    return sample;
}

std::string GpuMonitor::GetPrimaryGpuName() {
    const std::string name = GetHardwareGpuName(std::nullopt);
    return name.empty() ? "Unknown GPU" : name;
}

void GpuMonitor::InitializeCounters() {
    initialized_ = true;

    if (PdhOpenQueryA(nullptr, 0, &query_) != ERROR_SUCCESS) {
        query_ = nullptr;
        return;
    }

    DWORD path_list_size = 0;
    const char* wildcard_path = "\\GPU Engine(*)\\Utilization Percentage";
    PDH_STATUS expand_status = PdhExpandWildCardPathA(
        nullptr,
        wildcard_path,
        nullptr,
        &path_list_size,
        0);

    if (expand_status != PDH_MORE_DATA || path_list_size == 0) {
        return;
    }

    std::vector<char> path_list(path_list_size);
    expand_status = PdhExpandWildCardPathA(
        nullptr,
        wildcard_path,
        path_list.data(),
        &path_list_size,
        0);

    if (expand_status != ERROR_SUCCESS) {
        return;
    }

    const std::string pid_token = "pid_" + std::to_string(process_id_);
    const char* current_path = path_list.data();

    while (current_path != nullptr && current_path[0] != '\0') {
        const std::string path = current_path;
        const std::string lower_path = ToLower(path);

        if (lower_path.find(pid_token) != std::string::npos) {
            PDH_HCOUNTER counter = nullptr;
            if (PdhAddCounterA(query_, path.c_str(), 0, &counter) == ERROR_SUCCESS) {
                counters_.push_back(counter);
            }
        }

        current_path += path.size() + 1;
    }

    PdhCollectQueryData(query_);
}

}
