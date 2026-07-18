#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "core/ema.h"
#include "capture_source.h"

namespace yolo_nexus {

class ScreenCapture final : public ICaptureSource {
public:
    void Configure(const CaptureOptions& options) override;
    void Init(const CaptureSourceDescriptor& descriptor) override;
    bool Acquire(FramePacket& out) override;
    float GetCaptureLatencyMs() const override;

private:
    void Reinit();

    Microsoft::WRL::ComPtr<ID3D11Device> dev_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> dup_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> scaled_tex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> scaled_rtv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_tex_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> scale_vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> scale_pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> scale_sampler_;
    UINT desk_w_ = 0;
    UINT desk_h_ = 0;
    UINT frame_w_ = 0;
    UINT frame_h_ = 0;
    UINT acquire_timeout_ms_ = 0;
    CaptureOptions options_;
    CaptureSourceDescriptor descriptor_;
    Ema cap_ema_;
};

}
