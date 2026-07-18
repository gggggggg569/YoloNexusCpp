#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace yolo_nexus {

enum class RuntimeStatus {
    Stopped,
    Starting,
    Running,
    Stopping,
    Error
};

struct DetectionClassCount {
    int id = 0;
    std::string name;
    std::uint64_t count = 0;
};

struct RuntimeSnapshot {
    RuntimeStatus status = RuntimeStatus::Stopped;
    std::string status_text = "Ready to start";
    std::string active_backend = "—";
    std::string active_device = "—";
    std::string active_source = "—";
    float draw_fps = 0.0f;
    float average_draw_fps = 0.0f;
    float capture_fps = 0.0f;
    float average_capture_fps = 0.0f;
    float inference_fps = 0.0f;
    float average_inference_fps = 0.0f;
    float maximum_inference_fps = 0.0f;
    float one_percent_low_inference_fps = 0.0f;
    float point_one_percent_low_inference_fps = 0.0f;
    float capture_latency_ms = 0.0f;
    float average_capture_latency_ms = 0.0f;
    float inference_latency_ms = 0.0f;
    float average_inference_latency_ms = 0.0f;
    float preview_latency_ms = 0.0f;
    float average_preview_latency_ms = 0.0f;
    float detection_latency_ms = 0.0f;
    float gpu_load_percent = 0.0f;
    float cpu_load_percent = 0.0f;
    float memory_load_percent = 0.0f;
    std::uint64_t process_memory_bytes = 0;
    std::uint64_t total_detection_count = 0;
    std::vector<DetectionClassCount> detection_class_counts;
};

bool IsRuntimeActive(RuntimeStatus status);

}
