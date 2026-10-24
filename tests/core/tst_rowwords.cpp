// RowWords: what each row of the rows display says (board 46 `rowHtml()`, DECISIONS C24 H3,
// law 3), a case per question and, inside arriving, per state; and a cover's monogram.

#include "core/RowWords.hpp"
#include "core/TorrentFilterModel.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QTest>

using namespace omaseed::core;

namespace {

constexpr qint64 kKiB = 1024;
constexpr qint64 kMiB = 1024 * 1024;

Torrent make(const QJsonObject &json)
{
    Torrent t;
    t.hash = QStringLiteral("aa");
    t.applyJson(json);
    return t;
}

} // namespace

class TestRowWords : public QObject
{
    Q_OBJECT

private slots:
    void theMetaIsCategoryAndSize();
    void aDownloadSaysTimeToReady();
    void theOtherArrivingStatesSayWhatTheyWaitFor();
    void landedTodaySaysWhen();
    void givingBackSaysTheRatioAndWhoItSendsTo();
    void restingSaysStopped();
    void needsYouSaysTheCauseAndItsFix();
    void aFailureLeadsWithItsFix();
    void monogramsTakeTwoLetters_data();
    void monogramsTakeTwoLetters();
};

void TestRowWords::theMetaIsCategoryAndSize()
{
    const Torrent film = make(
        {{"state", "uploading"}, {"category", "films"}, {"size", 716 * kMiB}, {"progress", 1.0}});
    QCOMPARE(RowWords::of(film, TorrentFilterModel::GivingBack, {}).meta,
             QStringLiteral("films · 716 MB"));
    const Torrent loose = make({{"state", "uploading"}, {"size", 15 * kKiB}, {"progress", 1.0}});
    QCOMPARE(RowWords::of(loose, TorrentFilterModel::GivingBack, {}).meta, QStringLiteral("15 KB"));
    // Before the metadata there is no size to say.
    const Torrent magnet = make({{"state", "metaDL"}, {"total_size", -1}, {"size", 0}});
    QCOMPARE(RowWords::of(magnet, TorrentFilterModel::Arriving, {}).meta, QStringLiteral("magnet"));
}

void TestRowWords::aDownloadSaysTimeToReady()
{
    const Torrent t = make({{"state", "downloading"},
                            {"size", 480 * kMiB},
                            {"progress", 0.489},
                            {"eta", 180},
                            {"dlspeed", 1434 * kKiB},
                            {"num_seeds", 3},
                            {"num_leechs", 2}});
    const RowWords words = RowWords::of(t, TorrentFilterModel::Arriving, {});
    QCOMPARE(words.fact, QStringLiteral("3 min"));
    QCOMPARE(words.caption, QStringLiteral("to ready"));
    QCOMPARE(words.sentence, QStringLiteral("48.9 % of 480 MB · from 3 seeds at 1.40 MB/s"));
    QVERIFY(words.fix.isEmpty());

    // Nothing coming in this second: who it could come from.
    const Torrent slow = make({{"state", "downloading"},
                               {"size", 37 * kMiB},
                               {"progress", 0.12},
                               {"eta", 1140},
                               {"num_seeds", 1},
                               {"num_leechs", 2}});
    const RowWords quiet = RowWords::of(slow, TorrentFilterModel::Arriving, {});
    QCOMPARE(quiet.fact, QStringLiteral("19 min"));
    QCOMPARE(quiet.sentence, QStringLiteral("12.0 % of 37.0 MB · from 1 seed and 2 peers"));
}

