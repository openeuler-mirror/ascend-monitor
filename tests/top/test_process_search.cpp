#include "top/ProcessTable.h"
#include <iostream>
#include <stdexcept>
using namespace ascend;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
    try {
        Process p; p.pid = 12345; p.user = "TestUser";
        p.command = "/opt/Python train.py --model 模型 --pattern [a-z].*";
        check(processMatches(p, "PYTHON") && processMatches(p, "Train.PY --MODEL"), "full command, ASCII case-insensitive");
        check(processMatches(p, "testUSER"), "username search");
        check(processMatches(p, "234"), "PID substring search");
        check(processMatches(p, "模型"), "UTF-8 literal search");
        check(processMatches(p, "[a-z].*") && !processMatches(p, "[A-Z]+"), "literal, not regular expression");
        check(!processMatches(p, "") && !processMatches(p, "missing"), "empty and unmatched queries do not highlight");
        check(!processMatches(p, "12345TestUser"), "queries do not cross field boundaries");
        Snapshot s; s.processes = {p, p}; s.processes[1].chip = 1;
        s.processes.push_back(Process{});
        auto rows = sortedProcesses(s, ProcessSort{});
        int matches = 0;
        for (const auto* row : rows) if (processMatches(*row, "python")) ++matches;
        check(matches == 2 && rows.size() == 3 && s.processes[0].command == p.command,
              "cross-chip matches retain all rows and leave snapshot unchanged");
        std::cout << "process search tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
