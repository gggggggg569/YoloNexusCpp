#include "input_preprocessing.h"

#include <cstddef>
#include <stdexcept>

#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

namespace yolo_nexus {

void PrepareYoloInput(
    const cv::Mat& bgr,
    int input_width,
    int input_height,
    cv::Mat& resized,
    cv::Mat& blob) {
    const int channel_count = bgr.channels();
    if (channel_count != 3 && channel_count != 4) {
        throw std::runtime_error("Inference input must have three or four channels.");
    }

    cv::resize(bgr, resized, {input_width, input_height}, 0.0, 0.0, cv::INTER_LINEAR);

    const int blob_sizes[] = {1, 3, input_height, input_width};
    blob.create(4, blob_sizes, CV_32F);
    float* const blob_data = reinterpret_cast<float*>(blob.data);
    const std::size_t plane_size =
        static_cast<std::size_t>(input_width) * static_cast<std::size_t>(input_height);
    float* const red = blob_data;
    float* const green = blob_data + plane_size;
    float* const blue = blob_data + plane_size * 2;
    constexpr float kScale = 1.0f / 255.0f;

    cv::parallel_for_(cv::Range(0, input_height), [&](const cv::Range& range) {
        for (int y = range.start; y < range.end; ++y) {
            const unsigned char* pixel = resized.ptr<unsigned char>(y);
            std::size_t output_index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(input_width);
            for (int x = 0; x < input_width; ++x, ++output_index) {
                blue[output_index] = static_cast<float>(pixel[0]) * kScale;
                green[output_index] = static_cast<float>(pixel[1]) * kScale;
                red[output_index] = static_cast<float>(pixel[2]) * kScale;
                pixel += channel_count;
            }
        }
    });
}

}
