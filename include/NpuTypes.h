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

struct NpuDeviceParam {
    QString key;            // npu-smi set 参数类型，例如 "power-limit"
    QString name;           // 展示名称，例如 "功耗上限"
    QString cardId;
    QString chipId;
    QString value;          // 当前值（原始文本，查询失败时为空）
    QString unit;           // 单位，例如 "W" / "MHz"
    double minValue = 0.0;
    double maxValue = 0.0;
    bool editable = false;  // 是否可通过 npu-smi set 修改
    bool supported = false; // 查询是否成功（不支持的芯片/产品为 false）
    QString description;    // 说明信息
};

struct NpuParamSetResult {
    bool success = false;
    QString command;        // 实际执行的 npu-smi 命令
    QString message;        // 结果说明或错误信息
};

struct NpuProcessInfo {
    QString cardId;
    QString chipId;
    QString pid;
    QString processName;
    QString memoryMb;
};