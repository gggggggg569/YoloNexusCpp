#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "core/inference/onnxruntime_loader.h"

namespace {

bool Contains(const std::vector<std::string>& values, const std::string& expected) {
    return std::find(values.begin(), values.end(), expected) != values.end();
}

}

int main(int argc, char* argv[]) {
    try {
        if (argc != 2 || std::string(argv[1]) != "directml") {
            throw std::runtime_error("Expected backend argument: directml.");
        }

        yolo_nexus::LoadOnnxRuntime();
        const std::string expected_provider = "DmlExecutionProvider";

        const std::vector<std::string> providers = Ort::GetAvailableProviders();
        if (!Contains(providers, expected_provider)) {
            throw std::runtime_error(
                "Selected runtime does not expose " + expected_provider + ".");
        }
        if (!Contains(providers, "CPUExecutionProvider")) {
            throw std::runtime_error("Selected runtime does not expose CPUExecutionProvider.");
        }
        Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "YoloNexusRuntimeLoaderTest");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
