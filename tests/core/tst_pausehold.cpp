// PauseHold (docs/DECISIONS.md C36 M10) against a fake qBittorrent: pause stops what runs and
// holds it, resume starts exactly those again (never one kept stopped on purpose), a torrent
// removed meanwhile is skipped, each server keeps its own hold, and the hold outlives a
// restart through its store.

#include "FakeHttp.hpp"

#include "core/PauseHold.hpp"
#include "core/engines/qbt/QbtEngine.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

using namespace fakehttp;
using omaseed::core::PauseHold;
using omaseed::core::QbtEngine;

namespace {

/// Just the torrents part of qBittorrent: info by running/stopped, stop, start, force start.
class FakeQbt
{
public:
    struct Torrent
    {
        bool stopped = false;
        bool forced = false;
    };

    QMap<QString, Torrent> torrents;
    QList<QStringList> stops;
    QList<QStringList> starts;
    QList<QStringList> forceStarts;
    /// A forced answer to torrents/stop: "" none, "refuse" a 409, "drop" no answer at all.
    QString stopAnswer;

    explicit FakeQbt(FakeServer &server)
    {
        server.setHandler([this](const Recorded &request, int) { return answer(request); });
    }

private:
    static QStringList hashesIn(const Recorded &request)
    {
        const QUrlQuery form(QString::fromUtf8(request.body));
        const QString hashes = form.queryItemValue(QStringLiteral("hashes"), QUrl::FullyDecoded);
        return hashes.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    }

    QByteArray answer(const Recorded &request)
    {
        const QString target = QString::fromUtf8(request.target);
        if (target.startsWith(QStringLiteral("/api/v2/torrents/info"))) {
            const QUrlQuery query(target.section(QLatin1Char('?'), 1));
            const QString filter = query.queryItemValue(QStringLiteral("filter"));
            QJsonArray out;
            for (auto it = torrents.cbegin(); it != torrents.cend(); ++it) {
                const bool wanted = filter == QLatin1String("running")   ? !it->stopped
                                    : filter == QLatin1String("stopped") ? it->stopped
                                                                         : true;
                if (wanted)
                    out.append(QJsonObject{
                        {QStringLiteral("hash"), it.key()},
                        {QStringLiteral("force_start"), it->forced},
                        {QStringLiteral("state"),
                         it->stopped ? QStringLiteral("stoppedUP") : QStringLiteral("uploading")}});
            }
            return httpResponse(200, "OK", QJsonDocument(out).toJson(QJsonDocument::Compact));
        }
        if (target.startsWith(QStringLiteral("/api/v2/torrents/stop"))) {
            if (stopAnswer == QLatin1String("refuse"))
                return httpResponse(409, "Conflict", "");
            if (stopAnswer == QLatin1String("drop"))
                return {};
            const QStringList hashes = hashesIn(request);
            stops.append(hashes);
            for (const QString &hash : hashes)
                if (torrents.contains(hash))
                    torrents[hash] = Torrent{true, false};
            return httpResponse(200, "OK", "");
        }
        if (target.startsWith(QStringLiteral("/api/v2/torrents/start"))) {
            const QStringList hashes = hashesIn(request);
            starts.append(hashes);
            for (const QString &hash : hashes)
                if (torrents.contains(hash))
                    torrents[hash].stopped = false;
            return httpResponse(200, "OK", "");
        }
        if (target.startsWith(QStringLiteral("/api/v2/torrents/setForceStart"))) {
            const QStringList hashes = hashesIn(request);
            forceStarts.append(hashes);
            for (const QString &hash : hashes)
                if (torrents.contains(hash))
                    torrents[hash] = Torrent{false, true};
            return httpResponse(200, "OK", "");
        }
        return httpResponse(404, "Not Found", "");
    }
};

QStringList list(std::initializer_list<const char *> items)
{
    QStringList out;
    for (const char *item : items)
        out.append(QString::fromLatin1(item));
    return out;
}

} // namespace

