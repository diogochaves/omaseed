// core/SearchPlugins (#145, DECISIONS C26 SR10) against a fake qBittorrent: the list, a
// switch, uninstall, an install watched until it shows or the watch ends, and the update
// check's words from the log. Invented plugins.

#include "FakeHttp.hpp"

#include "core/SearchPlugins.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUrlQuery>

using namespace fakehttp;
using omaseed::core::QbtClient;
using omaseed::core::SearchPlugins;

namespace {

QJsonObject
plugin(const QString &name, const QString &version, bool enabled, const QStringList &types)
{
    QJsonArray categories{QJsonObject{{QStringLiteral("id"), QStringLiteral("all")},
                                      {QStringLiteral("name"), QStringLiteral("All categories")}}};
    for (const QString &type : types)
        categories.append(
            QJsonObject{{QStringLiteral("id"), type},
                        {QStringLiteral("name"), type.at(0).toUpper() + type.mid(1)}});
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("fullName"), QStringLiteral("plugin ") + name.right(1)},
                       {QStringLiteral("version"), version},
                       {QStringLiteral("url"), QStringLiteral("http://%1.invalid").arg(name)},
                       {QStringLiteral("enabled"), enabled},
                       {QStringLiteral("supportedCategories"), categories}};
}

/// The plugin list, as installs and switches change it.
struct FakeList
{
    QJsonArray list;
    QStringList installed; ///< sources installPlugin got
    QString installs;      ///< what an install adds: a name, or "" for nothing (a bad file)
    QJsonArray log;
    QList<QUrlQuery> writes;

    explicit FakeList(FakeServer &server)
    {
        server.setHandler([this](const Recorded &request, int) {
            const QString target = QString::fromUtf8(request.target);
            const QUrlQuery form(QString::fromUtf8(request.body));
            if (target == QLatin1String("/api/v2/search/plugins"))
                return httpResponse(200, "OK", QJsonDocument(list).toJson(QJsonDocument::Compact));
            if (target == QLatin1String("/api/v2/log/main") ||
                target.startsWith(QStringLiteral("/api/v2/log/main?")))
                return httpResponse(200, "OK", QJsonDocument(log).toJson(QJsonDocument::Compact));
            writes.append(form);
            if (target.endsWith(QStringLiteral("/installPlugin"))) {
                installed.append(
                    form.queryItemValue(QStringLiteral("sources"), QUrl::FullyDecoded));
                if (!installs.isEmpty())
                    list.append(
                        plugin(installs, QStringLiteral("1.0"), true, {QStringLiteral("books")}));
            } else if (target.endsWith(QStringLiteral("enablePlugin"))) {
                for (int i = 0; i < list.size(); ++i) {
                    QJsonObject one = list.at(i).toObject();
                    if (one.value(QStringLiteral("name")).toString() ==
                        form.queryItemValue(QStringLiteral("names"))) {
                        one.insert(QStringLiteral("enabled"),
                                   form.queryItemValue(QStringLiteral("enable")) ==
                                       QLatin1String("true"));
                        list[i] = one;
                    }
                }
            } else if (target.endsWith(QStringLiteral("uninstallPlugin"))) {
                for (int i = int(list.size()) - 1; i >= 0; --i)
                    if (list.at(i).toObject().value(QStringLiteral("name")).toString() ==
                        form.queryItemValue(QStringLiteral("names")))
                        list.removeAt(i);
            } else if (target.endsWith(QStringLiteral("updatePlugins"))) {
                log.append(
                    QJsonObject{{QStringLiteral("id"), 1},
                                {QStringLiteral("timestamp"), QDateTime::currentSecsSinceEpoch()},
                                {QStringLiteral("type"), 2},
                                {QStringLiteral("message"),
                                 QStringLiteral("All plugins are already up to date.")}});
            }
            return httpResponse(200, "OK", "");
        });
    }
};

} // namespace

class TestSearchPluginsFake : public QObject
{
    Q_OBJECT

private slots:
    void listsAndSwitches();
    void anInstallIsWatchedUntilItShows();
    void anInstallThatNeverShowsDidntInstall();
    void updatesAreReadFromTheLog();
};

