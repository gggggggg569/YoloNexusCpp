#include "fps_statistics.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace yolo_nexus {
namespace {

float AverageLowest(const std::vector<float>& sorted_samples, double fraction) {
    if (sorted_samples.empty()) {
        return 0.0f;
    }

    const std::size_t count = (std::max)(
        std::size_t{1},
        static_cast<std::size_t>(std::ceil(
            static_cast<double>(sorted_samples.size()) * fraction)));
    double sum = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        sum += sorted_samples[index];
    }
    return static_cast<float>(sum / static_cast<double>(count));
}

}

void FpsStatistics::AddSample(float fps) {
    if (!std::isfinite(fps) || fps <= 0.0f) {
        return;
    }

    samples_[next_sample_] = fps;
    next_sample_ = (next_sample_ + 1) % kCapacity;
    sample_count_ = (std::min)(sample_count_ + 1, kCapacity);
    maximum_ = (std::max)(maximum_, fps);
}

FpsSummary FpsStatistics::Calculate() const {
    std::vector<float> sorted_samples(samples_.begin(), samples_.begin() + sample_count_);
    std::sort(sorted_samples.begin(), sorted_samples.end());
    return {
        maximum_,
        AverageLowest(sorted_samples, 0.01),
        AverageLowest(sorted_samples, 0.001)};
}

}
