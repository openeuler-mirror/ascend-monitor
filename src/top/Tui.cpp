#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif
#include "top/Tui.h"
#include "top/Graph.h"
#include <clocale>
#include <cwchar>
#include <langinfo.h>
#include <cctype>
#include "top/Dcmi.h"
#include "top/Proc.h"
#include <algorithm>
#include <cmath>
#include <csignal>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <curses.h>

#ifndef ASCEND_MONITOR_TUI_VERSION
#define ASCEND_MONITOR_TUI_VERSION "dev"
#endif
namespace ascend {
Sampler::Sampler(double interval) : history_(interval) {
    worker_ = std::thread([this, interval] {
        Dcmi dcmi; ProcReader proc;
        while (true) {
            auto start = Clock::now(); auto s = std::make_shared<Snapshot>();
            try { dcmi.collect(*s); proc.collect(*s); }
            catch (const std::exception& e) { s->error = e.what(); }
            s->sampled = Clock::now(); s->wall = std::chrono::system_clock::now();
            s->collectionSeconds = std::chrono::duration<double>(s->sampled - start).count();
            std::unique_lock<std::mutex> lock(mutex_); latest_ = s; history_.append(*s);
            // Missed deadlines never cause back-to-back catch-up queries.
            auto delay = std::max(0.05, std::min(0.5, interval / 2.0) - s->collectionSeconds);
            if (wake_.wait_for(lock, std::chrono::duration<double>(delay), [this] { return stop_; })) break;
        }
    });
}
Sampler::~Sampler() { { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; } wake_.notify_all(); worker_.join(); }
std::shared_ptr<const Snapshot> Sampler::latest() { std::lock_guard<std::mutex> lock(mutex_); return latest_; }
History Sampler::history() { std::lock_guard<std::mutex> lock(mutex_); return history_.history(); }
static bool asciiGraphics = false;
static bool canDrawBraille() {
    // Respect the requested locale; C/POSIX and invalid locales fall back to ASCII.
    if (!std::setlocale(LC_ALL, "")) return false;
    std::string encoding = nl_langinfo(CODESET);
    std::string normalized;
    for (unsigned char c : encoding) if (std::isalnum(c)) normalized += static_cast<char>(std::toupper(c));
    return normalized == "UTF8" && wcwidth(L'\u2801') == 1 && wcwidth(L'\u28ff') == 1;
}
static chtype lineGlyph(chtype symbol) {
    if (!asciiGraphics) return symbol;
    if (symbol == ACS_HLINE) return '-';
    if (symbol == ACS_VLINE) return '|';
    return '+';
}
static volatile std::sig_atomic_t interrupted = 0;
static void stopSignal(int) { interrupted = 1; }
static void text(int y, int x, const std::string& s, int color = 0, int maxWidth = -1) {
    if (y < 0 || y >= LINES || x < 0 || x >= COLS) return;
    int width = std::max(0, COLS - x - 1); if (maxWidth >= 0) width = std::min(width, maxWidth);
    attrset(COLOR_PAIR(color)); mvaddnstr(y, x, safeText(s).c_str(), width); attrset(A_NORMAL);
}
static std::string fit(const std::string& s, size_t n) { if (s.size() > n) return s.substr(0, n - 1) + "~"; return s + std::string(n - s.size(), ' '); }
static std::string deviceId(std::optional<unsigned> v) { return v ? std::to_string(*v) : "N/A"; }
static int severity(Value v) { return !v ? 0 : *v >= 80 ? 3 : *v >= 50 ? 2 : 1; }
static void rule(int y, int x, int width) { if (y >= 0 && y < LINES && width > 0) mvhline(y, x, lineGlyph(ACS_HLINE), std::min(width, COLS - x - 1)); }
static void bar(int y, int x, int width, const std::string& label, Value v) {
    if (width < 15) return; text(y, x, label, severity(v));
    int w = width - 13;
    int fill = v && *v > 0 ? std::max(1, static_cast<int>(std::clamp(*v, 0.0, 100.0) * w / 100)) : 0;
    attrset(COLOR_PAIR(severity(v))); for (int i = 0; i < w; ++i) mvaddch(y, x + 5 + i, i < fill ? (static_cast<chtype>(' ') | A_REVERSE) : ' '); attrset(A_NORMAL);
    text(y, x + width - 7, number(v, 1, "%"), severity(v), 7);
}
static void chart(int y, int x, int width, int height, const History& h, Value HistoryPoint::*member, double secondsPerColumn, int color, Clock::time_point now, bool inverted = false) {
    if (width < 10 || height < 1) return;
    int resolution = asciiGraphics ? width : width * 2;
    auto columns = historyColumns(h, member, resolution, secondsPerColumn, now);
    if (asciiGraphics) {
        attrset(COLOR_PAIR(color));
        for (int col = 0; col < width; ++col) if (columns[col]) {
            int level = static_cast<int>(std::lround(std::clamp(*columns[col], 0.0, 100.0) / 100 * (height - 1)));
            for (int k = 0; k <= level; ++k)
                mvaddch(y + (inverted ? k : height - 1 - k), x + col, k == level ? '.' : ':');
        }
        attrset(A_NORMAL);
        return;
    }
    auto grid = brailleGraph(columns, height, inverted);
    for (int row = 0; row < height; ++row) for (int col = 0; col < width; ++col) {
        wchar_t symbol = grid[row][col];
        if (symbol == L' ') continue;
        // Shared host memory/swap cells retain both sets of dots on short panels.
        cchar_t existing{};
        wchar_t previous[CCHARW_MAX]{}; attr_t attributes = 0; short previousColor = 0;
        short selectedColor = static_cast<short>(color);
        if (mvwin_wch(stdscr, y + row, x + col, &existing) != ERR
            && getcchar(&existing, previous, &attributes, &previousColor, nullptr) != ERR
            && previous[0] >= 0x2800 && previous[0] <= 0x28ff) {
            symbol = static_cast<wchar_t>(symbol | previous[0]);
            selectedColor = previousColor;
        }
        wchar_t chars[] = {symbol, 0}; cchar_t cell{};
        if (setcchar(&cell, chars, A_NORMAL, selectedColor, nullptr) != ERR)
            mvadd_wch(y + row, x + col, &cell);
    }
}
static void ticks(int y, int x, int width, double secondsPerCell) {
    rule(y, x, width);
    int lastLeft = width - 5;
    text(y, x + lastLeft, " now ", 0, 5);
    for (int age = 30; age <= 600; age = age == 30 ? 60 : age + 60) {
        int position = width - 1 - static_cast<int>(std::lround(age / secondsPerCell));
        auto label = " " + std::to_string(age) + "s ";
        if (position < 0) break;
        int start = std::max(0, position - static_cast<int>(label.size()) / 2);
        if (start + static_cast<int>(label.size()) >= lastLeft) continue;
        text(y, x + start, label, 0, static_cast<int>(label.size()));
        lastLeft = start;
    }
}
static void help() {
    erase(); std::vector<std::string> lines = {
        "ascend-monitor-tui " ASCEND_MONITOR_TUI_VERSION " | Help",
        std::string("Graphics: ") + (asciiGraphics ? "ASCII (forced or locale fallback)" : "Unicode Braille (2x4 dots per cell)"), "", "q / Esc   Quit         h / ?   Toggle help",
        "Up/Down, j/k   Scroll focused list      Tab   Switch device/process focus",
        "PageUp/PageDown   Scroll a page        Home/End   First/last row",
        "Left/Right   Pan process command      Resize   Reflow without losing history", "",
        "NPU = physical device ID. Card/Chip identify the driver endpoint.",
        "DDR/HBM label describes capacity, never memory bandwidth.",
        "Power is CHIP power (W). Unsupported readings stay N/A; card MCU power is not substituted.",
        "NPU MEM = sum(used) / sum(total) of readable chips. CORE = mean of readable chips.",
        "Coverage (readable/total) is shown. Missing readings create gaps in charts.",
        "Host CPU: all logical CPUs combined, 0-100%; iowait counts as idle.",
        "Process CPU: one logical CPU = 100%; multithreaded processes can exceed 100%.",
        "First CPU sample is N/A. Process MEM = RSS / host RAM; TIME = elapsed wall time.",
        "Host MEM = total - available. No swap configured is 0%. Process exit/reuse resets CPU.",
        "One process may appear on several NPUs; each row has that device's NPU memory.",
        "Charts share a time scale: one averaged point per interval (default 1s).",
        "Default collection target: 0.5s. Finalized averages stay fixed; missing buckets stay blank.",
        "Collection is local. Driver errors/stale data are shown; press q to exit.", "", "Press h, ? or Esc to return."
    };
    for (size_t i = 0; i < lines.size() && static_cast<int>(i) < LINES; ++i) text(static_cast<int>(i), 1, lines[i]);
}
// All panels share real curses borders; text is clipped inside each cell.
static void vertical(int y, int x, int height) {
    if (height > 0) mvvline(y, x, lineGlyph(ACS_VLINE), height);
}
static void tableRule(int y, const std::vector<int>& columns, bool top = false, bool bottom = false) {
    rule(y, columns.front(), columns.back() - columns.front() + 1);
    for (size_t i = 0; i < columns.size(); ++i) {
        chtype joint = top ? ACS_TTEE : bottom ? ACS_BTEE : ACS_PLUS;
        if (i == 0) joint = top ? ACS_ULCORNER : bottom ? ACS_LLCORNER : ACS_LTEE;
        if (i + 1 == columns.size()) joint = top ? ACS_URCORNER : bottom ? ACS_LRCORNER : ACS_RTEE;
        mvaddch(y, columns[i], lineGlyph(joint));
    }
}
static void draw(const Snapshot& s, const History& history, int& deviceOffset, int& processOffset, bool deviceFocus, int pan, double interval) {
    erase();
    const int left = 1, right = COLS - 2;
    auto now = Clock::now();
    auto wall = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char date[80]{}; std::tm tm{}; localtime_r(&wall, &tm);
    std::strftime(date, sizeof(date), "%a %b %d %H:%M:%S %Y", &tm);
    text(0, left, date);
    text(0, std::max(33, COLS - 33), "h help | q quit | Tab switch");
    if (COLS < 90 || LINES < 24) {
        text(3, left, "Terminal too small: need 90 columns x 24 rows. Resize or press q.");
        return;
    }

    // Two lines per device, including on the side-by-side 120-column layout.
    const std::vector<int> columns{left, 21, 46, 65, right};
    auto cell = [&](int y, size_t col, const std::string& value, int color = 0) {
        text(y, columns[col] + 2, value, color, columns[col + 1] - columns[col] - 3);
    };
    tableRule(1, {left, right}, true);
    double age = std::chrono::duration<double>(now - s.sampled).count();
    text(2, left + 2, "ascend-monitor-tui " ASCEND_MONITOR_TUI_VERSION "    Driver: " + s.driver + "    CANN: " + s.cann
        + "    sample " + number(s.collectionSeconds, 2, "s")
        + (age > 3 ? "  STALE " + number(age, 0, "s") : ""), age > 3 ? 3 : 0, right - left - 3);
    vertical(2, left, 1); vertical(2, right, 1);
    tableRule(3, columns);
    for (size_t i = 1; i + 1 < columns.size(); ++i) mvaddch(3, columns[i], lineGlyph(ACS_TTEE));
    cell(4, 0, std::string(deviceFocus ? "> " : "") + "NPU  Name");
    const bool compact = LINES < 28;
    if (!compact) cell(5, 0, "Temp      Power");
    cell(4, 1, "Bus-Id / Memory type");
    if (!compact) cell(5, 1, "Memory used / total");
    cell(4, 2, "Card / Chip");
    if (!compact) cell(5, 2, "Core / Logic ID");
    cell(4, 3, "Memory / AI Core");
    for (int x : columns) vertical(4, x, compact ? 1 : 2);
    tableRule(compact ? 5 : 6, columns);

    int plotHeight = LINES >= 45 ? 3 : 1;
    int historyHeight = 5 + 2 * plotHeight;
    // Reserve the bordered process header and at least one process row.
    int visible = std::max(1, std::min(static_cast<int>(s.devices.size()), (LINES - 15 - historyHeight) / 3));
    deviceOffset = std::clamp(deviceOffset, 0, std::max(0, static_cast<int>(s.devices.size()) - visible));
    int y = compact ? 6 : 7;
    for (int i = deviceOffset; i < std::min(deviceOffset + visible, static_cast<int>(s.devices.size())); ++i) {
        const auto& d = s.devices[i]; int color = severity(d.memory.percent());
        cell(y, 0, fit(deviceId(d.physical), 5) + d.name, color);
        cell(y + 1, 0, fit(number(d.temperature, 0, "C"), 10) + number(d.power, 1, "W"), color);
        cell(y, 1, d.pci + " " + d.memory.kind, color);
        cell(y + 1, 1, bytes(d.memory.used) + " / " + bytes(d.memory.total), color);
        cell(y, 2, "Card " + std::to_string(d.card) + " / Chip " + std::to_string(d.chip), color);
        cell(y + 1, 2, number(d.core, 0, "%") + "  Logic " + deviceId(d.logical), color);
        bar(y, columns[3] + 2, right - columns[3] - 3, "MEM:", d.memory.percent());
        bar(y + 1, columns[3] + 2, right - columns[3] - 3, "UTL:", d.core);
        for (int x : columns) vertical(y, x, 2);
        tableRule(y + 2, columns);
        y += 3;
    }
    if (s.devices.empty()) {
        text(y, left + 2, "No NPU devices available", 2);
        vertical(y, left, 2); vertical(y, right, 2);
        tableRule(y + 2, {left, right}); y += 3;
    }

    const int split = std::min(columns[3], COLS - 38);
    const int hostX = left + 2, hostWidth = split - hostX - 1;
    const int npuX = split + 2, npuWidth = right - npuX - 1;
    const std::vector<int> panels{left, split, right};
    // Terminate the device columns and start the history panel divider.
    tableRule(y - 1, columns, false, true);
    mvaddch(y - 1, left, lineGlyph(ACS_LTEE)); mvaddch(y - 1, right, lineGlyph(ACS_RTEE));
    mvaddch(y - 1, split, lineGlyph(split == columns[3] ? ACS_PLUS : ACS_TTEE));
    auto a = aggregate(s);
    const auto graphTime = s.sampled;
    const int dotsPerCell = asciiGraphics ? 1 : 2;
    const double secondsPerColumn = interval;
    const int upper = plotHeight + 2;
    const int lowerY = y + upper + 1;
    const int bottom = y + historyHeight - 1;
    chart(y, hostX, hostWidth, upper, history, &HistoryPoint::cpu, secondsPerColumn, 4, graphTime);
    chart(y, npuX, npuWidth, upper, history, &HistoryPoint::npuMemory, secondsPerColumn, 2, graphTime);
    ticks(y + upper, hostX, hostWidth, dotsPerCell * secondsPerColumn);
    ticks(y + upper, npuX, npuWidth, dotsPerCell * secondsPerColumn);
    chart(lowerY, hostX, hostWidth, upper - 1, history, &HistoryPoint::memory, secondsPerColumn, 5, graphTime, true);
    chart(bottom, hostX, hostWidth, 1, history, &HistoryPoint::swap, secondsPerColumn, 6, graphTime);
    chart(lowerY, npuX, npuWidth, upper, history, &HistoryPoint::npuCore, secondsPerColumn, 1, graphTime, true);
    text(y, hostX, "Load: " + number(s.host.load1, 2) + " " + number(s.host.load5, 2) + " " + number(s.host.load15, 2), 0, hostWidth);
    text(y + 1, hostX, "CPU: " + number(s.host.cpu, 1, "%"), 0, hostWidth);
    text(y, npuX, "NPU MEM: " + number(a.memory, 1, "%") + " (" + std::to_string(a.memoryCount) + "/" + std::to_string(a.total) + ")", 0, npuWidth);
    text(bottom - 1, hostX, "MEM: " + bytes(s.host.memory.used) + " (" + number(s.host.memory.percent(), 1, "%") + ") ", 0, hostWidth);
    text(bottom, hostX, "SWP: " + bytes(s.host.swap.used) + " (" + number(s.host.swap.percent(), 1, "%") + ") ", 0, hostWidth);
    text(bottom, npuX, "NPU UTL: " + number(a.core, 1, "%") + " (" + std::to_string(a.coreCount) + "/" + std::to_string(a.total) + ") ", 0, npuWidth);
    for (int x : panels) vertical(y, x, historyHeight);
    mvaddch(y + 2 + plotHeight, left, lineGlyph(ACS_LTEE));
    mvaddch(y + 2 + plotHeight, split, lineGlyph(ACS_PLUS));
    mvaddch(y + 2 + plotHeight, right, lineGlyph(ACS_RTEE));
    y += historyHeight;
    tableRule(y++, panels, false, true);
    if (!compact) text(y++, left, s.host.root ? "! CAUTION: SUPERUSER LOGGED IN" : "", 2);

    const int processTop = y;
    tableRule(y++, {left, right}, true);
    text(y, left + 2, std::string(!deviceFocus ? "> " : "") + "Processes:" + (compact && s.host.root ? " [root]" : ""));
    auto identity = s.host.user + "@" + s.host.hostname;
    text(y++, std::max(left + 18, right - static_cast<int>(identity.size()) - 1), identity, 1, right - left - 20);
    text(y++, left + 2, fit("NPU", 5) + fit("PID", 9) + fit("USER", 10) + fit("NPU-MEM", 12)
        + fit("%CPU", 7) + fit("%MEM", 7) + fit("TIME", 10) + "COMMAND", 0, right - left - 3);
    tableRule(y++, {left, right});
    int rows = std::max(0, LINES - y - 2);
    processOffset = std::clamp(processOffset, 0, std::max(0, static_cast<int>(s.processes.size()) - rows));
    for (int i = processOffset; i < std::min(processOffset + rows, static_cast<int>(s.processes.size())); ++i) {
        const auto& p = s.processes[i];
        auto command = p.command.size() > static_cast<size_t>(pan) ? p.command.substr(pan) : "";
        text(y++, left + 2, fit(deviceId(p.physical), 5) + fit(std::to_string(p.pid), 9) + fit(p.user, 10)
            + fit(bytes(p.npuBytes), 12) + fit(number(p.cpu), 7) + fit(number(p.memoryPercent), 7)
            + fit(duration(p.elapsed), 10) + command, 0, right - left - 3);
    }
    if (s.processes.empty() && rows) {
        bool all = !s.devices.empty() && std::all_of(s.devices.begin(), s.devices.end(), [](const Device& d) { return d.processesAvailable; });
        text(y++, left + 2, all ? "No NPU processes" : "NPU process data unavailable", all ? 0 : 2);
    }
    int processBottom = std::min(y, LINES - 2);
    vertical(processTop + 1, left, processBottom - processTop - 1);
    vertical(processTop + 1, right, processBottom - processTop - 1);
    // Restore the header intersections after drawing the outside borders.
    mvaddch(processTop + 3, left, lineGlyph(ACS_LTEE)); mvaddch(processTop + 3, right, lineGlyph(ACS_RTEE));
    tableRule(processBottom, {left, right}, false, true);
    if (!s.error.empty()) text(LINES - 1, left, s.error, 2);
}
int runTui(double interval, bool ascii) {
    bool brailleAvailable = canDrawBraille();
    asciiGraphics = ascii || !brailleAvailable;
    struct Terminal { Terminal() { initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0); timeout(100); if (has_colors()) {
        start_color(); use_default_colors(); init_pair(1, COLOR_GREEN, -1); init_pair(2, COLOR_YELLOW, -1); init_pair(3, COLOR_RED, -1);
        init_pair(4, COLOR_CYAN, -1); init_pair(5, COLOR_MAGENTA, -1); init_pair(6, COLOR_BLUE, -1);
    }} ~Terminal() { endwin(); } } terminal;
    interrupted = 0; auto oldInt = std::signal(SIGINT, stopSignal); auto oldTerm = std::signal(SIGTERM, stopSignal);
    {
        Sampler sampler(interval); int deviceOffset = 0, processOffset = 0, pan = 0; bool deviceFocus = false, showHelp = false;
        while (!interrupted) {
            auto s = sampler.latest();
            if (showHelp) help(); else if (s) draw(*s, sampler.history(), deviceOffset, processOffset, deviceFocus, pan, interval);
            else { erase(); text(1, 1, "ascend-monitor-tui: collecting local NPU data... (q to quit)"); }
            refresh(); int key = getch(); int& offset = deviceFocus ? deviceOffset : processOffset;
            if (key == 'q' || (key == 27 && !showHelp)) break;
            if (key == 'h' || key == '?' || (key == 27 && showHelp)) showHelp = !showHelp;
            if (key == '\t') deviceFocus = !deviceFocus;
            if (key == KEY_DOWN || key == 'j') ++offset;
            if (key == KEY_UP || key == 'k') offset = std::max(0, offset - 1);
            if (key == KEY_NPAGE) offset += 5;
            if (key == KEY_PPAGE) offset = std::max(0, offset - 5);
            if (key == KEY_HOME) offset = 0;
            if (key == KEY_END) offset = s ? static_cast<int>(deviceFocus ? s->devices.size() : s->processes.size()) : 0;
            if (key == KEY_RIGHT) pan = std::min(1048576, pan + 8);
            if (key == KEY_LEFT) pan = std::max(0, pan - 8);
        }
    }
    std::signal(SIGINT, oldInt); std::signal(SIGTERM, oldTerm); return 0;
}
}
