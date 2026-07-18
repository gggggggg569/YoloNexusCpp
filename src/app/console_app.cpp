#include "console_app.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>

#include <conio.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "console_dashboard.h"
#include "application/config_file.h"
#include "application/runtime_controller.h"
#include "core/capture/source_catalog.h"
#include "core/hardware/system_info.h"
#include "core/platform/utf8.h"

namespace yolo_nexus {
namespace {

std::atomic_bool stop_requested = false;

BOOL WINAPI HandleConsoleSignal(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        stop_requested.store(true);
        return TRUE;
    }
    return FALSE;
}

void WaitForEnterBeforeExit(std::string_view message) {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (input == nullptr || input == INVALID_HANDLE_VALUE ||
        GetConsoleMode(input, &mode) == FALSE) {
        return;
    }

    std::cout << message << '\n';
    std::cout.flush();
    while (_getch() != '\r') {
    }
}

class ConsoleCtrlCInput final {
public:
    ConsoleCtrlCInput() {
        input_ = GetStdHandle(STD_INPUT_HANDLE);
        if (input_ == nullptr || input_ == INVALID_HANDLE_VALUE ||
            GetConsoleMode(input_, &original_mode_) == FALSE) {
            return;
        }

        active_ = SetConsoleMode(
            input_, original_mode_ & ~ENABLE_PROCESSED_INPUT) != FALSE;
    }

    ~ConsoleCtrlCInput() {
        Restore();
    }

