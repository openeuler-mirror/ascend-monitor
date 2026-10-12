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

    const CommandAttempt versionAttempt = runCommand({QStringLiteral("-v")});
    result.attempts.append(versionAttempt);
    if (versionAttempt.exitCode == 0 && !versionAttempt.stdOut.trimmed().isEmpty()) {
        result.npuSmiVersion = parseNpuSmiVersion(versionAttempt.stdOut);
    }

    const CommandAttempt listAttempt = runCommand({QStringLiteral("info"), QStringLiteral("-l")});
    result.attempts.append(listAttempt);
    result.rawListOutput = listAttempt.stdOut;
    result.cards = parseCardList(listAttempt.stdOut);

    const CommandAttempt mappingAttempt = runCommand({QStringLiteral("info"), QStringLiteral("-m")});
    result.attempts.append(mappingAttempt);
    result.rawMappingOutput = mappingAttempt.stdOut;
    result.mappings = parseMapping(mappingAttempt.stdOut);

    for (const QString &cardId : extractCardIds(result.cards, result.mappings)) {
        CommandAttempt boardAttempt = runCommand({
            QStringLiteral("info"),
            QStringLiteral("-t"),
            QStringLiteral("board"),
            QStringLiteral("-i"),
            cardId
        });
        result.attempts.append(boardAttempt);

        QString softwareVersion;
        QString firmwareVersion;
        if (boardAttempt.exitCode == 0 && !boardAttempt.stdOut.trimmed().isEmpty()) {
            parseBoardVersions(boardAttempt.stdOut, &softwareVersion, &firmwareVersion);
        }

        if (softwareVersion.isEmpty() && firmwareVersion.isEmpty()) {
            const CommandAttempt fallbackBoardAttempt = runCommand({
                QStringLiteral("info"),
                QStringLiteral("-i"),
                cardId,
                QStringLiteral("-t"),
                QStringLiteral("board")
            });
            result.attempts.append(fallbackBoardAttempt);
            if (fallbackBoardAttempt.exitCode == 0 && !fallbackBoardAttempt.stdOut.trimmed().isEmpty()) {
                parseBoardVersions(fallbackBoardAttempt.stdOut, &softwareVersion, &firmwareVersion);
            }
        }

        if (!softwareVersion.isEmpty() || !firmwareVersion.isEmpty()) {
            for (NpuCard &card : result.cards) {
                if (card.cardId != cardId) {
                    continue;
                }
                if (!softwareVersion.isEmpty()) {
                    card.softwareVersion = softwareVersion;
                }
                if (!firmwareVersion.isEmpty()) {
                    card.firmwareVersion = firmwareVersion;
                }
            }
        }

        const CommandAttempt memoryAttempt = runCommand({
            QStringLiteral("info"),
            QStringLiteral("-t"),
            QStringLiteral("memory"),
            QStringLiteral("-i"),
            cardId
        });
        result.attempts.append(memoryAttempt);
        if (memoryAttempt.exitCode == 0 && !memoryAttempt.stdOut.trimmed().isEmpty()) {
            QString cardMemoryTotal;
            const QMap<QString, QString> chipMemory = parseMemoryByChip(memoryAttempt.stdOut, &cardMemoryTotal);

            for (NpuCard &card : result.cards) {
                if (card.cardId == cardId) {
                    card.memoryTotal = cardMemoryTotal;
                    break;
                }
            }

            for (NpuMappingEntry &mapping : result.mappings) {
                if (mapping.cardId == cardId) {
                    mapping.memoryTotal = chipMemory.value(mapping.chipId);
                }
            }
        }

        const CommandAttempt healthAttempt = runCommand({
            QStringLiteral("info"),
            QStringLiteral("-t"),
            QStringLiteral("health"),
            QStringLiteral("-i"),
            cardId
        });
        result.attempts.append(healthAttempt);
        if (healthAttempt.exitCode == 0 && !healthAttempt.stdOut.trimmed().isEmpty()) {
            const QMap<QString, QString> healthByChip = parseHealthByChip(healthAttempt.stdOut);
            QStringList values;
            for (auto it = healthByChip.constBegin(); it != healthByChip.constEnd(); ++it) {
                if (it.key().trimmed().compare(QStringLiteral("MCU"), Qt::CaseInsensitive) == 0) {
                    continue;
                }
                values.append(it.value());
            }
            values.removeDuplicates();

            QString health = QStringLiteral("-");
            if (!values.isEmpty()) {
                health = values.join(QStringLiteral("/"));
            }

            for (NpuCard &card : result.cards) {
                if (card.cardId == cardId) {
                    card.health = health;
                    break;
                }
            }
        }

        const CommandAttempt cpuNumCfgAttempt = runCommand({
            QStringLiteral("info"),
            QStringLiteral("-t"),
            QStringLiteral("cpu-num-cfg"),
            QStringLiteral("-i"),
            cardId
        });
        result.attempts.append(cpuNumCfgAttempt);
        if (cpuNumCfgAttempt.exitCode == 0 && !cpuNumCfgAttempt.stdOut.trimmed().isEmpty()) {
            const QMap<QString, CpuNumConfig> cpuCfgByChip = parseCpuNumCfgByChip(cpuNumCfgAttempt.stdOut);
            for (NpuMappingEntry &mapping : result.mappings) {
                if (mapping.cardId != cardId) {
                    continue;
                }
                const CpuNumConfig cfg = cpuCfgByChip.value(mapping.chipId);
                if (!cfg.aiCore.isEmpty()) {
                    mapping.aiCore = cfg.aiCore;
                }
                if (!cfg.controlCore.isEmpty()) {
                    mapping.controlCore = cfg.controlCore;
                }
                if (!cfg.dataCore.isEmpty()) {
                    mapping.dataCore = cfg.dataCore;
                }
            }
        }
    }

    if (!result.hasData()) {
        QStringList failures;
        for (const CommandAttempt &attempt : result.attempts) {
            if (attempt.exitCode != 0 && !attempt.stdErr.trimmed().isEmpty()) {
                failures << QStringLiteral("%1: %2").arg(attempt.command, attempt.stdErr.trimmed());
            }
        }

        result.errorMessage = failures.isEmpty()
            ? QStringLiteral("`npu-smi` returned no structured data.")
            : QStringLiteral("Unable to collect NPU information. %1").arg(failures.join(QStringLiteral(" | ")));
    }

    return result;
}

