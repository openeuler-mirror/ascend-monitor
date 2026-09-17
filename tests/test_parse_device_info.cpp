// Unit tests for the device-identification parsers of NpuInfoProvider:
//   - parseCardList
//   - parseMapping
//   - parseNpuSmiVersion
//   - parseBoardVersions
//
// The parsers are private const member functions. This test-only translation
// unit exposes them with the classic `#define private public` include trick so
// that production headers stay untouched.

#define private public
#include "NpuInfoProvider.h"
#undef private

#include <QtTest>

class TestParseDeviceInfo : public QObject {
    Q_OBJECT

private slots:
    void parseCardList_emptyOutputReturnsEmpty();
    void parseCardList_whitespaceOnlyReturnsEmpty();
    void parseCardList_singleCard();
    void parseCardList_multipleCards();
    void parseCardList_repeatedSameNpuIdDoesNotSplit();
    void parseCardList_separatorsAndTotalCountSkipped();
    void parseCardList_quotedValueIsCleaned();
    void parseCardList_fieldsBeforeNpuIdIgnored();
    void parseMapping_emptyOutputReturnsEmpty();
    void parseMapping_singleRowWithChipName();
    void parseMapping_multipleRows();
    void parseMapping_chipNameOnFollowingLine();
    void parseMapping_missingChipNameFallsBackToDash();
    void parseNpuSmiVersion_colonSeparated();
    void parseNpuSmiVersion_equalsSeparatedCaseInsensitive();
    void parseNpuSmiVersion_embeddedInMultiLineOutput();
    void parseNpuSmiVersion_missingVersionReturnsEmpty();
    void parseNpuSmiVersion_withoutSeparatorReturnsEmpty();
    void parseBoardVersions_bothVersionsExtracted();
    void parseBoardVersions_firmwareOnly();
    void parseBoardVersions_emptyOutputClearsValues();
    void parseBoardVersions_nullSoftwarePointerIgnored();
    void parseBoardVersions_unrelatedKeysIgnored();

private:
    NpuInfoProvider m_provider;
};

void TestParseDeviceInfo::parseCardList_emptyOutputReturnsEmpty()
{
    QCOMPARE(m_provider.parseCardList(QString()).size(), 0);
}

void TestParseDeviceInfo::parseCardList_whitespaceOnlyReturnsEmpty()
{
    QCOMPARE(m_provider.parseCardList(QStringLiteral("  \n \n")).size(), 0);
}

void TestParseDeviceInfo::parseCardList_singleCard()
{
    const QString output = QStringLiteral(
        "NPU ID : 0\n"
        "Product Name : Ascend 910B\n"
        "Serial Number : 10432894256\n"
        "Chip Count : 1\n");

    const QVector<NpuCard> cards = m_provider.parseCardList(output);
    QCOMPARE(cards.size(), 1);
    QCOMPARE(cards.at(0).cardId, QStringLiteral("0"));
    QCOMPARE(cards.at(0).productName, QStringLiteral("Ascend 910B"));
    QCOMPARE(cards.at(0).serialNumber, QStringLiteral("10432894256"));
    QCOMPARE(cards.at(0).chipCount, QStringLiteral("1"));
}

void TestParseDeviceInfo::parseCardList_multipleCards()
{
    const QString output = QStringLiteral(
        "NPU ID : 0\n"
        "Product Name : Ascend 910B\n"
        "Serial Number : SN0001\n"
        "Chip Count : 1\n"
        "NPU ID : 1\n"
        "Product Name : Ascend 910B1\n"
        "Serial Number : SN0002\n"
        "Chip Count : 2\n");

    const QVector<NpuCard> cards = m_provider.parseCardList(output);
    QCOMPARE(cards.size(), 2);
    QCOMPARE(cards.at(0).cardId, QStringLiteral("0"));
    QCOMPARE(cards.at(0).serialNumber, QStringLiteral("SN0001"));
    QCOMPARE(cards.at(1).cardId, QStringLiteral("1"));
    QCOMPARE(cards.at(1).productName, QStringLiteral("Ascend 910B1"));
    QCOMPARE(cards.at(1).chipCount, QStringLiteral("2"));
}

void TestParseDeviceInfo::parseCardList_repeatedSameNpuIdDoesNotSplit()
{
    const QString output = QStringLiteral(
        "NPU ID : 0\n"
        "Product Name : Ascend 910B\n"
        "NPU ID : 0\n"
        "Serial Number : SN0001\n");

    const QVector<NpuCard> cards = m_provider.parseCardList(output);
    QCOMPARE(cards.size(), 1);
    QCOMPARE(cards.at(0).productName, QStringLiteral("Ascend 910B"));
    QCOMPARE(cards.at(0).serialNumber, QStringLiteral("SN0001"));
}