class TestPauseHold : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void pauseHoldsWhatRunsAndResumeStartsExactlyThose();
    void aTorrentRemovedMeanwhileIsSkipped();
    void aSecondPauseAddsWhatRunsNow();
    void forcedComesBackForced();
    void eachServerKeepsItsOwnHold();
    void theHoldOutlivesARestart();
    void aRefusedStopForgetsAMaybeLandedOneKeeps();
    void whatCantBeSent();
};

void TestPauseHold::pauseHoldsWhatRunsAndResumeStartsExactlyThose()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.torrents.insert(QStringLiteral("aa"), {});
    qbt.torrents.insert(QStringLiteral("bb"), {});
    qbt.torrents.insert(QStringLiteral("cc"), {true, false}); // kept stopped on purpose
    QbtEngine client;
    client.setBaseUrl(server.url());
    PauseHold hold;
    hold.setClient(&client);
    hold.setServer(QStringLiteral("public"));
    QSignalSpy stored(&hold, &PauseHold::storeChanged);
    QSignalSpy paused(&hold, &PauseHold::paused);
    QSignalSpy resumed(&hold, &PauseHold::resumed);
    QVERIFY(!hold.held());

    QVERIFY(hold.pause());
    QVERIFY(hold.busy());
    QTRY_COMPARE(paused.count(), 1);
    QCOMPARE(paused.at(0).at(0).toInt(), 2);
    QVERIFY(paused.at(0).at(1).toString().isEmpty());
    QVERIFY(!hold.busy());
    // Exactly the running ones, by hash, never `all`.
    QCOMPARE(qbt.stops, QList<QStringList>{list({"aa", "bb"})});
    QVERIFY(hold.held());
    QCOMPARE(hold.count(), 2);
    QCOMPARE(hold.hashes(), list({"aa", "bb"}));
    QVERIFY(stored.count() >= 1);
    QCOMPARE(hold.store()
                 .value(QStringLiteral("public"))
                 .toObject()
                 .value(QStringLiteral("hashes"))
                 .toArray(),
             QJsonArray::fromStringList(list({"aa", "bb"})));

    QVERIFY(hold.resume());
    QTRY_COMPARE(resumed.count(), 1);
    QCOMPARE(resumed.at(0).at(0).toInt(), 2);
    QCOMPARE(resumed.at(0).at(1).toInt(), 0);
    QCOMPARE(qbt.starts, QList<QStringList>{list({"aa", "bb"})});
    QVERIFY(qbt.forceStarts.isEmpty());
    // Start all would have started it; resume leaves it as it was.
    QVERIFY(qbt.torrents.value(QStringLiteral("cc")).stopped);
    QVERIFY(!qbt.torrents.value(QStringLiteral("aa")).stopped);
    QVERIFY(!hold.held());
    QVERIFY(hold.store().isEmpty());
    QVERIFY(!hold.resume()); // nothing held
}

void TestPauseHold::aTorrentRemovedMeanwhileIsSkipped()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.torrents.insert(QStringLiteral("aa"), {});
    qbt.torrents.insert(QStringLiteral("bb"), {});
    QbtEngine client;
    client.setBaseUrl(server.url());
    PauseHold hold;
    hold.setClient(&client);
    hold.setServer(QStringLiteral("public"));
    QSignalSpy paused(&hold, &PauseHold::paused);
    QSignalSpy resumed(&hold, &PauseHold::resumed);

    QVERIFY(hold.pause());
    QTRY_COMPARE(paused.count(), 1);
    qbt.torrents.remove(QStringLiteral("bb"));

    QVERIFY(hold.resume());
    QTRY_COMPARE(resumed.count(), 1);
    QCOMPARE(resumed.at(0).at(0).toInt(), 1);
    QCOMPARE(resumed.at(0).at(1).toInt(), 1);
    QCOMPARE(qbt.starts, QList<QStringList>{list({"aa"})}); // bb never sent
    QVERIFY(!hold.held());
}

