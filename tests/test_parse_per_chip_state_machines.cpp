// Unit tests for the per-chip state-machine parsers of NpuInfoProvider:
//   - parseUsageByChip
//   - parseCpuNumCfgByChip
//   - parseProcesses
//
// These parsers accumulate "pending" metric lines and associate them with
// the next "Chip ID" line; parseProcesses is a line-oriented state machine
// that only collects rows after seeing the process table header.
//
// The parsers are private const member functions. This test-only translation
// unit exposes them with the classic `#define private public` include trick so
// that production headers stay untouched.

#define private public
#include "NpuInfoProvider.h"
#undef private

#include <QtTest>

class TestParsePerChipStateMachines : public QObject {
    Q_OBJECT

private slots:
    void parseUsageByChip_emptyOutputReturnsEmpty();
    void parseUsageByChip_singleChipAllMetrics();
    void parseUsageByChip_hbmKeyVariantForMemory();
    void parseUsageByChip_aivectorKeyVariant();
    void parseUsageByChip_nonNumericMetricFlaggedFalse();
    void parseUsageByChip_allNonNumericChipOmitted();
    void parseUsageByChip_chipIdWithoutPendingMetricsIgnored();
    void parseUsageByChip_metricsBeforeAnyChipIdDiscarded();
    void parseUsageByChip_twoChipsIndependent();
    void parseUsageByChip_metricKeysCaseInsensitive();
    void parseCpuNumCfgByChip_emptyOutputReturnsEmpty();
    void parseCpuNumCfgByChip_singleChipAllFields();
    void parseCpuNumCfgByChip_partialFields();
    void parseCpuNumCfgByChip_twoChipsIndependent();
    void parseCpuNumCfgByChip_chipIdWithoutPendingFieldsIgnored();
    void parseCpuNumCfgByChip_unrelatedKeysIgnored();
    void parseProcesses_emptyOutputReturnsEmpty();
    void parseProcesses_headerWithoutRowsReturnsEmpty();
    void parseProcesses_noRunningProcessesMessage();
    void parseProcesses_singleRow();
    void parseProcesses_multipleRowsWithSpacesInName();
    void parseProcesses_separatorLinesIgnored();
    void parseProcesses_rowsBeforeHeaderIgnored();
    void parseProcesses_cardIdFilter();
    void parseProcesses_emptyCardIdFilterKeepsAll();

private:
    NpuInfoProvider m_provider;
};

void TestParsePerChipStateMachines::parseUsageByChip_emptyOutputReturnsEmpty()
{
    QVERIFY(m_provider.parseUsageByChip(QString()).isEmpty());
}

void TestParsePerChipStateMachines::parseUsageByChip_singleChipAllMetrics()
{
    const QString output = QStringLiteral(
        "DDR Usage Rate(%) : 62.5\n"
        "AICore Usage Rate(%) : 80\n"
        "VectorCore Usage Rate(%) : 30\n"
        "AICPU Usage Rate(%) : 12.25\n"
        "Chip ID : 0\n");

    const QMap<QString, NpuInfoProvider::UsageMetrics> result = m_provider.parseUsageByChip(output);
    QCOMPARE(result.size(), 1);
    const NpuInfoProvider::UsageMetrics metrics = result.value(QStringLiteral("0"));
    QVERIFY(metrics.hasMemoryUsageRate);
    QVERIFY(metrics.hasAicoreUsage);
    QVERIFY(metrics.hasAivectorUsage);
    QVERIFY(metrics.hasAicpuUsage);
    QCOMPARE(metrics.memoryUsageRate, 62.5);
    QCOMPARE(metrics.aicoreUsage, 80.0);
    QCOMPARE(metrics.aivectorUsage, 30.0);
    QCOMPARE(metrics.aicpuUsage, 12.25);
}

void TestParsePerChipStateMachines::parseUsageByChip_hbmKeyVariantForMemory()
{
    const QString output = QStringLiteral(
        "HBM Usage Rate(%) : 40\n"
        "Chip ID : 0\n");

    const QMap<QString, NpuInfoProvider::UsageMetrics> result = m_provider.parseUsageByChip(output);
    const NpuInfoProvider::UsageMetrics metrics = result.value(QStringLiteral("0"));
    QVERIFY(metrics.hasMemoryUsageRate);
    QCOMPARE(metrics.memoryUsageRate, 40.0);
}

