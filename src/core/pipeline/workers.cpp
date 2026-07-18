#include "workers.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core/timing.h"
#include "fps_statistics.h"

namespace yolo_nexus {
namespace {

constexpr float kSmoothingFactor = 0.10f;

float SmoothFps(float current, float sample) {
    return current == 0.0f
        ? sample
        : current * (1.0f - kSmoothingFactor) + sample * kSmoothingFactor;
}

void StoreWorkerError(PipelineState& state, std::string message) {
    {
        std::lock_guard<std::mutex> lock(state.error_mutex);
        state.worker_error = std::move(message);
    }
    state.stop_requested.store(true);
    state.frame_condition.notify_all();
}

}

void CaptureWorker(PipelineState& state, ICaptureSource& capture, int target_fps) {
    try {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        using namespace std::chrono;

        const auto interval = microseconds(1'000'000LL / target_fps);
        auto next_frame_time = steady_clock::now();
        auto last_capture_time = steady_clock::time_point{};
        auto first_capture_time = steady_clock::time_point{};
        float smoothed_capture_fps = 0.0f;
        double capture_latency_sum_ms = 0.0;
        std::uint64_t capture_count = 0;
        std::uint64_t next_frame_id = 1;

        while (!state.stop_requested.load()) {
            FramePacket packet;
            if (capture.Acquire(packet)) {
                const auto now = steady_clock::now();
                packet.frame_id = next_frame_id++;
                const float capture_latency_ms = capture.GetCaptureLatencyMs();
                state.capture_latency_ms.store(capture_latency_ms);
                capture_latency_sum_ms += capture_latency_ms;
                ++capture_count;
                state.average_capture_latency_ms.store(static_cast<float>(
                    capture_latency_sum_ms / static_cast<double>(capture_count)));

                if (first_capture_time == steady_clock::time_point{}) {
                    first_capture_time = now;
                } else {
                    const float elapsed_seconds = duration<float>(
                        now - first_capture_time).count();
                    if (elapsed_seconds > 0.0f) {
                        state.average_capture_fps.store(
                            static_cast<float>(capture_count - 1) / elapsed_seconds);
                    }
                }

                if (last_capture_time != steady_clock::time_point{}) {
                    const float elapsed_seconds = duration<float>(
                        now - last_capture_time).count();
                    if (elapsed_seconds > 0.0f) {
                        smoothed_capture_fps = SmoothFps(
                            smoothed_capture_fps, 1.0f / elapsed_seconds);
                        state.capture_fps.store(smoothed_capture_fps);
                    }
                }
                last_capture_time = now;

                {
                    std::lock_guard<std::mutex> lock(state.frame_mutex);
                    state.latest_frame = std::move(packet);
                }
                state.frame_condition.notify_one();
            }

            next_frame_time += interval;
            if (next_frame_time < steady_clock::now()) {
                next_frame_time = steady_clock::now();
            }
            PreciseWaitUntil(next_frame_time);
        }
    } catch (const std::exception& error) {
        StoreWorkerError(state, "Capture error: " + std::string(error.what()));
    }
}

void InferenceWorker(PipelineState& state, YoloInference& inference, int target_fps) {
    try {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        using namespace std::chrono;

        const auto interval = microseconds(1'000'000LL / target_fps);
        auto next_frame_time = steady_clock::now();
        auto last_completion_time = steady_clock::time_point{};
        auto first_completion_time = steady_clock::time_point{};
        float smoothed_fps = 0.0f;
        FpsStatistics fps_statistics;
        auto next_statistics_update = steady_clock::now();
        double inference_latency_sum_ms = 0.0;
        std::uint64_t inference_count = 0;
        std::uint64_t last_published_frame_id = 0;

        while (!state.stop_requested.load()) {
            if (next_frame_time > steady_clock::now()) {
                PreciseWaitUntil(next_frame_time);
            }

            FramePacket packet;
            {
                std::unique_lock<std::mutex> lock(state.frame_mutex);
                state.frame_condition.wait(lock, [&state] {
                    return state.stop_requested.load() || state.latest_frame.has_value();
                });
                if (state.stop_requested.load()) {
                    break;
                }
                packet = *state.latest_frame;
            }

            if (!packet.bgr.empty()) {
                std::vector<Detection> detections;
                inference.Run(packet.bgr, packet.capture_ts, detections);

                const bool has_new_capture = packet.frame_id != last_published_frame_id;
                if (has_new_capture) {
                    std::lock_guard<std::mutex> lock(state.detection_mutex);
                    state.latest_detections = std::move(detections);
                    state.latest_detection_capture_time = packet.capture_ts;
                    ++state.detection_revision;
                    last_published_frame_id = packet.frame_id;
                }

                const float inference_latency_ms = inference.GetInferenceLatencyMs();
                state.inference_latency_ms.store(inference_latency_ms);
                inference_latency_sum_ms += inference_latency_ms;
                ++inference_count;
                state.average_inference_latency_ms.store(static_cast<float>(
                    inference_latency_sum_ms / static_cast<double>(inference_count)));

                const auto now = steady_clock::now();
                if (has_new_capture) {
                    state.detection_latency_ms.store(
                        duration<float, std::milli>(now - packet.capture_ts).count());
                }
                if (last_completion_time != steady_clock::time_point{}) {
                    const float elapsed_seconds = duration<float>(
                        now - last_completion_time).count();
                    if (elapsed_seconds > 0.0f) {
                        const float instantaneous_fps = 1.0f / elapsed_seconds;
                        smoothed_fps = SmoothFps(
                            smoothed_fps, instantaneous_fps);
                        state.inference_fps.store(smoothed_fps);
                        fps_statistics.AddSample(instantaneous_fps);
                    }
                }
                if (first_completion_time == steady_clock::time_point{}) {
                    first_completion_time = now;
                } else {
                    const float elapsed_seconds = duration<float>(
                        now - first_completion_time).count();
                    if (elapsed_seconds > 0.0f) {
                        state.average_inference_fps.store(
                            static_cast<float>(inference_count - 1) / elapsed_seconds);
                    }
                }
                last_completion_time = now;

                if (now >= next_statistics_update) {
                    const FpsSummary summary = fps_statistics.Calculate();
                    state.maximum_inference_fps.store(summary.maximum);
                    state.one_percent_low_inference_fps.store(summary.one_percent_low);
                    state.point_one_percent_low_inference_fps.store(
                        summary.point_one_percent_low);
                    next_statistics_update = now + seconds(1);
                }
            }

            next_frame_time += interval;
            if (next_frame_time < steady_clock::now()) {
                next_frame_time = steady_clock::now();
            }
        }
    } catch (const std::exception& error) {
        StoreWorkerError(state, "Inference error: " + std::string(error.what()));
    }
}

}
