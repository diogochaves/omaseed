#include "core/Bencode.hpp"

#include <QRandomGenerator>
#include <QTest>

#include <iterator>

using namespace omaseed::core::bencode;

class TestBencode : public QObject
{
    Q_OBJECT

private slots:
    void decodesIntegers_data();
    void decodesIntegers();
    void decodesStrings();
    void decodesLists();
    void decodesDicts();
    void rejectsLeadingZeroInteger();
    void rejectsNegativeZero();
    void rejectsUnterminatedInteger();
    void rejectsUnterminatedString();
    void rejectsUnterminatedList();
    void rejectsUnterminatedDict();
    void rejectsTrailingGarbage();
    void rejectsExcessiveDepth();
    void rejectsMissingDigits();
    void rejectsLeadingZeroStringLength();
    void encodeSortsDictKeysByByteValue();
    void roundTripsThroughEncodeDecode();
    void recordsTopLevelSpans();
    void rejectsHugeStringLength_data();
    void rejectsHugeStringLength();
    void rejectsDuplicateDictKeys();
    void survivesMutatedInput();
};

void TestBencode::decodesIntegers_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<qint64>("expected");

    QTest::newRow("zero") << QByteArray("i0e") << qint64(0);
    QTest::newRow("positive") << QByteArray("i42e") << qint64(42);
    QTest::newRow("negative") << QByteArray("i-42e") << qint64(-42);
    QTest::newRow("large") << QByteArray("i9223372036854775807e") << qint64(9223372036854775807LL);
}

void TestBencode::decodesIntegers()
{
    QFETCH(QByteArray, input);
    QFETCH(qint64, expected);

    ParseError error;
    const Value v = decode(input, &error);
    QVERIFY(!error.hasError);
    QVERIFY(v.isInteger());
    QCOMPARE(v.toInteger(), expected);
}

void TestBencode::decodesStrings()
{
    ParseError error;
    const Value v = decode(QByteArrayLiteral("4:spam"), &error);
    QVERIFY(!error.hasError);
    QVERIFY(v.isString());
    QCOMPARE(v.toString(), QByteArrayLiteral("spam"));

    const Value empty = decode(QByteArrayLiteral("0:"), &error);
    QVERIFY(!error.hasError);
    QCOMPARE(empty.toString(), QByteArray());
}

void TestBencode::decodesLists()
{
    ParseError error;
    const Value v = decode(QByteArrayLiteral("l4:spami42ee"), &error);
    QVERIFY(!error.hasError);
    QVERIFY(v.isList());
    const List items = v.toList();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).toString(), QByteArrayLiteral("spam"));
    QCOMPARE(items.at(1).toInteger(), qint64(42));
}

void TestBencode::decodesDicts()
{
    ParseError error;
    // d3:bar4:spam3:fooi42ee -> {bar: "spam", foo: 42}
    const Value v = decode(QByteArrayLiteral("d3:bar4:spam3:fooi42ee"), &error);
    QVERIFY(!error.hasError);
    QVERIFY(v.isDict());
    QCOMPARE(v.value("bar").toString(), QByteArrayLiteral("spam"));
    QCOMPARE(v.value("foo").toInteger(), qint64(42));
    QVERIFY(!v.value("missing").isValid());
}

void TestBencode::rejectsLeadingZeroInteger()
{
    ParseError error;
    decode(QByteArrayLiteral("i042e"), &error);
    QVERIFY(error.hasError);
}

void TestBencode::rejectsNegativeZero()
{
    ParseError error;
    decode(QByteArrayLiteral("i-0e"), &error);
    QVERIFY(error.hasError);
}

void TestBencode::rejectsUnterminatedInteger()
{
    ParseError error;
    decode(QByteArrayLiteral("i42"), &error);
    QVERIFY(error.hasError);
}

void TestBencode::rejectsUnterminatedString()
{
    ParseError error;
    decode(QByteArrayLiteral("10:short"), &error);
    QVERIFY(error.hasError);
}

void TestBencode::rejectsUnterminatedList()
{
    ParseError error;
    decode(QByteArrayLiteral("l4:spam"), &error);
    QVERIFY(error.hasError);
}

void TestBencode::rejectsUnterminatedDict()
{
    ParseError error;
    decode(QByteArrayLiteral("d3:foo"), &error);
    QVERIFY(error.hasError);
}

void TestBencode::rejectsTrailingGarbage()
{
    ParseError error;
    decode(QByteArrayLiteral("i1eGARBAGE"), &error);
    QVERIFY(error.hasError);
}

void TestBencode::rejectsExcessiveDepth()
{
    QByteArray deeplyNested;
    for (int i = 0; i < 100; ++i)
        deeplyNested += 'l';
    deeplyNested += "i1e";
    for (int i = 0; i < 100; ++i)
        deeplyNested += 'e';

    ParseError error;
    decode(deeplyNested, &error);
    QVERIFY(error.hasError);
}

void TestBencode::rejectsMissingDigits()
{
    ParseError error;
    decode(QByteArrayLiteral("ie"), &error);
    QVERIFY(error.hasError);
}

void TestBencode::rejectsLeadingZeroStringLength()
{
    ParseError error;
    decode(QByteArrayLiteral("01:x"), &error);
    QVERIFY(error.hasError);
}