void TestParsePerChipStateMachines::parseUsageByChip_aivectorKeyVariant()
{
    const QString output = QStringLiteral(
        "AIVector Usage Rate(%) : 25\n"
        "Chip ID : 0\n");

    const QMap<QString, NpuInfoProvider::UsageMetrics> result = m_provider.parseUsageByChip(output);
    const NpuInfoProvider::UsageMetrics metrics = result.value(QStringLiteral("0"));
    QVERIFY(metrics.hasAivectorUsage);
    QCOMPARE(metrics.aivectorUsage, 25.0);
}

void TestParsePerChipStateMachines::parseUsageByChip_nonNumericMetricFlaggedFalse()
{
    const QString output = QStringLiteral(
        "DDR Usage Rate(%) : N/A\n"
        "AICore Usage Rate(%) : 50\n"
        "Chip ID : 0\n");

    const QMap<QString, NpuInfoProvider::UsageMetrics> result = m_provider.parseUsageByChip(output);
    const NpuInfoProvider::UsageMetrics metrics = result.value(QStringLiteral("0"));
    QVERIFY(!metrics.hasMemoryUsageRate);
    QCOMPARE(metrics.memoryUsageRate, 0.0);
    QVERIFY(metrics.hasAicoreUsage);
    QCOMPARE(metrics.aicoreUsage, 50.0);
}

void TestParsePerChipStateMachines::parseUsageByChip_allNonNumericChipOmitted()
{
    const QString output = QStringLiteral(
        "DDR Usage Rate(%) : -\n"
        "AICore Usage Rate(%) : -\n"
        "Chip ID : 0\n");

    QVERIFY(m_provider.parseUsageByChip(output).isEmpty());
}

void TestParsePerChipStateMachines::parseUsageByChip_chipIdWithoutPendingMetricsIgnored()
{
    const QString output = QStringLiteral(
        "Chip ID : 0\n"
        "DDR Usage Rate(%) : 60\n"
        "Chip ID : 1\n");

    const QMap<QString, NpuInfoProvider::UsageMetrics> result = m_provider.parseUsageByChip(output);
    QCOMPARE(result.size(), 1);
    QVERIFY(result.contains(QStringLiteral("1")));
    QVERIFY(!result.contains(QStringLiteral("0")));
}

void TestParsePerChipStateMachines::parseUsageByChip_metricsBeforeAnyChipIdDiscarded()
{
    // Metrics only materialize when a Chip ID line follows them; without one
    // they are dropped when a later Chip ID line resets the pending state.
    const QString output = QStringLiteral(
        "DDR Usage Rate(%) : 60\n"
        "Chip ID : 0\n"
        "AICore Usage Rate(%) : 70\n");

    const QMap<QString, NpuInfoProvider::UsageMetrics> result = m_provider.parseUsageByChip(output);
    QCOMPARE(result.size(), 1);
    const NpuInfoProvider::UsageMetrics metrics = result.value(QStringLiteral("0"));
    QVERIFY(metrics.hasMemoryUsageRate);
    QVERIFY(!metrics.hasAicoreUsage);
}

void TestParsePerChipStateMachines::parseUsageByChip_twoChipsIndependent()
{
    const QString output = QStringLiteral(
        "DDR Usage Rate(%) : 10\n"
        "AICore Usage Rate(%) : 20\n"
        "Chip ID : 0\n"
        "DDR Usage Rate(%) : 90\n"
        "AICore Usage Rate(%) : 80\n"
        "Chip ID : 1\n");

    const QMap<QString, NpuInfoProvider::UsageMetrics> result = m_provider.parseUsageByChip(output);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value(QStringLiteral("0")).memoryUsageRate, 10.0);
    QCOMPARE(result.value(QStringLiteral("0")).aicoreUsage, 20.0);
    QCOMPARE(result.value(QStringLiteral("1")).memoryUsageRate, 90.0);
    QCOMPARE(result.value(QStringLiteral("1")).aicoreUsage, 80.0);
}

void TestParsePerChipStateMachines::parseUsageByChip_metricKeysCaseInsensitive()
{
    const QString output = QStringLiteral(
        "ddr usage rate(%) : 33\n"
        "aicore usage rate(%) : 44\n"
        "chip id : 0\n");

    const QMap<QString, NpuInfoProvider::UsageMetrics> result = m_provider.parseUsageByChip(output);
    const NpuInfoProvider::UsageMetrics metrics = result.value(QStringLiteral("0"));
    QCOMPARE(metrics.memoryUsageRate, 33.0);
    QCOMPARE(metrics.aicoreUsage, 44.0);
}

