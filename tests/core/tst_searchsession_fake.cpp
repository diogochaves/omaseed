// core/SearchSession (#145, DECISIONS C26 SR1, SR2, SR12, SR13) against a fake qBittorrent:
// one job per plugin, at most four at once; answered, failed (from the log), "0 results",
// slow past the deadline and stopped; skipped types; no Python; the jobs deleted when replaced
// and on shutdown. Timings are shortened; plugin names are invented.

#include "FakeHttp.hpp"

#include "core/SearchSession.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

using namespace fakehttp;
using omaseed::core::QbtClient;
using omaseed::core::SearchSession;

namespace {

/// What one invented plugin does when asked.
struct Behaviour
{
    int results = 0;    ///< how many rows it sends
    int stopMs = 0;     ///< its job runs this long
    bool fails = false; ///< ends with nothing and a warning in the log
};

/// Just enough of qBittorrent's search API.
class FakeQbt
{
public:
    QHash<QString, Behaviour> plugins;
    QStringList started; ///< plugin names, in the order their jobs started
    QList<int> deleted;
    QList<int> stopped;
    QByteArray startAnswer; ///< a forced answer to search/start (a 409)
    QJsonArray log;
    int maxRunning = 0;

    explicit FakeQbt(FakeServer &server)
    {
        m_clock.start();
        server.setHandler([this](const Recorded &request, int) { return answer(request); });
    }

    int running() const
    {
        int n = 0;
        for (auto it = m_jobs.cbegin(); it != m_jobs.cend(); ++it)
            if (!stoppedNow(it.key()))
                ++n;
        return n;
    }

private:
    struct Job
    {
        QString plugin;
        qint64 startedMs;
        bool stopped = false;
    };
    QHash<int, Job> m_jobs;
    QElapsedTimer m_clock;
    int m_next = 100;

    bool stoppedNow(int id) const
    {
        const Job &job = m_jobs.value(id);
        return job.stopped || m_clock.elapsed() - job.startedMs >= plugins.value(job.plugin).stopMs;
    }

    QByteArray answer(const Recorded &request)
    {
        const QString target = QString::fromUtf8(request.target);
        const QUrlQuery form(QString::fromUtf8(request.body));
        const QUrlQuery query(target.section(QLatin1Char('?'), 1));
        if (target.startsWith(QStringLiteral("/api/v2/search/start"))) {
            if (!startAnswer.isEmpty())
                return startAnswer;
            const QString plugin = form.queryItemValue(QStringLiteral("plugins"));
            const int id = m_next++;
            m_jobs.insert(id, Job{plugin, m_clock.elapsed()});
            started.append(plugin);
            maxRunning = std::max(maxRunning, running());
            if (plugins.value(plugin).fails) {
                QJsonObject line{
                    {QStringLiteral("id"), int(log.size())},
                    {QStringLiteral("timestamp"), QDateTime::currentSecsSinceEpoch()},
                    {QStringLiteral("type"), 4},
                    {QStringLiteral("message"),
                     QStringLiteral(
                         "Error occurred in search engine. Search query: \"x\". Category: \"all\". "
                         "Engines: \"%1\". Error: \"Traceback (most recent call last):\n  File "
                         "\"/q/nova3/engines/%1.py\", line 33, in search\nRuntimeError: the "
                         "invented site answered nothing\".")
                         .arg(plugin)}};
                log.append(line);
            }
            return httpResponse(200, "OK", QByteArray("{\"id\":") + QByteArray::number(id) + "}");
        }
        if (target.startsWith(QStringLiteral("/api/v2/search/results"))) {
            const int id = query.queryItemValue(QStringLiteral("id")).toInt();
            const int offset = query.queryItemValue(QStringLiteral("offset")).toInt();
            if (!m_jobs.contains(id))
                return httpResponse(404, "Not Found", "");
            const Job &job = m_jobs.value(id);
            const Behaviour behaviour = plugins.value(job.plugin);
            const bool done = stoppedNow(id);
            // Its rows come when it ends, as a plugin's often do.
            const int have = done && !behaviour.fails && !job.stopped ? behaviour.results : 0;
            QJsonArray results;
            for (int i = offset; i < have; ++i)
                results.append(QJsonObject{
                    {QStringLiteral("fileName"),
                     QStringLiteral("%1 row %2").arg(job.plugin).arg(i)},
                    {QStringLiteral("fileSize"), 1000 + i},
                    {QStringLiteral("fileUrl"),
                     QStringLiteral("magnet:?xt=urn:btih:%1")
                         .arg(QString::number(id * 100 + i).rightJustified(40, QLatin1Char('0')))},
                    {QStringLiteral("nbSeeders"), 10 - i},
                    {QStringLiteral("nbLeechers"), 1},
                    {QStringLiteral("pubDate"), 1000},
                    {QStringLiteral("engineName"), job.plugin}});
            const QJsonObject body{{QStringLiteral("results"), results},
                                   {QStringLiteral("status"),
                                    done ? QStringLiteral("Stopped") : QStringLiteral("Running")},
                                   {QStringLiteral("total"), have}};
            return httpResponse(200, "OK", QJsonDocument(body).toJson(QJsonDocument::Compact));
        }
        if (target.startsWith(QStringLiteral("/api/v2/search/delete"))) {
            deleted.append(form.queryItemValue(QStringLiteral("id")).toInt());
            m_jobs.remove(form.queryItemValue(QStringLiteral("id")).toInt());
            return httpResponse(200, "OK", "");
        }
        if (target.startsWith(QStringLiteral("/api/v2/search/stop"))) {
            const int id = form.queryItemValue(QStringLiteral("id")).toInt();
            stopped.append(id);
            m_jobs[id].stopped = true;
            return httpResponse(200, "OK", "");
        }
        if (target.startsWith(QStringLiteral("/api/v2/log/main")))
            return httpResponse(200, "OK", QJsonDocument(log).toJson(QJsonDocument::Compact));
        return httpResponse(404, "Not Found", "");
    }
};

QList<SearchSession::Plugin> pluginsNamed(const QStringList &names,
                                          const QStringList &categories = {})
{
    QList<SearchSession::Plugin> list;
    for (const QString &name : names)
        list.append({name, QStringLiteral("plugin ") + name.right(1), categories});
    return list;
}

QVariantMap rowOf(const SearchSession &session, const QString &name)
{
    for (const QVariant &row : session.pluginRows())
        if (row.toMap().value(QStringLiteral("name")).toString() == name)
            return row.toMap();
    return {};
}

QString stateOf(const SearchSession &session, const QString &name)
{
    return rowOf(session, name).value(QStringLiteral("state")).toString();
}

} // namespace

