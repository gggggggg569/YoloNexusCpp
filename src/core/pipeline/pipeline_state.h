#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/types.h"

namespace yolo_nexus {

struct PipelineState {
    std::mutex frame_mutex;
    std::condition_variable frame_condition;
    std::mutex detection_mutex;
    std::mutex error_mutex;
    std::optional<FramePacket> latest_frame;
    std::vector<Detection> latest_detections;
    std::chrono::steady_clock::time_point latest_detection_capture_time;
    std::uint64_t detection_revision = 0;
    std::string worker_error;

    std::atomic_bool stop_requested{false};
    std::atomic<float> inference_fps{0.0f};
    std::atomic<float> average_inference_fps{0.0f};
    std::atomic<float> maximum_inference_fps{0.0f};
    std::atomic<float> one_percent_low_inference_fps{0.0f};
    std::atomic<float> point_one_percent_low_inference_fps{0.0f};
    std::atomic<float> capture_fps{0.0f};
    std::atomic<float> average_capture_fps{0.0f};
    std::atomic<float> inference_latency_ms{0.0f};
    std::atomic<float> average_inference_latency_ms{0.0f};
    std::atomic<float> capture_latency_ms{0.0f};
    std::atomic<float> average_capture_latency_ms{0.0f};
    std::atomic<float> preview_latency_ms{0.0f};
    std::atomic<float> average_preview_latency_ms{0.0f};
    std::atomic<float> detection_latency_ms{0.0f};
    double preview_latency_sum_ms = 0.0;
    std::uint64_t preview_latency_sample_count = 0;
};

}
