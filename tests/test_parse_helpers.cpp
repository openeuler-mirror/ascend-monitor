// Unit tests for the pure helper functions of NpuInfoProvider and
// NpuScanResult:
//   - cleanValue
//   - parseNumericValue
//   - extractCardIds
//   - NpuScanResult::hasData
//
// cleanValue / parseNumericValue / extractCardIds are private const member
// functions. This test-only translation unit exposes them with the classic
// `#define private public` include trick so that production headers stay
// untouched.

#define private public
#include "NpuInfoProvider.h"
#undef private

#include <QtTest>

class TestParseHelpers : public QObject {
    Q_OBJECT

private slots:
    void cleanValue_trimsWhitespace();
    void cleanValue_stripsDoubleQuotes();
    void cleanValue_stripsSingleQuotes();
    void cleanValue_singleCharQuotedReturnsEmpty();
    void cleanValue_mixedQuotesNotStripped();
    void cleanValue_emptyAndPlainValues();
    void parseNumericValue_plainInteger();
    void parseNumericValue_plainDouble();
    void parseNumericValue_negativeAndSigned();
    void parseNumericValue_leadingDotDecimal();
    void parseNumericValue_embeddedInUnitText();
    void parseNumericValue_embeddedInLongerText();
    void parseNumericValue_noNumberFails();
    void parseNumericValue_emptyFails();
    void parseNumericValue_nullOkPointer();
    void extractCardIds_emptyInputsReturnEmpty();
    void extractCardIds_onlyCards();
    void extractCardIds_onlyMappings();
    void extractCardIds_duplicatesRemovedCardsFirst();
    void extractCardIds_numericAscendingOrder();
    void extractCardIds_emptyIdsSkipped();
    void hasData_emptyResultIsFalse();
    void hasData_cardsOnlyIsTrue();
    void hasData_mappingsOnlyIsTrue();

private:
    NpuInfoProvider m_provider;
};

void TestParseHelpers::cleanValue_trimsWhitespace()
{
    QCOMPARE(m_provider.cleanValue(QStringLiteral("  value  ")), QStringLiteral("value"));
    QCOMPARE(m_provider.cleanValue(QStringLiteral("\t value\n")), QStringLiteral("value"));
}

void TestParseHelpers::cleanValue_stripsDoubleQuotes()
{
    QCOMPARE(m_provider.cleanValue(QStringLiteral("\"value\"")), QStringLiteral("value"));
    QCOMPARE(m_provider.cleanValue(QStringLiteral("  \"value\"  ")), QStringLiteral("value"));
}

void TestParseHelpers::cleanValue_stripsSingleQuotes()
{
    QCOMPARE(m_provider.cleanValue(QStringLiteral("'value'")), QStringLiteral("value"));
}

void TestParseHelpers::cleanValue_singleCharQuotedReturnsEmpty()
{
    // A lone quote character "starts and ends with the quote" and the strip
    // removes both characters.
    QCOMPARE(m_provider.cleanValue(QStringLiteral("\"")), QString());
    QCOMPARE(m_provider.cleanValue(QStringLiteral("'")), QString());
}

void TestParseHelpers::cleanValue_mixedQuotesNotStripped()
{
    QCOMPARE(m_provider.cleanValue(QStringLiteral("'value\"")), QStringLiteral("'value\""));
    QCOMPARE(m_provider.cleanValue(QStringLiteral("\"value'")), QStringLiteral("\"value'"));
}

void TestParseHelpers::cleanValue_emptyAndPlainValues()
{
    QCOMPARE(m_provider.cleanValue(QString()), QString());
    QCOMPARE(m_provider.cleanValue(QStringLiteral("12345")), QStringLiteral("12345"));
}

void TestParseHelpers::parseNumericValue_plainInteger()
{
    bool ok = false;
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral("42"), &ok), 42.0);
    QVERIFY(ok);
}

void TestParseHelpers::parseNumericValue_plainDouble()
{
    bool ok = false;
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral("3.5"), &ok), 3.5);
    QVERIFY(ok);
}

void TestParseHelpers::parseNumericValue_negativeAndSigned()
{
    bool ok = false;
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral("-12"), &ok), -12.0);
    QVERIFY(ok);
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral("+7.5"), &ok), 7.5);
    QVERIFY(ok);
}

void TestParseHelpers::parseNumericValue_leadingDotDecimal()
{
    bool ok = false;
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral(".5"), &ok), 0.5);
    QVERIFY(ok);
}

void TestParseHelpers::parseNumericValue_embeddedInUnitText()
{
    bool ok = false;
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral("65536 MB"), &ok), 65536.0);
    QVERIFY(ok);
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral("45.5C"), &ok), 45.5);
    QVERIFY(ok);
}

void TestParseHelpers::parseNumericValue_embeddedInLongerText()
{
    // The regex scans the whole string and returns the first numeric match.
    bool ok = false;
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral("abc12.7def"), &ok), 12.7);
    QVERIFY(ok);
}