class TestSearchSessionFake : public QObject
{
    Q_OBJECT

private slots:
    void atMostFourJobsAtOnce();
    void answeredFailedAndNothingFound();
    void slowPastTheDeadlineIsStillHeard();
    void stopAskingStopsItsJob();
    void aTypeAPluginLacksIsSkipped();
    void noPythonOnTheServer();
    void replacedAndShutDownJobsAreDeleted();
    void askAgainReplacesItsRows();
    void failureFromLogReadsTheLastLine();
    void aSnapshotComesBackDone();
};

void TestSearchSessionFake::atMostFourJobsAtOnce()
{
    FakeServer server;
    FakeQbt qbt(server);
    const QStringList names = {QStringLiteral("p_a"),
                               QStringLiteral("p_b"),
                               QStringLiteral("p_c"),
                               QStringLiteral("p_d"),
                               QStringLiteral("p_e"),
                               QStringLiteral("p_f")};
    for (const QString &name : names)
        qbt.plugins.insert(name, Behaviour{1, 150});
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchSession session;
    session.setClient(&client);
    session.setTimings(20, 5000, 4);
    session.start(QStringLiteral("x"), QStringLiteral("all"), pluginsNamed(names));
    QTRY_COMPARE(qbt.started.size(), 4);
    QCOMPARE(stateOf(session, QStringLiteral("p_f")), QStringLiteral("queued"));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), SearchSession::State::Done, 5000);
    QCOMPARE(qbt.started.size(), 6);
    QCOMPARE(qbt.maxRunning, 4);
    QCOMPARE(session.answered(), 6);
    QCOMPARE(session.resultCount(), 6);
}

// SR2: failed is what the log says; nothing found is "0 results", not a failure.
void TestSearchSessionFake::answeredFailedAndNothingFound()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.plugins.insert(QStringLiteral("p_a"), Behaviour{3, 30});
    qbt.plugins.insert(QStringLiteral("p_b"), Behaviour{0, 30, true});
    qbt.plugins.insert(QStringLiteral("p_c"), Behaviour{0, 30});
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchSession session;
    session.setClient(&client);
    session.setTimings(20, 5000, 4);
    QSignalSpy hits(&session, &SearchSession::hitsChanged);
    session.start(
        QStringLiteral("open movie"),
        QStringLiteral("all"),
        pluginsNamed({QStringLiteral("p_a"), QStringLiteral("p_b"), QStringLiteral("p_c")}));
    QCOMPARE(session.state(), SearchSession::State::Asking);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), SearchSession::State::Done, 5000);
    QCOMPARE(stateOf(session, QStringLiteral("p_a")), QStringLiteral("answered"));
    QCOMPARE(rowOf(session, QStringLiteral("p_a")).value(QStringLiteral("count")).toInt(), 3);
    QCOMPARE(stateOf(session, QStringLiteral("p_b")), QStringLiteral("failed"));
    QCOMPARE(rowOf(session, QStringLiteral("p_b")).value(QStringLiteral("reason")).toString(),
             QStringLiteral("RuntimeError: the invented site answered nothing"));
    QVERIFY(!rowOf(session, QStringLiteral("p_b"))
                 .value(QStringLiteral("logTime"))
                 .toString()
                 .isEmpty());
    QCOMPARE(stateOf(session, QStringLiteral("p_c")), QStringLiteral("answered"));
    QCOMPARE(rowOf(session, QStringLiteral("p_c")).value(QStringLiteral("count")).toInt(), 0);
    QCOMPARE(session.asked(), 3);
    QCOMPARE(session.answered(), 2);
    QCOMPARE(session.didntAnswer(), 1);
    QCOMPARE(session.resultCount(), 3);
    QVERIFY(hits.count() >= 2);
    QCOMPARE(session.hits().at(0).plugin, QStringLiteral("p_a"));
}

