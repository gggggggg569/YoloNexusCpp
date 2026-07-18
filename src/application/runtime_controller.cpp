#include "runtime_controller.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "core/capture/source_catalog.h"
#include "core/display/d3d_display.h"
#include "core/display/preview_window.h"
#include "core/hardware/cpu_monitor.h"
#include "core/hardware/gpu_monitor.h"
#include "core/hardware/memory_monitor.h"
#include "core/inference/yolo_inference.h"
#include "core/inference/onnxruntime_loader.h"
#include "core/overlay/overlay.h"
#include "core/pipeline/workers.h"
#include "core/platform/utf8.h"
#include "core/timing.h"

namespace yolo_nexus {
namespace {

std::wstring BuildPreviewTitle(const RuntimeSnapshot& snapshot) {
    std::wostringstream title;
    title << std::fixed << std::setprecision(1)
          << L"Yolo Nexus | Draw " << snapshot.draw_fps << L" FPS"
          << L" | AI " << snapshot.inference_fps << L" FPS"
          << L" | Capture " << snapshot.capture_fps << L" FPS"
          << L" | Cap Lat " << snapshot.capture_latency_ms << L" ms"
          << L" | AI Lat " << snapshot.inference_latency_ms << L" ms"
          << L" | E2E " << snapshot.preview_latency_ms << L" ms";
    return title.str();
}

bool RenderLatestFrame(
    PipelineState& state,
    D3DDisplay& display,
    int input_width,
    int input_height,
    const std::vector<DetectionClass>& detection_classes,
    std::optional<FramePacket>& displayed_frame,
    std::uint64_t& displayed_frame_id,
    std::uint64_t& uploaded_frame_id,
    std::uint64_t& displayed_detection_revision) {
    std::optional<FramePacket> frame_copy;
    std::vector<Detection> detections_copy;
    std::chrono::steady_clock::time_point detection_capture_ts;
    std::uint64_t detection_revision = 0;

    {
        std::lock_guard<std::mutex> lock(state.frame_mutex);
        frame_copy = state.latest_frame;
    }
    {
        std::lock_guard<std::mutex> lock(state.detection_mutex);
        detections_copy = state.latest_detections;
        detection_capture_ts = state.latest_detection_capture_time;
        detection_revision = state.detection_revision;
    }

    if (frame_copy && frame_copy->frame_id != displayed_frame_id) {
        displayed_frame = std::move(frame_copy);
        displayed_frame_id = displayed_frame->frame_id;
    }
    if (!displayed_frame || displayed_frame->bgr.empty()) {
        return false;
    }

    const int window_width = display.GetWinW();
    const int window_height = display.GetWinH();
    if (window_width <= 0 || window_height <= 0) {
        return false;
    }

    const bool presents_new_detection =
        detection_revision != 0 && detection_revision != displayed_detection_revision;
    if (displayed_frame_id != uploaded_frame_id || presents_new_detection) {
        cv::Mat display_frame;
        cv::resize(displayed_frame->bgr, display_frame, {window_width, window_height});
        DrawOverlay(
            display_frame,
            detections_copy,
            input_width,
            input_height,
            detection_classes);
        display.UpdateTexture(display_frame);
        uploaded_frame_id = displayed_frame_id;
        displayed_detection_revision = detection_revision;
    }

    const auto present_time = std::chrono::steady_clock::now();
    display.PresentFrame();
    if (presents_new_detection &&
        detection_capture_ts != std::chrono::steady_clock::time_point{}) {
        const float e2e_latency_ms = std::chrono::duration<float, std::milli>(
            present_time - detection_capture_ts).count();
        state.preview_latency_ms.store(e2e_latency_ms);
        state.preview_latency_sum_ms += e2e_latency_ms;
        ++state.preview_latency_sample_count;
        state.average_preview_latency_ms.store(static_cast<float>(
            state.preview_latency_sum_ms /
            static_cast<double>(state.preview_latency_sample_count)));
    }
    return true;
}

}

bool IsRuntimeActive(RuntimeStatus status) {
    return status == RuntimeStatus::Starting ||
        status == RuntimeStatus::Running ||
        status == RuntimeStatus::Stopping;
}

RuntimeController::~RuntimeController() {
    Shutdown();
}

void RuntimeController::Start(const AppSettings& settings, std::string source_name) {
    ValidateSettings(settings);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (IsRuntimeActive(snapshot_.status)) {
            throw std::logic_error("The pipeline is already running.");
        }
    }