void TestRowWords::theOtherArrivingStatesSayWhatTheyWaitFor()
{
    RowWords words =
        RowWords::of(make({{"state", "stalledDL"}, {"size", 103 * kMiB}, {"progress", 0.23}}),
                     TorrentFilterModel::Arriving,
                     {});
    QCOMPARE(words.fact, QStringLiteral("waiting"));
    QCOMPARE(words.caption, QStringLiteral("no seed"));
    QCOMPARE(words.sentence, QStringLiteral("no seed online · 23.0 % of 103 MB"));

    words = RowWords::of(
        make({{"state", "checkingDL"}, {"progress", 0.64}}), TorrentFilterModel::Arriving, {});
    QCOMPARE(words.fact, QStringLiteral("checking"));
    QCOMPARE(words.caption, QStringLiteral("64.0 %"));
    QCOMPARE(words.sentence, QStringLiteral("making sure the files on disk are whole"));

    words = RowWords::of(make({{"state", "metaDL"}, {"num_seeds", 0}, {"num_leechs", 2}}),
                         TorrentFilterModel::Arriving,
                         {});
    QCOMPARE(words.fact, QStringLiteral("finding files"));
    QCOMPARE(words.caption, QStringLiteral("magnet"));
    QCOMPARE(words.sentence, QStringLiteral("asking 2 peers for the file list"));

    words = RowWords::of(
        make({{"state", "queuedDL"}, {"priority", 1}}), TorrentFilterModel::Arriving, {});
    QCOMPARE(words.fact, QStringLiteral("next"));
    QCOMPARE(words.caption, QStringLiteral("in line"));
    QCOMPARE(words.sentence, QStringLiteral("starts when a download slot frees"));
    words = RowWords::of(
        make({{"state", "queuedDL"}, {"priority", 4}}), TorrentFilterModel::Arriving, {});
    QCOMPARE(words.fact, QStringLiteral("queued"));
}

void TestRowWords::landedTodaySaysWhen()
{
    const QDateTime at(QDate::currentDate(), QTime(9, 12));
    const Torrent t = make({{"state", "uploading"},
                            {"category", "software"},
                            {"size", 4 * kKiB},
                            {"progress", 1.0},
                            {"ratio", 0.41},
                            {"completion_on", at.toSecsSinceEpoch()}});
    const RowWords words = RowWords::of(t, TorrentFilterModel::LandedToday, {});
    QCOMPARE(words.fact, QStringLiteral("09:12"));
    QCOMPARE(words.caption, QStringLiteral("landed"));
    QCOMPARE(words.sentence, QStringLiteral("in software · 4 KB · given back 0.4× so far"));
}

void TestRowWords::givingBackSaysTheRatioAndWhoItSendsTo()
{
    const Torrent busy = make({{"state", "uploading"},
                               {"progress", 1.0},
                               {"ratio", 2.4},
                               {"upspeed", 12 * kKiB},
                               {"num_leechs", 4},
                               {"uploaded", 15 * kMiB}});
    RowWords words = RowWords::of(busy, TorrentFilterModel::GivingBack, {});
    QCOMPARE(words.fact, QStringLiteral("2.4×"));
    QCOMPARE(words.caption, QStringLiteral("given"));
    QCOMPARE(words.sentence, QStringLiteral("sending 12 KB/s to 4 peers · 15.0 MB given"));

    const Torrent idle = make({{"state", "stalledUP"}, {"progress", 1.0}, {"ratio", 0.8}});
    words = RowWords::of(idle, TorrentFilterModel::GivingBack, {});
    QCOMPARE(words.fact, QStringLiteral("0.8×"));
    QCOMPARE(words.sentence, QStringLiteral("nobody is asking now"));
}

void TestRowWords::restingSaysStopped()
{
    const Torrent done =
        make({{"state", "stoppedUP"}, {"progress", 1.0}, {"ratio", 1.2}, {"size", 12 * kKiB}});
    RowWords words = RowWords::of(done, TorrentFilterModel::Resting, {});
    QCOMPARE(words.fact, QStringLiteral("stopped"));
    QCOMPARE(words.caption, QStringLiteral("1.2×"));
    QCOMPARE(words.sentence, QStringLiteral("not sharing · 12 KB on disk"));

    const Torrent half = make({{"state", "stoppedDL"}, {"progress", 0.5}, {"size", 20 * kMiB}});
    words = RowWords::of(half, TorrentFilterModel::Resting, {});
    QCOMPARE(words.caption, QStringLiteral("50.0 %"));
    QCOMPARE(words.sentence, QStringLiteral("50.0 % of 20.0 MB · waiting for you to start it"));
}

