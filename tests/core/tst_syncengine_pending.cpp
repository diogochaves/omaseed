// SyncEngine's pending torrent values (docs/UX-NOTES M12): a torrent option the server has
// been asked to change shows at once, no poll flips it back while the request is out, and
// maindata is authoritative again once a poll sent after the answer has been applied.
//
// Polls are played with applyDemoData(), which counts as a poll sent and applied at once.

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
    engine.applyDemoData(full(false));
    QVERIFY(!seqDl(engine));

    engine.setPendingTorrentValue(kHash, kSeq, true, false);
    QVERIFY(seqDl(engine));
    QCOMPARE(engine.pendingTorrentValue(kHash, kSeq), std::optional<QJsonValue>(true));
}

void TestSyncEnginePending::aRefusalPutsTheOldValueBack()
{
    SyncEngine engine;
    engine.applyDemoData(full(false));
    engine.setPendingTorrentValue(kHash, kSeq, true, false);

    engine.settlePendingTorrentValue(kHash, kSeq, /*refused=*/true);
    QVERIFY(!seqDl(engine));
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
}

void TestSyncEnginePending::aPollAnsweredBeforeTheRequestDoesNotFlipItBack()
{
    // Sent before the request, answered after it: a full update (every field) carrying the
    // old value, then a partial one that also does.
    SyncEngine engine;
    engine.applyDemoData(full(false));
    engine.setPendingTorrentValue(kHash, kSeq, true, false);

    engine.applyDemoData(full(false));
    QVERIFY(seqDl(engine));
    engine.applyDemoData(partial({{kSeq, false}}));
    QVERIFY(seqDl(engine));

    // Still unanswered, so still pending, however many polls go by.
    QCOMPARE(engine.pendingTorrentValueCount(), 1);
}

void TestSyncEnginePending::theFirstPollAfterTheAnswerSettlesIt()
{
    SyncEngine engine;
    engine.applyDemoData(full(false));
    engine.setPendingTorrentValue(kHash, kSeq, true, false);
    engine.applyDemoData(partial({{kSeq, true}})); // the server took it early
    engine.settlePendingTorrentValue(kHash, kSeq, false);
    QCOMPARE(engine.pendingTorrentValueCount(), 1);

    engine.applyDemoData(partial());
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
    QVERIFY(seqDl(engine));

    // Authoritative again: the server turning it off shows.
    engine.applyDemoData(partial({{kSeq, false}}));
    QVERIFY(!seqDl(engine));
}

void TestSyncEnginePending::aServerThatIgnoredTheRequestEndsUpShowingItsValue()
{
    // Accepted but never applied (super seeding on a torrent still downloading, say): the
    // incremental polls never mention the field, so settling puts back what the server held.
    SyncEngine engine;
    engine.applyDemoData(full(false));
    engine.setPendingTorrentValue(kHash, kSeq, true, false);
    engine.settlePendingTorrentValue(kHash, kSeq, false);
    QVERIFY(seqDl(engine));

    engine.applyDemoData(partial());
    QVERIFY(!seqDl(engine));
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
}

void TestSyncEnginePending::aStackedRequestWaitsForBothAnswers()
{
    // On, then off again before the first answer: the model follows the last press, and the
    // first answer alone settles nothing.
    SyncEngine engine;
    engine.applyDemoData(full(false));
    engine.setPendingTorrentValue(kHash, kSeq, true, false);
    engine.setPendingTorrentValue(kHash, kSeq, false, true);
    QVERIFY(!seqDl(engine));
    QCOMPARE(engine.pendingTorrentValue(kHash, kSeq), std::optional<QJsonValue>(false));

    engine.settlePendingTorrentValue(kHash, kSeq, false);
    engine.applyDemoData(partial({{kSeq, true}})); // between the two requests
    QVERIFY(!seqDl(engine));
    QCOMPARE(engine.pendingTorrentValueCount(), 1);

    engine.settlePendingTorrentValue(kHash, kSeq, false);
    engine.applyDemoData(partial({{kSeq, false}}));
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
    QVERIFY(!seqDl(engine));
}

void TestSyncEnginePending::aRemovedTorrentForgetsItsPendingValues()
{
    SyncEngine engine;
    engine.applyDemoData(full(false));
    engine.setPendingTorrentValue(kHash, kSeq, true, false);

    QJsonObject gone = partial();
    gone.insert(QStringLiteral("torrents_removed"), QJsonArray{kHash});
    engine.applyDemoData(gone);
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
    QCOMPARE(engine.torrents()->indexOfHash(kHash), -1);

    // A late answer for it changes nothing, and adds no row.
    engine.settlePendingTorrentValue(kHash, kSeq, true);
    QCOMPARE(engine.torrents()->count(), 0);
}

void TestSyncEnginePending::anUnknownTorrentIsNeverAdded()
{
    SyncEngine engine;
    engine.applyDemoData(full(false));
    const QString other = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    engine.setPendingTorrentValue(other, kSeq, true, false);
    QCOMPARE(engine.torrents()->count(), 1);
    QCOMPARE(engine.pendingTorrentValueCount(), 0);
}

QTEST_MAIN(TestSyncEnginePending)
#include "tst_syncengine_pending.moc"