void TestSearchPluginsFake::listsAndSwitches()
{
    FakeServer server;
    FakeList fake(server);
    fake.list = {
        plugin(QStringLiteral("p_c"),
               QStringLiteral("0.9"),
               true,
               {QStringLiteral("music"), QStringLiteral("movies")}),
        plugin(QStringLiteral("p_a"),
               QStringLiteral("1.4"),
               true,
               {QStringLiteral("movies"), QStringLiteral("software")}),
        plugin(QStringLiteral("p_b"), QStringLiteral("2.0"), false, {QStringLiteral("tv")})};
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchPlugins plugins;
    plugins.setClient(&client);
    QVERIFY(!plugins.loaded());
    plugins.refresh();
    QTRY_VERIFY(plugins.loaded());
    QCOMPARE(plugins.rowCount(), 3);
    QCOMPARE(plugins.data(plugins.index(0), SearchPlugins::FullNameRole).toString(),
             QStringLiteral("plugin a"));
    QCOMPARE(plugins.data(plugins.index(0), SearchPlugins::TypesTextRole).toString(),
             QStringLiteral("movies · software"));
    QCOMPARE(plugins.enabledCount(), 2);
    // SR6: the types of the plugins that are on, once each.
    QCOMPARE(plugins.types().size(), 3);
    QCOMPARE(plugins.enabledPlugins().size(), 2);
    QCOMPARE(plugins.names().value(QStringLiteral("p_b")), QStringLiteral("plugin b"));

    plugins.setEnabled(QStringLiteral("p_b"), true);
    QCOMPARE(plugins.enabledCount(), 3); // at once (M12)
    QTRY_COMPARE(fake.writes.size(), 1);
    QCOMPARE(fake.writes.at(0).queryItemValue(QStringLiteral("enable")), QStringLiteral("true"));

    plugins.setNote(QStringLiteral("p_a"), QStringLiteral("updated 16:20"));
    QCOMPARE(plugins.data(plugins.index(0), SearchPlugins::NoteRole).toString(),
             QStringLiteral("updated 16:20"));
    plugins.uninstall(QStringLiteral("p_c"));
    QTRY_COMPARE(plugins.rowCount(), 2);
    plugins.clear();
    QCOMPARE(plugins.rowCount(), 0);
    QVERIFY(!plugins.loaded());
}

void TestSearchPluginsFake::anInstallIsWatchedUntilItShows()
{
    FakeServer server;
    FakeList fake(server);
    fake.list = {plugin(QStringLiteral("p_a"), QStringLiteral("1.4"), true, {})};
    fake.installs = QStringLiteral("p_d");
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchPlugins plugins;
    plugins.setClient(&client);
    plugins.setWatch(20, 2000);
    plugins.refresh();
    QTRY_VERIFY(plugins.loaded());
    // Another plugin turning up meanwhile (another client's) is not this install.
    fake.list.append(plugin(QStringLiteral("p_x"), QStringLiteral("1.0"), true, {}));
    plugins.install(QStringLiteral("  /srv/plugins/p_d.py "));
    QCOMPARE(plugins.installState(), QStringLiteral("installing"));
    QTRY_COMPARE(plugins.installState(), QStringLiteral("installed"));
    QCOMPARE(plugins.installName(), QStringLiteral("plugin d"));
    QCOMPARE(fake.installed, QStringList{QStringLiteral("/srv/plugins/p_d.py")});
    QCOMPARE(plugins.rowCount(), 3);
    plugins.dismissInstall();
    QCOMPARE(plugins.installState(), QString());
}

// SR10: qBittorrent answers 200 whatever happens; nothing new in the list is "didn't install".
void TestSearchPluginsFake::anInstallThatNeverShowsDidntInstall()
{
    FakeServer server;
    FakeList fake(server);
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchPlugins plugins;
    plugins.setClient(&client);
    plugins.setWatch(20, 150);
    plugins.install(QStringLiteral("https://plugins.example/not-a-plugin.txt"));
    QTRY_COMPARE(plugins.installState(), QStringLiteral("failed"));
    QCOMPARE(plugins.installSource(), QStringLiteral("https://plugins.example/not-a-plugin.txt"));
}

void TestSearchPluginsFake::updatesAreReadFromTheLog()
{
    FakeServer server;
    FakeList fake(server);
    QbtClient client;
    client.setBaseUrl(server.url());
    SearchPlugins plugins;
    plugins.setClient(&client);
    plugins.setWatch(20, 2000);
    plugins.checkUpdates();
    QCOMPARE(plugins.updateState(), QStringLiteral("checking"));
    QTRY_COMPARE(plugins.updateState(), QStringLiteral("done"));
    QVERIFY(plugins.updateText().contains(QStringLiteral("All plugins are already up to date.")));

    const QJsonArray lines{
        QJsonObject{{QStringLiteral("timestamp"), 10},
                    {QStringLiteral("message"),
                     QStringLiteral("Plugin \"p_a\" is outdated, updating to version 1.4")}},
        QJsonObject{{QStringLiteral("timestamp"), 11},
                    {QStringLiteral("message"), QStringLiteral("A torrent finished")}}};
    QVERIFY(SearchPlugins::updateWords(lines, 10).contains(QStringLiteral("is outdated")));
    QVERIFY(!SearchPlugins::updateWords(lines, 10).contains(QStringLiteral("finished")));
    QVERIFY(SearchPlugins::updateWords(lines, 20).isEmpty());
}

QTEST_GUILESS_MAIN(TestSearchPluginsFake)
#include "tst_searchplugins_fake.moc"
