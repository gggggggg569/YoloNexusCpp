#pragma once

#include <opencv2/core/mat.hpp>

namespace yolo_nexus {

void PrepareYoloInput(
    const cv::Mat& bgr,
    int input_width,
    int input_height,
    cv::Mat& resized,
    cv::Mat& blob);

}