NpuMonitorSnapshot NpuInfoProvider::collectMonitor(const QStringList &cardIds) const
{
    NpuMonitorSnapshot snapshot;

    for (const QString &cardId : cardIds) {
        const CommandAttempt tempAttempt = runCommand({
            QStringLiteral("info"), QStringLiteral("-t"), QStringLiteral("temp"), QStringLiteral("-i"), cardId
        });
        if (tempAttempt.exitCode != 0) {
            snapshot.errors.append(QStringLiteral("%1: %2").arg(tempAttempt.command, tempAttempt.stdErr.trimmed()));
        }

        const CommandAttempt usageAttempt = runCommand({
            QStringLiteral("info"), QStringLiteral("-t"), QStringLiteral("usages"), QStringLiteral("-i"), cardId
        });
        if (usageAttempt.exitCode != 0) {
            snapshot.errors.append(QStringLiteral("%1: %2").arg(usageAttempt.command, usageAttempt.stdErr.trimmed()));
        }

        const CommandAttempt powerAttempt = runCommand({
            QStringLiteral("info"), QStringLiteral("-t"), QStringLiteral("power"), QStringLiteral("-i"), cardId
        });
        if (powerAttempt.exitCode != 0) {
            snapshot.errors.append(QStringLiteral("%1: %2").arg(powerAttempt.command, powerAttempt.stdErr.trimmed()));
        }

        const QMap<QString, double> temperatureByChip = parseTemperatureByChip(tempAttempt.stdOut);
        const QMap<QString, UsageMetrics> usageByChip = parseUsageByChip(usageAttempt.stdOut);

        QSet<QString> chips;
        for (const QString &chipId : temperatureByChip.keys()) {
            chips.insert(chipId);
        }
        for (const QString &chipId : usageByChip.keys()) {
            chips.insert(chipId);
        }

        for (const QString &chipId : chips) {
            NpuCoreMonitor monitor;
            monitor.cardId = cardId;
            monitor.chipId = chipId;

            if (temperatureByChip.contains(chipId)) {
                monitor.temperature = temperatureByChip.value(chipId);
                monitor.hasTemperature = true;
            }

            if (usageByChip.contains(chipId)) {
                const UsageMetrics usage = usageByChip.value(chipId);
                monitor.memoryUsageRate = usage.memoryUsageRate;
                monitor.hasMemoryUsageRate = usage.hasMemoryUsageRate;
                monitor.aicoreUsage = usage.aicoreUsage;
                monitor.hasAicoreUsage = usage.hasAicoreUsage;
                monitor.aivectorUsage = usage.aivectorUsage;
                monitor.hasAivectorUsage = usage.hasAivectorUsage;
                monitor.aicpuUsage = usage.aicpuUsage;
                monitor.hasAicpuUsage = usage.hasAicpuUsage;
            }

            snapshot.coreMonitors.append(monitor);
        }

        bool powerOk = false;
        const double power = parsePower(powerAttempt.stdOut, &powerOk);
        NpuPowerMonitor powerMonitor;
        powerMonitor.cardId = cardId;
        powerMonitor.power = power;
        powerMonitor.hasPower = powerOk;
        snapshot.powerMonitors.append(powerMonitor);
    }

    return snapshot;
}

