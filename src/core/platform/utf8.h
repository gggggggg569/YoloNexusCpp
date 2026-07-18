#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace yolo_nexus {

std::string PathToUtf8(const std::filesystem::path& path);
std::filesystem::path Utf8ToPath(std::string_view value);

}
