#include "camera_capture.h"

#include <chrono>
#include <stdexcept>

namespace yolo_nexus {

void CameraCapture::Init(const CaptureSourceDescriptor& descriptor) {
    constexpr char prefix[] = "camera:";
    if (descriptor.id.rfind(prefix, 0) != 0) {
        throw std::invalid_argument("Invalid camera source identifier: " + descriptor.id);
    }
    const int index = std::stoi(descriptor.id.substr(sizeof(prefix) - 1));

    capture_.release();
    if (!capture_.open(index, cv::CAP_DSHOW) && !capture_.open(index, cv::CAP_ANY)) {
        throw std::runtime_error("Cannot open camera: " + descriptor.name);
    }
    capture_.set(cv::CAP_PROP_BUFFERSIZE, 1.0);
}

bool CameraCapture::Acquire(FramePacket& frame) {
    const auto start = std::chrono::steady_clock::now();
    cv::Mat image;
    if (!capture_.read(image) || image.empty()) {
        return false;
    }

    frame.bgr = std::move(image);
    frame.capture_ts = std::chrono::steady_clock::now();
    latency_.Update(std::chrono::duration<float, std::milli>(frame.capture_ts - start).count());
    return true;
}

float CameraCapture::GetCaptureLatencyMs() const {
    return latency_.Get();
}

}