QVector<NpuProcessInfo> NpuInfoProvider::collectProcesses(const QString &cardId) const
{
    const CommandAttempt infoAttempt = runCommand({QStringLiteral("info")});
    if (infoAttempt.exitCode != 0 || infoAttempt.stdOut.trimmed().isEmpty()) {
        return {};
    }

    return parseProcesses(infoAttempt.stdOut, cardId);
}

QVector<NpuCard> NpuInfoProvider::parseCardList(const QString &output) const
{
    QVector<NpuCard> cards;
    if (output.trimmed().isEmpty()) {
        return cards;
    }

    const QStringList lines = output.split('\n');
    const QRegularExpression keyedPattern(QStringLiteral("^\\s*([A-Za-z][A-Za-z0-9_ /()-]+?)\\s*[:=]\\s*(.+?)\\s*$"));

    NpuCard current;
    bool hasCurrent = false;

    auto finalize = [&cards, &current, &hasCurrent]() {
        if (!hasCurrent) {
            return;
        }
        if (!current.cardId.isEmpty()) {
            cards.append(current);
        }
        current = NpuCard();
        hasCurrent = false;
    };

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || looksLikeSeparator(line)) {
            continue;
        }

        const QRegularExpressionMatch match = keyedPattern.match(line);
        if (!match.hasMatch()) {
            continue;
        }

        const QString key = match.captured(1).trimmed().toLower();
        const QString value = cleanValue(match.captured(2));

        if (key == QStringLiteral("total count")) {
            continue;
        }

        if (key == QStringLiteral("npu id")) {
            if (!current.cardId.isEmpty() && current.cardId != value) {
                finalize();
            }
            current.cardId = value;
            hasCurrent = true;
            continue;
        }

        if (!hasCurrent) {
            continue;
        }

        if (key == QStringLiteral("product name")) {
            current.productName = value;
        } else if (key == QStringLiteral("serial number")) {
            current.serialNumber = value;
        } else if (key == QStringLiteral("chip count")) {
            current.chipCount = value;
        }
    }

    finalize();
    return cards;
}

QString NpuInfoProvider::parseNpuSmiVersion(const QString &output) const
{
    const QStringList lines = output.split('\n');
    const QRegularExpression versionPattern(
        QStringLiteral("npu-smi\\s+version\\s*[:=]\\s*([0-9][0-9A-Za-z._-]*)"),
        QRegularExpression::CaseInsensitiveOption);

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const QRegularExpressionMatch match = versionPattern.match(line);
        if (match.hasMatch()) {
            return cleanValue(match.captured(1));
        }
    }

    return QString();
}

