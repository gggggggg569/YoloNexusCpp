#include "screen_capture.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include <d3dcompiler.h>
#include <opencv2/imgproc.hpp>

namespace yolo_nexus {

namespace {

constexpr char kScaleShader[] = R"(
Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

struct VertexOutput {
    float4 position : SV_POSITION;
    float2 texture_coordinate : TEXCOORD0;
};

VertexOutput VsMain(uint vertex_id : SV_VertexID) {
    VertexOutput output;
    const float2 coordinate = float2((vertex_id << 1) & 2, vertex_id & 2);
    output.position = float4(
        coordinate * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f),
        0.0f,
        1.0f);
    output.texture_coordinate = coordinate;
    return output;
}

float4 PsMain(VertexOutput input) : SV_TARGET {
    return source_texture.Sample(source_sampler, input.texture_coordinate);
}
)";

void ThrowIfFailed(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw std::runtime_error(std::string(operation) + " failed (HRESULT " +
            std::to_string(static_cast<unsigned long>(result)) + ").");
    }
}

Microsoft::WRL::ComPtr<ID3DBlob> CompileScaleShader(
    const char* entry_point,
    const char* target) {
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        kScaleShader,
        sizeof(kScaleShader) - 1,
        nullptr,
        nullptr,
        nullptr,
        entry_point,
        target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &bytecode,
        &errors);
    if (FAILED(result)) {
        const std::string details = errors
            ? std::string(
                static_cast<const char*>(errors->GetBufferPointer()),
                errors->GetBufferSize())
            : std::string("unknown compiler error");
        throw std::runtime_error("D3D scale shader compilation failed: " + details);
    }
    return bytecode;
}

}

void ScreenCapture::Configure(const CaptureOptions& options) {
    options_ = options;
    if (options.target_fps <= 0) {
        acquire_timeout_ms_ = 0;
        return;
    }

    constexpr int kMaximumTimeoutMs = 16;
    acquire_timeout_ms_ = static_cast<UINT>((std::clamp)(
        1000 / options.target_fps, 1, kMaximumTimeoutMs));
}

void ScreenCapture::Init(const CaptureSourceDescriptor& descriptor) {
    descriptor_ = descriptor;
    dup_.Reset();
    scaled_tex_.Reset();
    scaled_rtv_.Reset();
    staging_tex_.Reset();
    scale_vertex_shader_.Reset();
    scale_pixel_shader_.Reset();
    scale_sampler_.Reset();
    ctx_.Reset();
    dev_.Reset();

    long luid_high = 0;
    unsigned long luid_low = 0;
    unsigned int output_index = 0;
    if (sscanf_s(descriptor.id.c_str(), "monitor:%ld:%lu:%u",
            &luid_high, &luid_low, &output_index) != 3) {
        throw std::invalid_argument("Invalid monitor source identifier: " + descriptor.id);
    }

    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");

    Microsoft::WRL::ComPtr<IDXGIAdapter1> selected_adapter;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 adapter_desc{};
        ThrowIfFailed(adapter->GetDesc1(&adapter_desc), "IDXGIAdapter1::GetDesc1");
        if (adapter_desc.AdapterLuid.HighPart == luid_high &&
            adapter_desc.AdapterLuid.LowPart == luid_low) {
            selected_adapter = adapter;
            break;
        }
        adapter.Reset();
    }
    if (!selected_adapter) {
        throw std::runtime_error("Selected monitor adapter is no longer available.");
    }

    const HRESULT device_result = D3D11CreateDevice(
        selected_adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &dev_,
        nullptr,
        &ctx_);

    ThrowIfFailed(device_result, "D3D11CreateDevice");

    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;

    ThrowIfFailed(selected_adapter->EnumOutputs(output_index, &output), "IDXGIAdapter::EnumOutputs");
    ThrowIfFailed(output.As(&output1), "IDXGIOutput::QueryInterface");

    DXGI_OUTPUT_DESC output_desc{};
    ThrowIfFailed(output->GetDesc(&output_desc), "IDXGIOutput::GetDesc");
    desk_w_ = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
    desk_h_ = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;
    frame_w_ = options_.output_width > 0
        ? static_cast<UINT>(options_.output_width)
        : desk_w_;
    frame_h_ = options_.output_height > 0
        ? static_cast<UINT>(options_.output_height)
        : desk_h_;
    ThrowIfFailed(output1->DuplicateOutput(dev_.Get(), &dup_), "IDXGIOutput1::DuplicateOutput");

    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = frame_w_;
    texture_desc.Height = frame_h_;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;

    if (frame_w_ != desk_w_ || frame_h_ != desk_h_) {
        texture_desc.Usage = D3D11_USAGE_DEFAULT;
        texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ThrowIfFailed(dev_->CreateTexture2D(&texture_desc, nullptr, &scaled_tex_),
            "ID3D11Device::CreateTexture2D (scaled capture)");
        ThrowIfFailed(dev_->CreateRenderTargetView(
            scaled_tex_.Get(), nullptr, &scaled_rtv_),
            "ID3D11Device::CreateRenderTargetView (scaled capture)");

        const Microsoft::WRL::ComPtr<ID3DBlob> vertex_bytecode =
            CompileScaleShader("VsMain", "vs_5_0");
        const Microsoft::WRL::ComPtr<ID3DBlob> pixel_bytecode =
            CompileScaleShader("PsMain", "ps_5_0");
        ThrowIfFailed(dev_->CreateVertexShader(
            vertex_bytecode->GetBufferPointer(),
            vertex_bytecode->GetBufferSize(),
            nullptr,
            &scale_vertex_shader_),
            "ID3D11Device::CreateVertexShader (capture scale)");
        ThrowIfFailed(dev_->CreatePixelShader(
            pixel_bytecode->GetBufferPointer(),
            pixel_bytecode->GetBufferSize(),
            nullptr,
            &scale_pixel_shader_),
            "ID3D11Device::CreatePixelShader (capture scale)");

        D3D11_SAMPLER_DESC sampler_desc{};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        ThrowIfFailed(dev_->CreateSamplerState(&sampler_desc, &scale_sampler_),
            "ID3D11Device::CreateSamplerState (capture scale)");
    }

    texture_desc.Usage = D3D11_USAGE_STAGING;
    texture_desc.BindFlags = 0;
    texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ThrowIfFailed(dev_->CreateTexture2D(&texture_desc, nullptr, &staging_tex_),
        "ID3D11Device::CreateTexture2D (staging)");
}

