#include "system_info.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <iomanip>
#include <sstream>
#include <vector>

#include "cpu_monitor.h"
#include "gpu_monitor.h"
#include "memory_monitor.h"

namespace yolo_nexus {
namespace {

unsigned int ReadCpuFrequencyMhz() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return 0;
    }
    DWORD mhz = 0;
    DWORD size = sizeof(mhz);
    const LONG result = RegQueryValueExW(key, L"~MHz", nullptr, nullptr,
        reinterpret_cast<BYTE*>(&mhz), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS ? mhz : 0;
}

unsigned int CountPhysicalCores() {
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (bytes == 0) return 0;
    std::vector<unsigned char> buffer(bytes);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes)) return 0;
    unsigned int count = 0;
    DWORD offset = 0;
    while (offset < bytes) {
        const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (entry->Relationship == RelationProcessorCore) ++count;
        if (entry->Size == 0) break;
        offset += entry->Size;
    }
    return count;
}

std::uint64_t GetPrimaryGpuMemory() {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return 0;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            return desc.DedicatedVideoMemory;
        }
        adapter.Reset();
    }
    return 0;
}

unsigned int ReadMemoryFrequencyMhz() {
    const DWORD size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    if (size < 8) return 0;
    std::vector<unsigned char> data(size);
    if (GetSystemFirmwareTable('RSMB', 0, data.data(), size) != size) return 0;
    const unsigned char* cursor = data.data() + 8;
    const unsigned char* end = data.data() + size;
    unsigned int fastest = 0;
    while (cursor + 4 <= end) {
        const unsigned char type = cursor[0];
        const unsigned char length = cursor[1];
        if (length < 4 || cursor + length > end) break;
        if (type == 17 && length > 0x16) {
            unsigned int speed = static_cast<unsigned int>(cursor[0x15]) |
                (static_cast<unsigned int>(cursor[0x16]) << 8);
            if (speed != 0 && speed != 0xFFFF) fastest = (std::max)(fastest, speed);
        }
        const unsigned char* next = cursor + length;
        while (next + 1 < end && (next[0] != 0 || next[1] != 0)) ++next;
        cursor = next + 2;
        if (type == 127) break;
    }
    return fastest;
}

}

SystemInfoSnapshot CollectSystemInfo() {
    SystemInfoSnapshot info;
    info.cpu_name = CpuMonitor::GetCpuName();
    info.gpu_name = GpuMonitor::GetPrimaryGpuName();
    info.dedicated_video_memory_bytes = GetPrimaryGpuMemory();
    info.total_memory_bytes = MemoryMonitor::GetTotalPhysicalBytes();
    info.physical_core_count = CountPhysicalCores();
    info.logical_core_count = CpuMonitor::GetLogicalCoreCount();
    info.cpu_max_frequency_mhz = ReadCpuFrequencyMhz();
    info.memory_frequency_mhz = ReadMemoryFrequencyMhz();
    return info;
}

std::string FormatBytes(std::uint64_t bytes) {
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;

    std::ostringstream out;
    out << std::fixed << std::setprecision(1);

    if (bytes >= static_cast<std::uint64_t>(gib)) {
        out << static_cast<double>(bytes) / gib << " GB";
    } else if (bytes >= static_cast<std::uint64_t>(mib)) {
        out << static_cast<double>(bytes) / mib << " MB";
    } else if (bytes >= static_cast<std::uint64_t>(kib)) {
        out << static_cast<double>(bytes) / kib << " KB";
    } else {
        out << bytes << " B";
    }

    return out.str();
}

}