QVector<NpuMappingEntry> NpuInfoProvider::parseMapping(const QString &output) const
{
    QVector<NpuMappingEntry> mappings;
    if (output.trimmed().isEmpty()) {
        return mappings;
    }

    const QStringList lines = output.split('\n');
    const QRegularExpression rowPattern(QStringLiteral("^\\s*(\\d+)\\s+(\\d+)\\s+([0-9-]+)(?:\\s+(.+?))?\\s*$"));

    for (int index = 0; index + 1 < lines.size(); ++index) {
        const QString firstLine = lines.at(index).trimmed();
        const QRegularExpressionMatch match = rowPattern.match(firstLine);
        if (!match.hasMatch()) {
            continue;
        }

        NpuMappingEntry entry;
        entry.cardId = match.captured(1);
        entry.chipId = match.captured(2);
        entry.logicId = match.captured(3);
        entry.chipName = cleanValue(match.captured(4));

        if (entry.chipName.isEmpty() && index + 1 < lines.size()) {
            const QString secondLine = lines.at(index + 1).trimmed();
            if (!secondLine.isEmpty()) {
                const QRegularExpressionMatch nextMatch = rowPattern.match(secondLine);
                if (!nextMatch.hasMatch()) {
                    entry.chipName = cleanValue(secondLine);
                    ++index;
                }
            }
        }

        if (entry.chipName.isEmpty()) {
            entry.chipName = QStringLiteral("-");
        }

        mappings.append(entry);
    }

    return mappings;
}

void NpuInfoProvider::parseBoardVersions(
    const QString &output,
    QString *softwareVersion,
    QString *firmwareVersion) const
{
    if (softwareVersion != nullptr) {
        softwareVersion->clear();
    }
    if (firmwareVersion != nullptr) {
        firmwareVersion->clear();
    }

    const QStringList lines = output.split('\n');
    const QRegularExpression keyedPattern(QStringLiteral("^\\s*([A-Za-z][A-Za-z0-9_ /()-]+?)\\s*[:=]\\s*(.+?)\\s*$"));

    for (const QString &rawLine : lines) {
        const QRegularExpressionMatch match = keyedPattern.match(rawLine.trimmed());
        if (!match.hasMatch()) {
            continue;
        }

        const QString key = match.captured(1).trimmed().toLower();
        const QString value = cleanValue(match.captured(2));

        if (softwareVersion != nullptr && key == QStringLiteral("software version")) {
            *softwareVersion = value;
        } else if (firmwareVersion != nullptr && key == QStringLiteral("firmware version")) {
            *firmwareVersion = value;
        }
    }
}

QMap<QString, QString> NpuInfoProvider::parseMemoryByChip(const QString &output, QString *cardMemoryTotal) const
{
    QMap<QString, QString> chipMemory;
    if (cardMemoryTotal != nullptr) {
        cardMemoryTotal->clear();
    }

    const QStringList lines = output.split('\n');
    const QRegularExpression keyedPattern(QStringLiteral("^\\s*([A-Za-z][A-Za-z0-9_ /()-]+?)\\s*[:=]\\s*(.+?)\\s*$"));

    QString pendingDdrCapacity;
    qint64 cardTotal = 0;

    for (const QString &rawLine : lines) {
        const QRegularExpressionMatch match = keyedPattern.match(rawLine.trimmed());
        if (!match.hasMatch()) {
            continue;
        }

        const QString key = match.captured(1).trimmed().toLower();
        const QString value = cleanValue(match.captured(2));

        if (key == QStringLiteral("chip id")) {
            if (pendingDdrCapacity.isEmpty()) {
                continue;
            }

            bool ok = false;
            const qint64 memoryValue = pendingDdrCapacity.toLongLong(&ok);
            if (ok) {
                chipMemory.insert(value, QStringLiteral("%1 MB").arg(memoryValue));
                cardTotal += memoryValue;
            } else {
                chipMemory.insert(value, pendingDdrCapacity);
            }
            pendingDdrCapacity.clear();
            continue;
        }

        if (key == QStringLiteral("ddr capacity(mb)")) {
            pendingDdrCapacity = value;
        }
    }

    if (cardMemoryTotal != nullptr && cardTotal > 0) {
        *cardMemoryTotal = QStringLiteral("%1 MB").arg(cardTotal);
    }

    return chipMemory;
}

