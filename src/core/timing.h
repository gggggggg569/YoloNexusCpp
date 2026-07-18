#pragma once

#include <chrono>

namespace yolo_nexus {

void PreciseWaitUntil(std::chrono::steady_clock::time_point target);

}
