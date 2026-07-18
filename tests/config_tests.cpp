#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "application/config_file.h"
#include "core/inference/class_metadata.h"

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() / "yolo_nexus_config_tests") {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& Path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    if (!output) {
        throw std::runtime_error("Cannot create test file.");
    }
}

template <typename Function>
void RequireThrows(Function&& function, const char* message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

}

int main() {
    try {
        TemporaryDirectory temporary;
        const auto model_path = temporary.Path() / "model.onnx";
        WriteText(model_path, "model");

        const auto config_path = temporary.Path() / "config.txt";
        WriteText(config_path,
            "model_path: \"model.onnx\"\n"
            "input_w: 640\n"
            "input_h: 384\n"
            "classes: \"0:person, 2:car\"\n"
            "draw_fps: 120\n"
            "inference_fps: 30\n"
            "out_w: 1280\n"
            "out_h: 720\n");

        const auto settings = yolo_nexus::LoadConfigFile(config_path);
        Require(settings.model_path == model_path,
            "Relative model path was not resolved.");
        Require(settings.input_width == 640 && settings.input_height == 384,
            "Input resolution was not parsed.");
        Require(settings.detection_classes.size() == 2 &&
                settings.detection_classes[0].id == 0 &&
                settings.detection_classes[0].name == "person" &&
                settings.detection_classes[1].id == 2 &&
                settings.detection_classes[1].name == "car",
            "Detection classes were not parsed.");
        Require(!settings.use_model_classes,
            "Explicit classes must enable class filtering.");
        Require(settings.draw_fps == 120, "draw_fps was not parsed.");
        Require(settings.inference_fps == 30, "inference_fps was not parsed.");
        auto capped_inference = settings;
        capped_inference.draw_fps = 20;
        Require(yolo_nexus::GetEffectiveInferenceFps(capped_inference) == 20,
            "inference_fps must be capped by draw_fps.");
        capped_inference.inference_fps = 10;
        Require(yolo_nexus::GetEffectiveInferenceFps(capped_inference) == 10,
            "inference_fps below draw_fps must remain unchanged.");
        Require(settings.preview_width == 1280 && settings.preview_height == 720,
            "Output resolution was not parsed.");
        Require(settings.source_id.empty(),
            "Capture source must be selected interactively.");

        auto unrestricted_resolution = settings;
        unrestricted_resolution.source_id = "monitor:test";
        unrestricted_resolution.input_width = 16;
        unrestricted_resolution.input_height = 8192;
        yolo_nexus::ValidateSettings(unrestricted_resolution);

        WriteText(config_path, "model_path: model.onnx\n");
        RequireThrows([&] { static_cast<void>(yolo_nexus::LoadConfigFile(config_path)); },
            "Missing required config fields must be rejected.");

        WriteText(config_path, "unknown: value\n");
        RequireThrows([&] { static_cast<void>(yolo_nexus::LoadConfigFile(config_path)); },
            "Unknown config key must be rejected.");

        WriteText(config_path,
            "model_path: model.onnx\ndraw_fps: invalid\n"
            "inference_fps: 30\nout_w: 1280\nout_h: 720\n"
            "input_w: 640\ninput_h: 640\nclasses: 0:person\n");
        RequireThrows([&] { static_cast<void>(yolo_nexus::LoadConfigFile(config_path)); },
            "Invalid numeric value must be rejected.");

        WriteText(config_path,
            "model_path: model.onnx\nmodel_path: other.onnx\n"
            "input_w: 640\ninput_h: 640\nclasses: 0:person\n"
            "draw_fps: 120\ninference_fps: 30\nout_w: 1280\nout_h: 720\n");
        RequireThrows([&] { static_cast<void>(yolo_nexus::LoadConfigFile(config_path)); },
            "Duplicate config keys must be rejected.");

        WriteText(config_path,
            "model_path: model.onnx\n"
            "input_w: 640\ninput_h: 640\nclasses:\n"
            "draw_fps: 120\ninference_fps: 30\n"
            "out_w: 1280\nout_h: 720\n");
        const auto unfiltered_settings = yolo_nexus::LoadConfigFile(config_path);
        Require(unfiltered_settings.detection_classes.empty(),
            "An empty classes value must enable all classes.");
        Require(unfiltered_settings.use_model_classes,
            "An empty classes value must load model classes.");

        WriteText(config_path,
            "model_path: model.onnx\n"
            "input_w: 640\ninput_h: 640\nclasses: full\n"
            "draw_fps: 120\ninference_fps: 30\n"
            "out_w: 1280\nout_h: 720\n");
        const auto full_settings = yolo_nexus::LoadConfigFile(config_path);
        Require(full_settings.use_model_classes && full_settings.detection_classes.empty(),
            "classes: full must load every class from model metadata.");

        const auto model_classes = yolo_nexus::ParseClassNamesMetadata(
            "{1: 'part', 0: 'tank'}");
        Require(model_classes.size() == 2 &&
                model_classes[0].id == 0 && model_classes[0].name == "tank" &&
                model_classes[1].id == 1 && model_classes[1].name == "part",
            "Model class metadata was not parsed and sorted.");
        RequireThrows(
            [] { static_cast<void>(yolo_nexus::ParseClassNamesMetadata("invalid")); },
            "Invalid model class metadata must be rejected.");

        std::cout << "config tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
