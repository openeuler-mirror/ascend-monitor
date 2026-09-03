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
    NpuScanResult result;

    const CommandAttempt listAttempt = runCommand({QStringLiteral("info"), QStringLiteral("-l")});
    result.attempts.append(listAttempt);
    result.rawListOutput = listAttempt.stdOut;

    if (listAttempt.exitCode != 0 && result.rawListOutput.trimmed().isEmpty()) {
        result.errorMessage = QStringLiteral("npu-smi info -l failed (exit %1): %2")
                                  .arg(listAttempt.exitCode)
                                  .arg(listAttempt.stdErr.trimmed());
        return result;
    }

    result.cards = parseCardList(result.rawListOutput);

    const CommandAttempt mappingAttempt = runCommand({QStringLiteral("info"), QStringLiteral("-m")});
    result.attempts.append(mappingAttempt);
    result.rawMappingOutput = mappingAttempt.stdOut;

    if (mappingAttempt.exitCode == 0) {
        result.mappings = parseMapping(result.rawMappingOutput);
    }

    const CommandAttempt boardAttempt = runCommand({QStringLiteral("info"), QStringLiteral("-b")});
    result.attempts.append(boardAttempt);
    result.rawBoardOutput = boardAttempt.stdOut;

    QString softwareVersion;
    QString firmwareVersion;
    parseBoardVersions(result.rawBoardOutput, &softwareVersion, &firmwareVersion);

    for (NpuCard &card : result.cards) {
        if (card.softwareVersion.trimmed().isEmpty()) {
            card.softwareVersion = softwareVersion;
        }
        if (card.firmwareVersion.trimmed().isEmpty()) {
            card.firmwareVersion = firmwareVersion;
        }
    }

    const CommandAttempt healthAttempt = runCommand({QStringLiteral("info"), QStringLiteral("-t"), QStringLiteral("health")});
    result.attempts.append(healthAttempt);

    if (healthAttempt.exitCode == 0) {
        const QMap<QString, QString> healthMap = parseHealthByChip(healthAttempt.stdOut);
        for (NpuCard &card : result.cards) {
            const QString chipKey = card.cardId.trimmed();
            if (healthMap.contains(chipKey)) {
                card.health = healthMap.value(chipKey);
            }
        }
    }

    const CommandAttempt memoryAttempt = runCommand({QStringLiteral("info"), QStringLiteral("-t"), QStringLiteral("memory")});
    result.attempts.append(memoryAttempt);

    if (memoryAttempt.exitCode == 0) {
        QString cardMemoryTotal;
        const QMap<QString, QString> memoryMap = parseMemoryByChip(memoryAttempt.stdOut, &cardMemoryTotal);
        for (NpuCard &card : result.cards) {
            const QString chipKey = card.cardId.trimmed();
            if (memoryMap.contains(chipKey)) {
                card.memoryTotal = memoryMap.value(chipKey);
            } else if (!cardMemoryTotal.isEmpty()) {
                card.memoryTotal = cardMemoryTotal;
            }
        }
    }

    const CommandAttempt versionAttempt = runCommand({QStringLiteral("-v")});
    result.attempts.append(versionAttempt);
    result.npuSmiVersion = parseNpuSmiVersion(versionAttempt.stdOut);

    return result;
}

