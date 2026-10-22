// EngineClient (#82, docs/DECISIONS.md C14 E2 a) without qBittorrent: a fake engine that
// hands out omaseed's own types drives SyncEngine, which proves the sync loop needs nothing
// of qBittorrent's wire; the optional calls settle as "not supported"; and mapReply, the way
// an engine turns its transport's replies into omaseed's, passes errors and aborts through.

#include "core/GlobalState.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "core/engines/EngineClient.hpp"

#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using namespace omaseed::core;

namespace {

/// A reply that settles with `value` on the next turn of the event loop.
template<class T>
Reply<T> *later(QObject *parent, Result<T> value)
{
    auto *reply = new Reply<T>(parent);
    QTimer::singleShot(0, reply, [reply, value]() { reply->complete(value); });
    return reply;
}

#define OMASEED_FAKE(type, signature)                                                              \
    Reply<type> *signature override                                                                \
    {                                                                                              \
        return unsupported<type>(QStringLiteral(#signature));                                      \
    }

/// An engine of the test's own: a probe and a sync it scripts, everything else unsupported.
class FakeEngine : public EngineClient
{
public:
    using EngineClient::EngineClient;

    QList<int> cursors;      ///< every sync(cursor) asked, in order
    QList<SyncDelta> deltas; ///< what the syncs answer, in order; the last repeats

    QUrl baseUrl() const override { return m_url; }
    void setBaseUrl(const QUrl &url) override { m_url = url; }
    void setApiKey(const QString &) override {}
    bool hasApiKey() const override { return false; }
    void setCredentials(const QString &, const QString &) override {}
    void clearAuth() override {}
    void allowLoginAgain() override {}
    void setPinnedCertificate(const QString &) override {}
    void setTimeout(std::chrono::milliseconds) override {}
    int maxRetries() const override { return m_retries; }
    void setMaxRetries(int retries) override { m_retries = retries; }
    void cancelAll() override {}
    QUrl reachableUrl(const QUrl &base) const override { return base; }

    Reply<ServerInfo> *probe() override
    {
        return later<ServerInfo>(this, ServerInfo{QStringLiteral("4.1.3"), QStringLiteral("18")});
    }
    EngineFeatures features(const ServerInfo &) const override
    {
        EngineFeatures f;
        f.categories = true;
        return f;
    }
    Reply<SyncDelta> *sync(int cursor) override
    {
        cursors.append(cursor);
        const SyncDelta delta =
            deltas.isEmpty()
                ? SyncDelta()
                : deltas.at(std::min<qsizetype>(cursors.size() - 1, deltas.size() - 1));
        return later<SyncDelta>(this, delta);
    }

    OMASEED_FAKE(QList<TorrentPatch>, torrents(const QStringList &))
    OMASEED_FAKE(QStringList, stoppedAmong(const QStringList &))
    OMASEED_FAKE(QList<CategoryPatch>, categories())
    OMASEED_FAKE(QStringList, tags())
    OMASEED_FAKE(TorrentProperties, properties(const QString &))
    OMASEED_FAKE(QList<ContentFile>, files(const QString &))
    OMASEED_FAKE(QList<TrackerEntry>, trackers(const QString &))
    OMASEED_FAKE(PeersDelta, peers(const QString &, int))
    OMASEED_FAKE(QStringList, webSeeds(const QString &))
    OMASEED_FAKE(QByteArray, pieceStates(const QString &))
    OMASEED_FAKE(Done, start(const QStringList &))
    OMASEED_FAKE(Done, stop(const QStringList &))
    OMASEED_FAKE(Done, startAll())
    OMASEED_FAKE(Done, stopAll())
    OMASEED_FAKE(Done, setForceStart(const QStringList &, bool))
    OMASEED_FAKE(Done, recheck(const QStringList &))
    OMASEED_FAKE(Done, reannounce(const QStringList &))
    OMASEED_FAKE(Done, rename(const QString &, const QString &))
    OMASEED_FAKE(Done, setLocation(const QStringList &, const QString &))
    OMASEED_FAKE(Done, renameFile(const QString &, const QString &, const QString &))
    OMASEED_FAKE(Done, renameFolder(const QString &, const QString &, const QString &))
    OMASEED_FAKE(Done, setFilePriority(const QString &, const QList<int> &, FilePriority))
    OMASEED_FAKE(Done, queueUp(const QStringList &))
    OMASEED_FAKE(Done, queueDown(const QStringList &))
    OMASEED_FAKE(Done, toggleSequentialDownload(const QStringList &))
    OMASEED_FAKE(Done, toggleFirstLastPiece(const QStringList &))
    OMASEED_FAKE(Done, setSuperSeeding(const QStringList &, bool))
    OMASEED_FAKE(Done, setAutoManagement(const QStringList &, bool))
    OMASEED_FAKE(Done, remove(const QStringList &, bool))
    OMASEED_FAKE(Done, addTrackers(const QString &, const QStringList &))
    OMASEED_FAKE(Done, removeTrackers(const QString &, const QStringList &))
    OMASEED_FAKE(Done, addWebSeeds(const QString &, const QStringList &))
    OMASEED_FAKE(Done, removeWebSeeds(const QString &, const QStringList &))
    OMASEED_FAKE(Done, addPeers(const QStringList &, const QStringList &))
    OMASEED_FAKE(Done, banPeers(const QStringList &))
    OMASEED_FAKE(Done, setCategory(const QStringList &, const QString &))
    OMASEED_FAKE(Done, createCategory(const QString &, const QString &))
    OMASEED_FAKE(Done, removeCategories(const QStringList &))
    OMASEED_FAKE(Done, addTags(const QStringList &, const QStringList &))
    OMASEED_FAKE(Done, removeTags(const QStringList &, const QStringList &))
    OMASEED_FAKE(Done, createTags(const QStringList &))
    OMASEED_FAKE(Done, deleteTags(const QStringList &))
    OMASEED_FAKE(Done, setDownloadLimit(const QStringList &, qint64))
    OMASEED_FAKE(Done, setUploadLimit(const QStringList &, qint64))
    OMASEED_FAKE(Done,
                 setShareLimits(const QStringList &,
                                RatioLimit,
                                MinutesLimit,
                                MinutesLimit,
                                const std::optional<QString> &))
    OMASEED_FAKE(GlobalLimits, globalLimits())
    OMASEED_FAKE(Done, setGlobalLimits(const GlobalLimits &))
    OMASEED_FAKE(Done, toggleAltSpeed())
    OMASEED_FAKE(Done, setAltSpeed(bool))
    OMASEED_FAKE(AddResult, addUrls(const QStringList &, const AddOptions &))
    OMASEED_FAKE(AddResult, addFile(const QString &, const QByteArray &, const AddOptions &))
    OMASEED_FAKE(AddResult,
                 addMany(const QList<AddFile> &, const QStringList &, const AddOptions &))

private:
    QUrl m_url;
    int m_retries = 3;
};

template<class T>
Result<T> await(Reply<T> *reply)
{
    Result<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("never finished"))};
    bool done = false;
    QObject context;
    reply->then(&context, [&](const Result<T> &r) {
        result = r;
        done = true;
    });
    if (!QTest::qWaitFor([&done]() { return done; }, 5000))
        qWarning("a reply never settled");
    return result;
}

TorrentPatch named(const QString &hash, const QString &name)
{
    return TorrentPatch(hash).set(&Torrent::name, TorrentField::Name, name);
}

} // namespace