void TestParseDeviceInfo::parseCardList_separatorsAndTotalCountSkipped()
{
    const QString output = QStringLiteral(
        "---------------------------------------\n"
        "NPU ID : 0\n"
        "Total Count : 2\n"
        "Product Name : Ascend 910B\n"
        "+------+------+\n");

    const QVector<NpuCard> cards = m_provider.parseCardList(output);
    QCOMPARE(cards.size(), 1);
    QCOMPARE(cards.at(0).cardId, QStringLiteral("0"));
    QCOMPARE(cards.at(0).productName, QStringLiteral("Ascend 910B"));
    QVERIFY(cards.at(0).serialNumber.isEmpty());
}

void TestParseDeviceInfo::parseCardList_quotedValueIsCleaned()
{
    const QString output = QStringLiteral(
        "NPU ID : 0\n"
        "Product Name : \"Ascend 910B\"\n");

    const QVector<NpuCard> cards = m_provider.parseCardList(output);
    QCOMPARE(cards.size(), 1);
    QCOMPARE(cards.at(0).productName, QStringLiteral("Ascend 910B"));
}

void TestParseDeviceInfo::parseCardList_fieldsBeforeNpuIdIgnored()
{
    const QString output = QStringLiteral(
        "Product Name : Orphan\n"
        "Serial Number : ORPHAN\n"
        "NPU ID : 0\n"
        "Chip Count : 1\n");

    const QVector<NpuCard> cards = m_provider.parseCardList(output);
    QCOMPARE(cards.size(), 1);
    QVERIFY(cards.at(0).productName.isEmpty());
    QVERIFY(cards.at(0).serialNumber.isEmpty());
    QCOMPARE(cards.at(0).chipCount, QStringLiteral("1"));
}

void TestParseDeviceInfo::parseMapping_emptyOutputReturnsEmpty()
{
    QCOMPARE(m_provider.parseMapping(QString()).size(), 0);
    QCOMPARE(m_provider.parseMapping(QStringLiteral("   \n")).size(), 0);
}

void TestParseDeviceInfo::parseMapping_singleRowWithChipName()
{
    const QString output = QStringLiteral(
        "Card ID  Chip ID  Logic ID  Chip Name\n"
        "0        0        0         Atlas 900 A3\n");

    const QVector<NpuMappingEntry> mappings = m_provider.parseMapping(output);
    QCOMPARE(mappings.size(), 1);
    QCOMPARE(mappings.at(0).cardId, QStringLiteral("0"));
    QCOMPARE(mappings.at(0).chipId, QStringLiteral("0"));
    QCOMPARE(mappings.at(0).logicId, QStringLiteral("0"));
    QCOMPARE(mappings.at(0).chipName, QStringLiteral("Atlas 900 A3"));
}

void TestParseDeviceInfo::parseMapping_multipleRows()
{
    const QString output = QStringLiteral(
        "Card ID  Chip ID  Logic ID  Chip Name\n"
        "0        0        0         Atlas 900 A3\n"
        "0        1        1         Atlas 900 A3\n"
        "1        0        2         Atlas 300I Duo\n");

    const QVector<NpuMappingEntry> mappings = m_provider.parseMapping(output);
    QCOMPARE(mappings.size(), 3);
    QCOMPARE(mappings.at(0).cardId, QStringLiteral("0"));
    QCOMPARE(mappings.at(0).chipId, QStringLiteral("0"));
    QCOMPARE(mappings.at(1).chipId, QStringLiteral("1"));
    QCOMPARE(mappings.at(2).cardId, QStringLiteral("1"));
    QCOMPARE(mappings.at(2).logicId, QStringLiteral("2"));
    QCOMPARE(mappings.at(2).chipName, QStringLiteral("Atlas 300I Duo"));
}

void TestParseDeviceInfo::parseMapping_chipNameOnFollowingLine()
{
    const QString output = QStringLiteral(
        "Card ID  Chip ID  Logic ID\n"
        "0        0        0\n"
        "Atlas 900 A3\n"
        "0        1        1\n"
        "Atlas 900 A3\n");

    const QVector<NpuMappingEntry> mappings = m_provider.parseMapping(output);
    QCOMPARE(mappings.size(), 2);
    QCOMPARE(mappings.at(0).chipName, QStringLiteral("Atlas 900 A3"));
    QCOMPARE(mappings.at(1).chipId, QStringLiteral("1"));
    QCOMPARE(mappings.at(1).chipName, QStringLiteral("Atlas 900 A3"));
}

