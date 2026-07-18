#pragma once

#include <opencv2/videoio.hpp>

#include "core/ema.h"
#include "capture_source.h"

namespace yolo_nexus {

class CameraCapture final : public ICaptureSource {
public:
    void Init(const CaptureSourceDescriptor& descriptor) override;
    bool Acquire(FramePacket& frame) override;
    float GetCaptureLatencyMs() const override;

private:
    cv::VideoCapture capture_;
    Ema latency_;
};

}
