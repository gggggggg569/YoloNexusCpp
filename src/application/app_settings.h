#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/capture/capture_source.h"
#include "core/types.h"

namespace yolo_nexus {

struct AppSettings {
    std::filesystem::path model_path;
    CaptureSourceKind source_kind = CaptureSourceKind::Monitor;
    std::string source_id;
    int input_width = 736;
    int input_height = 736;
    std::vector<DetectionClass> detection_classes;
    bool use_model_classes = false;
    int inference_fps = 5;
    int draw_fps = 120;
    int preview_width = 1280;
    int preview_height = 720;
};

void ValidateSettings(const AppSettings& settings);
int GetEffectiveInferenceFps(const AppSettings& settings);
}
