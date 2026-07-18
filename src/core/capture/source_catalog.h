#pragma once

#include <memory>
#include <vector>

#include "capture_source.h"

namespace yolo_nexus {

std::vector<CaptureSourceDescriptor> EnumerateCaptureSources();
std::unique_ptr<ICaptureSource> CreateCaptureSource(CaptureSourceKind kind);

}