NpuMonitorSnapshot NpuInfoProvider::collectMonitor(const QStringList &cardIds) const
{
    NpuMonitorSnapshot snapshot;

    for (const QString &cardId : cardIds) {
        const QStringList tempArgs = {QStringLiteral("info"), QStringLiteral("-t"), QStringLiteral("temp"), QStringLiteral("-i"), cardId};
        const CommandAttempt tempAttempt = runCommand(tempArgs);

        if (tempAttempt.exitCode == 0) {
            const QMap<QString, double> tempMap = parseTemperatureByChip(tempAttempt.stdOut);
            for (auto it = tempMap.constBegin(); it != tempMap.constEnd(); ++it) {
                NpuCoreMonitor monitor;
                monitor.cardId = cardId;
                monitor.chipId = it.key();
                monitor.temperature = it.value();
                monitor.hasTemperature = true;
                snapshot.coreMonitors.append(monitor);
            }
        }

        const QStringList usageArgs = {QStringLiteral("info"), QStringLiteral("-t"), QStringLiteral("usages"), QStringLiteral("-i"), cardId};
        const CommandAttempt usageAttempt = runCommand(usageArgs);

        if (usageAttempt.exitCode == 0) {
            const QMap<QString, UsageMetrics> usageMap = parseUsageByChip(usageAttempt.stdOut);
            for (auto it = usageMap.constBegin(); it != usageMap.constEnd(); ++it) {
                NpuCoreMonitor *existing = nullptr;
                for (NpuCoreMonitor &m : snapshot.coreMonitors) {
                    if (m.cardId == cardId && m.chipId == it.key()) {
                        existing = &m;
                        break;
                    }
                }

                if (existing == nullptr) {
                    NpuCoreMonitor monitor;
                    monitor.cardId = cardId;
                    monitor.chipId = it.key();
                    snapshot.coreMonitors.append(monitor);
                    existing = &snapshot.coreMonitors.last();
                }

                const UsageMetrics &metrics = it.value();
                if (metrics.hasMemoryUsageRate) {
                    existing->memoryUsageRate = metrics.memoryUsageRate;
                    existing->hasMemoryUsageRate = true;
                }
                if (metrics.hasAicoreUsage) {
                    existing->aicoreUsage = metrics.aicoreUsage;
                    existing->hasAicoreUsage = true;
                }
                if (metrics.hasAivectorUsage) {
                    existing->aivectorUsage = metrics.aivectorUsage;
                    existing->hasAivectorUsage = true;
                }
                if (metrics.hasAicpuUsage) {
                    existing->aicpuUsage = metrics.aicpuUsage;
                    existing->hasAicpuUsage = true;
                }
            }
        }

        const QStringList powerArgs = {QStringLiteral("info"), QStringLiteral("-t"), QStringLiteral("power"), QStringLiteral("-i"), cardId};
        const CommandAttempt powerAttempt = runCommand(powerArgs);

        if (powerAttempt.exitCode == 0) {
            bool ok = false;
            const double power = parsePower(powerAttempt.stdOut, &ok);
            if (ok) {
                NpuPowerMonitor powerMonitor;
                powerMonitor.cardId = cardId;
                powerMonitor.power = power;
                powerMonitor.hasPower = true;
                snapshot.powerMonitors.append(powerMonitor);
            }
        }
    }

    return snapshot;
}

QVector<NpuProcessInfo> NpuInfoProvider::collectProcesses(const QString &cardId) const
{
    const QStringList args = {QStringLiteral("info"), QStringLiteral("-t"), QStringLiteral("proc")};
    const CommandAttempt attempt = runCommand(args);

    if (attempt.exitCode != 0) {
        return {};
    }

    return parseProcesses(attempt.stdOut, cardId);
}

QVector<NpuCard> NpuInfoProvider::parseCardList(const QString &output) const
{
    QVector<NpuCard> cards;
    const QStringList lines = output.split('\n');

    NpuCard currentCard;
    bool inCard = false;

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();

        if (trimmed.startsWith(QStringLiteral("NPU ID")) || trimmed.startsWith(QStringLiteral("Card ID"))) {
            if (inCard && !currentCard.cardId.isEmpty()) {
                cards.append(currentCard);
                currentCard = NpuCard();
            }
            inCard = true;

            const int colonPos = trimmed.indexOf(':');
            if (colonPos >= 0) {
                currentCard.cardId = trimmed.mid(colonPos + 1).trimmed();
            }
        } else if (inCard && trimmed.startsWith(QStringLiteral("Product Name"))) {
            const int colonPos = trimmed.indexOf(':');
            if (colonPos >= 0) {
                currentCard.productName = trimmed.mid(colonPos + 1).trimmed();
            }
        } else if (inCard && trimmed.startsWith(QStringLiteral("Serial Number"))) {
            const int colonPos = trimmed.indexOf(':');
            if (colonPos >= 0) {
                currentCard.serialNumber = trimmed.mid(colonPos + 1).trimmed();
            }
        } else if (inCard && trimmed.startsWith(QStringLiteral("Chip Count"))) {
            const int colonPos = trimmed.indexOf(':');
            if (colonPos >= 0) {
                currentCard.chipCount = trimmed.mid(colonPos + 1).trimmed();
            }
        }
    }

    if (inCard && !currentCard.cardId.isEmpty()) {
        cards.append(currentCard);
    }

    return cards;
}

QString NpuInfoProvider::parseNpuSmiVersion(const QString &output) const
{
    const QStringList lines = output.split('\n');
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.contains(QStringLiteral("Version")) || trimmed.contains(QStringLiteral("version"))) {
            const int colonPos = trimmed.indexOf(':');
            if (colonPos >= 0) {
                return trimmed.mid(colonPos + 1).trimmed();
            }
        }
    }
    return output.trimmed();
}

