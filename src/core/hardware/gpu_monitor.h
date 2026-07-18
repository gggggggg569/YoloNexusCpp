#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <string>
#include <vector>

namespace yolo_nexus {

struct GpuSample {
    float process_load_percent = 0.0f;
};

class GpuMonitor {
public:
    GpuMonitor();
    ~GpuMonitor();

    GpuMonitor(const GpuMonitor&) = delete;
    GpuMonitor& operator=(const GpuMonitor&) = delete;

    GpuSample Sample();

    static std::string GetPrimaryGpuName();

private:
    void InitializeCounters();

    DWORD process_id_ = 0;
    PDH_HQUERY query_ = nullptr;
    std::vector<PDH_HCOUNTER> counters_;
    bool initialized_ = false;
};

}
