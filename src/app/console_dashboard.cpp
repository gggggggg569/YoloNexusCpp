#include "console_dashboard.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>

namespace yolo_nexus {

ConsoleDashboard::ConsoleDashboard()
    : output_(GetStdHandle(STD_OUTPUT_HANDLE)) {
    DWORD mode = 0;
    interactive_ = output_ != nullptr && output_ != INVALID_HANDLE_VALUE &&
        GetConsoleMode(output_, &mode) != FALSE;
}

void ConsoleDashboard::Render(const RuntimeSnapshot& snapshot) {
    const std::vector<std::string> lines = BuildLines(snapshot);
    if (!interactive_) {
        for (const auto& line : lines) {
            std::cout << line << '\n';
        }
        std::cout << std::flush;
        return;
    }
    if (IsConsoleScrolledAway()) {
        return;
    }

    EnsureRegion(lines.size());
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(output_, &info)) {
        return;
    }

    const DWORD line_width = static_cast<DWORD>(info.dwSize.X);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const COORD position{
            0,
            static_cast<SHORT>(origin_.Y + static_cast<SHORT>(index))};
        DWORD written = 0;
        FillConsoleOutputCharacterA(output_, ' ', line_width, position, &written);
        FillConsoleOutputAttribute(
            output_, info.wAttributes, line_width, position, &written);
        SetConsoleCursorPosition(output_, position);

        const std::string_view visible(
            lines[index].data(),
            (std::min)(
                lines[index].size(),
                line_width > 0 ? static_cast<std::size_t>(line_width - 1) : std::size_t{0}));
        WriteConsoleA(
            output_, visible.data(), static_cast<DWORD>(visible.size()), &written, nullptr);
    }
    SetCursorAfterRegion(lines.size());
}

void ConsoleDashboard::Finish() {
    if (interactive_ && initialized_) {
        SetCursorAfterRegion(region_line_count_);
        std::cout << '\n';
    }
}

std::vector<std::string> ConsoleDashboard::BuildLines(
    const RuntimeSnapshot& snapshot) const {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1);

    std::vector<std::string> lines;
    lines.reserve(24 + snapshot.detection_class_counts.size());
    const auto add_metric = [&](std::string_view name, float value, std::string_view unit) {
        output.str(std::string{});
        output.clear();
        output << std::left << std::setw(12) << name << ": " << value << unit;
        lines.push_back(output.str());
    };

    lines.emplace_back();
    lines.emplace_back("Detected objects:");
    output.str(std::string{});
    output.clear();
    output << "  Total  : " << snapshot.total_detection_count;
    lines.push_back(output.str());
    lines.emplace_back();
    lines.emplace_back("Classes:");
    for (const DetectionClassCount& class_count : snapshot.detection_class_counts) {
        output.str(std::string{});
        output.clear();
        output << "  " << class_count.id << ' ' << class_count.name
               << " : " << class_count.count;
        lines.push_back(output.str());
    }
    lines.emplace_back();
    lines.emplace_back("Current metrics (updated once per second):");
    add_metric("Draw FPS", snapshot.draw_fps, "");
    add_metric("AI FPS", snapshot.inference_fps, "");
    add_metric("Capture FPS", snapshot.capture_fps, "");
    add_metric("Cap Lat", snapshot.capture_latency_ms, " ms");
    add_metric("AI Lat", snapshot.inference_latency_ms, " ms");
    add_metric("E2E Lat", snapshot.preview_latency_ms, " ms");

    output.str(std::string{});
    output.clear();
    output << "Load        : CPU " << snapshot.cpu_load_percent
           << "% | GPU " << snapshot.gpu_load_percent
           << "% | RAM " << snapshot.memory_load_percent << '%';
    lines.push_back(output.str());

    lines.emplace_back();
    lines.emplace_back("Session averages:");
    add_metric("Draw FPS", snapshot.average_draw_fps, "");
    add_metric("AI FPS", snapshot.average_inference_fps, "");
    add_metric("Capture FPS", snapshot.average_capture_fps, "");
    add_metric("Cap Lat", snapshot.average_capture_latency_ms, " ms");
    add_metric("AI Lat", snapshot.average_inference_latency_ms, " ms");
    add_metric("E2E Lat", snapshot.average_preview_latency_ms, " ms");
    add_metric("AI FPS Max", snapshot.maximum_inference_fps, "");
    add_metric("AI 1% Low", snapshot.one_percent_low_inference_fps, "");
    add_metric("AI 0.1% Low", snapshot.point_one_percent_low_inference_fps, "");
    return lines;
}

void ConsoleDashboard::EnsureRegion(std::size_t line_count) {
    if (initialized_) {
        return;
    }
    for (std::size_t index = 0; index < line_count; ++index) {
        std::cout << '\n';
    }
    std::cout << std::flush;

    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(output_, &info)) {
        interactive_ = false;
        return;
    }
    origin_.X = 0;
    origin_.Y = static_cast<SHORT>(
        (std::max)(0, static_cast<int>(info.dwCursorPosition.Y) -
            static_cast<int>(line_count)));
    region_line_count_ = line_count;
    initialized_ = true;
}

bool ConsoleDashboard::IsConsoleScrolledAway() const {
    if (!initialized_) {
        return false;
    }
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(output_, &info)) {
        return false;
    }
    return info.dwCursorPosition.Y < info.srWindow.Top ||
        info.dwCursorPosition.Y > info.srWindow.Bottom;
}

void ConsoleDashboard::SetCursorAfterRegion(std::size_t line_count) const {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(output_, &info)) {
        return;
    }
    const SHORT last_row = static_cast<SHORT>(info.dwSize.Y - 1);
    const COORD position{
        0,
        (std::min)(
            last_row,
            static_cast<SHORT>(origin_.Y + static_cast<SHORT>(line_count)))};
    SetConsoleCursorPosition(output_, position);
}

}