    if (runtime_thread_.joinable()) {
        runtime_thread_.join();
    }

    stop_requested_.store(false);
    auto state = std::make_shared<PipelineState>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_state_ = state;
        snapshot_ = RuntimeSnapshot{};
        snapshot_.status = RuntimeStatus::Starting;
        snapshot_.status_text = "Initializing the pipeline...";
        snapshot_.active_source = source_name;
    }

    runtime_thread_ = std::thread(
        &RuntimeController::Run, this, settings, std::move(source_name), std::move(state));
}

void RuntimeController::RequestStop() {
    stop_requested_.store(true);

    std::shared_ptr<PipelineState> state;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state = pipeline_state_;
        if (IsRuntimeActive(snapshot_.status)) {
            snapshot_.status = RuntimeStatus::Stopping;
            snapshot_.status_text = "Stopping...";
        }
    }
    if (state) {
        state->stop_requested.store(true);
        state->frame_condition.notify_all();
    }

    const HWND hwnd = preview_hwnd_.load();
    if (hwnd != nullptr) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

void RuntimeController::Shutdown() {
    RequestStop();
    if (runtime_thread_.joinable()) {
        runtime_thread_.join();
    }
}

RuntimeSnapshot RuntimeController::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void RuntimeController::Run(
    AppSettings settings,
    std::string source_name,
    std::shared_ptr<PipelineState> state) {
    std::thread capture_thread;
    std::thread inference_thread;
    std::unique_ptr<ICaptureSource> source;
    std::unique_ptr<YoloInference> inference;
    std::string error_message;
    bool preview_closed_by_user = false;

    try {
        source = CreateCaptureSource(settings.source_kind);
        source->Configure({
            settings.draw_fps,
            (std::max)(settings.input_width, settings.preview_width),
            (std::max)(settings.input_height, settings.preview_height)});
        source->Init({settings.source_kind, settings.source_id, source_name});

        LoadOnnxRuntime();
        inference = std::make_unique<YoloInference>();
        inference->Init(
            PathToUtf8(settings.model_path),
            settings.input_width,
            settings.input_height,
            settings.detection_classes,
            settings.use_model_classes);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.active_backend = "DirectML";
            snapshot_.active_device = GpuMonitor::GetPrimaryGpuName();
            snapshot_.detection_class_counts.clear();
            for (const DetectionClass& detection_class : inference->GetDetectionClasses()) {
                snapshot_.detection_class_counts.push_back({
                    detection_class.id,
                    detection_class.name,
                    0});
            }
        }

        PreviewWindow preview_window;
        preview_window.Create(
            settings.preview_width,
            settings.preview_height,
            L"Yolo Nexus Preview");
        preview_hwnd_.store(preview_window.GetHandle());

        D3DDisplay display;
        display.Init(preview_window.GetHandle(), settings.preview_width, settings.preview_height);
        preview_window.AttachDisplay(display);

        const int capture_fps = settings.draw_fps;
        capture_thread = std::thread(
            CaptureWorker, std::ref(*state), std::ref(*source), capture_fps);
        const int effective_inference_fps = GetEffectiveInferenceFps(settings);
        inference_thread = std::thread(
            InferenceWorker,
            std::ref(*state),
            std::ref(*inference),
            effective_inference_fps);

        SetStatus(RuntimeStatus::Running, "Neural network is running");

        GpuMonitor gpu_monitor;
        CpuMonitor cpu_monitor;
        MemoryMonitor memory_monitor;
        const auto draw_interval = std::chrono::microseconds(
            static_cast<long long>(1'000'000.0 / settings.draw_fps));
        auto next_draw_time = std::chrono::steady_clock::now();
        auto next_telemetry_time = next_draw_time;

        std::optional<FramePacket> displayed_frame;
        std::uint64_t displayed_frame_id = 0;
        std::uint64_t uploaded_frame_id = 0;
        std::uint64_t displayed_detection_revision = 0;

        while (!stop_requested_.load() && !state->stop_requested.load()) {
            if (!preview_window.PumpMessages()) {
                preview_closed_by_user = true;
                break;
            }
            if (!preview_window.GetLastError().empty()) {
                throw std::runtime_error(preview_window.GetLastError());
            }

            RenderLatestFrame(
                *state,
                display,
                settings.input_width,
                settings.input_height,
                inference->GetDetectionClasses(),
                displayed_frame,
                displayed_frame_id,
                uploaded_frame_id,
                displayed_detection_revision);

            const auto now = std::chrono::steady_clock::now();
            if (now >= next_telemetry_time) {
                const GpuSample gpu = gpu_monitor.Sample();
                const CpuSample cpu = cpu_monitor.Sample();
                const MemorySample memory = memory_monitor.Sample();
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    snapshot_.gpu_load_percent = gpu.process_load_percent;
                    snapshot_.cpu_load_percent = cpu.process_load_percent;
                    snapshot_.memory_load_percent = memory.process_load_percent;
                    snapshot_.process_memory_bytes = memory.process_working_set_bytes;
                }
                UpdateTelemetry(
                    *state,
                    inference->GetDetectionClasses(),
                    display.GetDrawFps(),
                    display.GetAverageDrawFps());
                SetWindowTextW(
                    preview_window.GetHandle(), BuildPreviewTitle(GetSnapshot()).c_str());
                next_telemetry_time = now + std::chrono::seconds(1);
            }

            next_draw_time += draw_interval;
            if (next_draw_time < std::chrono::steady_clock::now()) {
                next_draw_time = std::chrono::steady_clock::now();
            }
            PreciseWaitUntil(next_draw_time);
        }
    } catch (const std::exception& error) {
        error_message = error.what();
    }

    state->stop_requested.store(true);
    state->frame_condition.notify_all();
    if (capture_thread.joinable()) {
        capture_thread.join();
    }
    if (inference_thread.joinable()) {
        inference_thread.join();
    }
    if (error_message.empty()) {
        std::lock_guard<std::mutex> lock(state->error_mutex);
        error_message = state->worker_error;
    }
    preview_hwnd_.store(nullptr);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_state_.reset();
        if (!error_message.empty()) {
            snapshot_.status = RuntimeStatus::Error;
            snapshot_.status_text = error_message;
        } else {
            snapshot_.status = RuntimeStatus::Stopped;
            snapshot_.status_text = preview_closed_by_user
                ? "Preview closed"
                : "Pipeline stopped";
        }
    }
}