void TestSearchSessionFake::slowPastTheDeadlineIsStillHeard()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.plugins.insert(QStringLiteral("p_a"), Behaviour{1, 20});
    qbt.plugins.insert(QStringLiteral("p_d"), Behaviour{2, 600});
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchSession session;
    session.setClient(&client);
    session.setTimings(20, 150, 4);
    session.start(QStringLiteral("x"),
                  QStringLiteral("all"),
                  pluginsNamed({QStringLiteral("p_a"), QStringLiteral("p_d")}));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), SearchSession::State::Done, 2000);
    QCOMPARE(stateOf(session, QStringLiteral("p_d")), QStringLiteral("slow"));
    QCOMPARE(session.didntAnswer(), 1);
    QCOMPARE(session.resultCount(), 1);
    // Still heard until it ends.
    QTRY_COMPARE_WITH_TIMEOUT(
        stateOf(session, QStringLiteral("p_d")), QStringLiteral("answered"), 3000);
    QCOMPARE(session.resultCount(), 3);
    QCOMPARE(session.didntAnswer(), 0);
}

void TestSearchSessionFake::stopAskingStopsItsJob()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.plugins.insert(QStringLiteral("p_d"), Behaviour{2, 60000});
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchSession session;
    session.setClient(&client);
    session.setTimings(20, 100, 4);
    session.start(
        QStringLiteral("x"), QStringLiteral("all"), pluginsNamed({QStringLiteral("p_d")}));
    QTRY_COMPARE_WITH_TIMEOUT(
        stateOf(session, QStringLiteral("p_d")), QStringLiteral("slow"), 2000);
    session.stopAsking(QStringLiteral("p_d"));
    QCOMPARE(stateOf(session, QStringLiteral("p_d")), QStringLiteral("stopped"));
    QTRY_COMPARE(qbt.stopped.size(), 1);
    QCOMPARE(session.state(), SearchSession::State::Done);
}

// SR6: a plugin without the chosen type is skipped, named, never asked.
void TestSearchSessionFake::aTypeAPluginLacksIsSkipped()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.plugins.insert(QStringLiteral("p_a"), Behaviour{1, 20});
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchSession session;
    session.setClient(&client);
    session.setTimings(20, 5000, 4);
    QList<SearchSession::Plugin> plugins =
        pluginsNamed({QStringLiteral("p_a")}, {QStringLiteral("tv")});
    plugins += pluginsNamed({QStringLiteral("p_b")}, {QStringLiteral("music")});
    session.start(QStringLiteral("x"), QStringLiteral("tv"), plugins);
    QCOMPARE(stateOf(session, QStringLiteral("p_b")), QStringLiteral("skipped"));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), SearchSession::State::Done, 2000);
    QCOMPARE(qbt.started, QStringList{QStringLiteral("p_a")});
    QCOMPARE(session.asked(), 1);
    QCOMPARE(session.category(), QStringLiteral("tv"));
}

void TestSearchSessionFake::noPythonOnTheServer()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.startAnswer =
        httpResponse(409, "Conflict", "Python must be installed to use the Search Engine.");
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchSession session;
    session.setClient(&client);
    session.setTimings(20, 5000, 4);
    session.start(QStringLiteral("x"),
                  QStringLiteral("all"),
                  pluginsNamed({QStringLiteral("p_a"), QStringLiteral("p_b")}));
    QTRY_COMPARE(session.state(), SearchSession::State::NoPython);
    QCOMPARE(session.stateName(), QStringLiteral("nopython"));
    QVERIFY(session.error().contains(QStringLiteral("Python")));
}