void TestParseDeviceInfo::parseMapping_missingChipNameFallsBackToDash()
{
    const QString output = QStringLiteral("0 0 0\n");

    const QVector<NpuMappingEntry> mappings = m_provider.parseMapping(output);
    QCOMPARE(mappings.size(), 1);
    QCOMPARE(mappings.at(0).chipName, QStringLiteral("-"));
}

void TestParseDeviceInfo::parseNpuSmiVersion_colonSeparated()
{
    QCOMPARE(m_provider.parseNpuSmiVersion(QStringLiteral("npu-smi version: 24.1.rc2")),
             QStringLiteral("24.1.rc2"));
}

void TestParseDeviceInfo::parseNpuSmiVersion_equalsSeparatedCaseInsensitive()
{
    QCOMPARE(m_provider.parseNpuSmiVersion(QStringLiteral("NPU-SMI Version = 24.1.0")),
             QStringLiteral("24.1.0"));
}

void TestParseDeviceInfo::parseNpuSmiVersion_embeddedInMultiLineOutput()
{
    const QString output = QStringLiteral(
        "npu-smi info\n"
        "npu-smi version: 24.1.rc1\n"
        "Copyright (c) 2018-2024 Huawei Technologies Co., Ltd\n");

    QCOMPARE(m_provider.parseNpuSmiVersion(output), QStringLiteral("24.1.rc1"));
}

void TestParseDeviceInfo::parseNpuSmiVersion_missingVersionReturnsEmpty()
{
    const QString output = QStringLiteral(
        "npu-smi info\n"
        "No version line here\n");

    QVERIFY(m_provider.parseNpuSmiVersion(output).isEmpty());
}

void TestParseDeviceInfo::parseNpuSmiVersion_withoutSeparatorReturnsEmpty()
{
    // The pattern requires ':' or '=' between "version" and the value.
    QVERIFY(m_provider.parseNpuSmiVersion(QStringLiteral("npu-smi version 24.1.0")).isEmpty());
}

void TestParseDeviceInfo::parseBoardVersions_bothVersionsExtracted()
{
    const QString output = QStringLiteral(
        "Board Info :\n"
        "Software Version : 24.1.rc1\n"
        "Firmware Version : 7.3.0.1.25\n");

    QString software;
    QString firmware;
    m_provider.parseBoardVersions(output, &software, &firmware);
    QCOMPARE(software, QStringLiteral("24.1.rc1"));
    QCOMPARE(firmware, QStringLiteral("7.3.0.1.25"));
}

void TestParseDeviceInfo::parseBoardVersions_firmwareOnly()
{
    const QString output = QStringLiteral("Firmware Version : 7.3.0.1.25\n");

    QString software = QStringLiteral("stale");
    QString firmware;
    m_provider.parseBoardVersions(output, &software, &firmware);
    QVERIFY(software.isEmpty());
    QCOMPARE(firmware, QStringLiteral("7.3.0.1.25"));
}

void TestParseDeviceInfo::parseBoardVersions_emptyOutputClearsValues()
{
    QString software = QStringLiteral("stale");
    QString firmware = QStringLiteral("stale");
    m_provider.parseBoardVersions(QString(), &software, &firmware);
    QVERIFY(software.isEmpty());
    QVERIFY(firmware.isEmpty());
}

void TestParseDeviceInfo::parseBoardVersions_nullSoftwarePointerIgnored()
{
    const QString output = QStringLiteral(
        "Software Version : 24.1.rc1\n"
        "Firmware Version : 7.3.0.1.25\n");

    QString firmware;
    m_provider.parseBoardVersions(output, nullptr, &firmware);
    QCOMPARE(firmware, QStringLiteral("7.3.0.1.25"));
}

void TestParseDeviceInfo::parseBoardVersions_unrelatedKeysIgnored()
{
    const QString output = QStringLiteral(
        "Board Name : Atlas 900\n"
        "Software Version : 1.0.0\n"
        "Some Other : value\n");

    QString software;
    QString firmware;
    m_provider.parseBoardVersions(output, &software, &firmware);
    QCOMPARE(software, QStringLiteral("1.0.0"));
    QVERIFY(firmware.isEmpty());
}

QTEST_GUILESS_MAIN(TestParseDeviceInfo)

#include "test_parse_device_info.moc"
