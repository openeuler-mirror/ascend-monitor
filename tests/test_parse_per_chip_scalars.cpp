// Unit tests for the per-chip scalar-metric parsers of NpuInfoProvider:
//   - parseMemoryByChip
//   - parseHealthByChip
//   - parseTemperatureByChip
//   - parsePower
//
// The parsers are private const member functions. This test-only translation
// unit exposes them with the classic `#define private public` include trick so
// that production headers stay untouched.

#define private public
#include "NpuInfoProvider.h"
#undef private

#include <QtTest>

class TestParsePerChipScalars : public QObject {
    Q_OBJECT

private slots:
    void parseMemoryByChip_emptyOutputReturnsEmptyAndClearsTotal();
    void parseMemoryByChip_singleChip();
    void parseMemoryByChip_multipleChipsSumIntoTotal();
    void parseMemoryByChip_nonNumericCapacityKeptRaw();
    void parseMemoryByChip_mixedNumericAndNonNumericTotal();
    void parseMemoryByChip_capacityWithoutChipIdIgnored();
    void parseMemoryByChip_nullCardTotalPointer();
    void parseMemoryByChip_unrelatedKeysIgnored();
    void parseHealthByChip_emptyOutputReturnsEmpty();
    void parseHealthByChip_singleChip();
    void parseHealthByChip_multipleChips();
    void parseHealthByChip_chipNameKey();
    void parseHealthByChip_healthWithoutChipIgnored();
    void parseHealthByChip_chipWithoutHealthIgnored();
    void parseTemperatureByChip_emptyOutputReturnsEmpty();
    void parseTemperatureByChip_singleChip();
    void parseTemperatureByChip_multipleChips();
    void parseTemperatureByChip_decimalValue();
    void parseTemperatureByChip_nonNumericValueSkipped();
    void parseTemperatureByChip_chipWithoutTemperatureIgnored();
    void parseTemperatureByChip_temperatureWithoutChipIgnored();
    void parsePower_powerDissipationKey();
    void parsePower_unitSuffixTolerated();
    void parsePower_firstMatchingKeyWins();
    void parsePower_noPowerLineFails();
    void parsePower_nonNumericValueFails();
    void parsePower_nullOkPointer();

private:
    NpuInfoProvider m_provider;
};

void TestParsePerChipScalars::parseMemoryByChip_emptyOutputReturnsEmptyAndClearsTotal()
{
    QString total = QStringLiteral("stale");
    const QMap<QString, QString> result = m_provider.parseMemoryByChip(QString(), &total);
    QVERIFY(result.isEmpty());
    QVERIFY(total.isEmpty());
}

void TestParsePerChipScalars::parseMemoryByChip_singleChip()
{
    const QString output = QStringLiteral(
        "DDR Capacity(MB) : 65536\n"
        "Chip ID : 0\n");

    QString total;
    const QMap<QString, QString> result = m_provider.parseMemoryByChip(output, &total);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("0")), QStringLiteral("65536 MB"));
    QCOMPARE(total, QStringLiteral("65536 MB"));
}

void TestParsePerChipScalars::parseMemoryByChip_multipleChipsSumIntoTotal()
{
    const QString output = QStringLiteral(
        "DDR Capacity(MB) : 65536\n"
        "Chip ID : 0\n"
        "DDR Capacity(MB) : 32768\n"
        "Chip ID : 1\n");

    QString total;
    const QMap<QString, QString> result = m_provider.parseMemoryByChip(output, &total);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value(QStringLiteral("0")), QStringLiteral("65536 MB"));
    QCOMPARE(result.value(QStringLiteral("1")), QStringLiteral("32768 MB"));
    QCOMPARE(total, QStringLiteral("98304 MB"));
}

void TestParsePerChipScalars::parseMemoryByChip_nonNumericCapacityKeptRaw()
{
    const QString output = QStringLiteral(
        "DDR Capacity(MB) : N/A\n"
        "Chip ID : 0\n");

    QString total;
    const QMap<QString, QString> result = m_provider.parseMemoryByChip(output, &total);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("0")), QStringLiteral("N/A"));
    QVERIFY(total.isEmpty());
}