void TestBencode::encodeSortsDictKeysByByteValue()
{
    // Built out of order on purpose; encode() must sort by raw byte value.
    Dict d;
    d.append({QByteArray("zebra"), Value(qint64(1))});
    d.append({QByteArray("apple"), Value(qint64(2))});
    d.append({QByteArray("Apple"), Value(qint64(3))}); // uppercase sorts before lowercase

    const QByteArray encoded = encode(Value(d));
    QCOMPARE(encoded, QByteArrayLiteral("d5:Applei3e5:applei2e5:zebrai1ee"));
}

void TestBencode::roundTripsThroughEncodeDecode()
{
    Dict inner;
    inner.append({QByteArray("a"), Value(qint64(1))});
    List list;
    list.append(Value(QByteArray("x")));
    list.append(Value(inner));
    Dict root;
    root.append({QByteArray("list"), Value(list)});
    root.append({QByteArray("n"), Value(qint64(-7))});

    const QByteArray encoded = encode(Value(root));
    ParseError error;
    const Value decoded = decode(encoded, &error);
    QVERIFY(!error.hasError);
    QCOMPARE(encode(decoded), encoded);
}

void TestBencode::recordsTopLevelSpans()
{
    const QByteArray data = QByteArrayLiteral("d4:infod4:name4:spamee");
    SpanMap spans;
    ParseError error;
    const Value root = decode(data, &error, &spans);
    QVERIFY(!error.hasError);
    QVERIFY(spans.contains("info"));

    const auto span = spans.value("info");
    const QByteArray infoBytes = data.mid(span.first, span.second);
    QCOMPARE(infoBytes, QByteArrayLiteral("d4:name4:spame"));

    ParseError infoError;
    const Value infoValue = decode(infoBytes, &infoError);
    QVERIFY(!infoError.hasError);
    QCOMPARE(infoValue.value("name").toString(), QByteArrayLiteral("spam"));
    QCOMPARE(root.value("info").value("name").toString(), QByteArrayLiteral("spam"));
}

void TestBencode::rejectsHugeStringLength_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::newRow("list, max qint64") << QByteArrayLiteral("l9223372036854775807:xe");
    QTest::newRow("dict key, near max") << QByteArrayLiteral("d9223372036854775800:xe");
    QTest::newRow("dict value") << QByteArrayLiteral("d1:a9223372036854775807:xe");
    QTest::newRow("top level") << QByteArrayLiteral("9223372036854775807:x");
    QTest::newRow("past qint64") << QByteArrayLiteral("l99999999999999999999:xe");
    QTest::newRow("one byte short") << QByteArrayLiteral("l3:xye");
}

void TestBencode::rejectsHugeStringLength()
{
    QFETCH(QByteArray, data);
    ParseError error;
    SpanMap spans;
    decode(data, &error, &spans);
    QVERIFY(error.hasError);
}

void TestBencode::rejectsDuplicateDictKeys()
{
    ParseError error;
    SpanMap spans;
    decode(QByteArrayLiteral("d4:infod1:ai1ee4:infod1:ai2eee"), &error, &spans);
    QVERIFY(error.hasError);

    ParseError nestedError;
    decode(QByteArrayLiteral("d1:xd1:ai1e1:ai2eee"), &nestedError);
    QVERIFY(nestedError.hasError);
}

// A cheap fuzz pass: every truncation of a realistic torrent, and a few thousand seeded
// random byte flips, digit-run insertions and length rewrites. Nothing may crash or assert;
// the parser either decodes or reports an error.
void TestBencode::survivesMutatedInput()
{
    const QByteArray seed = QByteArrayLiteral(
        "d8:announce19:http://t.example/an4:infod5:filesld6:lengthi5e4:pathl1:aeed6:lengthi7e4:"
        "pathl1:b1:ceee4:name4:demo12:piece lengthi16384e6:pieces20:01234567890123456789ee");
    ParseError seedError;
    decode(seed, &seedError);
    QVERIFY(!seedError.hasError);

    for (qsizetype n = 0; n <= seed.size(); ++n) {
        ParseError error;
        SpanMap spans;
        decode(seed.left(n), &error, &spans);
    }

    QRandomGenerator rng(0x5eed);
    static const char alphabet[] = "0123456789:ilde-";
    const QByteArray bigDigits[] = {QByteArrayLiteral("9223372036854775807"),
                                    QByteArrayLiteral("9223372036854775808"),
                                    QByteArrayLiteral("18446744073709551615"),
                                    QByteArrayLiteral("4294967296"),
                                    QByteArrayLiteral("2147483647")};
    for (int round = 0; round < 5000; ++round) {
        QByteArray data = seed;
        const int edits = 1 + int(rng.bounded(4));
        for (int e = 0; e < edits && !data.isEmpty(); ++e) {
            const qsizetype at = rng.bounded(int(data.size()));
            switch (rng.bounded(4)) {
            case 0:
                data[at] = alphabet[rng.bounded(int(sizeof(alphabet) - 1))];
                break;
            case 1:
                data.insert(at, bigDigits[rng.bounded(int(std::size(bigDigits)))]);
                break;
            case 2:
                data.remove(at, 1 + rng.bounded(8));
                break;
            default:
                data[at] = char(rng.bounded(256));
                break;
            }
        }
        ParseError error;
        SpanMap spans;
        const Value value = decode(data, &error, &spans);
        if (!error.hasError) {
            // Whatever decodes must also re-encode without trouble.
            (void)encode(value);
        }
    }
}

QTEST_APPLESS_MAIN(TestBencode)

#include "tst_bencode.moc"
