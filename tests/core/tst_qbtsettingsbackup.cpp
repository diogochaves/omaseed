// QbtSettingsExport and QbtSettingsImport without a server (docs/24 §5, §6; #32): the file
// never holds a secret, the import lists only differences, machine-specific keys are marked,
// keys the target lacks are skipped and counted, categories / tags / RSS are only added, and
// their folders follow the target's. The live half (two qBittorrents) is
// tst_qbtsettingsbackup_live.

#include "core/engines/qbt/QbtSettingsExport.hpp"
#include "core/engines/qbt/QbtSettingsImport.hpp"
#include "core/engines/qbt/ServerPreferences.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::QbtSettingsExport;
using omaseed::core::QbtSettingsImport;
using omaseed::core::QbtSettingsSnapshot;
using omaseed::core::ServerPreferences;

namespace {

/// A fresh qBittorrent 5.2.3's preferences (224 keys), its secrets set to `SECRET-<key>` and a
/// Web UI password added, as if a version ever returned one.
QJsonObject fixturePreferences()
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/preferences/qbittorrent-5.2.3.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonObject rssItems()
{
    return QJsonObject{
        {QStringLiteral("TV"),
         QJsonObject{
             {QStringLiteral("Shows"),
              QJsonObject{{QStringLiteral("uid"), QStringLiteral("{a}")},
                          {QStringLiteral("url"), QStringLiteral("https://feeds.example/tv")}}},
             {QStringLiteral("Anime"),
              QJsonObject{{QStringLiteral("Daily"),
                           QJsonObject{{QStringLiteral("uid"), QStringLiteral("{b}")},
                                       {QStringLiteral("url"),
                                        QStringLiteral("https://feeds.example/anime")}}}}}}},
        {QStringLiteral("Linux"),
         QJsonObject{{QStringLiteral("uid"), QStringLiteral("{c}")},
                     {QStringLiteral("url"), QStringLiteral("https://feeds.example/linux")}}}};
}

QJsonObject rule(const QString &savePath)
{
    return QJsonObject{
        {QStringLiteral("enabled"), true},
        {QStringLiteral("mustContain"), QStringLiteral("1080p")},
        {QStringLiteral("affectedFeeds"), QJsonArray{QStringLiteral("https://feeds.example/tv")}},
        {QStringLiteral("savePath"), savePath},
        {QStringLiteral("assignedCategory"), QStringLiteral("tv")},
        {QStringLiteral("torrentParams"),
         QJsonObject{{QStringLiteral("save_path"), savePath},
                     {QStringLiteral("download_path"), QString()},
                     {QStringLiteral("ssl_certificate"), QString()},
                     {QStringLiteral("ssl_private_key"), QStringLiteral("SECRET-ssl_private_key")},
                     {QStringLiteral("tags"), QJsonArray{QStringLiteral("hd")}}}}};
}

QJsonObject category(const QString &savePath, const QJsonValue &downloadPath = QJsonValue::Null)
{
    return QJsonObject{{QStringLiteral("name"), QStringLiteral("x")},
                       {QStringLiteral("savePath"), savePath},
                       {QStringLiteral("download_path"), downloadPath},
                       {QStringLiteral("ratio_limit"), -2},
                       {QStringLiteral("seeding_time_limit"), -2},
                       {QStringLiteral("inactive_seeding_time_limit"), -2},
                       {QStringLiteral("share_limit_action"), QStringLiteral("Default")}};
}

QbtSettingsSnapshot snapshot()
{
    QbtSettingsSnapshot s;
    s.created = QDateTime(QDate(2026, 10, 3), QTime(21, 4), QTimeZone::UTC);
    s.appVersion = QStringLiteral("0.1.0");
    s.serverName = QStringLiteral("home");
    s.serverUrl = QStringLiteral("http://127.0.0.1:8080");
    s.version = QStringLiteral("v5.2.3");
    s.apiVersion = QStringLiteral("2.15.1");
    s.preferences = fixturePreferences();
    s.categories =
        QJsonObject{{QStringLiteral("tv"), category(QStringLiteral("/srv/qbt/downloads/tv"))}};
    s.tags = {QStringLiteral("hd")};
    s.rssItems = rssItems();
    s.rssRules =
        QJsonObject{{QStringLiteral("TV 1080p"), rule(QStringLiteral("/srv/qbt/downloads/tv"))}};
    return s;
}

QStringList keysOf(const QList<QbtSettingsImport::PrefChange> &changes)
{
    QStringList out;
    for (const QbtSettingsImport::PrefChange &change : changes)
        out.append(change.key);
    return out;
}

} // namespace

