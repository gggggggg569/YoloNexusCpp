#include "cpu_monitor.h"

#include <algorithm>
#include <array>

namespace yolo_nexus {
namespace {

std::string ReadRegistryString(HKEY root, const char* path, const char* value_name) {
    HKEY key = nullptr;
    if (RegOpenKeyExA(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return "Unknown CPU";
    }

    std::array<char, 256> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    DWORD type = REG_SZ;
    const LSTATUS status = RegQueryValueExA(
        key,
        value_name,
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(buffer.data()),
        &size);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return "Unknown CPU";
    }

    return std::string(buffer.data());
}

}

CpuMonitor::CpuMonitor()
    : process_(GetCurrentProcess()),
      logical_core_count_(std::max(1u, GetLogicalCoreCount())) {}

CpuSample CpuMonitor::Sample() {
    FILETIME creation_time{};
    FILETIME exit_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};
    FILETIME wall_time{};

    GetProcessTimes(process_, &creation_time, &exit_time, &kernel_time, &user_time);
    GetSystemTimeAsFileTime(&wall_time);

    const unsigned long long process_time =
        FileTimeToUInt64(kernel_time) + FileTimeToUInt64(user_time);
    const unsigned long long wall = FileTimeToUInt64(wall_time);

    CpuSample sample;

    if (!has_previous_sample_) {
        last_process_time_ = process_time;
        last_wall_time_ = wall;
        has_previous_sample_ = true;
        return sample;
    }

    const unsigned long long process_delta = process_time - last_process_time_;
    const unsigned long long wall_delta = wall - last_wall_time_;
    last_process_time_ = process_time;
    last_wall_time_ = wall;

    if (wall_delta > 0) {
        sample.process_load_percent =
            100.0f * static_cast<float>(process_delta) /
            static_cast<float>(wall_delta * logical_core_count_);
    }

    return sample;
}

std::string CpuMonitor::GetCpuName() {
    return ReadRegistryString(
        HKEY_LOCAL_MACHINE,
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        "ProcessorNameString");
}

unsigned int CpuMonitor::GetLogicalCoreCount() {
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    return system_info.dwNumberOfProcessors;
}

unsigned long long CpuMonitor::FileTimeToUInt64(const FILETIME& file_time) {
    ULARGE_INTEGER value{};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return value.QuadPart;
}

}