void TestRowWords::needsYouSaysTheCauseAndItsFix()
{
    const Torrent t = make({{"state", "error"}, {"progress", 0.0}});
    RowTrouble trouble;
    trouble.cause = QStringLiteral("no access");
    trouble.headline = QStringLiteral("qBittorrent can't write to its save folder");
    trouble.actions = {QStringLiteral("start"), QStringLiteral("folder")};
    RowWords words = RowWords::of(t, TorrentFilterModel::NeedsYou, trouble);
    QCOMPARE(words.fact, QStringLiteral("no access"));
    QCOMPARE(words.caption, QStringLiteral("needs you"));
    QCOMPARE(words.sentence, QStringLiteral("qBittorrent can't write to its save folder"));
    QCOMPARE(words.fix, QStringLiteral("retry"));
    QCOMPARE(words.fixAction, QStringLiteral("problem:start"));

    // An error the log says nothing about still says so, and can still be retried (C20).
    words = RowWords::of(t, TorrentFilterModel::NeedsYou, {});
    QCOMPARE(words.fact, QStringLiteral("error"));
    QCOMPARE(words.sentence, QStringLiteral("qBittorrent stopped it with an error"));
    QCOMPARE(words.fixAction, QStringLiteral("problem:start"));

    // Missing files say where they were.
    const Torrent gone = make({{"state", "missingFiles"}, {"save_path", "/srv/a"}});
    words = RowWords::of(gone, TorrentFilterModel::NeedsYou, {});
    QCOMPARE(words.fact, QStringLiteral("files missing"));
    QCOMPARE(words.sentence, QStringLiteral("its files are gone from /srv/a"));
    QVERIFY(words.fix.isEmpty());
}

void TestRowWords::aFailureLeadsWithItsFix()
{
    const Torrent t = make({{"state", "uploading"}, {"progress", 1.0}});
    RowTrouble trouble;
    trouble.failureKind = QStringLiteral("move");
    trouble.failureHeadline = QStringLiteral("qBittorrent can't write to /tmp/locked/dest");
    trouble.failureActions = {QStringLiteral("log"), QStringLiteral("location")};
    const RowWords words = RowWords::of(t, TorrentFilterModel::NeedsYou, trouble);
    QCOMPARE(words.fact, QStringLiteral("didn't move"));
    QCOMPARE(words.sentence, QStringLiteral("qBittorrent can't write to /tmp/locked/dest"));
    QCOMPARE(words.fix, QStringLiteral("move…"));
    QCOMPARE(words.fixAction, QStringLiteral("failure:location"));
}

void TestRowWords::monogramsTakeTwoLetters_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("monogram");
    // Board 46's covers.
    QTest::newRow("words") << "Open Movie Night (2026)" << "OM";
    QTest::newRow("dashes") << "photo-archive-2019-2025" << "PA";
    QTest::newRow("a number second") << "distro-2026.09-x86_64.iso" << "DI";
    QTest::newRow("a mark second") << "Ünïcødé ✓ Spaces & Symbols" << "ÜN";
    QTest::newRow("dots") << "tiny-notes.txt" << "TN";
    QTest::newRow("one word") << "omarchy" << "OM";
    QTest::newRow("starts with a number") << "2026 Field Recordings" << "FR";
    QTest::newRow("no letter") << "1234" << "";
}

void TestRowWords::monogramsTakeTwoLetters()
{
    QFETCH(QString, name);
    QFETCH(QString, monogram);
    QCOMPARE(RowWords::monogram(name), monogram);
}

QTEST_GUILESS_MAIN(TestRowWords)
#include "tst_rowwords.moc"
