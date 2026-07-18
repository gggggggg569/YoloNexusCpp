#include <iostream>
#include <stdexcept>

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include "core/inference/input_preprocessing.h"

namespace {

void Verify(int channel_count) {
    cv::Mat source(1080, 1920, CV_MAKETYPE(CV_8U, channel_count));
    cv::randu(source, cv::Scalar::all(0), cv::Scalar::all(256));

    cv::Mat reference_source;
    if (channel_count == 4) {
        cv::cvtColor(source, reference_source, cv::COLOR_BGRA2BGR);
    } else {
        reference_source = source;
    }
    cv::Mat reference_resized;
    cv::resize(reference_source, reference_resized, {736, 736});
    const cv::Mat reference_blob = cv::dnn::blobFromImage(
        reference_resized,
        1.0 / 255.0,
        cv::Size(),
        cv::Scalar(),
        true,
        false,
        CV_32F);

    cv::Mat resized;
    cv::Mat actual_blob;
    yolo_nexus::PrepareYoloInput(source, 736, 736, resized, actual_blob);
    const unsigned char* const allocated_data = actual_blob.data;
    yolo_nexus::PrepareYoloInput(source, 736, 736, resized, actual_blob);
    if (actual_blob.data != allocated_data) {
        throw std::runtime_error("Preprocessing did not reuse the input blob allocation.");
    }

    const cv::Mat reference_flat(
        1,
        static_cast<int>(reference_blob.total()),
        CV_32F,
        const_cast<float*>(reference_blob.ptr<float>()));
    const cv::Mat actual_flat(
        1, static_cast<int>(actual_blob.total()), CV_32F, actual_blob.ptr<float>());
    const double maximum_difference = cv::norm(reference_flat, actual_flat, cv::NORM_INF);
    if (maximum_difference > 1.0e-6) {
        throw std::runtime_error(
            "Optimized preprocessing differs from the reference implementation.");
    }
}

}

int main() {
    try {
        Verify(3);
        Verify(4);
        std::cout << "Input preprocessing tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Input preprocessing tests failed: " << error.what() << '\n';
        return 1;
    }
}
