#pragma once

#include "core/capture/capture_source.h"
#include "core/inference/yolo_inference.h"
#include "pipeline_state.h"

namespace yolo_nexus {

void CaptureWorker(PipelineState& state, ICaptureSource& capture, int target_fps);
void InferenceWorker(PipelineState& state, YoloInference& inference, int target_fps);

}
