#pragma once
#include "top/Model.h"
namespace ascend {
std::vector<Value> historyColumns(const History& history, Value HistoryPoint::*member,
    int columns, double secondsPerColumn, Clock::time_point end);
// Each input value is one horizontal dot column; pairs occupy one terminal cell.
std::vector<std::wstring> brailleGraph(const std::vector<Value>& columns, int rows, bool inverted = false);
}
