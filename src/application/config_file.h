#pragma once

#include <filesystem>

#include "app_settings.h"

namespace yolo_nexus {

AppSettings LoadConfigFile(const std::filesystem::path& config_path);

}
