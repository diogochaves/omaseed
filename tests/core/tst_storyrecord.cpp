#include "core/StoryRecord.hpp"

#include <QJsonDocument>
#include <QTest>

#include <algorithm>

using omaseed::core::StoryEntry;
using omaseed::core::StoryRecord;
using What = StoryEntry::What;
using By = StoryEntry::By;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 6), QTime(14, 0), QTimeZone::UTC);
const QString kHash = QStringLiteral("aaaa");

/// A rename of `hash` from `was` to `now`, `minutes` after kNow.
StoryEntry rename(const QString &was,
                  const QString &now,
                  int minutes = 0,
                  What what = What::Torrent,
                  By by = By::Omaseed,
                  const QString &hash = kHash)
{
    StoryEntry r;
    r.at = kNow.addSecs(60LL * minutes);
    r.hash = hash;
    r.what = what;
    r.was = was;
    r.now = now;
    r.by = by;
    return r;
}

/// A filing or place entry of kHash, `minutes` after kNow.
StoryEntry edit(What what, int minutes = 0, By by = By::Omaseed)
{
    StoryEntry e;
    e.at = kNow.addSecs(60LL * minutes);
    e.hash = kHash;
    e.what = what;
    e.by = by;
    return e;
}

StoryEntry category(const QString &was, const QString &now, int minutes = 0, By by = By::Omaseed)
{
    StoryEntry e = edit(What::Category, minutes, by);
    e.was = was;
    e.now = now;
    return e;
}

StoryEntry
tags(const QStringList &tagged, const QStringList &untagged, int minutes = 0, By by = By::Omaseed)
{
    StoryEntry e = edit(What::Tags, minutes, by);
    e.tagged = tagged;
    e.untagged = untagged;
    return e;
}

StoryEntry place(const QString &was, const QString &now, int minutes = 0, By by = By::Elsewhere)
{
    StoryEntry e = edit(What::Place, minutes, by);
    e.was = was;
    e.now = now;
    return e;
}

QJsonArray throughText(const QJsonArray &array)
{
    return QJsonDocument::fromJson(QJsonDocument(array).toJson(QJsonDocument::Compact)).array();
}

} // namespace

