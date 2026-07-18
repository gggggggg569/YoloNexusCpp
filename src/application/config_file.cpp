#include "config_file.h"

#include <charconv>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "core/platform/utf8.h"

namespace yolo_nexus {
namespace {

std::string_view Trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

std::string ParseText(std::string_view value) {
    value = Trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    return std::string(value);
}

int ParseInteger(std::string_view value, std::string_view key, std::size_t line_number) {
    value = Trim(value);
    int result = 0;
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), result);
    if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size()) {
        throw std::runtime_error(
            "Invalid integer for '" + std::string(key) + "' at config line " +
            std::to_string(line_number) + '.');
    }
    return result;
}

std::vector<DetectionClass> ParseClasses(std::string_view value, std::size_t line_number) {
    const std::string text = ParseText(value);
    std::vector<DetectionClass> classes;
    std::string_view remaining = text;
    while (!remaining.empty()) {
        const std::size_t comma = remaining.find(',');
        const std::string_view entry = Trim(remaining.substr(0, comma));
        const std::size_t separator = entry.find(':');
        if (entry.empty() || separator == std::string_view::npos) {
            throw std::runtime_error(
                "Expected classes in 'id:name' format at config line " +
                std::to_string(line_number) + '.');
        }

        const int id = ParseInteger(entry.substr(0, separator), "classes", line_number);
        const std::string name(Trim(entry.substr(separator + 1)));
        if (name.empty()) {
            throw std::runtime_error(
                "Empty class name at config line " + std::to_string(line_number) + '.');
        }
        classes.push_back({id, name});

        if (comma == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(comma + 1);
    }
    return classes;
}

bool UseModelClasses(std::string_view value) {
    const std::string text = ParseText(value);
    if (text.empty()) {
        return true;
    }
    if (text.size() != 4) {
        return false;
    }
    return std::tolower(static_cast<unsigned char>(text[0])) == 'f' &&
        std::tolower(static_cast<unsigned char>(text[1])) == 'u' &&
        std::tolower(static_cast<unsigned char>(text[2])) == 'l' &&
        std::tolower(static_cast<unsigned char>(text[3])) == 'l';
}

struct RequiredFields {
    bool model_path = false;
    bool inference_fps = false;
    bool draw_fps = false;
    bool output_width = false;
    bool output_height = false;
    bool input_width = false;
    bool input_height = false;
    bool classes = false;

    bool Complete() const {
        return model_path && inference_fps && draw_fps && output_width && output_height &&
            input_width && input_height && classes;
    }
};

}

AppSettings LoadConfigFile(const std::filesystem::path& config_path) {
    std::ifstream input(config_path);
    if (!input) {
        throw std::runtime_error("Cannot open config file: " + config_path.string());
    }

    AppSettings settings;
    RequiredFields required;
    std::unordered_set<std::string> seen_keys;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        std::string_view content = Trim(line);
        if (line_number == 1 && content.starts_with("\xEF\xBB\xBF")) {
            content.remove_prefix(3);
            content = Trim(content);
        }
        if (content.empty() || content.front() == '#' || content.front() == ';') {
            continue;
        }

        const std::size_t separator = content.find(':');
        if (separator == std::string_view::npos) {
            throw std::runtime_error(
                "Expected 'key: value' at config line " + std::to_string(line_number) + '.');
        }

        const std::string key(Trim(content.substr(0, separator)));
        const std::string_view value = Trim(content.substr(separator + 1));
        if (!seen_keys.insert(key).second) {
            throw std::runtime_error(
                "Duplicate config key '" + key + "' at line " +
                std::to_string(line_number) + '.');
        }
        if (key == "model_path") {
            settings.model_path = Utf8ToPath(ParseText(value));
            required.model_path = true;
        } else if (key == "inference_fps") {
            settings.inference_fps = ParseInteger(value, key, line_number);
            required.inference_fps = true;
        } else if (key == "draw_fps") {
            settings.draw_fps = ParseInteger(value, key, line_number);
            required.draw_fps = true;
        } else if (key == "out_w") {
            settings.preview_width = ParseInteger(value, key, line_number);
            required.output_width = true;
        } else if (key == "out_h") {
            settings.preview_height = ParseInteger(value, key, line_number);
            required.output_height = true;
        } else if (key == "input_w") {
            settings.input_width = ParseInteger(value, key, line_number);
            required.input_width = true;
        } else if (key == "input_h") {
            settings.input_height = ParseInteger(value, key, line_number);
            required.input_height = true;
        } else if (key == "classes") {
            settings.use_model_classes = UseModelClasses(value);
            settings.detection_classes = settings.use_model_classes
                ? std::vector<DetectionClass>{}
                : ParseClasses(value, line_number);
            required.classes = true;
        } else {
            throw std::runtime_error(
                "Unknown config key '" + key + "' at line " + std::to_string(line_number) + '.');
        }
    }

    if (!required.Complete()) {
        throw std::runtime_error(
            "Config must define model_path, inference_fps, draw_fps, out_w, out_h, "
            "input_w, input_h and classes.");
    }
    if (settings.model_path.is_relative()) {
        settings.model_path = config_path.parent_path() / settings.model_path;
    }
    settings.model_path = settings.model_path.lexically_normal();
    return settings;
}

}
