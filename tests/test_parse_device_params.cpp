// Unit tests for NpuInfoProvider device-parameter helpers:
//   - parseParamValue
//   - parseParamValueAsDouble
//   - collectDeviceParams / setDeviceParam argument validation
//
// parseParamValue / parseParamValueAsDouble are private const member
// functions. This test-only translation unit exposes them with the classic
// `#define private public` include trick so that production headers stay
// untouched.

#define private public
#include "NpuInfoProvider.h"
#undef private

#include <QtTest>

class TestParseDeviceParams : public QObject {
    Q_OBJECT

private slots:
    void parseParamValue_matchesKeywordCaseInsensitive();
    void parseParamValue_ignoresNonNumericFallbackOnlyWhenNoNumber();
    void parseParamValue_missingFieldReturnsEmpty();
    void parseParamValue_emptyOutputReturnsEmpty();
    void parseParamValue_ignoresSeparatorLines();
    void parseParamValueAsDouble_parsesLimitValue();
    void parseParamValueAsDouble_failsOnMissingField();
    void setDeviceParam_rejectsUnknownKey();
    void setDeviceParam_rejectsEmptyInputs();

private:
    NpuInfoProvider m_provider;
};

void TestParseDeviceParams::parseParamValue_matchesKeywordCaseInsensitive()
{
    const QString output = QStringLiteral(
        "\n"
        "  Current Aicore frequency(MHz) : 1500\n"
        "\n");
    const QStringList keywords = { QStringLiteral("Aicore frequency") };

    QCOMPARE(
        m_provider.parseParamValue(output, keywords),
        QStringLiteral("1500"));
}

void TestParseDeviceParams::parseParamValue_ignoresNonNumericFallbackOnlyWhenNoNumber()
{
    const QString output = QStringLiteral("Power Limit : 200.0 W\n");
    const QStringList keywords = { QStringLiteral("Power Limit") };

    QCOMPARE(
        m_provider.parseParamValue(output, keywords),
        QStringLiteral("200"));
}

void TestParseDeviceParams::parseParamValue_missingFieldReturnsEmpty()
{
    const QString output = QStringLiteral("Other Field : 123\n");

    QVERIFY(m_provider.parseParamValue(
                output,
                { QStringLiteral("Power Limit") })
                .isEmpty());
}

void TestParseDeviceParams::parseParamValue_emptyOutputReturnsEmpty()
{
    QVERIFY(m_provider.parseParamValue(QString(), { QStringLiteral("Power Limit") }).isEmpty());
    QVERIFY(m_provider.parseParamValue(QStringLiteral("Power Limit : 1\n"), QStringList()).isEmpty());
}

void TestParseDeviceParams::parseParamValue_ignoresSeparatorLines()
{
    const QString output = QStringLiteral(
        "+-------------------+\n"
        " Power Limit : 150\n"
        "+-------------------+\n");

    QCOMPARE(
        m_provider.parseParamValue(output, { QStringLiteral("Power Limit") }),
        QStringLiteral("150"));
}

void TestParseDeviceParams::parseParamValueAsDouble_parsesLimitValue()
{
    const QString output = QStringLiteral("Aicore limit : 200.0\n");

    double value = 0.0;
    QVERIFY(m_provider.parseParamValueAsDouble(
        output,
        { QStringLiteral("Aicore limit") },
        &value));
    QCOMPARE(value, 200.0);
}

void TestParseDeviceParams::parseParamValueAsDouble_failsOnMissingField()
{
    double value = 0.0;
    QVERIFY(!m_provider.parseParamValueAsDouble(
        QStringLiteral("Unrelated : 10\n"),
        { QStringLiteral("Aicore limit") },
        &value));
    QCOMPARE(value, 0.0);
}

void TestParseDeviceParams::setDeviceParam_rejectsUnknownKey()
{
    const NpuParamSetResult result = m_provider.setDeviceParam(
        QStringLiteral("0"),
        QStringLiteral("0"),
        QStringLiteral("not-a-param"),
        QStringLiteral("100"));

    QVERIFY(!result.success);
    QVERIFY(!result.message.isEmpty());
}

void TestParseDeviceParams::setDeviceParam_rejectsEmptyInputs()
{
    const NpuParamSetResult missingCard = m_provider.setDeviceParam(
        QString(), QStringLiteral("0"), QStringLiteral("power-limit"), QStringLiteral("100"));
    QVERIFY(!missingCard.success);

    const NpuParamSetResult missingChip = m_provider.setDeviceParam(
        QStringLiteral("0"), QString(), QStringLiteral("power-limit"), QStringLiteral("100"));
    QVERIFY(!missingChip.success);

    const NpuParamSetResult missingValue = m_provider.setDeviceParam(
        QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("power-limit"), QStringLiteral("  "));
    QVERIFY(!missingValue.success);
}

QTEST_MAIN(TestParseDeviceParams)
#include "test_parse_device_params.moc"
