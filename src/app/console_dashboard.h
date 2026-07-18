#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

#include "application/runtime_snapshot.h"

namespace yolo_nexus {

class ConsoleDashboard final {
public:
    ConsoleDashboard();

    void Render(const RuntimeSnapshot& snapshot);
    void Finish();

private:
    std::vector<std::string> BuildLines(const RuntimeSnapshot& snapshot) const;
    void EnsureRegion(std::size_t line_count);
    bool IsConsoleScrolledAway() const;
    void SetCursorAfterRegion(std::size_t line_count) const;

    HANDLE output_ = INVALID_HANDLE_VALUE;
    COORD origin_{};
    std::size_t region_line_count_ = 0;
    bool interactive_ = false;
    bool initialized_ = false;
};

}