class TestStoryRecord : public QObject
{
    Q_OBJECT

private slots:
    void aRenameIsALine()
    {
        StoryRecord history;
        const int id = history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        QVERIFY(id > 0);
        const QList<StoryEntry> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().id, id);
        QCOMPARE(lines.first().was, QStringLiteral("a"));
        QCOMPARE(lines.first().now, QStringLiteral("b"));
        QVERIFY(history.recordsFor(QStringLiteral("other")).isEmpty());
    }

    void noChangeIsNoLine()
    {
        StoryRecord history;
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
        StoryRecord history;
        const int first = history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        StoryEntry second = rename(QStringLiteral("b"), QStringLiteral("c"), 4);
        second.disk = true;
        QCOMPARE(history.record(second), first);
        const QList<StoryEntry> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().was, QStringLiteral("a"));
        QCOMPARE(lines.first().now, QStringLiteral("c"));
        QCOMPARE(lines.first().at, kNow.addSecs(4 * 60));
        QVERIFY(lines.first().disk);
    }

    void filesFoldByTheirOwnName()
    {
        StoryRecord history;
        StoryEntry one = rename(QStringLiteral("x.mkv"), QStringLiteral("y.mkv"), 0, What::File);
        one.in = QStringLiteral("Season 1");
        StoryEntry two = rename(QStringLiteral("y.mkv"), QStringLiteral("z.mkv"), 2, What::File);
        two.in = QStringLiteral("Season 1");
        history.record(one);
        history.record(two);
        QCOMPARE(history.recordsFor(kHash).size(), 1);
        QCOMPARE(history.recordsFor(kHash).first().was, QStringLiteral("x.mkv"));

        // Another file (its old name isn't the last one's new name), or the same name in
        // another folder: its own line.
        StoryEntry other = rename(QStringLiteral("q.mkv"), QStringLiteral("r.mkv"), 3, What::File);
        other.in = QStringLiteral("Season 1");
        history.record(other);
        StoryEntry elsewhere =
            rename(QStringLiteral("r.mkv"), QStringLiteral("s.mkv"), 4, What::File);
        elsewhere.in = QStringLiteral("Season 2");
        history.record(elsewhere);
        QCOMPARE(history.recordsFor(kHash).size(), 3);
    }

    void elevenMinutesApartDontFold()
    {
        StoryRecord history;
        history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        history.record(rename(QStringLiteral("b"), QStringLiteral("c"), 11));
        const QList<StoryEntry> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).now, QStringLiteral("c")); // newest first
        QCOMPARE(lines.at(1).now, QStringLiteral("b"));
    }

    void tenMinutesApartFold()
    {
        StoryRecord history;
        history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        history.record(rename(QStringLiteral("b"), QStringLiteral("c"), 10));
        QCOMPARE(history.recordsFor(kHash).size(), 1);
    }

    // R6: a poll's renames never fold with omaseed's own.
    void omaseedAndElsewhereDontFold()
    {
        StoryRecord history;
        history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        history.record(
            rename(QStringLiteral("b"), QStringLiteral("c"), 1, What::Torrent, By::Elsewhere));
        QCOMPARE(history.recordsFor(kHash).size(), 2);
    }

    void filesRunsDontFold()
    {
        StoryRecord history;
        StoryEntry run =
            rename(QStringLiteral("01.flac"), QStringLiteral("01 Intro.flac"), 0, What::Files);
        run.count = 12;
        history.record(run);
        StoryEntry again =
            rename(QStringLiteral("01 Intro.flac"), QStringLiteral("01.flac"), 1, What::Files);
        again.count = 12;
        history.record(again);
        QCOMPARE(history.recordsFor(kHash).size(), 2);
    }

    // R6: a run that ends on the name it started with leaves no line.
    void aRunBackToTheStartIsNoLine()
    {
        StoryRecord history;
        const int kept =
            history.record(rename(QStringLiteral("old"), QStringLiteral("older"), -30));
        history.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        history.record(rename(QStringLiteral("b"), QStringLiteral("c"), 1));
        QCOMPARE(history.record(rename(QStringLiteral("c"), QStringLiteral("a"), 2)), 0);
        const QList<StoryEntry> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().id, kept);

        StoryRecord alone;
        alone.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        alone.record(rename(QStringLiteral("b"), QStringLiteral("a"), 1));
        QVERIFY(alone.isEmpty());
    }

    // C38 R9: at most 10 per torrent, the oldest goes.
    void tenPerTorrent()
    {
        StoryRecord history;
        for (int i = 0; i < 11; ++i)
            history.record(
                rename(QStringLiteral("n%1").arg(i), QStringLiteral("n%1").arg(i + 1), i * 20));
        history.record(rename(QStringLiteral("x"),
                              QStringLiteral("y"),
                              0,
                              What::Torrent,
                              By::Omaseed,
                              QStringLiteral("bbbb")));
        const QList<StoryEntry> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), StoryRecord::kMaxPerKind);
        QCOMPARE(lines.first().was, QStringLiteral("n10"));
        QCOMPARE(lines.last().was, QStringLiteral("n1")); // n0 went
        QCOMPARE(history.recordsFor(QStringLiteral("bbbb")).size(), 1);
    }

    void pruneDropsTorrentsThatLeft()
    {
        StoryRecord history;
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
        StoryRecord history;
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
        StoryRecord history;
        StoryEntry torrent = rename(QStringLiteral("night.market.rec.2026"),
                                    QStringLiteral("Night Market Field Recordings"),
                                    0,
                                    What::Torrent,
                                    By::Elsewhere);
        torrent.disk = true;
        StoryEntry file = rename(QStringLiteral("a.txt"), QStringLiteral("b.txt"), 1, What::File);
        file.in = QStringLiteral("docs/notes");
        StoryEntry folder =
            rename(QStringLiteral("cd1"), QStringLiteral("Disc 1"), 30, What::Folder);
        StoryEntry files = rename(QStringLiteral("01.flac"),
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
        const StoryRecord back = StoryRecord::fromJson(saved);
        QCOMPARE(back.recordsFor(kHash), history.recordsFor(kHash));
        QCOMPARE(back.recordsFor(QStringLiteral("bbbb")),
                 history.recordsFor(QStringLiteral("bbbb")));

        const StoryEntry run = back.recordsFor(QStringLiteral("bbbb")).first();
        QCOMPARE(run.what, What::Files);
        QCOMPARE(run.count, 12);
        QCOMPARE(run.folders, 3);
        QCOMPARE(run.in, QString());
        const QList<StoryEntry> lines = back.recordsFor(kHash);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.at(1).in, QStringLiteral("docs/notes"));
        QCOMPARE(lines.at(2).by, By::Elsewhere);
        QVERIFY(lines.at(2).disk);
        QCOMPARE(lines.at(2).at, kNow);

        // Ids carry on past the saved ones.
        StoryRecord more = back;
        const int id = more.record(rename(QStringLiteral("p"),
                                          QStringLiteral("q"),
                                          0,
                                          What::Torrent,
                                          By::Omaseed,
                                          QStringLiteral("cccc")));
        for (const StoryEntry &record : lines)
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
        const StoryRecord history = StoryRecord::fromJson(QJsonDocument::fromJson(text).array());
        const QList<StoryEntry> lines = history.recordsFor(QStringLiteral("aaaa"));
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).was, QStringLiteral("c")); // sorted newest first
        QCOMPARE(lines.at(1).count, 1);
        QCOMPARE(lines.at(1).folders, 1);
        // Every id unique, the missing and duplicate ones given fresh.
        const QList<StoryEntry> other = history.recordsFor(QStringLiteral("bbbb"));
        QCOMPARE(other.size(), 1);
        QSet<int> ids{lines.at(0).id, lines.at(1).id, other.first().id};
        QCOMPARE(ids.size(), 3);
        QVERIFY(!ids.contains(0));

        QVERIFY(StoryRecord::fromJson(QJsonArray()).isEmpty());
    }

    void savedHistoryKeepsTenPerTorrent()
    {
        QJsonArray array;
        for (int i = 0; i < 12; ++i)
            array.append(
                rename(QStringLiteral("n%1").arg(i), QStringLiteral("n%1").arg(i + 1), i * 20)
                    .toJson());
        const StoryRecord history = StoryRecord::fromJson(array);
        const QList<StoryEntry> lines = history.recordsFor(kHash);
        QCOMPARE(lines.size(), StoryRecord::kMaxPerKind);
        QCOMPARE(lines.first().was, QStringLiteral("n11"));
        QCOMPARE(lines.last().was, QStringLiteral("n2"));
    }

    // C43 Y10: a file C38 wrote reads as it did, and is written back field for field.
    void aC38FileReadsAsBefore()
    {
        const QByteArray text = R"([
            {"id": 3, "at": "2026-11-06T14:30:00Z", "hash": "aaaa", "what": "folder",
             "was": "cd1", "now": "Disc 1", "count": 1, "in": "", "by": "omaseed",
             "disk": false},
            {"id": 1, "at": "2026-11-06T14:00:00Z", "hash": "aaaa", "what": "torrent",
             "was": "night.market.rec.2026", "now": "Night Market Field Recordings",
             "count": 1, "in": "", "by": "elsewhere", "disk": true},
            {"id": 4, "at": "2026-11-06T14:00:00Z", "hash": "bbbb", "what": "files",
             "was": "01.flac", "now": "01 Intro.flac", "count": 12, "in": "", "by": "omaseed",
             "disk": false, "folders": 3}
        ])";
        const QJsonArray saved = QJsonDocument::fromJson(text).array();
        const StoryRecord record = StoryRecord::fromJson(saved);
        const QList<StoryEntry> lines = record.recordsFor(kHash);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).what, What::Folder);
        QCOMPARE(lines.at(1).by, By::Elsewhere);
        QVERIFY(lines.at(1).disk);
        QCOMPARE(record.recordsFor(QStringLiteral("bbbb")).first().folders, 3);
        QCOMPARE(record.toJson(), saved);
    }

    void everyKindRoundTrips()
    {
        StoryRecord record;
        StoryEntry filed = category(QStringLiteral("inbox"), QString(), 0, By::Elsewhere);
        filed.removed = true;
        StoryEntry tagged = tags({QStringLiteral("hd")}, {QStringLiteral("old")}, 1);
        StoryEntry gone = tags({}, {QStringLiteral("old")}, 2, By::Elsewhere);
        gone.removed = true;
        StoryEntry waiting = place(QStringLiteral("/a"), QStringLiteral("/b"), 3, By::Server);
        waiting.landed = false;
        waiting.cause = QStringLiteral("label");
        waiting.causeName = QStringLiteral("music");
        StoryEntry managed = place(QStringLiteral("/b"), QStringLiteral("/c"), 4, By::Server);
        managed.cause = QStringLiteral("category");
        StoryEntry outside = place(QStringLiteral("/c"), QStringLiteral("/d"), 5);
        for (const StoryEntry &entry : {filed, tagged, gone, waiting, managed, outside})
            QVERIFY(record.record(entry) > 0);
        record.record(rename(QStringLiteral("a"), QStringLiteral("b"), 6));

        const QJsonArray json = record.toJson();
        QCOMPARE(json.size(), 7);
        // Design 2: only what each kind needs, the defaults left out.
        const QJsonObject place5 = json.at(1).toObject();
        QCOMPARE(place5.value(QStringLiteral("what")).toString(), QStringLiteral("place"));
        QVERIFY(!place5.contains(QStringLiteral("landed")));
        QVERIFY(!place5.contains(QStringLiteral("cause")));
        QVERIFY(!place5.contains(QStringLiteral("count")));
        const QJsonObject waiting3 = json.at(3).toObject();
        QCOMPARE(waiting3.value(QStringLiteral("by")).toString(), QStringLiteral("server"));
        QCOMPARE(waiting3.value(QStringLiteral("landed")), QJsonValue(false));
        QCOMPARE(waiting3.value(QStringLiteral("cause")).toString(), QStringLiteral("label"));
        QCOMPARE(waiting3.value(QStringLiteral("causeName")).toString(), QStringLiteral("music"));
        const QJsonObject tags1 = json.at(5).toObject();
        QCOMPARE(tags1.value(QStringLiteral("what")).toString(), QStringLiteral("tags"));
        QVERIFY(!tags1.contains(QStringLiteral("was")));
        QVERIFY(!tags1.contains(QStringLiteral("removed")));
        QCOMPARE(tags1.value(QStringLiteral("tagged")).toArray(), QJsonArray{QStringLiteral("hd")});
        const QJsonObject category0 = json.at(6).toObject();
        QCOMPARE(category0.value(QStringLiteral("removed")), QJsonValue(true));
        QCOMPARE(category0.value(QStringLiteral("now")).toString(), QString());

        const StoryRecord back = StoryRecord::fromJson(throughText(json));
        QCOMPARE(back.recordsFor(kHash), record.recordsFor(kHash));
        QCOMPARE(back.toJson(), json);
    }

    // An older omaseed skips this one's new kinds the same way (C43 Y10).
    void anUnknownKindIsSkipped()
    {
        const QByteArray text = R"([
            {"hash": "aaaa", "at": "2026-11-06T14:00:00Z", "what": "label", "was": "a",
             "now": "b", "by": "omaseed"},
            {"hash": "aaaa", "at": "2026-11-06T14:00:00Z", "what": "place", "was": "/a",
             "now": "/b", "by": "robot"},
            {"hash": "aaaa", "at": "2026-11-06T14:00:00Z", "what": "tags", "tagged": [],
             "untagged": [], "by": "omaseed"},
            {"hash": "aaaa", "at": "2026-11-06T14:00:00Z", "what": "place", "was": "/a",
             "now": "/a", "by": "elsewhere"},
            {"hash": "aaaa", "at": "2026-11-06T14:01:00Z", "what": "category", "was": "",
             "now": "music", "by": "elsewhere"},
            {"hash": "aaaa", "at": "2026-11-06T14:02:00Z", "what": "tags", "tagged": ["hd", 4],
             "by": "server"}
        ])";
        const StoryRecord record = StoryRecord::fromJson(QJsonDocument::fromJson(text).array());
        const QList<StoryEntry> lines = record.recordsFor(kHash);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).what, What::Tags);
        QCOMPARE(lines.at(0).tagged, QStringList{QStringLiteral("hd")});
        QCOMPARE(lines.at(1).what, What::Category);
        QVERIFY(lines.at(1).landed);
        QVERIFY(!lines.at(1).removed);
    }

    // C43 Y9: the category changed again within 10 minutes is one line, from its first to its
    // last.
    void categoryEditsFoldWithinTenMinutes()
    {
        StoryRecord record;
        const int first = record.record(category(QString(), QStringLiteral("inbox")));
        QCOMPARE(record.record(category(QStringLiteral("inbox"), QStringLiteral("music"), 5)),
                 first);
        QList<StoryEntry> lines = record.recordsFor(kHash);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().was, QString());
        QCOMPARE(lines.first().now, QStringLiteral("music"));
        QCOMPARE(lines.first().at, kNow.addSecs(5 * 60));

        // Eleven minutes on, or not from where the last one left it: a line of its own.
        record.record(category(QStringLiteral("music"), QStringLiteral("films"), 16));
        record.record(category(QStringLiteral("other"), QStringLiteral("films2"), 17));
        lines = record.recordsFor(kHash);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.at(0).was, QStringLiteral("other"));
        QCOMPARE(lines.at(1).was, QStringLiteral("music"));
    }

    void aCategoryRunBackToItsStartLeavesNone()
    {
        StoryRecord record;
        const int kept = record.record(rename(QStringLiteral("a"), QStringLiteral("b"), -30));
        record.record(category(QStringLiteral("inbox"), QStringLiteral("music")));
        record.record(category(QStringLiteral("music"), QStringLiteral("films"), 1));
        QCOMPARE(record.record(category(QStringLiteral("films"), QStringLiteral("inbox"), 2)), 0);
        const QList<StoryEntry> lines = record.recordsFor(kHash);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().id, kept);

        StoryRecord alone;
        alone.record(category(QString(), QStringLiteral("music")));
        QCOMPARE(alone.record(category(QStringLiteral("music"), QString(), 1)), 0);
        QVERIFY(alone.isEmpty());
    }

    // C43 Y9: tags edited again within 10 minutes are their net change.
    void tagEditsFoldToTheirNetChange()
    {
        StoryRecord record;
        const int first = record.record(tags({QStringLiteral("x"), QStringLiteral("y")}, {}));
        QCOMPARE(record.record(tags({QStringLiteral("z")}, {QStringLiteral("y")}, 3)), first);
        QList<StoryEntry> lines = record.recordsFor(kHash);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().tagged, (QStringList{QStringLiteral("x"), QStringLiteral("z")}));
        QCOMPARE(lines.first().untagged, QStringList());
        QCOMPARE(lines.first().at, kNow.addSecs(3 * 60));

        StoryRecord other;
        other.record(tags({}, {QStringLiteral("a")}));
        other.record(tags({QStringLiteral("b"), QStringLiteral("a")}, {QStringLiteral("c")}, 1));
        lines = other.recordsFor(kHash);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().tagged, QStringList{QStringLiteral("b")});
        QCOMPARE(lines.first().untagged, QStringList{QStringLiteral("c")});
    }

    void taggedAndUntaggedAgainLeavesNone()
    {
        StoryRecord record;
        record.record(tags({QStringLiteral("hd")}, {}));
        QCOMPARE(record.record(tags({}, {QStringLiteral("hd")}, 1)), 0);
        QVERIFY(record.isEmpty());
        QCOMPARE(record.record(tags({}, {})), 0);
        QVERIFY(record.isEmpty());
    }

    // C38 R6, C43 Y9: a poll's edits never fold with omaseed's own.
    void seenNeverFoldsWithOmaseeds()
    {
        StoryRecord record;
        record.record(category(QString(), QStringLiteral("inbox")));
        record.record(category(QStringLiteral("inbox"), QStringLiteral("music"), 1, By::Elsewhere));
        record.record(tags({QStringLiteral("x")}, {}, 2));
        record.record(tags({QStringLiteral("y")}, {}, 3, By::Elsewhere));
        QCOMPARE(record.recordsFor(kHash).size(), 4);
    }

    // C43 Y9: a fold is into the torrent's newest entry of any kind.
    void aLineBetweenStopsTheFold()
    {
        StoryRecord record;
        record.record(rename(QStringLiteral("a"), QStringLiteral("b")));
        record.record(tags({QStringLiteral("x")}, {}, 1));
        record.record(rename(QStringLiteral("b"), QStringLiteral("c"), 2));
        record.record(category(QString(), QStringLiteral("inbox"), 3));
        record.record(tags({QStringLiteral("y")}, {}, 4));
        QCOMPARE(record.recordsFor(kHash).size(), 5);
    }

    void placesNeverFold()
    {
        StoryRecord record;
        record.record(place(QStringLiteral("/a"), QStringLiteral("/b")));
        record.record(place(QStringLiteral("/b"), QStringLiteral("/c"), 1));
        StoryEntry server = place(QStringLiteral("/c"), QStringLiteral("/d"), 2, By::Server);
        server.cause = QStringLiteral("category");
        record.record(server);
        server.was = QStringLiteral("/d");
        server.now = QStringLiteral("/e");
        server.at = kNow.addSecs(3 * 60);
        record.record(server);
        QCOMPARE(record.recordsFor(kHash).size(), 4);
        // Back where it was is still a line: places never fold away.
        QVERIFY(record.record(place(QStringLiteral("/e"), QStringLiteral("/a"), 4)) > 0);
        QCOMPARE(record.recordsFor(kHash).size(), 5);
    }

    void aRemovedEntryNeverFolds()
    {
        StoryRecord record;
        StoryEntry removed = category(QStringLiteral("inbox"), QString(), 0, By::Elsewhere);
        removed.removed = true;
        record.record(removed);
        record.record(category(QString(), QStringLiteral("music"), 1, By::Elsewhere));
        QCOMPARE(record.recordsFor(kHash).size(), 2);

        StoryRecord other;
        other.record(tags({QStringLiteral("x")}, {}, 0, By::Elsewhere));
        StoryEntry gone = tags({}, {QStringLiteral("x")}, 1, By::Elsewhere);
        gone.removed = true;
        other.record(gone);
        QCOMPARE(other.recordsFor(kHash).size(), 2);
    }

    // C43 Y10: the last 10 of each group, so eleven tag edits leave the rename.
    void capsArePerKind()
    {
        StoryRecord record;
        record.record(rename(QStringLiteral("a"), QStringLiteral("b"), -60));
        for (int i = 0; i < 11; ++i)
            record.record(tags({QStringLiteral("t%1").arg(i)}, {}, i * 20));
        for (int i = 0; i < 11; ++i)
            record.record(
                place(QStringLiteral("/p%1").arg(i), QStringLiteral("/p%1").arg(i + 1), i));
        const QList<StoryEntry> lines = record.recordsFor(kHash);
        QCOMPARE(lines.size(), 1 + 2 * StoryRecord::kMaxPerKind);
        QCOMPARE(lines.last().what, What::Torrent);
        const auto count = [&lines](What what) {
            return std::count_if(lines.cbegin(), lines.cend(), [what](const StoryEntry &e) {
                return e.what == what;
            });
        };
        QCOMPARE(count(What::Tags), StoryRecord::kMaxPerKind);
        QCOMPARE(count(What::Place), StoryRecord::kMaxPerKind);
        // The oldest of each went: t0 and /p0.
        for (const StoryEntry &entry : lines) {
            QVERIFY(entry.tagged != QStringList{QStringLiteral("t0")});
            QVERIFY(entry.was != QStringLiteral("/p0"));
        }
    }

    void fromJsonKeepsTenPerKind()
    {
        QJsonArray array;
        for (int i = 0; i < 12; ++i) {
            array.append(
                rename(QStringLiteral("n%1").arg(i), QStringLiteral("n%1").arg(i + 1), i * 20)
                    .toJson());
            array.append(tags({QStringLiteral("t%1").arg(i)}, {}, i * 20 + 1).toJson());
            array.append(
                category(QStringLiteral("c%1").arg(i), QStringLiteral("d%1").arg(i), i * 20 + 2)
                    .toJson());
        }
        array.append(place(QStringLiteral("/a"), QStringLiteral("/b"), -100).toJson());
        const QList<StoryEntry> lines = StoryRecord::fromJson(array).recordsFor(kHash);
        // Names 10, filing (tags and category together) 10, places 1.
        QCOMPARE(lines.size(), 2 * StoryRecord::kMaxPerKind + 1);
        QCOMPARE(lines.last().what, What::Place);
        int names = 0;
        for (const StoryEntry &entry : lines)
            names += entry.what == What::Torrent;
        QCOMPARE(names, StoryRecord::kMaxPerKind);
    }

    void tagsKeepTheTagListsOrder()
    {
        StoryRecord record;
        record.record(tags({QStringLiteral("b10"), QStringLiteral("B2"), QStringLiteral("a")},
                           {QStringLiteral("z"), QStringLiteral("Y")}));
        StoryEntry line = record.recordsFor(kHash).first();
        QCOMPARE(line.tagged,
                 (QStringList{QStringLiteral("a"), QStringLiteral("B2"), QStringLiteral("b10")}));
        QCOMPARE(line.untagged, (QStringList{QStringLiteral("Y"), QStringLiteral("z")}));
        // Folded, and read back.
        record.record(tags({QStringLiteral("b1"), QStringLiteral("A0")}, {}, 1));
        line = record.recordsFor(kHash).first();
        QCOMPARE(line.tagged,
                 (QStringList{QStringLiteral("a"),
                              QStringLiteral("A0"),
                              QStringLiteral("b1"),
                              QStringLiteral("B2"),
                              QStringLiteral("b10")}));
        const QByteArray text = R"([{"hash": "aaaa", "at": "2026-11-06T14:00:00Z",
            "what": "tags", "tagged": ["b10", "B2", "b10"], "untagged": [], "by": "omaseed"}])";
        QCOMPARE(StoryRecord::fromJson(QJsonDocument::fromJson(text).array())
                     .recordsFor(kHash)
                     .first()
                     .tagged,
                 (QStringList{QStringLiteral("B2"), QStringLiteral("b10")}));
    }
};

QTEST_GUILESS_MAIN(TestStoryRecord)

#include "tst_storyrecord.moc"