// SR12: omaseed deletes the jobs it started when replaced and on quit, never others'.
void TestSearchSessionFake::replacedAndShutDownJobsAreDeleted()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.plugins.insert(QStringLiteral("p_a"), Behaviour{1, 20});
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchSession session;
    session.setClient(&client);
    session.setTimings(20, 5000, 4);
    session.start(
        QStringLiteral("one"), QStringLiteral("all"), pluginsNamed({QStringLiteral("p_a")}));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), SearchSession::State::Done, 2000);
    const QList<int> first = session.jobIds();
    QCOMPARE(first.size(), 1);
    session.start(
        QStringLiteral("two"), QStringLiteral("all"), pluginsNamed({QStringLiteral("p_a")}));
    QTRY_COMPARE(qbt.deleted, first);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), SearchSession::State::Done, 2000);
    const QList<int> second = session.jobIds();
    session.shutdown();
    QTRY_COMPARE(qbt.deleted, first + second);
    QVERIFY(session.jobIds().isEmpty());
    QCOMPARE(session.resultCount(), 1);
}

void TestSearchSessionFake::askAgainReplacesItsRows()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.plugins.insert(QStringLiteral("p_a"), Behaviour{2, 20});
    qbt.plugins.insert(QStringLiteral("p_b"), Behaviour{0, 20, true});
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchSession session;
    session.setClient(&client);
    session.setTimings(20, 5000, 4);
    session.start(QStringLiteral("x"),
                  QStringLiteral("all"),
                  pluginsNamed({QStringLiteral("p_a"), QStringLiteral("p_b")}));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), SearchSession::State::Done, 2000);
    QCOMPARE(stateOf(session, QStringLiteral("p_b")), QStringLiteral("failed"));
    qbt.plugins[QStringLiteral("p_b")] = Behaviour{1, 20};
    session.askAgain(QStringLiteral("p_b"));
    QCOMPARE(session.state(), SearchSession::State::Asking);
    QTRY_COMPARE_WITH_TIMEOUT(
        stateOf(session, QStringLiteral("p_b")), QStringLiteral("answered"), 2000);
    QTRY_COMPARE(session.state(), SearchSession::State::Done);
    QCOMPARE(session.resultCount(), 3);
    QCOMPARE(qbt.deleted.size(), 1);
}

void TestSearchSessionFake::failureFromLogReadsTheLastLine()
{
    const QJsonArray lines = {
        QJsonObject{
            {QStringLiteral("timestamp"), 50},
            {QStringLiteral("message"),
             QStringLiteral("Error occurred in search engine. Engines: \"p_b\". Error: \"old\".")}},
        QJsonObject{{QStringLiteral("timestamp"), 200},
                    {QStringLiteral("message"), QStringLiteral("Something else entirely")}},
        QJsonObject{{QStringLiteral("timestamp"), 210},
                    {QStringLiteral("message"),
                     QStringLiteral("Error occurred in search engine. Search query: \"x\". "
                                    "Category: \"all\". Engines: "
                                    "\"p_b\". Error: \"Traceback:\n  File "
                                    "\"engines/p_b.py\"\nURLError: timed out\n\".")}},
    };
    const QVariantMap failure = SearchSession::failureFromLog(lines, QStringLiteral("p_b"), 100);
    QCOMPARE(failure.value(QStringLiteral("reason")).toString(),
             QStringLiteral("URLError: timed out"));
    QVERIFY(SearchSession::failureFromLog(lines, QStringLiteral("p_c"), 100).isEmpty());
    // Older than the search: not this search's failure.
    QVERIFY(SearchSession::failureFromLog(QJsonArray{lines.at(0)}, QStringLiteral("p_b"), 100)
                .isEmpty());
}

void TestSearchSessionFake::aSnapshotComesBackDone()
{
    FakeServer server;
    FakeQbt qbt(server);
    qbt.plugins.insert(QStringLiteral("p_a"), Behaviour{2, 20});
    qbt.plugins.insert(QStringLiteral("p_d"), Behaviour{2, 60000});
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchSession session;
    session.setClient(&client);
    session.setTimings(20, 5000, 4);
    session.start(QStringLiteral("x"),
                  QStringLiteral("all"),
                  pluginsNamed({QStringLiteral("p_a"), QStringLiteral("p_d")}));
    QTRY_COMPARE_WITH_TIMEOUT(session.resultCount(), 2, 2000);
    const SearchSession::Snapshot kept = session.snapshot();
    const QList<int> left = session.jobIds();
    session.forget();
    QCOMPARE(session.state(), SearchSession::State::Idle);
    QCOMPARE(session.resultCount(), 0);
    session.restore(kept);
    QCOMPARE(session.state(), SearchSession::State::Done);
    QCOMPARE(session.pattern(), QStringLiteral("x"));
    QCOMPARE(session.resultCount(), 2);
    QCOMPARE(stateOf(session, QStringLiteral("p_d")), QStringLiteral("stopped"));
    session.deleteJobs(left);
    QTRY_COMPARE(qbt.deleted.size(), 2);
}

QTEST_GUILESS_MAIN(TestSearchSessionFake)
#include "tst_searchsession_fake.moc"