QMap<QString, QString> NpuInfoProvider::parseHealthByChip(const QString &output) const
{
    QMap<QString, QString> healthByChip;
    const QStringList lines = output.split('\n');
    const QRegularExpression keyedPattern(QStringLiteral("^\\s*([A-Za-z][A-Za-z0-9_ /()-]+?)\\s*[:=]\\s*(.+?)\\s*$"));

    QString pendingHealth;
    QString pendingChipName;

    for (const QString &rawLine : lines) {
        const QRegularExpressionMatch match = keyedPattern.match(rawLine.trimmed());
        if (!match.hasMatch()) {
            continue;
        }
        const QString key = match.captured(1).trimmed().toLower();
        const QString value = cleanValue(match.captured(2));

        if (key == QStringLiteral("health")) {
            pendingHealth = value;
        } else if (key == QStringLiteral("chip id")) {
            if (!pendingHealth.isEmpty()) {
                healthByChip.insert(value, pendingHealth);
                pendingHealth.clear();
            }
        } else if (key == QStringLiteral("chip name")) {
            pendingChipName = value;
            if (!pendingHealth.isEmpty() && !pendingChipName.isEmpty()) {
                healthByChip.insert(pendingChipName, pendingHealth);
                pendingHealth.clear();
                pendingChipName.clear();
            }
        }
    }

    return healthByChip;
}

QMap<QString, double> NpuInfoProvider::parseTemperatureByChip(const QString &output) const
{
    QMap<QString, double> temperatureByChip;
    const QStringList lines = output.split('\n');
    const QRegularExpression keyedPattern(QStringLiteral("^\\s*([A-Za-z][A-Za-z0-9_ /()-]+?)\\s*[:=]\\s*(.+?)\\s*$"));

    QString pendingTemp;

    for (const QString &rawLine : lines) {
        const QRegularExpressionMatch match = keyedPattern.match(rawLine.trimmed());
        if (!match.hasMatch()) {
            continue;
        }
        const QString key = match.captured(1).trimmed().toLower();
        const QString value = cleanValue(match.captured(2));

        if (key.startsWith(QStringLiteral("temperature"))) {
            pendingTemp = value;
        } else if (key == QStringLiteral("chip id")) {
            bool ok = false;
            const double temp = parseNumericValue(pendingTemp, &ok);
            if (ok) {
                temperatureByChip.insert(value, temp);
            }
            pendingTemp.clear();
        }
    }

    return temperatureByChip;
}

QMap<QString, NpuInfoProvider::UsageMetrics> NpuInfoProvider::parseUsageByChip(const QString &output) const
{
    QMap<QString, UsageMetrics> usageByChip;
    const QStringList lines = output.split('\n');
    const QRegularExpression keyedPattern(QStringLiteral("^\\s*([A-Za-z][A-Za-z0-9_ /()%.-]+?)\\s*[:=]\\s*(.+?)\\s*$"));

    UsageMetrics pending;
    bool hasPending = false;

    for (const QString &rawLine : lines) {
        const QRegularExpressionMatch match = keyedPattern.match(rawLine.trimmed());
        if (!match.hasMatch()) {
            continue;
        }
        const QString key = match.captured(1).trimmed().toLower();
        const QString value = cleanValue(match.captured(2));

        bool ok = false;
        if (key == QStringLiteral("ddr usage rate(%)") || key == QStringLiteral("hbm usage rate(%)")) {
            pending.memoryUsageRate = parseNumericValue(value, &ok);
            pending.hasMemoryUsageRate = ok;
            hasPending = hasPending || ok;
        } else if (key == QStringLiteral("aicore usage rate(%)")) {
            pending.aicoreUsage = parseNumericValue(value, &ok);
            pending.hasAicoreUsage = ok;
            hasPending = hasPending || ok;
        } else if (key == QStringLiteral("vectorcore usage rate(%)") || key == QStringLiteral("aivector usage rate(%)")) {
            pending.aivectorUsage = parseNumericValue(value, &ok);
            pending.hasAivectorUsage = ok;
            hasPending = hasPending || ok;
        } else if (key == QStringLiteral("aicpu usage rate(%)")) {
            pending.aicpuUsage = parseNumericValue(value, &ok);
            pending.hasAicpuUsage = ok;
            hasPending = hasPending || ok;
        } else if (key == QStringLiteral("chip id")) {
            if (hasPending) {
                usageByChip.insert(value, pending);
            }
            pending = UsageMetrics();
            hasPending = false;
        }
    }

    return usageByChip;
}

