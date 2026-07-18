#include "onnxruntime_loader.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace yolo_nexus {
namespace {

std::filesystem::path GetExecutableDirectory() {
    std::vector<wchar_t> buffer(260);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::runtime_error("Cannot resolve the executable directory.");
        }
        if (length < buffer.size() - 1) {
            return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::string WindowsErrorMessage(DWORD error_code) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        0,
        reinterpret_cast<wchar_t*>(&message),
        0,
        nullptr);
    if (length == 0 || message == nullptr) {
        return "Windows error " + std::to_string(error_code);
    }

    const int utf8_size = WideCharToMultiByte(
        CP_UTF8, 0, message, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(utf8_size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, message, static_cast<int>(length), result.data(), utf8_size, nullptr, nullptr);
    LocalFree(message);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
        result.pop_back();
    }
    return result;
}

}

void LoadOnnxRuntime() {
    static std::mutex mutex;
    static HMODULE runtime_module = nullptr;

    std::lock_guard<std::mutex> lock(mutex);
    const std::filesystem::path runtime_directory =
        GetExecutableDirectory() / "runtimes" / "directml";
    const std::filesystem::path runtime_path = runtime_directory / "onnxruntime.dll";

    if (runtime_module != nullptr) {
        return;
    }

    if (!std::filesystem::is_regular_file(runtime_path)) {
        throw std::runtime_error(
            "ONNX Runtime backend is missing: " + runtime_path.string());
    }

    if (!SetDllDirectoryW(runtime_directory.c_str())) {
        throw std::runtime_error(
            "Cannot register the ONNX Runtime directory: " +
            WindowsErrorMessage(GetLastError()));
    }

    runtime_module = LoadLibraryW(runtime_path.c_str());
    if (runtime_module == nullptr) {
        throw std::runtime_error(
            "Cannot load " + runtime_path.string() + ": " +
            WindowsErrorMessage(GetLastError()));
    }

    using GetApiBaseFunction = const OrtApiBase*(ORT_API_CALL*)();
    const auto get_api_base = reinterpret_cast<GetApiBaseFunction>(
        GetProcAddress(runtime_module, "OrtGetApiBase"));
    if (get_api_base == nullptr) {
        throw std::runtime_error("The selected onnxruntime.dll does not export OrtGetApiBase.");
    }

    const OrtApiBase* api_base = get_api_base();
    const OrtApi* api = api_base == nullptr ? nullptr : api_base->GetApi(ORT_API_VERSION);
    if (api == nullptr) {
        throw std::runtime_error("The selected ONNX Runtime has an incompatible API version.");
    }

    Ort::InitApi(api);
}

}