QVector<NpuMappingEntry> NpuInfoProvider::parseMapping(const QString &output) const
{
    QVector<NpuMappingEntry> entries;
    const QStringList lines = output.split('\n');

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || looksLikeSeparator(trimmed)) {
            continue;
        }

        const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() >= 4) {
            NpuMappingEntry entry;
            entry.cardId = parts.value(0);
            entry.chipId = parts.value(1);
            entry.logicId = parts.value(2);
            entry.chipName = parts.value(3);
            if (parts.size() >= 5) entry.memoryTotal = parts.value(4);
            if (parts.size() >= 6) entry.aiCore = parts.value(5);
            if (parts.size() >= 7) entry.controlCore = parts.value(6);
            if (parts.size() >= 8) entry.dataCore = parts.value(7);
            entries.append(entry);
        }
    }

    return entries;
}

void NpuInfoProvider::parseBoardVersions(
    const QString &output, QString *softwareVersion, QString *firmwareVersion) const
{
    const QStringList lines = output.split('\n');
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.contains(QStringLiteral("Software Version")) || trimmed.contains(QStringLiteral("software version"))) {
            const int colonPos = trimmed.indexOf(':');
            if (colonPos >= 0 && softwareVersion) {
                *softwareVersion = trimmed.mid(colonPos + 1).trimmed();
            }
        }
        if (trimmed.contains(QStringLiteral("Firmware Version")) || trimmed.contains(QStringLiteral("firmware version"))) {
            const int colonPos = trimmed.indexOf(':');
            if (colonPos >= 0 && firmwareVersion) {
                *firmwareVersion = trimmed.mid(colonPos + 1).trimmed();
            }
        }
    }
}

QMap<QString, QString> NpuInfoProvider::parseMemoryByChip(const QString &output, QString *cardMemoryTotal) const
{
    QMap<QString, QString> result;
    const QStringList lines = output.split('\n');

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || looksLikeSeparator(trimmed)) {
            continue;
        }

        if (trimmed.contains(QStringLiteral("Memory Total")) || trimmed.contains(QStringLiteral("Total Memory"))) {
            const int colonPos = trimmed.indexOf(':');
            if (colonPos >= 0 && cardMemoryTotal) {
                *cardMemoryTotal = trimmed.mid(colonPos + 1).trimmed();
            }
        }

        const QRegularExpression re(QStringLiteral("(\\d+)\\s*:\\s*(.+)"));
        const QRegularExpressionMatch match = re.match(trimmed);
        if (match.hasMatch()) {
            result[match.captured(1)] = match.captured(2).trimmed();
        }
    }

    return result;
}

QMap<QString, QString> NpuInfoProvider::parseHealthByChip(const QString &output) const
{
    QMap<QString, QString> result;
    const QStringList lines = output.split('\n');

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || looksLikeSeparator(trimmed)) {
            continue;
        }

        const QRegularExpression re(QStringLiteral("(\\d+)\\s*:\\s*(.+)"));
        const QRegularExpressionMatch match = re.match(trimmed);
        if (match.hasMatch()) {
            result[match.captured(1)] = match.captured(2).trimmed();
        }
    }

    return result;
}

QMap<QString, double> NpuInfoProvider::parseTemperatureByChip(const QString &output) const
{
    QMap<QString, double> result;
    const QStringList lines = output.split('\n');

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || looksLikeSeparator(trimmed)) {
            continue;
        }

        const QRegularExpression re(QStringLiteral("(\\d+)\\s*:\\s*([\\d.]+)"));
        const QRegularExpressionMatch match = re.match(trimmed);
        if (match.hasMatch()) {
            bool ok = false;
            const double value = match.captured(2).toDouble(&ok);
            if (ok) {
                result[match.captured(1)] = value;
            }
        }
    }

    return result;
}

