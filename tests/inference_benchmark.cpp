#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

#include <opencv2/core.hpp>

#include "core/inference/onnxruntime_loader.h"
#include "core/inference/yolo_inference.h"

namespace {

std::filesystem::path GetExecutableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        throw std::runtime_error("Cannot resolve executable directory.");
    }
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

float Percentile(const std::vector<float>& sorted, double percentile) {
    const std::size_t index = static_cast<std::size_t>(
        percentile * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

}

int main() {
    try {
        constexpr int kWarmupRuns = 5;
        constexpr int kMeasuredRuns = 40;
        constexpr int kInputWidth = 736;
        constexpr int kInputHeight = 736;

        yolo_nexus::LoadOnnxRuntime();
        yolo_nexus::YoloInference inference;
        inference.Init(
            (GetExecutableDirectory() / "best4k.onnx").string(),
            kInputWidth,
            kInputHeight,
            {{0, "tank"}, {1, "part"}},
            false);

        cv::Mat frame(1080, 1920, CV_8UC4);
        cv::randu(frame, cv::Scalar::all(0), cv::Scalar::all(256));
        std::vector<yolo_nexus::Detection> detections;
        for (int index = 0; index < kWarmupRuns; ++index) {
            inference.Run(frame, std::chrono::steady_clock::now(), detections);
        }

        std::vector<float> samples;
        samples.reserve(kMeasuredRuns);
        for (int index = 0; index < kMeasuredRuns; ++index) {
            const auto start = std::chrono::steady_clock::now();
            inference.Run(frame, start, detections);
            samples.push_back(std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - start).count());
        }

        std::sort(samples.begin(), samples.end());
        const float average = std::accumulate(samples.begin(), samples.end(), 0.0f) /
            static_cast<float>(samples.size());
        std::cout << std::fixed << std::setprecision(3)
                  << "runs=" << samples.size() << '\n'
                  << "average_ms=" << average << '\n'
                  << "median_ms=" << Percentile(samples, 0.50) << '\n'
                  << "p95_ms=" << Percentile(samples, 0.95) << '\n'
                  << "minimum_ms=" << samples.front() << '\n'
                  << "average_fps=" << (1000.0f / average) << '\n'
                  << "detections=" << detections.size() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Inference benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
