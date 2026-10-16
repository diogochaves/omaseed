// QbtSettingsFetch against real qBittorrents (docs/34 D6 a, #70): another server's settings,
// read through a client of the fetch's own. The :8081 playground (no login from localhost) is
// only read. A throwaway qbittorrent-nox with a password login (tests/ThrowawayQbt.hpp) checks
// the cookie path: read with the right password, and refused with a wrong one. That half skips
// when qbittorrent-nox is not installed.

#include "core/QbtClient.hpp"
#include "core/QbtSettingsFetch.hpp"
#include "core/Reply.hpp"
#include "core/Server.hpp"

#include "LiveServer.hpp"
#include "ThrowawayQbt.hpp"

#include <QSignalSpy>
#include <QTest>

using omaseed::core::AuthMode;
using omaseed::core::QbtClient;
using omaseed::core::QbtSettingsFetch;
using omaseed::core::QbtSettingsSnapshot;
using omaseed::core::Result;
using omaseed::core::Server;

namespace {

struct Outcome
{
    std::optional<QbtSettingsSnapshot> snapshot;
    QString reason;
};

Outcome fetchFrom(const Server &server, const QString &secret)
{
    QbtSettingsFetch fetch;
    QSignalSpy fetched(&fetch, &QbtSettingsFetch::fetched);
    QSignalSpy failed(&fetch, &QbtSettingsFetch::failed);
    fetch.fetch(server, secret, QStringLiteral("test"));
    if (!QTest::qWaitFor([&] { return !fetched.isEmpty() || !failed.isEmpty(); }, 30000))
        return {std::nullopt, QStringLiteral("no answer")};
    if (!failed.isEmpty())
        return {std::nullopt, failed.at(0).at(0).toString()};
    return {fetched.at(0).at(0).value<QbtSettingsSnapshot>(), QString()};
}

bool answers(const QUrl &url)
{
    QbtClient client;
    client.setBaseUrl(url);
    client.setMaxRetries(0);
    bool done = false;
    bool ok = false;
    client.appVersion()->then(&client, [&](const Result<QString> &r) {
        ok = r.isValid();
        done = true;
    });
    return QTest::qWaitFor([&] { return done; }, 5000) && ok;
}

} // namespace

class TestQbtSettingsFetchLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsThePlayground()
    {
        const QUrl url = LiveServer::playground();
        if (!answers(url))
            QSKIP("the :8081 playground is not running (scripts/dev/test-server.sh start)");
        Server server;
        server.id = QStringLiteral("playground-id");
        server.name = QStringLiteral("playground");
        server.url = url;
        const Outcome out = fetchFrom(server, QString());
        QVERIFY2(out.snapshot, qPrintable(out.reason));
        QCOMPARE(out.snapshot->serverName, QStringLiteral("playground"));
        QCOMPARE(out.snapshot->serverUrl, url.toString());
        QVERIFY(out.snapshot->version.startsWith(QLatin1Char('v')));
        QVERIFY(out.snapshot->preferences.contains(QStringLiteral("listen_port")));
        // Secrets stay out, as in any export.
        QVERIFY(!out.snapshot->preferences.contains(QStringLiteral("web_ui_api_key")));
    }

    void signsInWithAPassword()
    {
        ThrowawayQbt qbt;
        if (!qbt.start())
            QSKIP(qPrintable(qbt.skipReason().isEmpty() ? qbt.error() : qbt.skipReason()));
        Server server;
        server.id = QStringLiteral("throwaway-id");
        server.name = QStringLiteral("private");
        server.url = qbt.url();
        server.authMode = AuthMode::Cookie;
        server.username = qbt.username();

        const Outcome out = fetchFrom(server, qbt.password());
        QVERIFY2(out.snapshot, qPrintable(out.reason + QLatin1Char('\n') + qbt.log()));
        QCOMPARE(out.snapshot->serverName, QStringLiteral("private"));
        QVERIFY(!out.snapshot->preferences.isEmpty());

        // A wrong password: refused, in the server sheet's words.
        const Outcome refused = fetchFrom(server, QStringLiteral("not-the-password"));
        QVERIFY(!refused.snapshot);
        QCOMPARE(refused.reason, QStringLiteral("wrong username or password"));
    }
};

QTEST_GUILESS_MAIN(TestQbtSettingsFetchLive)
#include "tst_qbtsettingsfetch_live.moc"
