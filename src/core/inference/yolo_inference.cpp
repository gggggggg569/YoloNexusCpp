#include "yolo_inference.h"

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <stdexcept>
#include <utility>

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include "amd_neural_runner.h"
#include "class_metadata.h"
#include "input_preprocessing.h"
#include "core/platform/utf8.h"

namespace yolo_nexus {
void YoloInference::Init(
    const std::string& model_path,
    int input_width,
    int input_height,
    std::vector<DetectionClass> detection_classes,
    bool use_model_classes) {
    input_width_ = input_width;
    input_height_ = input_height;
    input_shape_ = {1, 3, input_height_, input_width_};
    filter_classes_ = use_model_classes
        ? std::vector<DetectionClass>{}
        : std::move(detection_classes);
    const std::wstring wide_model_path = Utf8ToPath(model_path).wstring();
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    options.DisableMemPattern();
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    AppendAmdDirectMlProvider(options);
    session_ = Ort::Session(env_, wide_model_path.c_str(), options);

    if (use_model_classes) {
        Ort::AllocatorWithDefaultOptions allocator;
        auto names = session_.GetModelMetadata()
            .LookupCustomMetadataMapAllocated("names", allocator);
        if (!names) {
            throw std::runtime_error(
                "classes: full requires ONNX metadata named 'names'.");
        }
        display_classes_ = ParseClassNamesMetadata(names.get());
    } else {
        display_classes_ = filter_classes_;
    }

    auto input_name = session_.GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
    input_name_ = input_name.get();
    auto output_name = session_.GetOutputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
    output_name_ = output_name.get();

    const auto input_shape = session_.GetInputTypeInfo(0)
        .GetTensorTypeAndShapeInfo().GetShape();
    if (input_shape.size() != 4) {
        throw std::runtime_error("The model input tensor must have four dimensions (NCHW).");
    }
    if (input_shape[2] > 0 && input_shape[2] != input_height_) {
        throw std::runtime_error(
            "The model expects input height " + std::to_string(input_shape[2]) +
            ", but input_h is " + std::to_string(input_height_) + ".");
    }
    if (input_shape[3] > 0 && input_shape[3] != input_width_) {
        throw std::runtime_error(
            "The model expects input width " + std::to_string(input_shape[3]) +
            ", but input_w is " + std::to_string(input_width_) + ".");
    }

    const auto output_shape_info = session_.GetOutputTypeInfo(0)
        .GetTensorTypeAndShapeInfo();
    if (output_shape_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        throw std::runtime_error("The model output tensor must contain float values.");
    }
    output_shape_ = output_shape_info.GetShape();
    if (output_shape_.size() != 3) {
        throw std::runtime_error("The model output tensor must be three-dimensional.");
    }
    has_reusable_output_ = std::all_of(
        output_shape_.begin(), output_shape_.end(),
        [](int64_t dimension) { return dimension > 0; });
    if (has_reusable_output_) {
        const std::size_t output_element_count = static_cast<std::size_t>(std::accumulate(
            output_shape_.begin(), output_shape_.end(), int64_t{1},
            std::multiplies<int64_t>()));
        output_buffer_.resize(output_element_count);
        output_tensor_ = Ort::Value::CreateTensor<float>(
            cpu_memory_info_,
            output_buffer_.data(),
            output_buffer_.size(),
            output_shape_.data(),
            output_shape_.size());

        const bool channels_first = output_shape_[1] <= output_shape_[2];
        const int candidate_count = static_cast<int>(
            channels_first ? output_shape_[2] : output_shape_[1]);
        const int feature_count = static_cast<int>(
            channels_first ? output_shape_[1] : output_shape_[2]);
        if (feature_count < 5) {
            throw std::runtime_error("The model output must contain at least five features.");
        }
        candidate_boxes_.reserve(static_cast<std::size_t>(candidate_count));
        candidate_confidences_.reserve(static_cast<std::size_t>(candidate_count));
        candidate_classes_.reserve(static_cast<std::size_t>(candidate_count));
        selected_indices_.reserve(static_cast<std::size_t>(candidate_count));
    }

    const int blob_sizes[] = {1, 3, input_height_, input_width_};
    input_blob_.create(4, blob_sizes, CV_32F);
    input_tensor_ = Ort::Value::CreateTensor<float>(
        cpu_memory_info_,
        reinterpret_cast<float*>(input_blob_.data),
        input_blob_.total(),
        input_shape_.data(),
        input_shape_.size());

}

void YoloInference::Run(
    const cv::Mat& bgr,
    const std::chrono::steady_clock::time_point& capture_ts,
    std::vector<Detection>& detections) {
    const auto start = std::chrono::steady_clock::now();

    PrepareYoloInput(bgr, input_width_, input_height_, resized_input_, input_blob_);

    const char* input_names[] = {input_name_.c_str()};
    const char* output_names[] = {output_name_.c_str()};
    const float* output_data = nullptr;
    const std::vector<int64_t>* current_output_shape = &output_shape_;
    std::vector<int64_t> dynamic_output_shape;
    std::vector<Ort::Value> dynamic_outputs;
    if (has_reusable_output_) {
        session_.Run(
            Ort::RunOptions{},
            input_names,
            &input_tensor_,
            1,
            output_names,
            &output_tensor_,
            1);
        output_data = output_buffer_.data();
    } else {
        dynamic_outputs = session_.Run(
            Ort::RunOptions{}, input_names, &input_tensor_, 1, output_names, 1);
        dynamic_output_shape = dynamic_outputs[0]
            .GetTensorTypeAndShapeInfo().GetShape();
        current_output_shape = &dynamic_output_shape;
        output_data = dynamic_outputs[0].GetTensorData<float>();
    }
    if (current_output_shape->size() != 3) {
        throw std::runtime_error("Unexpected YOLO output tensor shape.");
    }

    const bool channels_first = (*current_output_shape)[1] <= (*current_output_shape)[2];
    const int candidate_count = static_cast<int>(
        channels_first ? (*current_output_shape)[2] : (*current_output_shape)[1]);
    const int feature_count = static_cast<int>(
        channels_first ? (*current_output_shape)[1] : (*current_output_shape)[2]);
    if (candidate_count <= 0 || feature_count < 5) {
        throw std::runtime_error("Unexpected YOLO output tensor dimensions.");
    }
    Postprocess(
        output_data,
        candidate_count,
        feature_count,
        channels_first,
        capture_ts,
        detections);

    ai_ema_.Update(std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - start).count());
}

