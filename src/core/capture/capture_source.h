#pragma once

#include <string>

#include "core/types.h"

namespace yolo_nexus {

enum class CaptureSourceKind {
    Monitor,
    Window,
    Camera
};

struct CaptureSourceDescriptor {
    CaptureSourceKind kind = CaptureSourceKind::Monitor;
    std::string id;
    std::string name;
};

struct CaptureOptions {
    int target_fps = 0;
    int output_width = 0;
    int output_height = 0;
};

class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;

    virtual void Configure(const CaptureOptions&) {}
    virtual void Init(const CaptureSourceDescriptor& descriptor) = 0;
    virtual bool Acquire(FramePacket& frame) = 0;
    virtual float GetCaptureLatencyMs() const = 0;
};

}