QMap<QString, NpuInfoProvider::CpuNumConfig> NpuInfoProvider::parseCpuNumCfgByChip(const QString &output) const
{
    QMap<QString, CpuNumConfig> configByChip;
    const QStringList lines = output.split('\n');
    const QRegularExpression keyedPattern(QStringLiteral("^\\s*([A-Za-z][A-Za-z0-9_ /()%.-]+?)\\s*[:=]\\s*(.+?)\\s*$"));

    CpuNumConfig pending;
    bool hasPending = false;

    for (const QString &rawLine : lines) {
        const QRegularExpressionMatch match = keyedPattern.match(rawLine.trimmed());
        if (!match.hasMatch()) {
            continue;
        }

        const QString key = match.captured(1).trimmed().toLower();
        const QString value = cleanValue(match.captured(2));

        if (key == QStringLiteral("current ai cpu number")) {
            pending.aiCore = value;
            hasPending = true;
        } else if (key == QStringLiteral("current control cpu number")) {
            pending.controlCore = value;
            hasPending = true;
        } else if (key == QStringLiteral("current data cpu number")) {
            pending.dataCore = value;
            hasPending = true;
        } else if (key == QStringLiteral("chip id")) {
            if (hasPending) {
                configByChip.insert(value, pending);
            }
            pending = CpuNumConfig();
            hasPending = false;
        }
    }

    return configByChip;
}

QVector<NpuProcessInfo> NpuInfoProvider::parseProcesses(const QString &output, const QString &cardId) const
{
    QVector<NpuProcessInfo> processes;
    const QStringList lines = output.split('\n');

    const QRegularExpression rowPattern(
        QStringLiteral("^\\|\\s*(\\d+)\\s+(\\d+)\\s*\\|\\s*(\\d+)\\s*\\|\\s*(.*?)\\s*\\|\\s*(\\S+)\\s*\\|"));

    bool inProcessSection = false;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.contains(QStringLiteral("Process id"), Qt::CaseInsensitive)) {
            inProcessSection = true;
            continue;
        }

        if (!inProcessSection) {
            continue;
        }

        if (trimmed.contains(QStringLiteral("No running processes found in NPU"), Qt::CaseInsensitive)) {
            continue;
        }

        const QRegularExpressionMatch match = rowPattern.match(trimmed);
        if (!match.hasMatch()) {
            continue;
        }

        NpuProcessInfo process;
        process.cardId = match.captured(1).trimmed();
        process.chipId = match.captured(2).trimmed();
        process.pid = match.captured(3).trimmed();
        process.processName = cleanValue(match.captured(4));
        process.memoryMb = match.captured(5).trimmed();

        if (!cardId.trimmed().isEmpty() && process.cardId != cardId.trimmed()) {
            continue;
        }
        processes.append(process);
    }

    return processes;
}

double NpuInfoProvider::parsePower(const QString &output, bool *ok) const
{
    const QStringList lines = output.split('\n');
    const QRegularExpression keyedPattern(QStringLiteral("^\\s*([A-Za-z][A-Za-z0-9_ /()%.-]+?)\\s*[:=]\\s*(.+?)\\s*$"));

    for (const QString &rawLine : lines) {
        const QRegularExpressionMatch match = keyedPattern.match(rawLine.trimmed());
        if (!match.hasMatch()) {
            continue;
        }
        const QString key = match.captured(1).trimmed().toLower();
        if (!key.startsWith(QStringLiteral("power dissipation")) && !key.startsWith(QStringLiteral("power"))) {
            continue;
        }
        return parseNumericValue(cleanValue(match.captured(2)), ok);
    }

    if (ok != nullptr) {
        *ok = false;
    }
    return 0.0;
}

double NpuInfoProvider::parseNumericValue(const QString &value, bool *ok) const
{
    const QRegularExpression numberPattern(QStringLiteral("([-+]?[0-9]*\\.?[0-9]+)"));
    const QRegularExpressionMatch match = numberPattern.match(value);
    if (!match.hasMatch()) {
        if (ok != nullptr) {
            *ok = false;
        }
        return 0.0;
    }

    bool localOk = false;
    const double parsed = match.captured(1).toDouble(&localOk);
    if (ok != nullptr) {
        *ok = localOk;
    }
    return localOk ? parsed : 0.0;
}

