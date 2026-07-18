#pragma once

#include <array>
#include <chrono>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <opencv2/core/mat.hpp>

#include "core/ema.h"
#include "core/types.h"

namespace yolo_nexus {

class YoloInference {
public:
    void Init(
        const std::string& model_path,
        int input_width,
        int input_height,
        std::vector<DetectionClass> detection_classes,
        bool use_model_classes);
    void Run(
        const cv::Mat& bgr,
        const std::chrono::steady_clock::time_point& capture_ts,
        std::vector<Detection>& detections);

    float GetInferenceLatencyMs() const;
    const std::vector<DetectionClass>& GetDetectionClasses() const;

private:
    void Postprocess(
        const float* output,
        int candidate_count,
        int feature_count,
        bool channels_first,
        const std::chrono::steady_clock::time_point& capture_ts,
        std::vector<Detection>& detections);

    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "YOLOv8"};
    Ort::Session session_{nullptr};
    Ort::MemoryInfo cpu_memory_info_{
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
    Ort::Value input_tensor_{nullptr};
    Ort::Value output_tensor_{nullptr};
    std::string input_name_;
    std::string output_name_;
    std::array<int64_t, 4> input_shape_{};
    std::vector<int64_t> output_shape_;
    std::vector<float> output_buffer_;
    bool has_reusable_output_ = false;
    cv::Mat resized_input_;
    cv::Mat input_blob_;
    std::vector<cv::Rect> candidate_boxes_;
    std::vector<float> candidate_confidences_;
    std::vector<int> candidate_classes_;
    std::vector<int> selected_indices_;
    Ema ai_ema_;
    int input_width_ = 0;
    int input_height_ = 0;
    std::vector<DetectionClass> filter_classes_;
    std::vector<DetectionClass> display_classes_;
};

}