void TestParseHelpers::parseNumericValue_noNumberFails()
{
    bool ok = true;
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral("N/A"), &ok), 0.0);
    QVERIFY(!ok);
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral("-"), &ok), 0.0);
    QVERIFY(!ok);
}

void TestParseHelpers::parseNumericValue_emptyFails()
{
    bool ok = true;
    QCOMPARE(m_provider.parseNumericValue(QString(), &ok), 0.0);
    QVERIFY(!ok);
}

void TestParseHelpers::parseNumericValue_nullOkPointer()
{
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral("8"), nullptr), 8.0);
    QCOMPARE(m_provider.parseNumericValue(QStringLiteral("N/A"), nullptr), 0.0);
}

void TestParseHelpers::extractCardIds_emptyInputsReturnEmpty()
{
    QVERIFY(m_provider.extractCardIds(QVector<NpuCard>(), QVector<NpuMappingEntry>()).isEmpty());
}

void TestParseHelpers::extractCardIds_onlyCards()
{
    QVector<NpuCard> cards;
    NpuCard card0;
    card0.cardId = QStringLiteral("0");
    cards.append(card0);
    NpuCard card2;
    card2.cardId = QStringLiteral("2");
    cards.append(card2);

    const QStringList ids = m_provider.extractCardIds(cards, QVector<NpuMappingEntry>());
    QCOMPARE(ids.size(), 2);
    QCOMPARE(ids.at(0), QStringLiteral("0"));
    QCOMPARE(ids.at(1), QStringLiteral("2"));
}

void TestParseHelpers::extractCardIds_onlyMappings()
{
    QVector<NpuMappingEntry> mappings;
    NpuMappingEntry entry;
    entry.cardId = QStringLiteral("1");
    mappings.append(entry);

    const QStringList ids = m_provider.extractCardIds(QVector<NpuCard>(), mappings);
    QCOMPARE(ids.size(), 1);
    QCOMPARE(ids.at(0), QStringLiteral("1"));
}

void TestParseHelpers::extractCardIds_duplicatesRemovedCardsFirst()
{
    QVector<NpuCard> cards;
    NpuCard card1;
    card1.cardId = QStringLiteral("1");
    cards.append(card1);

    QVector<NpuMappingEntry> mappings;
    NpuMappingEntry mapping1;
    mapping1.cardId = QStringLiteral("1");
    mappings.append(mapping1);
    NpuMappingEntry mapping3;
    mapping3.cardId = QStringLiteral("3");
    mappings.append(mapping3);

    const QStringList ids = m_provider.extractCardIds(cards, mappings);
    QCOMPARE(ids.size(), 2);
    QCOMPARE(ids.at(0), QStringLiteral("1"));
    QCOMPARE(ids.at(1), QStringLiteral("3"));
}

void TestParseHelpers::extractCardIds_numericAscendingOrder()
{
    QVector<NpuCard> cards;
    NpuCard card2;
    card2.cardId = QStringLiteral("2");
    cards.append(card2);
    NpuCard card0;
    card0.cardId = QStringLiteral("0");
    cards.append(card0);

    QVector<NpuMappingEntry> mappings;
    NpuMappingEntry mapping1;
    mapping1.cardId = QStringLiteral("1");
    mappings.append(mapping1);

    const QStringList ids = m_provider.extractCardIds(cards, mappings);
    QCOMPARE(ids.size(), 3);
    QCOMPARE(ids.at(0), QStringLiteral("0"));
    QCOMPARE(ids.at(1), QStringLiteral("1"));
    QCOMPARE(ids.at(2), QStringLiteral("2"));
}

void TestParseHelpers::extractCardIds_emptyIdsSkipped()
{
    QVector<NpuCard> cards;
    NpuCard emptyCard;
    cards.append(emptyCard);
    NpuCard card5;
    card5.cardId = QStringLiteral("5");
    cards.append(card5);

    QVector<NpuMappingEntry> mappings;
    NpuMappingEntry emptyMapping;
    mappings.append(emptyMapping);

    const QStringList ids = m_provider.extractCardIds(cards, mappings);
    QCOMPARE(ids.size(), 1);
    QCOMPARE(ids.at(0), QStringLiteral("5"));
}

void TestParseHelpers::hasData_emptyResultIsFalse()
{
    const NpuScanResult result;
    QVERIFY(!result.hasData());
}

void TestParseHelpers::hasData_cardsOnlyIsTrue()
{
    NpuScanResult result;
    NpuCard card;
    card.cardId = QStringLiteral("0");
    result.cards.append(card);
    QVERIFY(result.hasData());
}

void TestParseHelpers::hasData_mappingsOnlyIsTrue()
{
    NpuScanResult result;
    NpuMappingEntry entry;
    entry.cardId = QStringLiteral("0");
    entry.chipId = QStringLiteral("0");
    result.mappings.append(entry);
    QVERIFY(result.hasData());
}

QTEST_GUILESS_MAIN(TestParseHelpers)

#include "test_parse_helpers.moc"
