#include "app_settings.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <stdexcept>

namespace yolo_nexus {
namespace {

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

}

void ValidateSettings(const AppSettings& settings) {
    if (settings.model_path.empty()) {
        throw std::invalid_argument("ONNX model path is empty.");
    }
    if (Lowercase(settings.model_path.extension().string()) != ".onnx") {
        throw std::invalid_argument("The model file must have the .onnx extension.");
    }
    if (!std::filesystem::is_regular_file(settings.model_path)) {
        throw std::invalid_argument("Model file not found: " + settings.model_path.string());
    }
    if (settings.source_id.empty()) {
        throw std::invalid_argument("No capture source is selected.");
    }
    if (settings.input_width <= 0 || settings.input_height <= 0) {
        throw std::invalid_argument("Input width and height must be positive.");
    }
    std::unordered_set<int> class_ids;
    if (settings.use_model_classes && !settings.detection_classes.empty()) {
        throw std::invalid_argument(
            "Model classes and explicitly configured classes cannot be used together.");
    }
    if (!settings.use_model_classes && settings.detection_classes.empty()) {
        throw std::invalid_argument(
            "At least one detection class must be configured, or classes must be full.");
    }
    for (const DetectionClass& detection_class : settings.detection_classes) {
        if (detection_class.id < 0) {
            throw std::invalid_argument("Detection class IDs cannot be negative.");
        }
        if (detection_class.name.empty()) {
            throw std::invalid_argument("Detection class names cannot be empty.");
        }
        if (!class_ids.insert(detection_class.id).second) {
            throw std::invalid_argument(
                "Duplicate detection class ID: " + std::to_string(detection_class.id));
        }
    }
    if (settings.inference_fps < 1 || settings.inference_fps > 1000) {
        throw std::invalid_argument("inference_fps must be between 1 and 1000.");
    }
    if (settings.draw_fps < 1 || settings.draw_fps > 1000) {
        throw std::invalid_argument("draw_fps must be between 1 and 1000.");
    }
    if (settings.preview_width < 320 || settings.preview_width > 7680 ||
        settings.preview_height < 240 || settings.preview_height > 4320) {
        throw std::invalid_argument("Output resolution must be between 320x240 and 7680x4320.");
    }
}

int GetEffectiveInferenceFps(const AppSettings& settings) {
    return (std::min)(settings.inference_fps, settings.draw_fps);
}

}
