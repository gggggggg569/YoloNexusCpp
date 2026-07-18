#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>

namespace yolo_nexus {

struct Detection {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float score = 0.0f;
    int cls = 0;
    std::chrono::steady_clock::time_point capture_ts;
};

struct DetectionClass {
    int id = 0;
    std::string name;
};

struct FramePacket {
    cv::Mat bgr;
    std::chrono::steady_clock::time_point capture_ts;
    std::uint64_t frame_id = 0;
};

}