void TestParsePerChipStateMachines::parseCpuNumCfgByChip_emptyOutputReturnsEmpty()
{
    QVERIFY(m_provider.parseCpuNumCfgByChip(QString()).isEmpty());
}

void TestParsePerChipStateMachines::parseCpuNumCfgByChip_singleChipAllFields()
{
    const QString output = QStringLiteral(
        "Current AI CPU Number : 1\n"
        "Current Control CPU Number : 2\n"
        "Current Data CPU Number : 3\n"
        "Chip ID : 0\n");

    const QMap<QString, NpuInfoProvider::CpuNumConfig> result = m_provider.parseCpuNumCfgByChip(output);
    QCOMPARE(result.size(), 1);
    const NpuInfoProvider::CpuNumConfig cfg = result.value(QStringLiteral("0"));
    QCOMPARE(cfg.aiCore, QStringLiteral("1"));
    QCOMPARE(cfg.controlCore, QStringLiteral("2"));
    QCOMPARE(cfg.dataCore, QStringLiteral("3"));
}

void TestParsePerChipStateMachines::parseCpuNumCfgByChip_partialFields()
{
    const QString output = QStringLiteral(
        "Current AI CPU Number : 1\n"
        "Chip ID : 0\n");

    const QMap<QString, NpuInfoProvider::CpuNumConfig> result = m_provider.parseCpuNumCfgByChip(output);
    QCOMPARE(result.size(), 1);
    const NpuInfoProvider::CpuNumConfig cfg = result.value(QStringLiteral("0"));
    QCOMPARE(cfg.aiCore, QStringLiteral("1"));
    QVERIFY(cfg.controlCore.isEmpty());
    QVERIFY(cfg.dataCore.isEmpty());
}

void TestParsePerChipStateMachines::parseCpuNumCfgByChip_twoChipsIndependent()
{
    const QString output = QStringLiteral(
        "Current AI CPU Number : 1\n"
        "Chip ID : 0\n"
        "Current AI CPU Number : 2\n"
        "Current Control CPU Number : 4\n"
        "Chip ID : 1\n");

    const QMap<QString, NpuInfoProvider::CpuNumConfig> result = m_provider.parseCpuNumCfgByChip(output);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value(QStringLiteral("0")).aiCore, QStringLiteral("1"));
    QVERIFY(result.value(QStringLiteral("0")).controlCore.isEmpty());
    QCOMPARE(result.value(QStringLiteral("1")).aiCore, QStringLiteral("2"));
    QCOMPARE(result.value(QStringLiteral("1")).controlCore, QStringLiteral("4"));
}

void TestParsePerChipStateMachines::parseCpuNumCfgByChip_chipIdWithoutPendingFieldsIgnored()
{
    const QString output = QStringLiteral(
        "Chip ID : 0\n"
        "Current AI CPU Number : 1\n"
        "Chip ID : 1\n");

    const QMap<QString, NpuInfoProvider::CpuNumConfig> result = m_provider.parseCpuNumCfgByChip(output);
    QCOMPARE(result.size(), 1);
    QVERIFY(result.contains(QStringLiteral("1")));
    QVERIFY(!result.contains(QStringLiteral("0")));
}

void TestParsePerChipStateMachines::parseCpuNumCfgByChip_unrelatedKeysIgnored()
{
    const QString output = QStringLiteral(
        "Board Name : Atlas 900\n"
        "Current AI CPU Number : 1\n"
        "Chip ID : 0\n");

    const QMap<QString, NpuInfoProvider::CpuNumConfig> result = m_provider.parseCpuNumCfgByChip(output);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("0")).aiCore, QStringLiteral("1"));
}

void TestParsePerChipStateMachines::parseProcesses_emptyOutputReturnsEmpty()
{
    QVERIFY(m_provider.parseProcesses(QString(), QStringLiteral("0")).isEmpty());
}

void TestParsePerChipStateMachines::parseProcesses_headerWithoutRowsReturnsEmpty()
{
    const QString output = QStringLiteral(
        "| Process id      | Chip  | Pid   | Process name | Memory(MB) |\n"
        "+-----------------+-------+-------+--------------+------------+\n");

    QVERIFY(m_provider.parseProcesses(output, QStringLiteral("0")).isEmpty());
}

void TestParsePerChipStateMachines::parseProcesses_noRunningProcessesMessage()
{
    const QString output = QStringLiteral(
        "| Process id      | Chip  | Pid   | Process name | Memory(MB) |\n"
        "No running processes found in NPU device 0\n");

    QVERIFY(m_provider.parseProcesses(output, QStringLiteral("0")).isEmpty());
}

