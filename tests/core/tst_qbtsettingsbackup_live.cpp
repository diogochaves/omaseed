// QbtSettingsExport and QbtSettingsImport against real qBittorrents (docs/24 §8 "checks"; #32).
//
// The export reads the :8081 playground and writes nothing there. The round trip runs between
// two qbittorrent-nox of the test's own (tests/ThrowawayQbt.hpp): settings, categories, tags,
// an RSS feed and rule set on one, exported, imported into the other, and compared on the
// second server itself. The round trip skips when qbittorrent-nox is not installed.

#include "core/QbtSettingsExport.hpp"
#include "core/QbtSettingsImport.hpp"
#include "core/engines/qbt/QbtClient.hpp"

#include "LiveServer.hpp"
#include "ThrowawayQbt.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using omaseed::core::QbtClient;
using omaseed::core::QbtSettingsExport;
using omaseed::core::QbtSettingsImport;
using omaseed::core::QbtSettingsSnapshot;
using omaseed::core::Result;

namespace {

std::optional<QbtSettingsSnapshot> collect(QbtClient &client, QString *error)
{
    QbtSettingsExport exporter(&client);
    QSignalSpy collected(&exporter, &QbtSettingsExport::collected);
    QSignalSpy failed(&exporter, &QbtSettingsExport::failed);
    exporter.collect(QStringLiteral("test"), client.baseUrl().toString(), QStringLiteral("test"));
    if (!QTest::qWaitFor([&] { return !collected.isEmpty() || !failed.isEmpty(); }, 20000)) {
        *error = QStringLiteral("no answer");
        return std::nullopt;
    }
    if (!failed.isEmpty()) {
        *error = failed.at(0).at(0).toString();
        return std::nullopt;
    }
    return collected.at(0).at(0).value<QbtSettingsSnapshot>();
}

bool post(QbtClient &client, const QString &path, const QList<QPair<QString, QString>> &fields)
{
    QUrlQuery form;
    for (const auto &[key, value] : fields)
        form.addQueryItem(key, QString::fromLatin1(QUrl::toPercentEncoding(value)));
    bool done = false;
    bool ok = false;
    client.post(path, form)->then(&client, [&](const Result<QByteArray> &r) {
        ok = r.isValid();
        done = true;
    });
    return QTest::qWaitFor([&] { return done; }, 10000) && ok;
}

} // namespace

class TestQbtSettingsBackupLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // docs/24 §8: the export reads the playground; no secret in what it hands back.
    void exportsThePlayground()
    {
        QbtClient client;
        client.setBaseUrl(LiveServer::playground());
        QString error;
        const std::optional<QbtSettingsSnapshot> s = collect(client, &error);
        if (!s && error.contains(QStringLiteral("refused"), Qt::CaseInsensitive))
            QSKIP("the :8081 playground is not running (scripts/dev/test-server.sh start)");
        QVERIFY2(s.has_value(), qPrintable(error));
        QVERIFY2(s->preferences.size() > 200, qPrintable(QString::number(s->preferences.size())));
        QVERIFY(s->version.startsWith(QLatin1Char('v')));
        QVERIFY(!s->apiVersion.isEmpty());
        for (const QString &key : QbtSettingsExport::knownSecretKeys())
            QVERIFY2(!s->preferences.contains(key), qPrintable(key));
        const QJsonObject file = QJsonDocument::fromJson(QbtSettingsExport::toJson(*s)).object();
        for (const QString &key : file.value(QStringLiteral("preferences")).toObject().keys())
            QVERIFY2(!QbtSettingsExport::isSecretKey(key), qPrintable(key));
    }

    // One server's setup into another: what differs and is ticked lands, machine-specific
    // keys and secrets stay, categories / tags / RSS are added with their folders moved, and a
    // second run finds nothing left to do.
    void roundTripBetweenTwoServers()
    {
        ThrowawayQbt from;
        if (!from.start())
            QSKIP(qPrintable(from.skipReason().isEmpty() ? from.error() : from.skipReason()));
        ThrowawayQbt to;
        QVERIFY2(to.start(), qPrintable(to.error()));

        QbtClient a;
        a.setBaseUrl(from.url());
        a.setCredentials(from.username(), from.password());
        QbtClient b;
        b.setBaseUrl(to.url());
        b.setCredentials(to.username(), to.password());

        const QString fromSave = from.downloadsPath();
        QVERIFY(post(
            a,
            QStringLiteral("app/setPreferences"),
            {{QStringLiteral("json"),
              QString::fromUtf8(
                  QJsonDocument(QJsonObject{
                                    {QStringLiteral("max_uploads"), 33},
                                    {QStringLiteral("dont_count_slow_torrents"), true},
                                    {QStringLiteral("max_ratio_enabled"), true},
                                    {QStringLiteral("max_ratio"), 1.5},
                                    {QStringLiteral("alt_dl_limit"), 2048000},
                                    {QStringLiteral("proxy_password"), QStringLiteral("pw-from")},
                                    {QStringLiteral("save_path"), fromSave},
                                })
                      .toJson(QJsonDocument::Compact))}}));
        QVERIFY(post(a,
                     QStringLiteral("torrents/createCategory"),
                     {{QStringLiteral("category"), QStringLiteral("films")},
                      {QStringLiteral("savePath"), fromSave + QStringLiteral("/films")}}));
        QVERIFY(post(a,
                     QStringLiteral("torrents/createCategory"),
                     {{QStringLiteral("category"), QStringLiteral("elsewhere")},
                      {QStringLiteral("savePath"), QStringLiteral("/opt/not-on-the-target")}}));
        QVERIFY(post(a,
                     QStringLiteral("torrents/createTags"),
                     {{QStringLiteral("tags"), QStringLiteral("hd,keep")}}));
        QVERIFY(post(
            a, QStringLiteral("rss/addFolder"), {{QStringLiteral("path"), QStringLiteral("TV")}}));
        QVERIFY(post(a,
                     QStringLiteral("rss/addFeed"),
                     {{QStringLiteral("url"), QStringLiteral("http://127.0.0.1:9/tv.xml")},
                      {QStringLiteral("path"), QStringLiteral("TV\\Shows")}}));
        QVERIFY(post(
            a,
            QStringLiteral("rss/setRule"),
            {{QStringLiteral("ruleName"), QStringLiteral("TV 1080p")},
             {QStringLiteral("ruleDef"),
              QString::fromUtf8(
                  QJsonDocument(
                      QJsonObject{{QStringLiteral("enabled"), true},
                                  {QStringLiteral("mustContain"), QStringLiteral("1080p")},
                                  {QStringLiteral("affectedFeeds"),
                                   QJsonArray{QStringLiteral("http://127.0.0.1:9/tv.xml")}},
                                  {QStringLiteral("savePath"), fromSave + QStringLiteral("/tv")}})
                      .toJson(QJsonDocument::Compact))}}));

        QString error;
        const std::optional<QbtSettingsSnapshot> exported = collect(a, &error);
        QVERIFY2(exported.has_value(), qPrintable(error));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("qbittorrent-from.json"));
        QVERIFY2(QbtSettingsExport::writeFile(*exported, path, &error), qPrintable(error));
        QFile written(path);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QVERIFY(!written.readAll().contains("pw-from"));
        const std::optional<QbtSettingsSnapshot> file = QbtSettingsExport::parseFile(path, &error);
        QVERIFY2(file.has_value(), qPrintable(error));

        const std::optional<QbtSettingsSnapshot> before = collect(b, &error);
        QVERIFY2(before.has_value(), qPrintable(error));
        const QbtSettingsImport::Diff d = QbtSettingsImport::diff(*file, *before);
        QStringList safe;
        for (const QbtSettingsImport::PrefChange &change : d.changes) {
            if (change.group.isEmpty())
                safe.append(change.key);
        }
        for (const char *key :
             {"max_uploads", "dont_count_slow_torrents", "max_ratio", "alt_dl_limit"})
            QVERIFY2(safe.contains(QLatin1String(key)), qPrintable(safe.join(QLatin1Char(' '))));
        QVERIFY(d.machineCount() > 0); // the two profiles' ports and folders differ
        QCOMPARE(d.categories, (QStringList{QStringLiteral("elsewhere"), QStringLiteral("films")}));
        QCOMPARE(d.tags, (QStringList{QStringLiteral("hd"), QStringLiteral("keep")}));
        QCOMPARE(d.feeds.size(), 1);
        QCOMPARE(d.rules, QStringList{QStringLiteral("TV 1080p")});

        QbtSettingsImport import(&b);
        QSignalSpy finished(&import, &QbtSettingsImport::finished);
        QbtSettingsImport::Plan plan;
        plan.keys = safe; // machine-specific ones stay unticked
        import.apply(*file, *before, plan);
        QVERIFY(finished.wait(30000));
        const auto outcome = finished.at(0).at(0).value<QbtSettingsImport::Outcome>();
        QVERIFY2(outcome.ok(), qPrintable(outcome.errors.join(QLatin1Char('\n'))));
        QCOMPARE(outcome.categories, 2);
        QCOMPARE(outcome.tags, 2);
        QCOMPARE(outcome.feeds, 1);
        QCOMPARE(outcome.rules, 1);

        const std::optional<QbtSettingsSnapshot> after = collect(b, &error);
        QVERIFY2(after.has_value(), qPrintable(error));
        for (const QString &key : std::as_const(safe)) {
            if (outcome.kept.contains(key))
                continue;
            QVERIFY2(after->preferences.value(key) == file->preferences.value(key) ||
                         after->preferences.value(key).toDouble() ==
                             file->preferences.value(key).toDouble(),
                     qPrintable(key));
        }
        QVERIFY2(outcome.kept.isEmpty(), qPrintable(outcome.kept.join(QLatin1Char(' '))));
        // Machine-specific: as they were. Secrets: never sent.
        for (const char *key : {"save_path", "listen_port", "web_ui_port", "temp_path"})
            QCOMPARE(after->preferences.value(QLatin1String(key)),
                     before->preferences.value(QLatin1String(key)));
        QCOMPARE(after->preferences.value(QStringLiteral("proxy_password")).toString(), QString());

        // Folders under the source's save path follow the target's; others are left out.
        const QString toSave = before->preferences.value(QStringLiteral("save_path")).toString();
        auto clean = [](QString p) {
            while (p.endsWith(QLatin1Char('/')))
                p.chop(1);
            return p;
        };
        QCOMPARE(clean(after->categories.value(QStringLiteral("films"))
                           .toObject()
                           .value(QStringLiteral("savePath"))
                           .toString()),
                 clean(toSave) + QStringLiteral("/films"));
        QCOMPARE(after->categories.value(QStringLiteral("elsewhere"))
                     .toObject()
                     .value(QStringLiteral("savePath"))
                     .toString(),
                 QString());
        QVERIFY(after->tags.contains(QStringLiteral("hd")));
        QVERIFY(after->tags.contains(QStringLiteral("keep")));
        QCOMPARE(after->feedCount(), 1);
        const QJsonObject rule = after->rssRules.value(QStringLiteral("TV 1080p")).toObject();
        QCOMPARE(rule.value(QStringLiteral("mustContain")).toString(), QStringLiteral("1080p"));
        QCOMPARE(clean(rule.value(QStringLiteral("savePath")).toString()),
                 clean(toSave) + QStringLiteral("/tv"));

        // Again: nothing left to add, and no ticked difference left.
        const QbtSettingsImport::Diff again = QbtSettingsImport::diff(*file, *after);
        QVERIFY(again.categories.isEmpty());
        QVERIFY(again.tags.isEmpty());
        QVERIFY(again.feeds.isEmpty());
        QVERIFY(again.folders.isEmpty());
        QVERIFY(again.rules.isEmpty());
        for (const QbtSettingsImport::PrefChange &change : again.changes)
            QVERIFY2(!change.group.isEmpty(), qPrintable(change.key));
    }
};

QTEST_GUILESS_MAIN(TestQbtSettingsBackupLive)
#include "tst_qbtsettingsbackup_live.moc"
