// SyncEngine's pending torrent values (docs/DECISIONS.md M12): a torrent option the server has
// been asked to change shows at once, no poll flips it back while the request is out, and
// maindata is authoritative again once a poll sent after the answer has been applied.
//
// Polls are played with applyDemoData(), which counts as a poll sent and applied at once.

#include "QbtFeed.hpp"
#include "core/SyncEngine.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentModel.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace omaseed::core;

namespace {

const QString kHash = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
const QString kSeq = QStringLiteral("seq_dl");

/// A partial poll: `fields` for kHash, or nothing about it when `fields` is empty.
QJsonObject partial(const QJsonObject &fields = {})
{
    QJsonObject data{{QStringLiteral("rid"), 2}};
    if (!fields.isEmpty())
        data.insert(QStringLiteral("torrents"), QJsonObject{{kHash, fields}});
    return data;
}

QJsonObject full(bool seqDl)
{
    return QJsonObject{
        {QStringLiteral("rid"), 1},
        {QStringLiteral("full_update"), true},
        {QStringLiteral("torrents"),
         QJsonObject{{kHash,
                      QJsonObject{{QStringLiteral("name"), QStringLiteral("t")},
                                  {QStringLiteral("state"), QStringLiteral("downloading")},
                                  {kSeq, seqDl}}}}},
    };
}

bool seqDl(const SyncEngine &engine)
{
    return engine.torrents()->torrent(kHash).seqDl;
}

/// What the menu asks for: sequential download `on` for `hash`.
TorrentPatch seq(const QString &hash, bool on)
{
    return TorrentPatch(hash).set(&Torrent::seqDl, TorrentField::SeqDl, on);
}

std::optional<bool> pendingSeq(const SyncEngine &engine)
{
    const std::optional<TorrentPatch> pending =
        engine.pendingTorrentValue(kHash, TorrentField::SeqDl);
    if (!pending)
        return std::nullopt;
    return pending->values.seqDl;
}

} // namespace

class TestSyncEnginePending : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theModelShowsTheChangeAtOnce();
    void aRefusalPutsTheOldValueBack();
    void aPollAnsweredBeforeTheRequestDoesNotFlipItBack();
    void theFirstPollAfterTheAnswerSettlesIt();
    void aServerThatIgnoredTheRequestEndsUpShowingItsValue();
    void aStackedRequestWaitsForBothAnswers();
    void aRemovedTorrentForgetsItsPendingValues();
    void anUnknownTorrentIsNeverAdded();
};

void TestSyncEnginePending::theModelShowsTheChangeAtOnce()
{
    SyncEngine engine;
    qbtfeed::demo(engine, full(false));
    QVERIFY(!seqDl(engine));

    engine.setPendingTorrentValue(seq(kHash, true));
    QVERIFY(seqDl(engine));
    QCOMPARE(pendingSeq(engine), std::optional<bool>(true));
}

void TestSyncEnginePending::aRefusalPutsTheOldValueBack()
{
    SyncEngine engine;
    qbtfeed::demo(engine, full(false));
    engine.setPendingTorrentValue(seq(kHash, true));

    engine.settlePendingTorrentValue(kHash, TorrentField::SeqDl, /*refused=*/true);
    QVERIFY(!seqDl(engine));
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
}

void TestSyncEnginePending::aPollAnsweredBeforeTheRequestDoesNotFlipItBack()
{
    // Sent before the request, answered after it: a full update (every field) carrying the
    // old value, then a partial one that also does.
    SyncEngine engine;
    qbtfeed::demo(engine, full(false));
    engine.setPendingTorrentValue(seq(kHash, true));

    qbtfeed::demo(engine, full(false));
    QVERIFY(seqDl(engine));
    qbtfeed::demo(engine, partial({{kSeq, false}}));
    QVERIFY(seqDl(engine));

    // Still unanswered, so still pending, however many polls go by.
    QCOMPARE(engine.pendingTorrentValueCount(), 1);
}

void TestSyncEnginePending::theFirstPollAfterTheAnswerSettlesIt()
{
    SyncEngine engine;
    qbtfeed::demo(engine, full(false));
    engine.setPendingTorrentValue(seq(kHash, true));
    qbtfeed::demo(engine, partial({{kSeq, true}})); // the server took it early
    engine.settlePendingTorrentValue(kHash, TorrentField::SeqDl, false);
    QCOMPARE(engine.pendingTorrentValueCount(), 1);

    qbtfeed::demo(engine, partial());
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
    QVERIFY(seqDl(engine));

    // Authoritative again: the server turning it off shows.
    qbtfeed::demo(engine, partial({{kSeq, false}}));
    QVERIFY(!seqDl(engine));
}

void TestSyncEnginePending::aServerThatIgnoredTheRequestEndsUpShowingItsValue()
{
    // Accepted but never applied (super seeding on a torrent still downloading, say): the
    // incremental polls never mention the field, so settling puts back what the server held.
    SyncEngine engine;
    qbtfeed::demo(engine, full(false));
    engine.setPendingTorrentValue(seq(kHash, true));
    engine.settlePendingTorrentValue(kHash, TorrentField::SeqDl, false);
    QVERIFY(seqDl(engine));

    qbtfeed::demo(engine, partial());
    QVERIFY(!seqDl(engine));
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
}

void TestSyncEnginePending::aStackedRequestWaitsForBothAnswers()
{
    // On, then off again before the first answer: the model follows the last press, and the
    // first answer alone settles nothing.
    SyncEngine engine;
    qbtfeed::demo(engine, full(false));
    engine.setPendingTorrentValue(seq(kHash, true));
    engine.setPendingTorrentValue(seq(kHash, false));
    QVERIFY(!seqDl(engine));
    QCOMPARE(pendingSeq(engine), std::optional<bool>(false));

    engine.settlePendingTorrentValue(kHash, TorrentField::SeqDl, false);
    qbtfeed::demo(engine, partial({{kSeq, true}})); // between the two requests
    QVERIFY(!seqDl(engine));
    QCOMPARE(engine.pendingTorrentValueCount(), 1);

    engine.settlePendingTorrentValue(kHash, TorrentField::SeqDl, false);
    qbtfeed::demo(engine, partial({{kSeq, false}}));
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
    QVERIFY(!seqDl(engine));
}

void TestSyncEnginePending::aRemovedTorrentForgetsItsPendingValues()
{
    SyncEngine engine;
    qbtfeed::demo(engine, full(false));
    engine.setPendingTorrentValue(seq(kHash, true));

    QJsonObject gone = partial();
    gone.insert(QStringLiteral("torrents_removed"), QJsonArray{kHash});
    qbtfeed::demo(engine, gone);
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
    QCOMPARE(engine.torrents()->indexOfHash(kHash), -1);

    // A late answer for it changes nothing, and adds no row.
    engine.settlePendingTorrentValue(kHash, TorrentField::SeqDl, true);
    QCOMPARE(engine.torrents()->count(), 0);
}

void TestSyncEnginePending::anUnknownTorrentIsNeverAdded()
{
    SyncEngine engine;
    qbtfeed::demo(engine, full(false));
    const QString other = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    engine.setPendingTorrentValue(seq(other, true));
    QCOMPARE(engine.torrents()->count(), 1);
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
}

QTEST_MAIN(TestSyncEnginePending)
#include "tst_syncengine_pending.moc"