QStringList NpuInfoProvider::extractCardIds(const QVector<NpuCard> &cards, const QVector<NpuMappingEntry> &mappings) const
{
    QStringList cardIds;
    QSet<QString> seen;

    for (const NpuCard &card : cards) {
        if (!card.cardId.isEmpty() && !seen.contains(card.cardId)) {
            seen.insert(card.cardId);
            cardIds.append(card.cardId);
        }
    }

    for (const NpuMappingEntry &mapping : mappings) {
        if (!mapping.cardId.isEmpty() && !seen.contains(mapping.cardId)) {
            seen.insert(mapping.cardId);
            cardIds.append(mapping.cardId);
        }
    }

    std::sort(cardIds.begin(), cardIds.end(), [](const QString &left, const QString &right) {
        return left.toInt() < right.toInt();
    });
    return cardIds;
}

CommandAttempt NpuInfoProvider::runCommand(const QStringList &arguments) const
{
    CommandAttempt attempt;
    const QString baseCommand = QStringLiteral("npu-smi %1").arg(arguments.join(' '));
    attempt.command = baseCommand;

#ifndef ACEND_MONITOR_ENABLE_REMOTE
    if (m_connectionOptions.useSsh) {
        attempt.exitCode = -1;
        attempt.stdErr = QStringLiteral("Remote mode is available only in Debug builds.");
        return attempt;
    }
#endif

    QProcess process;
    QString program = QStringLiteral("npu-smi");
    QStringList processArguments = arguments;

    if (m_connectionOptions.useSsh) {
        if (m_connectionOptions.host.trimmed().isEmpty()) {
            attempt.exitCode = -1;
            attempt.stdErr = QStringLiteral("SSH host is empty.");
            return attempt;
        }

        program = QStringLiteral("ssh");
        processArguments.clear();
        if (m_connectionOptions.port > 0 && m_connectionOptions.port != 22) {
            processArguments << QStringLiteral("-p") << QString::number(m_connectionOptions.port);
        }

        const QString destination = m_connectionOptions.user.trimmed().isEmpty()
            ? m_connectionOptions.host.trimmed()
            : QStringLiteral("%1@%2").arg(m_connectionOptions.user.trimmed(), m_connectionOptions.host.trimmed());
        QStringList sshArguments;
        if (m_connectionOptions.port > 0 && m_connectionOptions.port != 22) {
            sshArguments << QStringLiteral("-p") << QString::number(m_connectionOptions.port);
        }
        sshArguments << destination << QStringLiteral("npu-smi") << arguments;

        if (!m_connectionOptions.password.isEmpty()) {
            const QString sshpassPath = QStandardPaths::findExecutable(QStringLiteral("sshpass"));
            if (sshpassPath.isEmpty()) {
                attempt.exitCode = -1;
                attempt.stdErr = QStringLiteral("Password mode requires sshpass. Install sshpass or use SSH key-based login.");
                return attempt;
            }
            program = sshpassPath;
            processArguments.clear();
            processArguments << QStringLiteral("-p") << m_connectionOptions.password << QStringLiteral("ssh") << sshArguments;
        } else {
            processArguments = sshArguments;
        }

        attempt.command = QStringLiteral("ssh %1 %2").arg(destination, baseCommand);
    }

    process.start(program, processArguments);

    if (!process.waitForStarted(3000)) {
        attempt.exitCode = -1;
        attempt.stdErr = m_connectionOptions.useSsh
            ? QStringLiteral("Failed to start ssh. Ensure ssh is installed and reachable.")
            : QStringLiteral("Failed to start npu-smi. Ensure it is installed and in PATH.");
        return attempt;
    }

    if (!process.waitForFinished(8000)) {
        process.kill();
        process.waitForFinished(1000);
        attempt.exitCode = -1;
        attempt.stdErr = QStringLiteral("Command timed out.");
        return attempt;
    }

    attempt.exitCode = process.exitCode();
    attempt.stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
    attempt.stdErr = QString::fromLocal8Bit(process.readAllStandardError());
    return attempt;
}

QString NpuInfoProvider::cleanValue(QString value) const
{
    value = value.trimmed();
    if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith('\'') && value.endsWith('\''))) {
        value = value.mid(1, value.size() - 2);
    }
    return value.trimmed();
}