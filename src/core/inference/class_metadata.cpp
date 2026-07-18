#include "class_metadata.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace yolo_nexus {
namespace {

void SkipWhitespace(std::string_view text, std::size_t& position) {
    while (position < text.size() &&
           std::isspace(static_cast<unsigned char>(text[position])) != 0) {
        ++position;
    }
}

int ParseClassId(std::string_view text, std::size_t& position) {
    SkipWhitespace(text, position);
    const std::size_t begin = position;
    while (position < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
        ++position;
    }
    int id = -1;
    const auto conversion = std::from_chars(
        text.data() + begin, text.data() + position, id);
    if (begin == position || conversion.ec != std::errc{}) {
        throw std::runtime_error("Invalid class ID in ONNX metadata 'names'.");
    }
    return id;
}

std::string ParseClassName(std::string_view text, std::size_t& position) {
    SkipWhitespace(text, position);
    if (position >= text.size() ||
        (text[position] != '\'' && text[position] != '"')) {
        throw std::runtime_error("Invalid class name in ONNX metadata 'names'.");
    }

    const char quote = text[position++];
    std::string name;
    while (position < text.size() && text[position] != quote) {
        if (text[position] == '\\') {
            ++position;
            if (position >= text.size()) {
                throw std::runtime_error("Invalid escape in ONNX metadata 'names'.");
            }
        }
        name.push_back(text[position++]);
    }
    if (position >= text.size() || name.empty()) {
        throw std::runtime_error("Invalid class name in ONNX metadata 'names'.");
    }
    ++position;
    return name;
}

}

std::vector<DetectionClass> ParseClassNamesMetadata(std::string_view metadata) {
    std::size_t position = 0;
    SkipWhitespace(metadata, position);
    if (position >= metadata.size() || metadata[position++] != '{') {
        throw std::runtime_error("ONNX metadata 'names' must be a class dictionary.");
    }

    std::vector<DetectionClass> classes;
    std::unordered_set<int> class_ids;
    while (true) {
        SkipWhitespace(metadata, position);
        if (position < metadata.size() && metadata[position] == '}') {
            ++position;
            break;
        }

        const int id = ParseClassId(metadata, position);
        SkipWhitespace(metadata, position);
        if (position >= metadata.size() || metadata[position++] != ':') {
            throw std::runtime_error("Missing ':' in ONNX metadata 'names'.");
        }
        std::string name = ParseClassName(metadata, position);
        if (!class_ids.insert(id).second) {
            throw std::runtime_error("Duplicate class ID in ONNX metadata 'names'.");
        }
        classes.push_back({id, std::move(name)});

        SkipWhitespace(metadata, position);
        if (position < metadata.size() && metadata[position] == ',') {
            ++position;
            continue;
        }
        if (position >= metadata.size() || metadata[position] != '}') {
            throw std::runtime_error("Invalid separator in ONNX metadata 'names'.");
        }
    }

    SkipWhitespace(metadata, position);
    if (position != metadata.size() || classes.empty()) {
        throw std::runtime_error("ONNX metadata 'names' contains no valid classes.");
    }
    std::sort(classes.begin(), classes.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return classes;
}

}
