#include "top/Dcmi.h"
#include "top/Proc.h"
#include "top/Tui.h"
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <unistd.h>
#ifndef ASCEND_MONITOR_TUI_VERSION
#define ASCEND_MONITOR_TUI_VERSION "dev"
#endif
int main(int argc, char** argv) {
    double interval = 1; bool json = false, ascii = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "ascend-monitor-tui " ASCEND_MONITOR_TUI_VERSION " - local Ascend NPU monitor\n"
                "Usage: ascend-monitor-tui [--interval SECONDS] [--json] [--ascii]\n"
                "  --json       Two samples, emit one JSON snapshot (CPU needs a delta)\n"
                "  --interval   History/JSON interval, 0.2 to 60 seconds (default 1)\n"
                "  --ascii      Use ASCII graphics instead of Unicode Braille\n"
                "  --version    Show version\n"
                "Keys: h help, q quit, Tab focus, arrows/PageUp/PageDown scroll.\n"; return 0;
        }
        if (arg == "--version") { std::cout << "ascend-monitor-tui " ASCEND_MONITOR_TUI_VERSION "\n"; return 0; }
        if (arg == "--ascii") { ascii = true; continue; }
        if (arg == "--json") { json = true; continue; }
        if (arg == "--interval" && i + 1 < argc) {
            try { size_t end = 0; std::string value = argv[++i]; interval = std::stod(value, &end);
                if (end != value.size() || !std::isfinite(interval) || interval < .2 || interval > 60) throw 0;
            } catch (...) { std::cerr << "Interval must be between 0.2 and 60 seconds\n"; return 2; } continue;
        }
        std::cerr << "Unknown or incomplete option: " << arg << '\n'; return 2;
    }
    try {
        if (json) {
            ascend::Dcmi dcmi; ascend::ProcReader proc; ascend::Snapshot first;
            dcmi.collect(first); proc.collect(first); std::this_thread::sleep_for(std::chrono::duration<double>(interval));
            auto begin = ascend::Clock::now(); ascend::Snapshot s; dcmi.collect(s); proc.collect(s);
            s.collectionSeconds = std::chrono::duration<double>(ascend::Clock::now() - begin).count();
            std::cout << ascend::snapshotJson(s) << '\n'; return s.devices.empty() || !s.error.empty() ? 1 : 0;
        }
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) { std::cerr << "Interactive terminal required; use --json for a snapshot\n"; return 2; }
        auto term = std::getenv("TERM"); if (!term || std::string(term) == "dumb") { std::cerr << "Set TERM to your terminal type\n"; return 2; }
        return ascend::runTui(interval, ascii);
    } catch (const std::exception& e) { std::cerr << "ascend-monitor-tui: " << e.what() << '\n'; return 1; }
}
