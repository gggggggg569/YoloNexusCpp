#pragma once

#include <cstdint>
#include <string>

namespace yolo_nexus {

struct SystemInfoSnapshot {
    std::string cpu_name;
    std::string gpu_name;
    std::uint64_t dedicated_video_memory_bytes = 0;
    std::uint64_t total_memory_bytes = 0;
    unsigned int physical_core_count = 0;
    unsigned int logical_core_count = 0;
    unsigned int cpu_max_frequency_mhz = 0;
    unsigned int memory_frequency_mhz = 0;
};

SystemInfoSnapshot CollectSystemInfo();
std::string FormatBytes(std::uint64_t bytes);

}