void TestParsePerChipScalars::parseMemoryByChip_mixedNumericAndNonNumericTotal()
{
    const QString output = QStringLiteral(
        "DDR Capacity(MB) : 65536\n"
        "Chip ID : 0\n"
        "DDR Capacity(MB) : -\n"
        "Chip ID : 1\n");

    QString total;
    const QMap<QString, QString> result = m_provider.parseMemoryByChip(output, &total);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value(QStringLiteral("1")), QStringLiteral("-"));
    // Only numeric capacities contribute to the card total.
    QCOMPARE(total, QStringLiteral("65536 MB"));
}

void TestParsePerChipScalars::parseMemoryByChip_capacityWithoutChipIdIgnored()
{
    const QString output = QStringLiteral(
        "DDR Capacity(MB) : 65536\n"
        "Some Other Key : value\n");

    QString total;
    const QMap<QString, QString> result = m_provider.parseMemoryByChip(output, &total);
    QVERIFY(result.isEmpty());
    QVERIFY(total.isEmpty());
}

void TestParsePerChipScalars::parseMemoryByChip_nullCardTotalPointer()
{
    const QString output = QStringLiteral(
        "DDR Capacity(MB) : 65536\n"
        "Chip ID : 0\n");

    const QMap<QString, QString> result = m_provider.parseMemoryByChip(output, nullptr);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("0")), QStringLiteral("65536 MB"));
}

void TestParsePerChipScalars::parseMemoryByChip_unrelatedKeysIgnored()
{
    const QString output = QStringLiteral(
        "Board Name : Atlas 900\n"
        "DDR Capacity(MB) : 65536\n"
        "Chip ID : 0\n");

    QString total;
    const QMap<QString, QString> result = m_provider.parseMemoryByChip(output, &total);
    QCOMPARE(result.size(), 1);
    QCOMPARE(total, QStringLiteral("65536 MB"));
}

void TestParsePerChipScalars::parseHealthByChip_emptyOutputReturnsEmpty()
{
    QVERIFY(m_provider.parseHealthByChip(QString()).isEmpty());
}

void TestParsePerChipScalars::parseHealthByChip_singleChip()
{
    const QString output = QStringLiteral(
        "Health : OK\n"
        "Chip ID : 0\n");

    const QMap<QString, QString> result = m_provider.parseHealthByChip(output);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("0")), QStringLiteral("OK"));
}

void TestParsePerChipScalars::parseHealthByChip_multipleChips()
{
    const QString output = QStringLiteral(
        "Health : OK\n"
        "Chip ID : 0\n"
        "Health : Warning\n"
        "Chip ID : 1\n");

    const QMap<QString, QString> result = m_provider.parseHealthByChip(output);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value(QStringLiteral("0")), QStringLiteral("OK"));
    QCOMPARE(result.value(QStringLiteral("1")), QStringLiteral("Warning"));
}

void TestParsePerChipScalars::parseHealthByChip_chipNameKey()
{
    const QString output = QStringLiteral(
        "Health : OK\n"
        "Chip Name : Ascend910\n");

    const QMap<QString, QString> result = m_provider.parseHealthByChip(output);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("Ascend910")), QStringLiteral("OK"));
}

void TestParsePerChipScalars::parseHealthByChip_healthWithoutChipIgnored()
{
    const QString output = QStringLiteral(
        "Health : OK\n"
        "Board Name : Atlas 900\n");

    QVERIFY(m_provider.parseHealthByChip(output).isEmpty());
}

void TestParsePerChipScalars::parseHealthByChip_chipWithoutHealthIgnored()
{
    const QString output = QStringLiteral(
        "Chip ID : 0\n"
        "Chip ID : 1\n");

    QVERIFY(m_provider.parseHealthByChip(output).isEmpty());
}

void TestParsePerChipScalars::parseTemperatureByChip_emptyOutputReturnsEmpty()
{
    QVERIFY(m_provider.parseTemperatureByChip(QString()).isEmpty());
}

