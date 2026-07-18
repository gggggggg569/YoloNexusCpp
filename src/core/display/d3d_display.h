#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>

#include <opencv2/core/mat.hpp>

namespace yolo_nexus {

class D3DDisplay {
public:
    void Init(HWND hwnd, int width, int height);
    void UpdateTexture(const cv::Mat& bgr);
    void PresentFrame();
    void OnResize();

    int GetWinW() const;
    int GetWinH() const;
    float GetDrawFps() const;
    float GetAverageDrawFps() const;

private:
    void CreateRenderTargetView();
    void CreateDynamicTexture(int width, int height);
    void CreateSampler();
    void CompileShaders();

    Microsoft::WRL::ComPtr<ID3D11Device> dev_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> tex_srv_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;

    HWND hwnd_ = nullptr;
    int tex_w_ = 0;
    int tex_h_ = 0;
    float draw_fps_ = 0.0f;
    int frames_ = 0;
    std::uint64_t total_frames_ = 0;
    LARGE_INTEGER freq_{};
    LARGE_INTEGER last_fps_tick_{};
    LARGE_INTEGER first_frame_tick_{};
};

}
