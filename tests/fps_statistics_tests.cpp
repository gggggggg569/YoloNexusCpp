#include <cmath>
#include <iostream>
#include <stdexcept>

#include "core/pipeline/fps_statistics.h"

namespace {

void RequireNear(float actual, float expected, const char* message) {
    if (std::abs(actual - expected) > 0.001f) {
        throw std::runtime_error(message);
    }
}

}

int main() {
    try {
        yolo_nexus::FpsStatistics statistics;
        for (int fps = 1; fps <= 1000; ++fps) {
            statistics.AddSample(static_cast<float>(fps));
        }
        statistics.AddSample(0.0f);

        const yolo_nexus::FpsSummary summary = statistics.Calculate();
        RequireNear(summary.maximum, 1000.0f, "Maximum FPS is incorrect.");
        RequireNear(summary.one_percent_low, 5.5f, "1% Low FPS is incorrect.");
        RequireNear(summary.point_one_percent_low, 1.0f, "0.1% Low FPS is incorrect.");
        std::cout << "FPS statistics tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FPS statistics tests failed: " << error.what() << '\n';
        return 1;
    }
}