class TestEngineClient : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theOptionalCallsAreNotSupportedByDefault();
    void syncEngineRunsOnAnyEngine();
    void mapReplyConvertsAndPassesErrors();
    void mapReplyForwardsAnAbort();
};

void TestEngineClient::theOptionalCallsAreNotSupportedByDefault()
{
    FakeEngine engine;
    const Result<QList<LogEntry>> log = await(engine.log(-1, true));
    QVERIFY(log.isError());
    QCOMPARE(log.error().kind(), ApiError::Kind::NotFound);
    QVERIFY(log.error().message().contains(QStringLiteral("not supported")));
    const Result<FetchedMetadata> metadata = await(engine.fetchMetadata(QStringLiteral("magnet:")));
    QVERIFY(metadata.isError());
    QCOMPARE(metadata.error().kind(), ApiError::Kind::NotFound);
    // A folder listing answers all the same: this engine can't list folders (C16 L13).
    const Result<ServerFolder> folder = await(engine.folderContent(QStringLiteral("/srv")));
    QVERIFY(!folder.isError());
    QCOMPARE(folder.value().state, ServerFolder::State::Unsupported);
}

// SyncEngine probes, takes the full delta, then asks with the cursor the delta carried.
void TestEngineClient::syncEngineRunsOnAnyEngine()
{
    FakeEngine engine;
    engine.setBaseUrl(QUrl(QStringLiteral("http://fake.invalid")));
    SyncDelta full;
    full.full = true;
    full.cursor = 7;
    full.torrents = {named(QStringLiteral("aa"), QStringLiteral("Alpha")),
                     named(QStringLiteral("bb"), QStringLiteral("Beta"))};
    SyncDelta partial;
    partial.cursor = 8;
    partial.removedTorrents = {QStringLiteral("bb")};
    engine.deltas = {full, partial};

    SyncEngine sync;
    sync.setIntervals(std::chrono::milliseconds(10),
                      std::chrono::milliseconds(10),
                      std::chrono::milliseconds(10),
                      std::chrono::milliseconds(10));
    sync.setClient(&engine);
    QSignalSpy first(&sync, &SyncEngine::firstSyncFinished);
    sync.start();
    QVERIFY(first.wait(5000));
    QCOMPARE(sync.globalState()->serverVersion(), QStringLiteral("4.1.3"));
    QCOMPARE(sync.globalState()->apiVersion(), QStringLiteral("18"));
    QCOMPARE(sync.torrents()->count(), 2);
    QTRY_COMPARE(sync.torrents()->count(), 1);
    QCOMPARE(sync.torrents()->torrent(QStringLiteral("aa")).name, QStringLiteral("Alpha"));
    QVERIFY(engine.cursors.size() >= 2);
    QCOMPARE(engine.cursors.at(0), 0);
    QCOMPARE(engine.cursors.at(1), 7);
    // The loop owns the retries while it runs, and gives them back.
    QCOMPARE(engine.maxRetries(), 0);
    sync.stop();
    QCOMPARE(engine.maxRetries(), 3);
}