void TestParsePerChipScalars::parseTemperatureByChip_singleChip()
{
    const QString output = QStringLiteral(
        "Temperature : 45\n"
        "Chip ID : 0\n");

    const QMap<QString, double> result = m_provider.parseTemperatureByChip(output);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("0")), 45.0);
}

void TestParsePerChipScalars::parseTemperatureByChip_multipleChips()
{
    const QString output = QStringLiteral(
        "Temperature : 45\n"
        "Chip ID : 0\n"
        "Temperature : 50\n"
        "Chip ID : 1\n");

    const QMap<QString, double> result = m_provider.parseTemperatureByChip(output);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value(QStringLiteral("0")), 45.0);
    QCOMPARE(result.value(QStringLiteral("1")), 50.0);
}

void TestParsePerChipScalars::parseTemperatureByChip_decimalValue()
{
    const QString output = QStringLiteral(
        "Temperature : 45.5\n"
        "Chip ID : 0\n");

    const QMap<QString, double> result = m_provider.parseTemperatureByChip(output);
    QCOMPARE(result.value(QStringLiteral("0")), 45.5);
}

void TestParsePerChipScalars::parseTemperatureByChip_nonNumericValueSkipped()
{
    const QString output = QStringLiteral(
        "Temperature : N/A\n"
        "Chip ID : 0\n"
        "Temperature : 45\n"
        "Chip ID : 1\n");

    const QMap<QString, double> result = m_provider.parseTemperatureByChip(output);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("1")), 45.0);
}

void TestParsePerChipScalars::parseTemperatureByChip_chipWithoutTemperatureIgnored()
{
    const QString output = QStringLiteral("Chip ID : 0\n");
    QVERIFY(m_provider.parseTemperatureByChip(output).isEmpty());
}

void TestParsePerChipScalars::parseTemperatureByChip_temperatureWithoutChipIgnored()
{
    const QString output = QStringLiteral("Temperature : 45\n");
    QVERIFY(m_provider.parseTemperatureByChip(output).isEmpty());
}

void TestParsePerChipScalars::parsePower_powerDissipationKey()
{
    const QString output = QStringLiteral(
        "Power Dissipation(W) : 300.5\n"
        "Board Name : Atlas 900\n");

    bool ok = false;
    const double power = m_provider.parsePower(output, &ok);
    QVERIFY(ok);
    QCOMPARE(power, 300.5);
}

void TestParsePerChipScalars::parsePower_unitSuffixTolerated()
{
    const QString output = QStringLiteral("Power : 295W\n");

    bool ok = false;
    const double power = m_provider.parsePower(output, &ok);
    QVERIFY(ok);
    QCOMPARE(power, 295.0);
}

void TestParsePerChipScalars::parsePower_firstMatchingKeyWins()
{
    const QString output = QStringLiteral(
        "Power : 250\n"
        "Power Dissipation(W) : 300\n");

    bool ok = false;
    const double power = m_provider.parsePower(output, &ok);
    QVERIFY(ok);
    QCOMPARE(power, 250.0);
}

void TestParsePerChipScalars::parsePower_noPowerLineFails()
{
    const QString output = QStringLiteral(
        "Board Name : Atlas 900\n"
        "Temperature : 45\n");

    bool ok = true;
    const double power = m_provider.parsePower(output, &ok);
    QVERIFY(!ok);
    QCOMPARE(power, 0.0);
}

void TestParsePerChipScalars::parsePower_nonNumericValueFails()
{
    const QString output = QStringLiteral("Power : N/A\n");

    bool ok = true;
    const double power = m_provider.parsePower(output, &ok);
    QVERIFY(!ok);
    QCOMPARE(power, 0.0);
}

void TestParsePerChipScalars::parsePower_nullOkPointer()
{
    const QString output = QStringLiteral("Power : 295W\n");
    const double power = m_provider.parsePower(output, nullptr);
    QCOMPARE(power, 295.0);

    QCOMPARE(m_provider.parsePower(QStringLiteral("no power here"), nullptr), 0.0);
}

QTEST_GUILESS_MAIN(TestParsePerChipScalars)

#include "test_parse_per_chip_scalars.moc"
