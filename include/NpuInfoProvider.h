#pragma once

#include "NpuTypes.h"

#include <QObject>

struct NpuConnectionOptions {
    bool useSsh = false;
    QString user;
    QString host;
    QString password;
    int port = 22;
};

class NpuInfoProvider : public QObject {
    Q_OBJECT

public:
    explicit NpuInfoProvider(QObject *parent = nullptr);

    void setConnectionOptions(const NpuConnectionOptions &options);
    NpuScanResult scan() const;
    NpuMonitorSnapshot collectMonitor(const QStringList &cardIds) const;
    QVector<NpuProcessInfo> collectProcesses(const QString &cardId) const;
    QVector<NpuDeviceParam> collectDeviceParams(const QString &cardId, const QStringList &chipIds) const;
    NpuParamSetResult setDeviceParam(
        const QString &cardId,
        const QString &chipId,
        const QString &paramKey,
        const QString &value) const;

private:
    struct UsageMetrics {
        double memoryUsageRate = 0.0;
        bool hasMemoryUsageRate = false;
        double aicoreUsage = 0.0;
        bool hasAicoreUsage = false;
        double aivectorUsage = 0.0;
        bool hasAivectorUsage = false;
        double aicpuUsage = 0.0;
        bool hasAicpuUsage = false;
    };

    struct CpuNumConfig {
        QString aiCore;
        QString controlCore;
        QString dataCore;
    };

    QVector<NpuCard> parseCardList(const QString &output) const;
    QString parseNpuSmiVersion(const QString &output) const;
    QVector<NpuMappingEntry> parseMapping(const QString &output) const;
    void parseBoardVersions(const QString &output, QString *softwareVersion, QString *firmwareVersion) const;
    QMap<QString, QString> parseMemoryByChip(const QString &output, QString *cardMemoryTotal) const;
    QMap<QString, QString> parseHealthByChip(const QString &output) const;
    QMap<QString, double> parseTemperatureByChip(const QString &output) const;
    QMap<QString, UsageMetrics> parseUsageByChip(const QString &output) const;
    QMap<QString, CpuNumConfig> parseCpuNumCfgByChip(const QString &output) const;
    QVector<NpuProcessInfo> parseProcesses(const QString &output, const QString &cardId) const;
    double parsePower(const QString &output, bool *ok) const;
    QString parseParamValue(const QString &output, const QStringList &fieldKeywords) const;
    bool parseParamValueAsDouble(
        const QString &output,
        const QStringList &fieldKeywords,
        double *value) const;
    double parseNumericValue(const QString &value, bool *ok) const;
    QStringList extractCardIds(const QVector<NpuCard> &cards, const QVector<NpuMappingEntry> &mappings) const;
    CommandAttempt runCommand(const QStringList &arguments) const;
    QString cleanValue(QString value) const;

    NpuConnectionOptions m_connectionOptions;
};