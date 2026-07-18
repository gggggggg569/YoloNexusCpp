#pragma once

#include <onnxruntime_cxx_api.h>

namespace yolo_nexus {

void AppendAmdDirectMlProvider(Ort::SessionOptions& options, int device_id = 0);

}
