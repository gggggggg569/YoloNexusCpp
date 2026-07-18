#include "overlay.h"

#include <algorithm>

#include <opencv2/imgproc.hpp>

namespace yolo_nexus {

void DrawOverlay(
    cv::Mat& frame,
    const std::vector<Detection>& detections,
    int input_width,
    int input_height,
    const std::vector<DetectionClass>& detection_classes) {
    const float scale_x = static_cast<float>(frame.cols) / static_cast<float>(input_width);
    const float scale_y = static_cast<float>(frame.rows) / static_cast<float>(input_height);

    for (const Detection& detection : detections) {
        const cv::Point top_left{
            static_cast<int>(detection.x1 * scale_x),
            static_cast<int>(detection.y1 * scale_y)};
        const cv::Point bottom_right{
            static_cast<int>(detection.x2 * scale_x),
            static_cast<int>(detection.y2 * scale_y)};

        cv::rectangle(frame, top_left, bottom_right, {0, 255, 0}, 2);

        char label[64]{};
        const auto configured_class = std::find_if(
            detection_classes.begin(),
            detection_classes.end(),
            [&detection](const DetectionClass& item) { return item.id == detection.cls; });
        const std::string class_name = configured_class != detection_classes.end()
            ? configured_class->name
            : "class " + std::to_string(detection.cls);
        sprintf_s(label, "%s %.2f", class_name.c_str(), detection.score);

        int baseline = 0;
        const cv::Size label_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseline);
        const int label_y = std::max(top_left.y - label_size.height - 8, 0);

        cv::rectangle(
            frame,
            {top_left.x, label_y},
            {top_left.x + label_size.width + 8, label_y + label_size.height + 8},
            {0, 255, 0},
            -1);

        cv::putText(
            frame,
            label,
            {top_left.x + 4, label_y + label_size.height + 2},
            cv::FONT_HERSHEY_SIMPLEX,
            0.6,
            {0, 0, 0},
            2);
    }
}

}
