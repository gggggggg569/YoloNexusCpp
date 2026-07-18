#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <cstdint>

namespace yolo_nexus {

struct MemorySample {
    std::uint64_t process_working_set_bytes = 0;
    std::uint64_t total_physical_bytes = 0;
    float process_load_percent = 0.0f;
};

class MemoryMonitor {
public:
    MemorySample Sample() const;

    static std::uint64_t GetTotalPhysicalBytes();
};

}
