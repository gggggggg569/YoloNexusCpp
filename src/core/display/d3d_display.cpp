#include "d3d_display.h"

#include <d3dcompiler.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace yolo_nexus {
namespace {

void ThrowIfFailed(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw std::runtime_error(std::string(operation) + " failed (HRESULT " +
            std::to_string(static_cast<unsigned long>(result)) + ").");
    }
}

}

void D3DDisplay::Init(HWND hwnd, int width, int height) {
    hwnd_ = hwnd;

    ThrowIfFailed(D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &dev_,
        nullptr,
        &ctx_), "D3D11CreateDevice");

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_dev;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    ThrowIfFailed(dev_.As(&dxgi_dev), "ID3D11Device::QueryInterface");
    ThrowIfFailed(dxgi_dev->GetAdapter(&adapter), "IDXGIDevice::GetAdapter");
    ThrowIfFailed(adapter->GetParent(
        __uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf())),
        "IDXGIAdapter::GetParent");

    DXGI_SWAP_CHAIN_DESC1 swap_desc{};
    swap_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = 2;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_desc.Scaling = DXGI_SCALING_STRETCH;

    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        dev_.Get(), hwnd, &swap_desc, nullptr, nullptr, &swap_),
        "IDXGIFactory2::CreateSwapChainForHwnd");
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    CreateRenderTargetView();
    CompileShaders();
    CreateDynamicTexture(width, height);
    CreateSampler();

    const FLOAT clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    ctx_->ClearRenderTargetView(rtv_.Get(), clear_color);
    ThrowIfFailed(swap_->Present(0, 0), "IDXGISwapChain::Present initial frame");

    QueryPerformanceFrequency(&freq_);
    QueryPerformanceCounter(&last_fps_tick_);
}

void D3DDisplay::UpdateTexture(const cv::Mat& bgr) {
    if (bgr.empty()) {
        return;
    }

    cv::Mat converted;
    const cv::Mat* bgra = &bgr;
    if (bgr.channels() == 3) {
        cv::cvtColor(bgr, converted, cv::COLOR_BGR2BGRA);
        bgra = &converted;
    } else if (bgr.channels() != 4) {
        return;
    }

    if (bgra->cols != tex_w_ || bgra->rows != tex_h_) {
        CreateDynamicTexture(bgra->cols, bgra->rows);
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    ThrowIfFailed(ctx_->Map(tex_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped),
        "ID3D11DeviceContext::Map");

    for (int y = 0; y < tex_h_; ++y) {
        std::memcpy(
            static_cast<std::uint8_t*>(mapped.pData) + y * mapped.RowPitch,
            bgra->ptr(y),
            static_cast<std::size_t>(tex_w_) * 4);
    }

    ctx_->Unmap(tex_.Get(), 0);
}

void D3DDisplay::PresentFrame() {
    RECT rect{};
    GetClientRect(hwnd_, &rect);

    const float width = static_cast<float>(rect.right - rect.left);
    const float height = static_cast<float>(rect.bottom - rect.top);
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    D3D11_VIEWPORT viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
    FLOAT clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    ctx_->ClearRenderTargetView(rtv_.Get(), clear_color);
    ctx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
    ctx_->RSSetViewports(1, &viewport);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    ctx_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    ctx_->PSSetShaderResources(0, 1, tex_srv_.GetAddressOf());
    ctx_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    ctx_->Draw(3, 0);
    ThrowIfFailed(swap_->Present(0, 0), "IDXGISwapChain::Present");

    ++frames_;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (total_frames_ == 0) {
        first_frame_tick_ = now;
    }
    ++total_frames_;

    if (now.QuadPart - last_fps_tick_.QuadPart >= freq_.QuadPart) {
        draw_fps_ = frames_ * static_cast<float>(freq_.QuadPart) /
            static_cast<float>(now.QuadPart - last_fps_tick_.QuadPart);
        frames_ = 0;
        last_fps_tick_ = now;
    }
}

void D3DDisplay::OnResize() {
    if (!swap_) {
        return;
    }

    RECT rect{};
    GetClientRect(hwnd_, &rect);
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }

    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    rtv_.Reset();
    ThrowIfFailed(swap_->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0),
        "IDXGISwapChain::ResizeBuffers");
    CreateRenderTargetView();
}

int D3DDisplay::GetWinW() const {
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    return rect.right - rect.left;
}

int D3DDisplay::GetWinH() const {
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    return rect.bottom - rect.top;
}

float D3DDisplay::GetDrawFps() const {
    return draw_fps_;
}

float D3DDisplay::GetAverageDrawFps() const {
    if (total_frames_ < 2 || freq_.QuadPart == 0) {
        return 0.0f;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const LONGLONG elapsed_ticks = now.QuadPart - first_frame_tick_.QuadPart;
    if (elapsed_ticks <= 0) {
        return 0.0f;
    }
    return static_cast<float>(total_frames_ - 1) * static_cast<float>(freq_.QuadPart) /
        static_cast<float>(elapsed_ticks);
}

void D3DDisplay::CreateRenderTargetView() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
    ThrowIfFailed(swap_->GetBuffer(
        0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back_buffer.GetAddressOf())),
        "IDXGISwapChain::GetBuffer");
    ThrowIfFailed(dev_->CreateRenderTargetView(back_buffer.Get(), nullptr, &rtv_),
        "ID3D11Device::CreateRenderTargetView");
}

void D3DDisplay::CreateDynamicTexture(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    tex_.Reset();
    tex_srv_.Reset();
    tex_w_ = width;
    tex_h_ = height;

    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DYNAMIC;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ThrowIfFailed(dev_->CreateTexture2D(&texture_desc, nullptr, &tex_),
        "ID3D11Device::CreateTexture2D (display)");
    ThrowIfFailed(dev_->CreateShaderResourceView(tex_.Get(), nullptr, &tex_srv_),
        "ID3D11Device::CreateShaderResourceView");
}

void D3DDisplay::CreateSampler() {
    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ThrowIfFailed(dev_->CreateSamplerState(&sampler_desc, &sampler_),
        "ID3D11Device::CreateSamplerState");
}

void D3DDisplay::CompileShaders() {
    const char* shader = R"(
        struct VS_OUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

        VS_OUT VS(uint id : SV_VertexID) {
            VS_OUT output;
            output.uv = float2((id << 1) & 2, id & 2);
            output.pos = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
            return output;
        }

        Texture2D source_texture : register(t0);
        SamplerState source_sampler : register(s0);

        float4 PS(VS_OUT input) : SV_Target {
            return source_texture.Sample(source_sampler, input.uv);
        }
    )";

    Microsoft::WRL::ComPtr<ID3DBlob> vertex_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> pixel_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    ThrowIfFailed(D3DCompile(
        shader, std::strlen(shader), nullptr, nullptr, nullptr,
        "VS", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vertex_blob, &errors),
        "D3DCompile (vertex shader)");
    errors.Reset();
    ThrowIfFailed(D3DCompile(
        shader, std::strlen(shader), nullptr, nullptr, nullptr,
        "PS", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &pixel_blob, &errors),
        "D3DCompile (pixel shader)");

    ThrowIfFailed(dev_->CreateVertexShader(
        vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(), nullptr, &vertex_shader_),
        "ID3D11Device::CreateVertexShader");
    ThrowIfFailed(dev_->CreatePixelShader(
        pixel_blob->GetBufferPointer(), pixel_blob->GetBufferSize(), nullptr, &pixel_shader_),
        "ID3D11Device::CreatePixelShader");
}

}