float YoloInference::GetInferenceLatencyMs() const {
    return ai_ema_.Get();
}

const std::vector<DetectionClass>& YoloInference::GetDetectionClasses() const {
    return display_classes_;
}

void YoloInference::Postprocess(
    const float* output,
    int candidate_count,
    int feature_count,
    bool channels_first,
    const std::chrono::steady_clock::time_point& capture_ts,
    std::vector<Detection>& detections) {
    candidate_boxes_.clear();
    candidate_confidences_.clear();
    candidate_classes_.clear();

    const auto value_at = [=](int candidate, int feature) {
        return channels_first
            ? output[feature * candidate_count + candidate]
            : output[candidate * feature_count + feature];
    };
    for (int candidate = 0; candidate < candidate_count; ++candidate) {

        int class_id = 0;
        float confidence = value_at(candidate, 4);
        for (int column = 5; column < feature_count; ++column) {
            const float class_confidence = value_at(candidate, column);
            if (class_confidence > confidence) {
                confidence = class_confidence;
                class_id = column - 4;
            }
        }
        if (confidence <= 0.35f) {
            continue;
        }
        if (!filter_classes_.empty()) {
            const auto configured_class = std::find_if(
                filter_classes_.begin(),
                filter_classes_.end(),
                [class_id](const DetectionClass& item) { return item.id == class_id; });
            if (configured_class == filter_classes_.end()) {
                continue;
            }
        }

        const float x = value_at(candidate, 0);
        const float y = value_at(candidate, 1);
        const float width = value_at(candidate, 2);
        const float height = value_at(candidate, 3);

        candidate_boxes_.emplace_back(
            static_cast<int>(x - width * 0.5f),
            static_cast<int>(y - height * 0.5f),
            static_cast<int>(width),
            static_cast<int>(height));
        candidate_confidences_.push_back(confidence);
        candidate_classes_.push_back(class_id);
    }

    selected_indices_.clear();
    cv::dnn::NMSBoxes(
        candidate_boxes_, candidate_confidences_, 0.35f, 0.5f, selected_indices_);

    detections.clear();
    detections.reserve(selected_indices_.size());

    for (int index : selected_indices_) {
        const cv::Rect& box = candidate_boxes_[index];
        detections.push_back({
            static_cast<float>(box.x),
            static_cast<float>(box.y),
            static_cast<float>(box.x + box.width),
            static_cast<float>(box.y + box.height),
            candidate_confidences_[index],
            candidate_classes_[index],
            capture_ts});
    }
}

}