void TestEngineClient::mapReplyConvertsAndPassesErrors()
{
    QObject parent;
    auto *ok = later<QString>(&parent, QStringLiteral("42"));
    const Result<int> value =
        await(mapReply<int>(ok, &parent, [](const QString &text) { return text.toInt(); }));
    QVERIFY(!value.isError());
    QCOMPARE(value.value(), 42);

    auto *failed = later<QString>(
        &parent, Result<QString>(ApiError(ApiError::Kind::Conflict, QStringLiteral("no"), 409)));
    const Result<int> error =
        await(mapReply<int>(failed, &parent, [](const QString &text) { return text.toInt(); }));
    QVERIFY(error.isError());
    QCOMPARE(error.error().kind(), ApiError::Kind::Conflict);
    QCOMPARE(error.error().httpStatus(), 409);
}

void TestEngineClient::mapReplyForwardsAnAbort()
{
    QObject parent;
    auto *source = new Reply<QString>(&parent);
    bool sourceAborted = false;
    connect(source, &ReplyBase::abortRequested, &parent, [&sourceAborted, source]() {
        sourceAborted = true;
        source->complete(Result<QString>(ApiError(ApiError::Kind::Cancelled, QStringLiteral("x"))));
    });
    auto *mapped = mapReply<int>(source, &parent, [](const QString &) { return 1; });
    QTimer::singleShot(0, mapped, [mapped]() { mapped->abort(); });
    const Result<int> result = await(mapped);
    QVERIFY(sourceAborted);
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Cancelled);
}

QTEST_GUILESS_MAIN(TestEngineClient)
#include "tst_engineclient.moc"
