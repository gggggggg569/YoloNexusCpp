#include "memory_monitor.h"

#include <psapi.h>

namespace yolo_nexus {

MemorySample MemoryMonitor::Sample() const {
    MemorySample sample;
    sample.total_physical_bytes = GetTotalPhysicalBytes();

    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters))) {
        sample.process_working_set_bytes = counters.WorkingSetSize;
    }

    if (sample.total_physical_bytes > 0) {
        sample.process_load_percent =
            100.0f * static_cast<float>(sample.process_working_set_bytes) /
            static_cast<float>(sample.total_physical_bytes);
    }

    return sample;
}

std::uint64_t MemoryMonitor::GetTotalPhysicalBytes() {
    MEMORYSTATUSEX memory_status{};
    memory_status.dwLength = sizeof(memory_status);

    if (!GlobalMemoryStatusEx(&memory_status)) {
        return 0;
    }

    return memory_status.ullTotalPhys;
}

}