void TestPauseHold::aSecondPauseAddsWhatRunsNow()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.torrents.insert(QStringLiteral("aa"), {});
    qbt.torrents.insert(QStringLiteral("cc"), {true, false});
    QbtEngine client;
    client.setBaseUrl(server.url());
    PauseHold hold;
    hold.setClient(&client);
    hold.setServer(QStringLiteral("public"));
    QSignalSpy paused(&hold, &PauseHold::paused);

    QVERIFY(hold.pause());
    QTRY_COMPARE(paused.count(), 1);
    qbt.torrents[QStringLiteral("cc")].stopped = false; // started by hand meanwhile
    QVERIFY(hold.pause());
    QTRY_COMPARE(paused.count(), 2);
    QCOMPARE(qbt.stops.value(1), list({"cc"}));
    QCOMPARE(hold.hashes(), list({"aa", "cc"}));

    // Nothing running: no stop sent, the hold as it was.
    QVERIFY(hold.pause());
    QTRY_COMPARE(paused.count(), 3);
    QCOMPARE(paused.at(2).at(0).toInt(), 0);
    QCOMPARE(qbt.stops.size(), 2);
    QCOMPARE(hold.count(), 2);
}

void TestPauseHold::forcedComesBackForced()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.torrents.insert(QStringLiteral("aa"), {false, true});
    qbt.torrents.insert(QStringLiteral("bb"), {});
    QbtEngine client;
    client.setBaseUrl(server.url());
    PauseHold hold;
    hold.setClient(&client);
    hold.setServer(QStringLiteral("public"));
    QSignalSpy paused(&hold, &PauseHold::paused);
    QSignalSpy resumed(&hold, &PauseHold::resumed);

    QVERIFY(hold.pause());
    QTRY_COMPARE(paused.count(), 1);
    QCOMPARE(hold.forced(), list({"aa"}));
    QVERIFY(!qbt.torrents.value(QStringLiteral("aa")).forced);

    QVERIFY(hold.resume());
    QTRY_COMPARE(resumed.count(), 1);
    QCOMPARE(resumed.at(0).at(0).toInt(), 2);
    QCOMPARE(qbt.starts, QList<QStringList>{list({"bb"})});
    QCOMPARE(qbt.forceStarts, QList<QStringList>{list({"aa"})});
    QVERIFY(qbt.torrents.value(QStringLiteral("aa")).forced);
    QVERIFY(!qbt.torrents.value(QStringLiteral("aa")).stopped);
}

void TestPauseHold::eachServerKeepsItsOwnHold()
{
    FakeServer serverOne;
    FakeQbt one(serverOne);
    one.torrents.insert(QStringLiteral("aa"), {});
    one.torrents.insert(QStringLiteral("bb"), {});
    FakeServer serverTwo;
    FakeQbt two(serverTwo);
    two.torrents.insert(QStringLiteral("xx"), {});
    QbtEngine client;
    client.setBaseUrl(serverOne.url());
    PauseHold hold;
    hold.setClient(&client);
    hold.setServer(QStringLiteral("one"));
    QSignalSpy paused(&hold, &PauseHold::paused);
    QSignalSpy resumed(&hold, &PauseHold::resumed);

    QVERIFY(hold.pause());
    QTRY_COMPARE(paused.count(), 1);
    QCOMPARE(hold.count(), 2);

    client.setBaseUrl(serverTwo.url());
    hold.setServer(QStringLiteral("two"));
    QVERIFY(!hold.held());
    QVERIFY(!hold.resume()); // nothing held here
    QVERIFY(hold.pause());
    QTRY_COMPARE(paused.count(), 2);
    QCOMPARE(hold.hashes(), list({"xx"}));
    QVERIFY(one.stops.size() == 1 && two.stops.size() == 1);

    client.setBaseUrl(serverOne.url());
    hold.setServer(QStringLiteral("one"));
    QCOMPARE(hold.hashes(), list({"aa", "bb"}));
    QVERIFY(hold.resume());
    QTRY_COMPARE(resumed.count(), 1);
    QCOMPARE(one.starts, QList<QStringList>{list({"aa", "bb"})});
    QVERIFY(two.starts.isEmpty());
    QVERIFY(!hold.held());
    // Two's hold waits for it.
    QCOMPARE(hold.store().keys(), list({"two"}));
}

