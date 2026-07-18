#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "app_settings.h"
#include "runtime_snapshot.h"
#include "core/pipeline/pipeline_state.h"

namespace yolo_nexus {

class RuntimeController {
public:
    RuntimeController() = default;
    ~RuntimeController();

    RuntimeController(const RuntimeController&) = delete;
    RuntimeController& operator=(const RuntimeController&) = delete;

    void Start(const AppSettings& settings, std::string source_name);
    void RequestStop();
    void Shutdown();

    RuntimeSnapshot GetSnapshot() const;

private:
    void Run(AppSettings settings, std::string source_name, std::shared_ptr<PipelineState> state);
    void SetStatus(RuntimeStatus status, std::string text);
    void UpdateTelemetry(
        PipelineState& state,
        const std::vector<DetectionClass>& detection_classes,
        float draw_fps,
        float average_draw_fps);

    mutable std::mutex mutex_;
    RuntimeSnapshot snapshot_;
    std::thread runtime_thread_;
    std::shared_ptr<PipelineState> pipeline_state_;
    std::atomic<HWND> preview_hwnd_{nullptr};
    std::atomic<bool> stop_requested_{false};
};

}
