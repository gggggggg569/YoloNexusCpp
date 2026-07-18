#pragma once

#include <vector>

#include <opencv2/core/mat.hpp>

#include "core/types.h"

namespace yolo_nexus {

void DrawOverlay(
    cv::Mat& frame,
    const std::vector<Detection>& detections,
    int input_width,
    int input_height,
    const std::vector<DetectionClass>& detection_classes);

}