void TestPauseHold::theHoldOutlivesARestart()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.torrents.insert(QStringLiteral("aa"), {false, true});
    qbt.torrents.insert(QStringLiteral("bb"), {});
    QbtEngine client;
    client.setBaseUrl(server.url());
    QJsonObject saved;
    {
        PauseHold before;
        before.setClient(&client);
        before.setServer(QStringLiteral("public"));
        QSignalSpy paused(&before, &PauseHold::paused);
        QVERIFY(before.pause());
        QTRY_COMPARE(paused.count(), 1);
        saved = before.store();
    }

    // state.json's section, read back by the next run.
    const QJsonObject reread = QJsonDocument::fromJson(QJsonDocument(saved).toJson()).object();
    PauseHold after;
    after.setClient(&client);
    after.setStore(reread);
    QVERIFY(!after.held()); // no server yet
    after.setServer(QStringLiteral("public"));
    QCOMPARE(after.hashes(), list({"aa", "bb"}));
    QCOMPARE(after.forced(), list({"aa"}));
    QSignalSpy resumed(&after, &PauseHold::resumed);
    QVERIFY(after.resume());
    QTRY_COMPARE(resumed.count(), 1);
    QCOMPARE(qbt.starts, QList<QStringList>{list({"bb"})});
    QCOMPARE(qbt.forceStarts, QList<QStringList>{list({"aa"})});
    QVERIFY(after.store().isEmpty());
}

void TestPauseHold::aRefusedStopForgetsAMaybeLandedOneKeeps()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.torrents.insert(QStringLiteral("aa"), {});
    QbtEngine client;
    client.setBaseUrl(server.url());
    PauseHold hold;
    hold.setClient(&client);
    hold.setServer(QStringLiteral("public"));
    QSignalSpy paused(&hold, &PauseHold::paused);

    // The server said no: nothing was stopped, nothing is held.
    qbt.stopAnswer = QStringLiteral("refuse");
    QVERIFY(hold.pause());
    QTRY_COMPARE(paused.count(), 1);
    QCOMPARE(paused.at(0).at(0).toInt(), 0);
    QVERIFY(!paused.at(0).at(1).toString().isEmpty());
    QVERIFY(!hold.held());
    QVERIFY(!hold.busy());

    // No answer: the stop may have landed, so the hold stays.
    qbt.stopAnswer = QStringLiteral("drop");
    QVERIFY(hold.pause());
    QTRY_COMPARE(paused.count(), 2);
    QVERIFY(!paused.at(1).at(1).toString().isEmpty());
    QCOMPARE(hold.hashes(), list({"aa"}));
}

void TestPauseHold::whatCantBeSent()
{
    PauseHold alone;
    alone.setServer(QStringLiteral("public"));
    QVERIFY(!alone.pause()); // no client

    FakeServer server;
    FakeQbt qbt(server);
    qbt.torrents.insert(QStringLiteral("aa"), {});
    QbtEngine client;
    client.setBaseUrl(server.url());
    PauseHold hold;
    hold.setClient(&client);
    QVERIFY(!hold.pause()); // no server
    hold.setServer(QStringLiteral("public"));
    QVERIFY(!hold.resume()); // nothing held

    QSignalSpy paused(&hold, &PauseHold::paused);
    QVERIFY(hold.pause());
    QVERIFY(hold.busy());
    QVERIFY(!hold.pause()); // one on its way
    QVERIFY(!hold.resume());
    QTRY_COMPARE(paused.count(), 1);
    QVERIFY(!hold.busy());
    QCOMPARE(qbt.stops.size(), 1);
}

QTEST_GUILESS_MAIN(TestPauseHold)
#include "tst_pausehold.moc"
