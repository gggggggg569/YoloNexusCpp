#include "timing.h"

#include <thread>
#include <xmmintrin.h>

namespace yolo_nexus {

void PreciseWaitUntil(std::chrono::steady_clock::time_point target) {
    using namespace std::chrono;

    const auto now = steady_clock::now();
    if (target <= now) {
        return;
    }

    const auto remaining = target - now;
    if (remaining > milliseconds(2)) {
        std::this_thread::sleep_for(remaining - milliseconds(1));
    }

    while (steady_clock::now() < target) {
        _mm_pause();
    }
}

}
