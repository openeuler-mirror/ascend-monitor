#pragma once

#include <QMap>
#include <QString>
#include <QVector>

struct NpuCard {
    QString cardId;
    QString softwareVersion;
    QString firmwareVersion;
    QString productName;
    QString serialNumber;
    QString chipCount;
    QString health;
    QString memoryTotal;
};

struct NpuMappingEntry {
    QString cardId;
    QString chipId;
    QString logicId;
    QString chipName;
    QString memoryTotal;
    QString aiCore;
    QString controlCore;
    QString dataCore;
};

struct CommandAttempt {
    QString command;
    int exitCode = -1;
    QString stdOut;
    QString stdErr;
};

struct NpuScanResult {
    QString npuSmiVersion;
    QVector<NpuCard> cards;
    QVector<NpuMappingEntry> mappings;
    QVector<CommandAttempt> attempts;
    QString rawListOutput;
    QString rawMappingOutput;
    QString rawBoardOutput;
    QString errorMessage;

    bool hasData() const
    {
        return !cards.isEmpty() || !mappings.isEmpty();
    }
};

struct NpuCoreMonitor {
    QString cardId;
    QString chipId;
    double temperature = 0.0;
    bool hasTemperature = false;
    double memoryUsageRate = 0.0;
    bool hasMemoryUsageRate = false;
    double aicoreUsage = 0.0;
    bool hasAicoreUsage = false;
    double aivectorUsage = 0.0;
    bool hasAivectorUsage = false;
    double aicpuUsage = 0.0;
    bool hasAicpuUsage = false;
};

struct NpuPowerMonitor {
    QString cardId;
    double power = 0.0;
    bool hasPower = false;
};

struct NpuMonitorSnapshot {
    QVector<NpuCoreMonitor> coreMonitors;
    QVector<NpuPowerMonitor> powerMonitors;
    QStringList errors;
};

struct NpuProcessInfo {
    QString cardId;
    QString chipId;
    QString pid;
    QString processName;
    QString memoryMb;
};