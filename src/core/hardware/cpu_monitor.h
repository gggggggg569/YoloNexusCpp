#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <string>

namespace yolo_nexus {

struct CpuSample {
    float process_load_percent = 0.0f;
};

class CpuMonitor {
public:
    CpuMonitor();

    CpuSample Sample();

    static std::string GetCpuName();
    static unsigned int GetLogicalCoreCount();

private:
    static unsigned long long FileTimeToUInt64(const FILETIME& file_time);

    HANDLE process_ = nullptr;
    unsigned int logical_core_count_ = 1;
    unsigned long long last_process_time_ = 0;
    unsigned long long last_wall_time_ = 0;
    bool has_previous_sample_ = false;
};

}
