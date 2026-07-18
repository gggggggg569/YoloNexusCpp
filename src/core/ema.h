#pragma once

namespace yolo_nexus {

class Ema {
public:
    void Update(float value) {
        value_ = has_value_ ? value_ * 0.85f + value * 0.15f : value;
        has_value_ = true;
    }

    float Get() const {
        return value_;
    }

private:
    float value_ = 0.0f;
    bool has_value_ = false;
};

}
