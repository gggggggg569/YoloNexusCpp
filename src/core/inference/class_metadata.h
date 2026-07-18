#pragma once

#include <string_view>
#include <vector>

#include "core/types.h"

namespace yolo_nexus {

std::vector<DetectionClass> ParseClassNamesMetadata(std::string_view metadata);

}
