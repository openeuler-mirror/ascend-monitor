#include "NpuInfoProvider.h"

#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {
bool looksLikeSeparator(const QString &line)
{
    const QString simplified = line.trimmed();
    return simplified.startsWith('+') || simplified.startsWith("---") || simplified.startsWith("===");
}
}

NpuInfoProvider::NpuInfoProvider(QObject *parent)
    : QObject(parent)
{
}

void NpuInfoProvider::setConnectionOptions(const NpuConnectionOptions &options)
{
    m_connectionOptions = options;
}

NpuScanResult NpuInfoProvider::scan() const
{