void TestParsePerChipStateMachines::parseProcesses_singleRow()
{
    const QString output = QStringLiteral(
        "NPU ID : 0\n"
        "| Process id      | Chip  | Pid   | Process name | Memory(MB) |\n"
        "+-----------------+-------+-------+--------------+------------+\n"
        "| 0 0             | 12345 | python       | 65536        |\n");

    const QVector<NpuProcessInfo> processes = m_provider.parseProcesses(output, QStringLiteral("0"));
    QCOMPARE(processes.size(), 1);
    QCOMPARE(processes.at(0).cardId, QStringLiteral("0"));
    QCOMPARE(processes.at(0).chipId, QStringLiteral("0"));
    QCOMPARE(processes.at(0).pid, QStringLiteral("12345"));
    QCOMPARE(processes.at(0).processName, QStringLiteral("python"));
    QCOMPARE(processes.at(0).memoryMb, QStringLiteral("65536"));
}

void TestParsePerChipStateMachines::parseProcesses_multipleRowsWithSpacesInName()
{
    const QString output = QStringLiteral(
        "| Process id      | Chip  | Pid   | Process name | Memory(MB) |\n"
        "+-----------------+-------+-------+--------------+------------+\n"
        "| 0 0             | 1000  | some process | 1024       |\n"
        "| 0 1             | 2000  | training.bin | 2048       |\n");

    const QVector<NpuProcessInfo> processes = m_provider.parseProcesses(output, QString());
    QCOMPARE(processes.size(), 2);
    QCOMPARE(processes.at(0).processName, QStringLiteral("some process"));
    QCOMPARE(processes.at(0).chipId, QStringLiteral("0"));
    QCOMPARE(processes.at(1).processName, QStringLiteral("training.bin"));
    QCOMPARE(processes.at(1).memoryMb, QStringLiteral("2048"));
}

void TestParsePerChipStateMachines::parseProcesses_separatorLinesIgnored()
{
    const QString output = QStringLiteral(
        "| Process id      | Chip  | Pid   | Process name | Memory(MB) |\n"
        "+-----------------+-------+-------+--------------+------------+\n"
        "| 0 0             | 3000  | python       | 512        |\n"
        "+-----------------+-------+-------+--------------+------------+\n");

    const QVector<NpuProcessInfo> processes = m_provider.parseProcesses(output, QString());
    QCOMPARE(processes.size(), 1);
    QCOMPARE(processes.at(0).pid, QStringLiteral("3000"));
}

void TestParsePerChipStateMachines::parseProcesses_rowsBeforeHeaderIgnored()
{
    // A row-looking line before the process table header is not collected.
    const QString output = QStringLiteral(
        "| 0 0             | 1     | orphan       | 256        |\n"
        "| Process id      | Chip  | Pid   | Process name | Memory(MB) |\n"
        "| 0 0             | 4000  | python       | 128        |\n");

    const QVector<NpuProcessInfo> processes = m_provider.parseProcesses(output, QString());
    QCOMPARE(processes.size(), 1);
    QCOMPARE(processes.at(0).pid, QStringLiteral("4000"));
}

void TestParsePerChipStateMachines::parseProcesses_cardIdFilter()
{
    const QString output = QStringLiteral(
        "| Process id      | Chip  | Pid   | Process name | Memory(MB) |\n"
        "+-----------------+-------+-------+--------------+------------+\n"
        "| 0 0             | 1000  | python       | 1024       |\n"
        "| 1 0             | 2000  | python       | 2048       |\n");

    const QVector<NpuProcessInfo> filtered = m_provider.parseProcesses(output, QStringLiteral("1"));
    QCOMPARE(filtered.size(), 1);
    QCOMPARE(filtered.at(0).cardId, QStringLiteral("1"));
    QCOMPARE(filtered.at(0).pid, QStringLiteral("2000"));
}

void TestParsePerChipStateMachines::parseProcesses_emptyCardIdFilterKeepsAll()
{
    const QString output = QStringLiteral(
        "| Process id      | Chip  | Pid   | Process name | Memory(MB) |\n"
        "+-----------------+-------+-------+--------------+------------+\n"
        "| 0 0             | 1000  | python       | 1024       |\n"
        "| 1 0             | 2000  | python       | 2048       |\n");

    const QVector<NpuProcessInfo> all = m_provider.parseProcesses(output, QString());
    QCOMPARE(all.size(), 2);
}

QTEST_GUILESS_MAIN(TestParsePerChipStateMachines)

#include "test_parse_per_chip_state_machines.moc"