QMap<QString, NpuInfoProvider::UsageMetrics> NpuInfoProvider::parseUsageByChip(const QString &output) const
{
    QMap<QString, UsageMetrics> result;
    const QStringList lines = output.split('\n');

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || looksLikeSeparator(trimmed)) {
            continue;
        }

        const QRegularExpression re(QStringLiteral("(\\d+)\\s*:\\s*(.+)"));
        const QRegularExpressionMatch match = re.match(trimmed);
        if (!match.hasMatch()) {
            continue;
        }

        const QString chipId = match.captured(1);
        const QString values = match.captured(2).trimmed();

        if (!result.contains(chipId)) {
            result[chipId] = UsageMetrics();
        }

        UsageMetrics &metrics = result[chipId];

        if (trimmed.contains(QStringLiteral("Memory Usage Rate")) || trimmed.contains(QStringLiteral("Memory Usage"))) {
            bool ok = false;
            const double val = parseNumericValue(values, &ok);
            if (ok) {
                metrics.memoryUsageRate = val;
                metrics.hasMemoryUsageRate = true;
            }
        }
        if (trimmed.contains(QStringLiteral("AICore"))) {
            bool ok = false;
            const double val = parseNumericValue(values, &ok);
            if (ok) {
                metrics.aicoreUsage = val;
                metrics.hasAicoreUsage = true;
            }
        }
        if (trimmed.contains(QStringLiteral("AIVector"))) {
            bool ok = false;
            const double val = parseNumericValue(values, &ok);
            if (ok) {
                metrics.aivectorUsage = val;
                metrics.hasAivectorUsage = true;
            }
        }
        if (trimmed.contains(QStringLiteral("AICPU"))) {
            bool ok = false;
            const double val = parseNumericValue(values, &ok);
            if (ok) {
                metrics.aicpuUsage = val;
                metrics.hasAicpuUsage = true;
            }
        }
    }

    return result;
}

QMap<QString, NpuInfoProvider::CpuNumConfig> NpuInfoProvider::parseCpuNumCfgByChip(const QString &output) const
{
    QMap<QString, CpuNumConfig> result;
    const QStringList lines = output.split('\n');

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || looksLikeSeparator(trimmed)) {
            continue;
        }

        const QRegularExpression re(QStringLiteral("(\\d+)\\s*:\\s*(.+)"));
        const QRegularExpressionMatch match = re.match(trimmed);
        if (!match.hasMatch()) {
            continue;
        }

        const QString chipId = match.captured(1);
        const QString values = match.captured(2).trimmed();

        if (!result.contains(chipId)) {
            result[chipId] = CpuNumConfig();
        }

        CpuNumConfig &config = result[chipId];

        if (trimmed.contains(QStringLiteral("AI Core"))) {
            config.aiCore = values;
        }
        if (trimmed.contains(QStringLiteral("Control Core"))) {
            config.controlCore = values;
        }
        if (trimmed.contains(QStringLiteral("Data Core"))) {
            config.dataCore = values;
        }
    }

    return result;
}

QVector<NpuProcessInfo> NpuInfoProvider::parseProcesses(const QString &output, const QString &cardId) const
{
    QVector<NpuProcessInfo> processes;
    const QStringList lines = output.split('\n');

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || looksLikeSeparator(trimmed)) {
            continue;
        }

        const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            NpuProcessInfo info;
            info.pid = parts.value(0);
            info.processName = parts.value(1);
            info.cardId = cardId;
            if (parts.size() >= 4) {
                info.chipId = parts.value(2);
                info.memoryMb = parts.value(3);
            } else if (parts.size() >= 3) {
                info.memoryMb = parts.value(2);
            }
            processes.append(info);
        }
    }

    return processes;
}

double NpuInfoProvider::parsePower(const QString &output, bool *ok) const
{
    if (ok) {
        *ok = false;
    }

    const QStringList lines = output.split('\n');
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.contains(QStringLiteral("Power")) || trimmed.contains(QStringLiteral("power"))) {
            const QRegularExpression re(QStringLiteral("([\\d.]+)\\s*[Ww]"));
            const QRegularExpressionMatch match = re.match(trimmed);
            if (match.hasMatch()) {
                const double val = match.captured(1).toDouble(ok);
                if (ok && *ok) {
                    return val;
                }
            }
        }
    }

    return 0.0;
}

double NpuInfoProvider::parseNumericValue(const QString &value, bool *ok) const
{
    if (ok) {
        *ok = false;
    }

    QString cleaned = value.trimmed();
    cleaned.remove(QStringLiteral("%"));
    cleaned.remove(QStringLiteral("W"));
    cleaned.remove(QStringLiteral("MB"));
    cleaned.remove(QStringLiteral("KB"));
    cleaned.remove(QStringLiteral("GB"));
    cleaned.remove(QStringLiteral("C"));
    cleaned.remove(QStringLiteral("°"));
    cleaned = cleaned.trimmed();

    return cleaned.toDouble(ok);
}

QStringList NpuInfoProvider::extractCardIds(const QVector<NpuCard> &cards, const QVector<NpuMappingEntry> &mappings) const
