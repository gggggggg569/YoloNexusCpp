#pragma once

#include <array>
#include <cstddef>

namespace yolo_nexus {

struct FpsSummary {
    float maximum = 0.0f;
    float one_percent_low = 0.0f;
    float point_one_percent_low = 0.0f;
};

class FpsStatistics final {
public:
    void AddSample(float fps);
    FpsSummary Calculate() const;

private:
    static constexpr std::size_t kCapacity = 10'000;

    std::array<float, kCapacity> samples_{};
    std::size_t sample_count_ = 0;
    std::size_t next_sample_ = 0;
    float maximum_ = 0.0f;
};

}
