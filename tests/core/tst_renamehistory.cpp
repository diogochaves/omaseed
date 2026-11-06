#include "core/RenameHistory.hpp"

#include <QJsonDocument>
#include <QTest>

using omaseed::core::RenameHistory;
using omaseed::core::RenameRecord;
using What = RenameRecord::What;
using By = RenameRecord::By;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 6), QTime(14, 0), QTimeZone::UTC);
const QString kHash = QStringLiteral("aaaa");

/// A rename of `hash` from `was` to `now`, `minutes` after kNow.
RenameRecord rename(const QString &was,
                    const QString &now,
                    int minutes = 0,
                    What what = What::Torrent,
                    By by = By::Omaseed,
                    const QString &hash = kHash)
{
    RenameRecord r;
    r.at = kNow.addSecs(60LL * minutes);
    r.hash = hash;
    r.what = what;
    r.was = was;
    r.now = now;
    r.by = by;
    return r;
}

} // namespace

class TestRenameHistory : public QObject
{
    Q_OBJECT

private slots:
    void aRenameIsALine()
    {
        RenameHistory history;
        const int id = history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        QVERIFY(id > 0);
        const QList<RenameRecord> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().id, id);
        QCOMPARE(lines.first().was, QStringLiteral("a"));
        QCOMPARE(lines.first().now, QStringLiteral("b"));
        QVERIFY(history.recordsFor(QStringLiteral("other")).isEmpty());
    }

    void noChangeIsNoLine()
    {
        RenameHistory history;
        QCOMPARE(history.record(rename(QStringLiteral("a"), QStringLiteral("a"))), 0);
        QCOMPARE(history.record(rename(QStringLiteral("a"),
                                       QStringLiteral("b"),
                                       0,
                                       What::Torrent,
                                       By::Omaseed,
                                       QString())),
                 0);
        QVERIFY(history.isEmpty());
    }

    // C38 R6: two in a row fold, the first one's old name, the last one's time.
    void twoInARowFold()
    {
        RenameHistory history;
        const int first = history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        RenameRecord second = rename(QStringLiteral("b"), QStringLiteral("c"), 4);
        second.disk = true;
        QCOMPARE(history.record(second), first);
        const QList<RenameRecord> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().was, QStringLiteral("a"));
        QCOMPARE(lines.first().now, QStringLiteral("c"));
        QCOMPARE(lines.first().at, kNow.addSecs(4 * 60));
        QVERIFY(lines.first().disk);
    }

    void filesFoldByTheirOwnName()
    {
        RenameHistory history;
        RenameRecord one = rename(QStringLiteral("x.mkv"), QStringLiteral("y.mkv"), 0, What::File);
        one.in = QStringLiteral("Season 1");
        RenameRecord two = rename(QStringLiteral("y.mkv"), QStringLiteral("z.mkv"), 2, What::File);
        two.in = QStringLiteral("Season 1");
        history.record(one);
        history.record(two);
        QCOMPARE(history.recordsFor(kHash).size(), 1);
        QCOMPARE(history.recordsFor(kHash).first().was, QStringLiteral("x.mkv"));

        // Another file (its old name isn't the last one's new name), or the same name in
        // another folder: its own line.
        RenameRecord other =
            rename(QStringLiteral("q.mkv"), QStringLiteral("r.mkv"), 3, What::File);
        other.in = QStringLiteral("Season 1");
        history.record(other);
        RenameRecord elsewhere =
            rename(QStringLiteral("r.mkv"), QStringLiteral("s.mkv"), 4, What::File);
        elsewhere.in = QStringLiteral("Season 2");
        history.record(elsewhere);
        QCOMPARE(history.recordsFor(kHash).size(), 3);
    }

    void elevenMinutesApartDontFold()
    {
        RenameHistory history;
        history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        history.record(rename(QStringLiteral("b"), QStringLiteral("c"), 11));
        const QList<RenameRecord> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).now, QStringLiteral("c")); // newest first
        QCOMPARE(lines.at(1).now, QStringLiteral("b"));
    }

    void tenMinutesApartFold()
    {
        RenameHistory history;
        history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        history.record(rename(QStringLiteral("b"), QStringLiteral("c"), 10));
        QCOMPARE(history.recordsFor(kHash).size(), 1);
    }

    // R6: a poll's renames never fold with omaseed's own.
    void omaseedAndElsewhereDontFold()
    {
        RenameHistory history;
        history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        history.record(
            rename(QStringLiteral("b"), QStringLiteral("c"), 1, What::Torrent, By::Elsewhere));
        QCOMPARE(history.recordsFor(kHash).size(), 2);
    }

    void filesRunsDontFold()
    {
        RenameHistory history;
        RenameRecord run =
            rename(QStringLiteral("01.flac"), QStringLiteral("01 Intro.flac"), 0, What::Files);
        run.count = 12;
        history.record(run);
        RenameRecord again =
            rename(QStringLiteral("01 Intro.flac"), QStringLiteral("01.flac"), 1, What::Files);
        again.count = 12;
        history.record(again);
        QCOMPARE(history.recordsFor(kHash).size(), 2);
    }

    // R6: a run that ends on the name it started with leaves no line.
    void aRunBackToTheStartIsNoLine()
    {
        RenameHistory history;
        const int kept =
            history.record(rename(QStringLiteral("old"), QStringLiteral("older"), -30));
        history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        history.record(rename(QStringLiteral("b"), QStringLiteral("c"), 1));
        QCOMPARE(history.record(rename(QStringLiteral("c"), QStringLiteral("a"), 2)), 0);
        const QList<RenameRecord> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().id, kept);

        RenameHistory alone;
        alone.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        alone.record(rename(QStringLiteral("b"), QStringLiteral("a"), 1));
        QVERIFY(alone.isEmpty());
    }

    // C38 R9: at most 10 per torrent, the oldest goes.
    void tenPerTorrent()
    {
        RenameHistory history;
        for (int i = 0; i < 11; ++i)
            history.record(
                rename(QStringLiteral("n%1").arg(i), QStringLiteral("n%1").arg(i + 1), i * 20));
        history.record(rename(QStringLiteral("x"),
                              QStringLiteral("y"),
                              0,
                              What::Torrent,
                              By::Omaseed,
                              QStringLiteral("bbbb")));
        const QList<RenameRecord> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), RenameHistory::kMaxPerTorrent);
        QCOMPARE(lines.first().was, QStringLiteral("n10"));
        QCOMPARE(lines.last().was, QStringLiteral("n1")); // n0 went
        QCOMPARE(history.recordsFor(QStringLiteral("bbbb")).size(), 1);
    }

    void pruneDropsTorrentsThatLeft()
    {
        RenameHistory history;
        history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        history.record(rename(QStringLiteral("x"),
                              QStringLiteral("y"),
                              0,
                              What::Torrent,
                              By::Omaseed,
                              QStringLiteral("bbbb")));
        QVERIFY(!history.prune({kHash, QStringLiteral("bbbb"), QStringLiteral("cccc")}));
        QVERIFY(history.prune({QStringLiteral("bbbb")}));
        QVERIFY(history.recordsFor(kHash).isEmpty());
        QCOMPARE(history.recordsFor(QStringLiteral("bbbb")).size(), 1);
        QVERIFY(history.prune({}));
        QVERIFY(history.isEmpty());
    }

    // R3: a rename answered OK, then logged as failed, takes its line back.
    void removeTakesALineBack()
    {
        RenameHistory history;
        const int first = history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        const int second =
            history.record(rename(QStringLiteral("x.mkv"), QStringLiteral("y.mkv"), 1, What::File));
        QVERIFY(first != second);
        QVERIFY(!history.remove(QStringLiteral("bbbb"), second));
        QVERIFY(!history.remove(kHash, 9999));
        QVERIFY(history.remove(kHash, second));
        QCOMPARE(history.recordsFor(kHash).size(), 1);
        QCOMPARE(history.recordsFor(kHash).first().id, first);
        QVERIFY(history.remove(kHash, first));
        QVERIFY(history.isEmpty());
    }

    void jsonRoundTrip()
    {
        RenameHistory history;
        RenameRecord torrent = rename(QStringLiteral("night.market.rec.2026"),
                                      QStringLiteral("Night Market Field Recordings"),
                                      0,
                                      What::Torrent,
                                      By::Elsewhere);
        torrent.disk = true;
        RenameRecord file = rename(QStringLiteral("a.txt"), QStringLiteral("b.txt"), 1, What::File);
        file.in = QStringLiteral("docs/notes");
        RenameRecord folder =
            rename(QStringLiteral("cd1"), QStringLiteral("Disc 1"), 30, What::Folder);
        RenameRecord files = rename(QStringLiteral("01.flac"),
                                    QStringLiteral("01 Intro.flac"),
                                    0,
                                    What::Files,
                                    By::Omaseed,
                                    QStringLiteral("bbbb"));
        files.count = 12;
        files.folders = 3;
        history.record(torrent);
        history.record(file);
        history.record(folder);
        history.record(files);

        const QJsonArray json = history.toJson();
        QCOMPARE(json.size(), 4);
        const QJsonObject first = json.first().toObject();
        QCOMPARE(first.value(QStringLiteral("hash")).toString(), kHash);
        QCOMPARE(first.value(QStringLiteral("what")).toString(), QStringLiteral("folder"));
        QCOMPARE(first.value(QStringLiteral("at")).toString(),
                 QStringLiteral("2026-11-06T14:30:00Z"));

        // Through text, as it is saved.
        const QJsonArray saved =
            QJsonDocument::fromJson(QJsonDocument(json).toJson(QJsonDocument::Compact)).array();
        const RenameHistory back = RenameHistory::fromJson(saved);
        QCOMPARE(back.recordsFor(kHash), history.recordsFor(kHash));
        QCOMPARE(back.recordsFor(QStringLiteral("bbbb")),
                 history.recordsFor(QStringLiteral("bbbb")));

        const RenameRecord run = back.recordsFor(QStringLiteral("bbbb")).first();
        QCOMPARE(run.what, What::Files);
        QCOMPARE(run.count, 12);
        QCOMPARE(run.folders, 3);
        QCOMPARE(run.in, QString());
        const QList<RenameRecord> lines = back.recordsFor(kHash);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.at(1).in, QStringLiteral("docs/notes"));
        QCOMPARE(lines.at(2).by, By::Elsewhere);
        QVERIFY(lines.at(2).disk);
        QCOMPARE(lines.at(2).at, kNow);

        // Ids carry on past the saved ones.
        RenameHistory more = back;
        const int id = more.record(rename(QStringLiteral("p"),
                                          QStringLiteral("q"),
                                          0,
                                          What::Torrent,
                                          By::Omaseed,
                                          QStringLiteral("cccc")));
        for (const RenameRecord &record : lines)
            QVERIFY(record.id != id);
    }

    void malformedJsonIsSkipped()
    {
        const QByteArray text = R"([
            42,
            "torrent",
            {},
            {"hash": "aaaa", "at": "not a time", "what": "torrent", "was": "a", "now": "b",
             "by": "omaseed"},
            {"hash": "", "at": "2026-11-06T14:00:00Z", "what": "torrent", "was": "a", "now": "b",
             "by": "omaseed"},
            {"hash": "aaaa", "at": "2026-11-06T14:00:00Z", "what": "rename", "was": "a",
             "now": "b", "by": "omaseed"},
            {"hash": "aaaa", "at": "2026-11-06T14:00:00Z", "what": "torrent", "was": "a",
             "now": "b", "by": "someone"},
            {"hash": "aaaa", "at": "2026-11-06T14:00:00Z", "what": "torrent", "was": "a",
             "now": "a", "by": "omaseed"},
            {"hash": "aaaa", "at": "2026-11-06T14:00:00Z", "what": "file", "was": "a",
             "now": "b", "by": "elsewhere", "count": -3, "folders": "x"},
            {"id": 7, "hash": "aaaa", "at": "2026-11-06T15:00:00Z", "what": "torrent",
             "was": "c", "now": "d", "by": "omaseed"},
            {"id": 7, "hash": "bbbb", "at": "2026-11-06T15:00:00Z", "what": "torrent",
             "was": "e", "now": "f", "by": "omaseed"}
        ])";
        const RenameHistory history =
            RenameHistory::fromJson(QJsonDocument::fromJson(text).array());
        const QList<RenameRecord> lines = history.recordsFor(QStringLiteral("aaaa"));
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).was, QStringLiteral("c")); // sorted newest first
        QCOMPARE(lines.at(1).count, 1);
        QCOMPARE(lines.at(1).folders, 1);
        // Every id unique, the missing and duplicate ones given fresh.
        const QList<RenameRecord> other = history.recordsFor(QStringLiteral("bbbb"));
        QCOMPARE(other.size(), 1);
        QSet<int> ids{lines.at(0).id, lines.at(1).id, other.first().id};
        QCOMPARE(ids.size(), 3);
        QVERIFY(!ids.contains(0));

        QVERIFY(RenameHistory::fromJson(QJsonArray()).isEmpty());
    }

    void savedHistoryKeepsTenPerTorrent()
    {
        QJsonArray array;
        for (int i = 0; i < 12; ++i)
            array.append(
                rename(QStringLiteral("n%1").arg(i), QStringLiteral("n%1").arg(i + 1), i * 20)
                    .toJson());
        const RenameHistory history = RenameHistory::fromJson(array);
        const QList<RenameRecord> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), RenameHistory::kMaxPerTorrent);
        QCOMPARE(lines.first().was, QStringLiteral("n11"));
        QCOMPARE(lines.last().was, QStringLiteral("n2"));
    }
};

QTEST_GUILESS_MAIN(TestRenameHistory)

#include "tst_renamehistory.moc"
