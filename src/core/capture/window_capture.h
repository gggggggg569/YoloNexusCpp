#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core/ema.h"
#include "capture_source.h"

namespace yolo_nexus {

class WindowCapture final : public ICaptureSource {
public:
    void Init(const CaptureSourceDescriptor& descriptor) override;
    bool Acquire(FramePacket& frame) override;
    float GetCaptureLatencyMs() const override;

private:
    enum class CaptureMode {
        Undetermined,
        PrintWindow,
        Desktop
    };

    HWND hwnd_ = nullptr;
    CaptureMode capture_mode_ = CaptureMode::Undetermined;
    Ema latency_;
};

}