class TestQbtSettingsBackup : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fixtureHasEverySecret()
    {
        const QJsonObject prefs = fixturePreferences();
        QCOMPARE(prefs.size(), 224);
        for (const char *key : {"web_ui_password",
                                "web_ui_api_key",
                                "proxy_password",
                                "mail_notification_password",
                                "dyndns_password"})
            QCOMPARE(prefs.value(QLatin1String(key)).toString(),
                     QStringLiteral("SECRET-") + QLatin1String(key));
    }

    void secretKeys_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<bool>("secret");
        // Every secret qBittorrent 5.2 (and 4.1) hands back, and the forms a later one would use.
        for (const char *key : {"web_ui_password",
                                "web_ui_api_key",
                                "proxy_password",
                                "mail_notification_password",
                                "dyndns_password",
                                "ssl_key",
                                "ssl_cert",
                                "ssl_private_key",
                                "smtp_password",
                                "some_future_api_key",
                                "tracker_auth_token",
                                "oauth_secret"})
            QTest::newRow(key) << QString::fromLatin1(key) << true;
        // Look alike, are not.
        for (const char *key : {"web_ui_username",
                                "proxy_username",
                                "proxy_auth_enabled",
                                "mail_notification_auth_enabled",
                                "dyndns_username",
                                "web_ui_https_key_path",
                                "web_ui_https_cert_path",
                                "bdecode_token_limit",
                                "bypass_local_auth",
                                "ssl_enabled",
                                "ssl_listen_port",
                                "ssl_certificate"})
            QTest::newRow(key) << QString::fromLatin1(key) << false;
    }

    void secretKeys()
    {
        QFETCH(QString, key);
        QFETCH(bool, secret);
        QCOMPARE(QbtSettingsExport::isSecretKey(key), secret);
    }

    // D5 a: no secret reaches the file, at any depth; every other key does.
    void exportLeavesEverySecretOut()
    {
        const QbtSettingsSnapshot s = snapshot();
        const QByteArray bytes = QbtSettingsExport::toJson(s);
        QVERIFY2(!bytes.contains("SECRET-"), bytes.constData());
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        const QJsonObject prefs = root.value(QStringLiteral("preferences")).toObject();
        for (const QString &key : QbtSettingsExport::knownSecretKeys())
            QVERIFY2(!prefs.contains(key), qPrintable(key));
        QCOMPARE(prefs.size(), 224 - 5);
        QCOMPARE(prefs.value(QStringLiteral("web_ui_username")).toString(),
                 QStringLiteral("admin"));
        QCOMPARE(prefs.value(QStringLiteral("proxy_username")).toString(), QString());
        const QJsonObject params = root.value(QStringLiteral("rss"))
                                       .toObject()
                                       .value(QStringLiteral("rules"))
                                       .toObject()
                                       .value(QStringLiteral("TV 1080p"))
                                       .toObject()
                                       .value(QStringLiteral("torrentParams"))
                                       .toObject();
        QVERIFY(!params.contains(QStringLiteral("ssl_private_key")));
        QVERIFY(params.contains(QStringLiteral("ssl_certificate")));
        QCOMPARE(root.value(QStringLiteral("format")).toString(), QbtSettingsExport::kFormat);
        QCOMPARE(root.value(QStringLiteral("server"))
                     .toObject()
                     .value(QStringLiteral("qbittorrent"))
                     .toString(),
                 QStringLiteral("v5.2.3"));
    }

    void fileRoundTrips()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("qbittorrent-home.json"));
        QString error;
        QVERIFY2(QbtSettingsExport::writeFile(snapshot(), path, &error), qPrintable(error));
        QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());
        const std::optional<QbtSettingsSnapshot> back = QbtSettingsExport::parseFile(path, &error);
        QVERIFY2(back.has_value(), qPrintable(error));
        QCOMPARE(back->serverName, QStringLiteral("home"));
        QCOMPARE(back->version, QStringLiteral("v5.2.3"));
        QCOMPARE(back->apiVersion, QStringLiteral("2.15.1"));
        QCOMPARE(back->created, snapshot().created);
        QCOMPARE(back->preferences.size(), 224 - 5);
        QCOMPARE(back->categories.keys(), QStringList{QStringLiteral("tv")});
        QCOMPARE(back->tags, QStringList{QStringLiteral("hd")});
        QCOMPARE(back->feedCount(), 3);
        QCOMPARE(back->rssRules.keys(), QStringList{QStringLiteral("TV 1080p")});
    }

    // A file edited by hand to carry a secret: it is dropped on reading.
    void parseDropsSecrets()
    {
        QJsonObject root = QJsonDocument::fromJson(QbtSettingsExport::toJson(snapshot())).object();
        QJsonObject prefs = root.value(QStringLiteral("preferences")).toObject();
        prefs.insert(QStringLiteral("proxy_password"), QStringLiteral("hunter2"));
        prefs.insert(QStringLiteral("web_ui_api_key"), QStringLiteral("qbt_abc"));
        root.insert(QStringLiteral("preferences"), prefs);
        const std::optional<QbtSettingsSnapshot> back =
            QbtSettingsExport::parse(QJsonDocument(root).toJson());
        QVERIFY(back.has_value());
        QVERIFY(!back->preferences.contains(QStringLiteral("proxy_password")));
        QVERIFY(!back->preferences.contains(QStringLiteral("web_ui_api_key")));
    }

    void parseRefusesOtherFiles()
    {
        QString error;
        QVERIFY(!QbtSettingsExport::parse("not json", &error));
        QVERIFY(error.contains(QStringLiteral("not a qBittorrent settings export")));
        QVERIFY(!QbtSettingsExport::parse(R"({"format":"omaseed-backup","version":1})", &error));
        QVERIFY(error.contains(QStringLiteral("not a qBittorrent settings export")));
        QVERIFY(!QbtSettingsExport::parse(
            R"({"format":"omaseed-qbittorrent-settings","version":9,"preferences":{}})", &error));
        QVERIFY(error.contains(QStringLiteral("newer omaseed")));
        QVERIFY(!QbtSettingsExport::parse(
            R"({"format":"omaseed-qbittorrent-settings","version":1})", &error));
        QVERIFY(error.contains(QStringLiteral("no preferences")));
    }

    void machineSpecific_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("group");
        const QList<std::pair<const char *, const char *>> rows = {
            {"save_path", "folders"},
            {"temp_path", "folders"},
            {"export_dir", "folders"},
            {"export_dir_fin", "folders"},
            {"scan_dirs", "folders"},
            {"file_log_path", "folders"},
            {"ip_filter_path", "folders"},
            {"listen_port", "network"},
            {"current_network_interface", "network"},
            {"current_interface_address", "network"},
            {"upnp", "network"},
            {"i2p_enabled", "network"},
            {"i2p_address", "network"},
            {"i2p_port", "network"},
            {"web_ui_address", "webui"},
            {"web_ui_port", "webui"},
            {"web_ui_username", "webui"},
            {"web_ui_https_cert_path", "webui"},
            {"web_ui_https_key_path", "webui"},
            {"use_https", "webui"},
            {"bypass_local_auth", "webui"},
            {"bypass_auth_subnet_whitelist", "webui"},
            {"bypass_auth_subnet_whitelist_enabled", "webui"},
            {"web_ui_domain_list", "webui"},
            {"web_ui_reverse_proxy_enabled", "webui"},
            {"web_ui_reverse_proxies_list", "webui"},
            {"autorun_program", "programs"},
            {"autorun_on_torrent_added_program", "programs"},
            {"python_executable_path", "programs"},
            {"alternative_webui_path", "programs"},
            {"max_uploads", ""},
            {"dont_count_slow_torrents", ""},
            {"max_ratio", ""},
            {"dl_limit", ""},
            {"temp_path_enabled", ""},
            {"web_ui_session_timeout", ""},
        };
        for (const auto &[key, group] : rows)
            QTest::newRow(key) << QString::fromLatin1(key) << QString::fromLatin1(group);
    }

    void machineSpecific()
    {
        QFETCH(QString, key);
        QFETCH(QString, group);
        QCOMPARE(QbtSettingsImport::machineGroup(key), group);
    }

    // Only differences; safe keys first, machine-specific after; keys the target lacks skipped
    // and counted; keys the file lacks left alone; secrets never compared.
    void diffListsOnlyDifferences()
    {
        const QbtSettingsSnapshot here = snapshot();
        QbtSettingsSnapshot file = *QbtSettingsExport::parse(QbtSettingsExport::toJson(snapshot()));
        file.preferences.insert(QStringLiteral("max_uploads"), 40);
        file.preferences.insert(QStringLiteral("dont_count_slow_torrents"), true);
        file.preferences.insert(QStringLiteral("max_ratio"), 1.0);
        file.preferences.insert(QStringLiteral("listen_port"), 51413);
        file.preferences.insert(QStringLiteral("save_path"), QStringLiteral("/data/torrents"));
        file.preferences.insert(QStringLiteral("web_ui_username"), QStringLiteral("diogo"));
        file.preferences.insert(QStringLiteral("future_key"), 1);      // not on the target
        file.preferences.insert(QStringLiteral("another_new_key"), 2); // not on the target
        file.preferences.insert(QStringLiteral("proxy_password"), QStringLiteral("x"));
        file.preferences.remove(QStringLiteral("locale")); // the file lacks it
        // A number the server writes as 10240.0 is the same as 10240.
        file.preferences.insert(QStringLiteral("alt_dl_limit"), 10240.0);

        const QbtSettingsImport::Diff d = QbtSettingsImport::diff(file, here);
        QCOMPARE(keysOf(d.changes),
                 (QStringList{QStringLiteral("dont_count_slow_torrents"),
                              QStringLiteral("max_ratio"),
                              QStringLiteral("max_uploads"),
                              QStringLiteral("listen_port"),
                              QStringLiteral("save_path"),
                              QStringLiteral("web_ui_username")}));
        QCOMPARE(d.machineCount(), 3);
        QCOMPARE(d.changes.at(0).group, QString());
        QCOMPARE(d.changes.at(3).group, QStringLiteral("network"));
        QCOMPARE(d.changes.at(4).group, QStringLiteral("folders"));
        QCOMPARE(d.changes.at(5).group, QStringLiteral("webui"));
        QCOMPARE(d.changes.at(2).here.toInt(), 20);
        QCOMPARE(d.changes.at(2).file.toInt(), 40);
        QCOMPARE(d.skipped,
                 (QStringList{QStringLiteral("another_new_key"), QStringLiteral("future_key")}));
        // 224 here, 5 secrets, locale missing from the file, 6 changed.
        QCOMPARE(d.same, 224 - 5 - 1 - 6);
        QCOMPARE(d.secretsHere, 5);
    }

    void identicalServersHaveNothingToChange()
    {
        const QbtSettingsSnapshot s = snapshot();
        const QbtSettingsImport::Diff d = QbtSettingsImport::diff(s, s);
        QVERIFY(d.changes.isEmpty());
        QVERIFY(d.skipped.isEmpty());
        QVERIFY(d.categories.isEmpty());
        QVERIFY(d.tags.isEmpty());
        QVERIFY(d.feeds.isEmpty());
        QVERIFY(d.folders.isEmpty());
        QVERIFY(d.rules.isEmpty());
        QCOMPARE(d.same, 224 - 5);
    }

    // Categories, tags and rules by name, feeds by URL: only what is missing, nothing taken.
    void categoriesTagsAndRssAreOnlyAdded()
    {
        QbtSettingsSnapshot file = snapshot();
        file.categories.insert(QStringLiteral("films"),
                               category(QStringLiteral("/srv/qbt/downloads/films")));
        QJsonObject limited = category(QString());
        limited.insert(QStringLiteral("ratio_limit"), 2.0);
        file.categories.insert(QStringLiteral("audio"), limited);
        file.tags = {QStringLiteral("hd"),
                     QStringLiteral("4k"),
                     QStringLiteral("keep"),
                     QStringLiteral("4k")};
        file.rssRules.insert(QStringLiteral("Anime"), rule(QString()));

        QbtSettingsSnapshot here = snapshot();
        here.categories.insert(QStringLiteral("only-here"), category(QString()));
        here.tags = {QStringLiteral("hd"), QStringLiteral("mine")};
        // The TV feed is here under another name and folder; same URL, so the same feed.
        here.rssItems = QJsonObject{
            {QStringLiteral("Series"),
             QJsonObject{{QStringLiteral("uid"), QStringLiteral("{z}")},
                         {QStringLiteral("url"), QStringLiteral("https://feeds.example/tv")}}},
            {QStringLiteral("TV"), QJsonObject{}}};
        here.rssRules.insert(QStringLiteral("Mine"), rule(QString()));

        const QbtSettingsImport::Diff d = QbtSettingsImport::diff(file, here);
        QCOMPARE(d.categories, (QStringList{QStringLiteral("audio"), QStringLiteral("films")}));
        QCOMPARE(d.categoriesHere, 2);
        QCOMPARE(d.categoryLimitsLeftOut, 1);
        QCOMPARE(d.tags, (QStringList{QStringLiteral("4k"), QStringLiteral("keep")}));
        QCOMPARE(d.tagsHere, 2);
        QCOMPARE(d.feeds.size(), 2);
        QCOMPARE(d.feeds.at(0).url, QStringLiteral("https://feeds.example/linux"));
        QCOMPARE(d.feeds.at(0).path, QStringLiteral("Linux"));
        QCOMPARE(d.feeds.at(1).url, QStringLiteral("https://feeds.example/anime"));
        QCOMPARE(d.feeds.at(1).path, QStringLiteral("TV\\Anime\\Daily"));
        // TV is here already: only the folder under it is new, parents first.
        QCOMPARE(d.folders, QStringList{QStringLiteral("TV\\Anime")});
        QCOMPARE(d.feedsHere, 1);
        QCOMPARE(d.rules, QStringList{QStringLiteral("Anime")});
        QCOMPARE(d.rulesHere, 2);
        QVERIFY(d.hasFolders); // films names one
    }

    void rebaseFolder_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("keep");
        QTest::addColumn<QString>("expected");
        QTest::newRow("under the save path")
            << "/data/torrents/films" << false << "/mnt/media/films";
        QTest::newRow("the save path itself") << "/data/torrents/" << false << "/mnt/media";
        QTest::newRow("under the temp path") << "/data/incomplete/tv" << false << "/mnt/fast/tv";
        QTest::newRow("elsewhere: left out") << "/opt/other" << false << "";
        QTest::newRow("a sibling, not under") << "/data/torrents-old/x" << false << "";
        QTest::newRow("relative: kept") << "films" << false << "films";
        QTest::newRow("empty: kept") << "" << false << "";
        QTest::newRow("ticked: kept") << "/opt/other" << true << "/opt/other";
        QTest::newRow("Windows path elsewhere") << "D:\\Torrents\\films" << false << "";
    }

    void rebaseFolder()
    {
        QFETCH(QString, path);
        QFETCH(bool, keep);
        QFETCH(QString, expected);
        const QList<QPair<QString, QString>> roots = {
            {QStringLiteral("/data/torrents"), QStringLiteral("/mnt/media/")},
            {QStringLiteral("/data/incomplete"), QStringLiteral("/mnt/fast")}};
        QCOMPARE(QbtSettingsImport::rebaseFolder(path, roots, keep), expected);
    }

    // The save path row ticked: the folders follow the file's own save path, which is now
    // the target's too.
    void folderRootsFollowTheTickedSavePath()
    {
        QbtSettingsSnapshot file = snapshot();
        file.preferences.insert(QStringLiteral("save_path"), QStringLiteral("/data/torrents"));
        QbtSettingsSnapshot here = snapshot();
        here.preferences.insert(QStringLiteral("save_path"), QStringLiteral("/mnt/media"));
        QbtSettingsImport::Plan plan;
        auto roots = QbtSettingsImport::folderRoots(file, here, plan);
        QCOMPARE(roots.at(0).first, QStringLiteral("/data/torrents"));
        QCOMPARE(roots.at(0).second, QStringLiteral("/mnt/media"));
        plan.keys = {QStringLiteral("save_path")};
        roots = QbtSettingsImport::folderRoots(file, here, plan);
        QCOMPARE(roots.at(0).second, QStringLiteral("/data/torrents"));
    }

    void categoryFormMovesItsFolders()
    {
        const QList<QPair<QString, QString>> roots = {
            {QStringLiteral("/data/torrents"), QStringLiteral("/mnt/media")},
            {QStringLiteral("/data/incomplete"), QStringLiteral("/mnt/fast")}};
        using Form = QList<QPair<QString, QString>>;
        QCOMPARE(QbtSettingsImport::categoryForm(QStringLiteral("films"),
                                                 category(QStringLiteral("/data/torrents/films"),
                                                          QStringLiteral("/data/incomplete/films")),
                                                 roots,
                                                 false),
                 (Form{{QStringLiteral("category"), QStringLiteral("films")},
                       {QStringLiteral("savePath"), QStringLiteral("/mnt/media/films")},
                       {QStringLiteral("downloadPathEnabled"), QStringLiteral("true")},
                       {QStringLiteral("downloadPath"), QStringLiteral("/mnt/fast/films")}}));
        // Elsewhere: left out, so qBittorrent's own default (<save path>/<name>) applies.
        QCOMPARE(QbtSettingsImport::categoryForm(
                     QStringLiteral("films"),
                     category(QStringLiteral("/opt/films"), QStringLiteral("/opt/inc")),
                     roots,
                     false),
                 (Form{{QStringLiteral("category"), QStringLiteral("films")},
                       {QStringLiteral("savePath"), QString()}}));
        // Ticked: as exported.
        QCOMPARE(QbtSettingsImport::categoryForm(
                     QStringLiteral("films"), category(QStringLiteral("/opt/films")), roots, true),
                 (Form{{QStringLiteral("category"), QStringLiteral("films")},
                       {QStringLiteral("savePath"), QStringLiteral("/opt/films")}}));
        // The incomplete folder turned off for the category stays off.
        QCOMPARE(QbtSettingsImport::categoryForm(
                     QStringLiteral("films"), category(QString(), false), roots, false),
                 (Form{{QStringLiteral("category"), QStringLiteral("films")},
                       {QStringLiteral("savePath"), QString()},
                       {QStringLiteral("downloadPathEnabled"), QStringLiteral("false")}}));
    }

    void ruleDefinitionMovesItsFoldersAndDropsSecrets()
    {
        const QList<QPair<QString, QString>> roots = {
            {QStringLiteral("/data/torrents"), QStringLiteral("/mnt/media")}};
        const QJsonObject def = QbtSettingsImport::ruleDefinition(
            rule(QStringLiteral("/data/torrents/tv")), roots, false);
        QCOMPARE(def.value(QStringLiteral("savePath")).toString(), QStringLiteral("/mnt/media/tv"));
        const QJsonObject params = def.value(QStringLiteral("torrentParams")).toObject();
        QCOMPARE(params.value(QStringLiteral("save_path")).toString(),
                 QStringLiteral("/mnt/media/tv"));
        QVERIFY(!params.contains(QStringLiteral("ssl_private_key")));
        QCOMPARE(def.value(QStringLiteral("affectedFeeds")).toArray().first().toString(),
                 QStringLiteral("https://feeds.example/tv"));
        QCOMPARE(QbtSettingsImport::ruleDefinition(rule(QStringLiteral("/opt/tv")), roots, true)
                     .value(QStringLiteral("savePath"))
                     .toString(),
                 QStringLiteral("/opt/tv"));
    }

    // Only the ticked keys that still differ; never a secret, never a key the target lacks.
    void preferenceChangesAreTheTickedDifferences()
    {
        const QbtSettingsSnapshot here = snapshot();
        QbtSettingsSnapshot file = snapshot();
        file.preferences.insert(QStringLiteral("max_uploads"), 40);
        file.preferences.insert(QStringLiteral("listen_port"), 51413);
        file.preferences.insert(QStringLiteral("future_key"), 1);
        QbtSettingsImport::Plan plan;
        plan.keys = {QStringLiteral("max_uploads"),
                     QStringLiteral("future_key"),
                     QStringLiteral("proxy_password"),
                     QStringLiteral("dht"), // the same on both sides
                     QbtSettingsImport::kFoldersKey};
        const QJsonObject changes = QbtSettingsImport::preferenceChanges(file, here, plan);
        QCOMPARE(changes, (QJsonObject{{QStringLiteral("max_uploads"), 40}}));
    }

    // Preferences go through ServerPreferences, so the panes show them at once.
    void applyWritesPreferencesThroughServerPreferences()
    {
        ServerPreferences prefs(nullptr);
        prefs.setOffline(fixturePreferences().toVariantMap());
        QSignalSpy posted(&prefs, &ServerPreferences::posted);

        const QbtSettingsSnapshot here = snapshot();
        QbtSettingsSnapshot file = snapshot();
        file.preferences.insert(QStringLiteral("max_uploads"), 40);
        file.preferences.insert(QStringLiteral("listen_port"), 51413);

        QbtSettingsImport import(nullptr);
        import.setPreferences(&prefs);
        QSignalSpy finished(&import, &QbtSettingsImport::finished);
        QbtSettingsImport::Plan plan;
        plan.keys = {QStringLiteral("max_uploads")}; // the port stays unticked
        plan.categories = plan.tags = plan.rss = false;
        import.apply(file, here, plan);
        QTRY_COMPARE(finished.size(), 1);
        const auto outcome = finished.at(0).at(0).value<QbtSettingsImport::Outcome>();
        QVERIFY2(outcome.ok(), qPrintable(outcome.errors.join(QLatin1Char('\n'))));
        QCOMPARE(outcome.preferences, 1);
        QCOMPARE(posted.size(), 1);
        QCOMPARE(prefs.value(QStringLiteral("max_uploads")).toInt(), 40);
        QCOMPARE(prefs.value(QStringLiteral("listen_port")).toInt(), 6881);
        QVERIFY(!import.isBusy());
    }
};

QTEST_GUILESS_MAIN(TestQbtSettingsBackup)
#include "tst_qbtsettingsbackup.moc"