    bool WasPressed() const {
        if (!active_) {
            return false;
        }

        DWORD pending = 0;
        while (GetNumberOfConsoleInputEvents(input_, &pending) != FALSE &&
               pending > 0) {
            INPUT_RECORD records[32]{};
            const DWORD requested = (std::min)(
                pending, static_cast<DWORD>(std::size(records)));
            DWORD read = 0;
            if (ReadConsoleInputW(input_, records, requested, &read) == FALSE) {
                return false;
            }

            for (DWORD index = 0; index < read; ++index) {
                const INPUT_RECORD& record = records[index];
                if (record.EventType != KEY_EVENT ||
                    record.Event.KeyEvent.bKeyDown == FALSE ||
                    record.Event.KeyEvent.wVirtualKeyCode != 'C') {
                    continue;
                }

                const DWORD control_state = record.Event.KeyEvent.dwControlKeyState;
                if ((control_state &
                     (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0) {
                    return true;
                }
            }
        }
        return false;
    }

    void Restore() {
        if (active_) {
            SetConsoleMode(input_, original_mode_);
            active_ = false;
        }
    }

private:
    HANDLE input_ = INVALID_HANDLE_VALUE;
    DWORD original_mode_ = 0;
    bool active_ = false;
};

class TimerResolution final {
public:
    TimerResolution() {
        active_ = timeBeginPeriod(1) == TIMERR_NOERROR;
    }

    ~TimerResolution() {
        if (active_) {
            timeEndPeriod(1);
        }
    }

private:
    bool active_ = false;
};

class ComApartment final {
public:
    ComApartment() {
        const HRESULT result = CoInitializeEx(
            nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
            throw std::runtime_error("CoInitializeEx failed.");
        }
        initialized_ = SUCCEEDED(result);
    }

    ~ComApartment() {
        if (initialized_) {
            CoUninitialize();
        }
    }

private:
    bool initialized_ = false;
};

std::filesystem::path GetExecutableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("Cannot determine the executable directory.");
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::string_view Trim(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

std::string_view SourceTypeName(CaptureSourceKind kind) {
    switch (kind) {
        case CaptureSourceKind::Monitor:
            return "Monitor";
        case CaptureSourceKind::Window:
            return "Window";
        case CaptureSourceKind::Camera:
            return "Camera";
    }
    return "Unknown";
}

std::string FormatSourceName(const CaptureSourceDescriptor& source) {
    return '[' + std::string(SourceTypeName(source.kind)) + "] " + source.name;
}

void PrintSources(const std::vector<CaptureSourceDescriptor>& sources) {
    std::cout << "\nDetected capture sources:\n";
    for (std::size_t index = 0; index < sources.size(); ++index) {
        std::cout << "  " << (index + 1) << ". "
                  << FormatSourceName(sources[index]) << '\n';
    }
}

std::size_t PromptForSource(
    const std::vector<CaptureSourceDescriptor>& sources) {
    if (sources.empty()) {
        throw std::runtime_error("No capture sources were found.");
    }

    while (true) {
        std::cout << "\nEnter the capture source number [1-"
                  << sources.size() << "]: " << std::flush;

        std::string input;
        if (!std::getline(std::cin, input)) {
            throw std::runtime_error("Console input was closed.");
        }
        const std::string_view value = Trim(input);
        std::size_t selection = 0;
        const auto conversion = std::from_chars(
            value.data(), value.data() + value.size(), selection);
        if (conversion.ec != std::errc{} ||
            conversion.ptr != value.data() + value.size()) {
            std::cout << "Enter an integer from the displayed range.\n";
            continue;
        }
        if (selection == 0 || selection > sources.size()) {
            std::cout << "There is no capture source with that number.\n";
            continue;
        }
        return selection - 1;
    }
}

void ClearConsoleScreen() {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (output == nullptr || output == INVALID_HANDLE_VALUE ||
        GetConsoleMode(output, &mode) == FALSE ||
        GetConsoleScreenBufferInfo(output, &info) == FALSE) {
        return;
    }

    const DWORD cell_count = static_cast<DWORD>(info.dwSize.X) *
        static_cast<DWORD>(info.dwSize.Y);
    const COORD origin{0, 0};
    DWORD written = 0;
    FillConsoleOutputCharacterA(output, ' ', cell_count, origin, &written);
    FillConsoleOutputAttribute(
        output, info.wAttributes, cell_count, origin, &written);
    SetConsoleCursorPosition(output, origin);
}

void PrintSystemInfo(const SystemInfoSnapshot& info) {
    std::cout
        << "\nSystem:\n"
        << "  CPU:  " << info.cpu_name << '\n'
        << "  GPU:  " << info.gpu_name << '\n'
        << "  RAM:  " << FormatBytes(info.total_memory_bytes) << '\n'
        << "  VRAM: " << FormatBytes(info.dedicated_video_memory_bytes) << '\n'
        << "  CPU cores: " << info.physical_core_count << " physical / "
        << info.logical_core_count << " logical\n";
}

void PrintConfiguration(
    const AppSettings& settings,
    const CaptureSourceDescriptor& source) {
    std::cout
        << "\nLaunch configuration:\n"
        << "  Model: " << PathToUtf8(settings.model_path) << '\n'
        << "  Backend: directml\n"
        << "  Input resolution: "
        << settings.input_width << 'x' << settings.input_height << '\n'
        << "  Output resolution: "
        << settings.preview_width << 'x' << settings.preview_height << '\n'
        << "  FPS: draw=" << settings.draw_fps
        << ", inference=" << settings.inference_fps << '\n'
        << "  Classes: ";
    if (settings.use_model_classes) {
        std::cout << "full (names from model)";
    }
    for (std::size_t index = 0; index < settings.detection_classes.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        const auto& detection_class = settings.detection_classes[index];
        std::cout << detection_class.id << ':' << detection_class.name;
    }
    std::cout << "\n  Source: " << FormatSourceName(source) << '\n';
}

int RunApplication() {
    const std::filesystem::path executable_directory = GetExecutableDirectory();
    const std::filesystem::path config_path = executable_directory / "config.txt";
    if (!std::filesystem::is_regular_file(config_path)) {
        std::cerr
            << "Cannot find config.txt next to the executable.\n"
            << "Path: " << PathToUtf8(config_path) << '\n';
        WaitForEnterBeforeExit("Press Enter to close the console.");
        return 1;
    }

    const AppSettings config = LoadConfigFile(config_path);
    TimerResolution timer_resolution;
    ComApartment com_apartment;

    std::cout << "Yolo Nexus Console\n"
              << "Config: " << PathToUtf8(config_path) << '\n';

    const auto sources = EnumerateCaptureSources();
    PrintSources(sources);
    const CaptureSourceDescriptor& source = sources[PromptForSource(sources)];

    AppSettings settings = config;
    settings.source_kind = source.kind;
    settings.source_id = source.id;
    settings.inference_fps = GetEffectiveInferenceFps(settings);
    ValidateSettings(settings);

    ClearConsoleScreen();
    std::cout << "Yolo Nexus Console\n"
              << "Config: " << PathToUtf8(config_path) << '\n';
    PrintSystemInfo(CollectSystemInfo());
    PrintConfiguration(settings, source);
    std::cout << "\nLoading the model and starting inference...\n"
              << "Press Ctrl+C to stop, or Right Alt to stop and keep "
                 "the console open.\n";

    ConsoleCtrlCInput ctrl_c_input;
    RuntimeController runtime;
    runtime.Start(settings, source.name);

    ConsoleDashboard dashboard;
    RuntimeStatus previous_status = RuntimeStatus::Stopped;
    std::string previous_backend;
    bool stopped_by_right_alt = false;
    auto next_dashboard_update = std::chrono::steady_clock::now();
    while (!stop_requested.load()) {
        const RuntimeSnapshot snapshot = runtime.GetSnapshot();
        if (snapshot.status == RuntimeStatus::Running &&
            snapshot.active_backend != previous_backend) {
            previous_backend = snapshot.active_backend;
            std::cout << "Neural network is running on device: "
                      << snapshot.active_device << " ("
                      << previous_backend << ")\n";
        }
        if (snapshot.status == RuntimeStatus::Error) {
            dashboard.Finish();
            runtime.Shutdown();
            ctrl_c_input.Restore();
            std::cerr << "\nRuntime error: " << snapshot.status_text << '\n';
            WaitForEnterBeforeExit("Press Enter to close the console.");
            return 1;
        }
        if (snapshot.status == RuntimeStatus::Stopped &&
            previous_status != RuntimeStatus::Stopped) {
            break;
        }
        previous_status = snapshot.status;

        const auto now = std::chrono::steady_clock::now();
        if (snapshot.status == RuntimeStatus::Running &&
            now >= next_dashboard_update) {
            dashboard.Render(snapshot);
            next_dashboard_update = now + std::chrono::seconds(1);
        }

        if ((GetAsyncKeyState(VK_RMENU) & 0x8000) != 0) {
            stopped_by_right_alt = true;
            stop_requested.store(true);
        }
        if (ctrl_c_input.WasPressed()) {
            stop_requested.store(true);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    runtime.Shutdown();
    ctrl_c_input.Restore();
    dashboard.Finish();
    std::cout << "Stopped.\n";
    if (stopped_by_right_alt) {
        WaitForEnterBeforeExit(
            "The console will remain open. Press Enter to close it.");
    }
    return 0;
}

}

int RunConsoleApplication() {
    stop_requested.store(false);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleCtrlHandler(HandleConsoleSignal, TRUE);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    try {
        return RunApplication();
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        WaitForEnterBeforeExit("Press Enter to close the console.");
        return 1;
    }
}

}