void RuntimeController::SetStatus(RuntimeStatus status, std::string text) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.status = status;
    snapshot_.status_text = std::move(text);
}

void RuntimeController::UpdateTelemetry(
    PipelineState& state,
    const std::vector<DetectionClass>& detection_classes,
    float draw_fps,
    float average_draw_fps) {
    std::vector<DetectionClassCount> detection_class_counts;
    detection_class_counts.reserve(detection_classes.size());
    for (const DetectionClass& detection_class : detection_classes) {
        detection_class_counts.push_back({
            detection_class.id,
            detection_class.name,
            0});
    }

    std::uint64_t total_detection_count = 0;
    {
        std::lock_guard<std::mutex> detection_lock(state.detection_mutex);
        total_detection_count = static_cast<std::uint64_t>(state.latest_detections.size());
        for (const Detection& detection : state.latest_detections) {
            const auto count = std::find_if(
                detection_class_counts.begin(),
                detection_class_counts.end(),
                [&detection](const DetectionClassCount& item) {
                    return item.id == detection.cls;
                });
            if (count != detection_class_counts.end()) {
                ++count->count;
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.total_detection_count = total_detection_count;
    snapshot_.detection_class_counts = std::move(detection_class_counts);
    snapshot_.draw_fps = draw_fps;
    snapshot_.average_draw_fps = average_draw_fps;
    snapshot_.capture_fps = state.capture_fps.load();
    snapshot_.average_capture_fps = state.average_capture_fps.load();
    snapshot_.inference_fps = state.inference_fps.load();
    snapshot_.average_inference_fps = state.average_inference_fps.load();
    snapshot_.maximum_inference_fps = state.maximum_inference_fps.load();
    snapshot_.one_percent_low_inference_fps =
        state.one_percent_low_inference_fps.load();
    snapshot_.point_one_percent_low_inference_fps =
        state.point_one_percent_low_inference_fps.load();
    snapshot_.capture_latency_ms = state.capture_latency_ms.load();
    snapshot_.average_capture_latency_ms = state.average_capture_latency_ms.load();
    snapshot_.inference_latency_ms = state.inference_latency_ms.load();
    snapshot_.average_inference_latency_ms = state.average_inference_latency_ms.load();
    snapshot_.preview_latency_ms = state.preview_latency_ms.load();
    snapshot_.average_preview_latency_ms = state.average_preview_latency_ms.load();
    snapshot_.detection_latency_ms = state.detection_latency_ms.load();
}

}