bool ScreenCapture::Acquire(FramePacket& out) {
    const auto start = std::chrono::steady_clock::now();

    if (!dup_) {
        Reinit();
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO frame_info{};
    const HRESULT acquire_result = dup_->AcquireNextFrame(
        acquire_timeout_ms_, &frame_info, &resource);

    if (acquire_result == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;
    }

    if (FAILED(acquire_result)) {
        Reinit();
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktop_texture;
    if (FAILED(resource.As(&desktop_texture))) {
        dup_->ReleaseFrame();
        return false;
    }

    if (scaled_tex_) {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> desktop_srv;
        if (FAILED(dev_->CreateShaderResourceView(
                desktop_texture.Get(), nullptr, &desktop_srv))) {
            dup_->ReleaseFrame();
            return false;
        }

        const D3D11_VIEWPORT viewport{
            0.0f,
            0.0f,
            static_cast<float>(frame_w_),
            static_cast<float>(frame_h_),
            0.0f,
            1.0f};
        ctx_->OMSetRenderTargets(1, scaled_rtv_.GetAddressOf(), nullptr);
        ctx_->RSSetViewports(1, &viewport);
        ctx_->IASetInputLayout(nullptr);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->VSSetShader(scale_vertex_shader_.Get(), nullptr, 0);
        ctx_->PSSetShader(scale_pixel_shader_.Get(), nullptr, 0);
        ctx_->PSSetShaderResources(0, 1, desktop_srv.GetAddressOf());
        ctx_->PSSetSamplers(0, 1, scale_sampler_.GetAddressOf());
        ctx_->Draw(3, 0);

        ID3D11ShaderResourceView* null_srv = nullptr;
        ctx_->PSSetShaderResources(0, 1, &null_srv);
        ctx_->CopyResource(staging_tex_.Get(), scaled_tex_.Get());
    } else {
        ctx_->CopyResource(staging_tex_.Get(), desktop_texture.Get());
    }
    dup_->ReleaseFrame();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx_->Map(staging_tex_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }

    const cv::Mat bgra(
        static_cast<int>(frame_h_), static_cast<int>(frame_w_),
        CV_8UC4, mapped.pData, mapped.RowPitch);
    out.bgr = bgra.clone();
    ctx_->Unmap(staging_tex_.Get(), 0);

    out.capture_ts = std::chrono::steady_clock::now();
    cap_ema_.Update(std::chrono::duration<float, std::milli>(out.capture_ts - start).count());
    return true;
}

float ScreenCapture::GetCaptureLatencyMs() const {
    return cap_ema_.Get();
}

void ScreenCapture::Reinit() {
    dup_.Reset();

    try {
        Init(descriptor_);
    } catch (const std::exception&) {
    }
}

}
