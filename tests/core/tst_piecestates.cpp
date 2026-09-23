// PieceStates: the pieces bar of docs/12 G3, parsed from `torrents/pieceStates` and cut into
// as many buckets as the bar has pixels.

#include "core/PieceStates.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

namespace PieceStates = omaseed::core::PieceStates;

namespace {

QByteArray states(std::initializer_list<int> values)
{
    QByteArray bytes;
    for (int value : values)
        bytes.append(char(value));
    return bytes;
}

} // namespace

class TestPieceStates : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parses_the_api_array();
    void unknown_values_read_as_missing();
    void fewer_pieces_than_pixels_is_one_cell_each();
    void a_bucket_is_have_only_when_every_piece_is();
    void a_bucket_with_one_downloading_piece_is_downloading();
    void buckets_cover_every_piece_once();
    void nothing_to_draw();
    void counts();
};

void TestPieceStates::parses_the_api_array()
{
    // The shape qBittorrent 5.2 answers: one number per piece.
    const QJsonArray array = QJsonDocument::fromJson("[2,2,1,0,0,2]").array();
    QCOMPARE(PieceStates::fromJson(array), states({2, 2, 1, 0, 0, 2}));
    QCOMPARE(PieceStates::fromJson(QJsonArray()), QByteArray());
}

void TestPieceStates::unknown_values_read_as_missing()
{
    const QJsonArray array = QJsonDocument::fromJson(R"([2,7,-1,"x",null,1])").array();
    QCOMPARE(PieceStates::fromJson(array), states({2, 0, 0, 0, 0, 1}));
}

void TestPieceStates::fewer_pieces_than_pixels_is_one_cell_each()
{
    const QList<int> cells = PieceStates::bucket(states({2, 1, 0}), 300);
    QCOMPARE(cells, (QList<int>{2, 1, 0}));
}

void TestPieceStates::a_bucket_is_have_only_when_every_piece_is()
{
    // Four pieces into two cells: [2,2] is had, [2,0] still lacks one.
    QCOMPARE(PieceStates::bucket(states({2, 2, 2, 0}), 2), (QList<int>{2, 0}));
    QCOMPARE(PieceStates::bucket(states({0, 2, 2, 2}), 2), (QList<int>{0, 2}));
}

void TestPieceStates::a_bucket_with_one_downloading_piece_is_downloading()
{
    QCOMPARE(PieceStates::bucket(states({2, 1, 0, 2, 2, 2}), 2), (QList<int>{1, 2}));
    // Downloading wins over had and missing alike.
    QCOMPARE(PieceStates::bucket(states({0, 0, 1, 2, 2, 2}), 1), (QList<int>{1}));
}

void TestPieceStates::buckets_cover_every_piece_once()
{
    // 50 000 pieces on a 617 px bar: every piece lands in exactly one cell, so a single
    // missing piece anywhere shows, and the cells are all there.
    QByteArray many(50000, char(PieceStates::Have));
    many[49999] = char(PieceStates::Missing);
    many[12345] = char(PieceStates::Downloading);
    const QList<int> cells = PieceStates::bucket(many, 617);
    QCOMPARE(cells.size(), 617);
    QCOMPARE(cells.last(), int(PieceStates::Missing));
    int downloading = 0;
    int missing = 0;
    for (int cell : cells) {
        downloading += cell == PieceStates::Downloading;
        missing += cell == PieceStates::Missing;
    }
    QCOMPARE(downloading, 1);
    QCOMPARE(missing, 1);
    QCOMPARE(cells.at(12345 * 617 / 50000), int(PieceStates::Downloading));
}

void TestPieceStates::nothing_to_draw()
{
    QVERIFY(PieceStates::bucket(QByteArray(), 100).isEmpty());
    QVERIFY(PieceStates::bucket(states({2, 2}), 0).isEmpty());
    QVERIFY(PieceStates::bucket(states({2, 2}), -4).isEmpty());
}

void TestPieceStates::counts()
{
    const QByteArray bytes = states({2, 2, 1, 0, 0, 2});
    QCOMPARE(PieceStates::count(bytes, PieceStates::Have), 3);
    QCOMPARE(PieceStates::count(bytes, PieceStates::Downloading), 1);
    QCOMPARE(PieceStates::count(bytes, PieceStates::Missing), 2);
}

QTEST_GUILESS_MAIN(TestPieceStates)
#include "tst_piecestates.moc"
