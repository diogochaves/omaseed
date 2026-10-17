// #32 (docs/24 §4–§5): App's side of a qBittorrent's settings, on the demo's offline server.
// Export writes a file with no secret; the import sheet's rows list only differences, with
// the machine-specific ones unticked and the keys this qBittorrent lacks counted; applying
// changes the preferences the Settings panes read, at once; an omaseed backup's copy of the
// settings is found by the server's address. The real servers are tst_qbtsettingsbackup_live.

#include "core/OmaseedBackup.hpp"
#include "core/engines/qbt/QbtSettingsExport.hpp"
#include "core/engines/qbt/ServerPreferences.hpp"
#include "ui/app/App.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::app::App;
using omaseed::core::OmaseedBackup;
using omaseed::core::QbtSettingsExport;
using omaseed::core::TarArchive;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeJson(const QString &path, const QJsonObject &root)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(QJsonDocument(root).toJson()) > 0;
}

QVariantMap rowOf(const QVariantMap &info, const QString &key)
{
    for (const QVariant &row : info.value(QStringLiteral("rows")).toList()) {
        if (row.toMap().value(QStringLiteral("key")).toString() == key)
            return row.toMap();
    }
    return {};
}

} // namespace

class TestAppQbtBackup : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config")).toUtf8());
    }

    void cleanup()
    {
        if (m_previousConfigHome.isNull())
            qunsetenv("XDG_CONFIG_HOME");
        else
            qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
    }

    void exportWritesEveryKeyButSecrets()
    {
        App app;
        app.startDemo(16);
        // The demo's server has secrets set, as a real one may.
        app.serverPrefs()->apply({{QStringLiteral("proxy_password"), QStringLiteral("SECRET-1")},
                                  {QStringLiteral("web_ui_api_key"), QStringLiteral("SECRET-2")}});
        QSignalSpy finished(&app, &App::qbtExportFinished);
        const QString path = m_dir.filePath(QStringLiteral("qbittorrent-home.json"));
        app.exportQbtSettings(path);
        QVERIFY(finished.wait(5000));
        QVERIFY2(finished.at(0).at(1).toBool(), qPrintable(finished.at(0).at(0).toString()));
        // docs/34 D2 a: the result names the server.
        QVERIFY(finished.at(0).at(0).toString().startsWith(
            QStringLiteral("Exported home's settings to ")));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(!bytes.contains("SECRET-"));
        const QJsonObject prefs =
            QJsonDocument::fromJson(bytes).object().value(QStringLiteral("preferences")).toObject();
        QVERIFY(prefs.size() > 200);
        for (const QString &key : prefs.keys())
            QVERIFY2(!QbtSettingsExport::isSecretKey(key), qPrintable(key));
        QVERIFY2(app.qbtExportSummary().endsWith(
                     QStringLiteral("Passwords are left out. Saved as qbittorrent-home-%1.json.")
                         .arg(QDate::currentDate().toString(Qt::ISODate))),
                 qPrintable(app.qbtExportSummary()));
    }

    void importListsOnlyDifferencesAndApplies()
    {
        App app;
        app.startDemo(16);
        QSignalSpy exported(&app, &App::qbtExportFinished);
        const QString path = m_dir.filePath(QStringLiteral("other.json"));
        app.exportQbtSettings(path);
        QVERIFY(exported.wait(5000));

        // The file as another server would have it.
        QJsonObject root = readJson(path);
        QJsonObject prefs = root.value(QStringLiteral("preferences")).toObject();
        const int ports = prefs.value(QStringLiteral("listen_port")).toInt();
        prefs.insert(QStringLiteral("max_uploads"), 77);
        prefs.insert(QStringLiteral("listen_port"), ports + 1);
        prefs.insert(QStringLiteral("a_key_from_the_future"), true);
        root.insert(QStringLiteral("preferences"), prefs);
        QJsonObject categories = root.value(QStringLiteral("categories")).toObject();
        categories.insert(QStringLiteral("zz-new"),
                          QJsonObject{{QStringLiteral("name"), QStringLiteral("zz-new")},
                                      {QStringLiteral("savePath"), QString()}});
        root.insert(QStringLiteral("categories"), categories);
        QVERIFY(writeJson(path, root));

        QSignalSpy read(&app, &App::qbtImportRead);
        QCOMPARE(app.readQbtSettings(path), QString());
        QVERIFY(read.wait(5000));
        const QVariantMap info = read.at(0).at(0).toMap();
        QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
                 qPrintable(info.value(QStringLiteral("error")).toString()));
        QCOMPARE(info.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Import settings into home"));
        QVERIFY2(info.value(QStringLiteral("sub"))
                     .toString()
                     .contains(QStringLiteral("2 settings differ from what home has now")),
                 qPrintable(info.value(QStringLiteral("sub")).toString()));
        // docs/34 D2 a: the file's server and date in the "from" line, and in the columns.
        QVERIFY2(info.value(QStringLiteral("sub"))
                     .toString()
                     .startsWith(QStringLiteral("From home's export of ")),
                 qPrintable(info.value(QStringLiteral("sub")).toString()));
        QVERIFY(
            info.value(QStringLiteral("sub")).toString().contains(QStringLiteral(", other.json.")));
        QCOMPARE(info.value(QStringLiteral("nowHead")).toString(), QStringLiteral("home now"));
        QCOMPARE(info.value(QStringLiteral("fromHead")).toString(), QStringLiteral("from home"));
        QCOMPARE(info.value(QStringLiteral("exportText")).toString(),
                 QStringLiteral("export home first"));
        QCOMPARE(info.value(QStringLiteral("copy")).toBool(), false);
        QCOMPARE(info.value(QStringLiteral("changeCount")).toInt(), 2);

        const QVariantMap uploads = rowOf(info, QStringLiteral("max_uploads"));
        QCOMPARE(uploads.value(QStringLiteral("checked")).toBool(), true);
        QCOMPARE(uploads.value(QStringLiteral("from")).toInt(), 77);
        const QVariantMap port = rowOf(info, QStringLiteral("listen_port"));
        QCOMPARE(port.value(QStringLiteral("checked")).toBool(), false);
        QCOMPARE(port.value(QStringLiteral("group")).toString(), QStringLiteral("network"));
        const QVariantMap added = rowOf(info, QStringLiteral("@categories"));
        QCOMPARE(added.value(QStringLiteral("from")).toString(), QStringLiteral("+ zz-new"));
        QVERIFY(rowOf(info, QStringLiteral("a_key_from_the_future")).isEmpty());
        const QStringList notes = info.value(QStringLiteral("notes")).toStringList();
        QCOMPARE(notes.size(), 1);
        QVERIFY2(notes.first().contains(QStringLiteral("1 setting this qBittorrent doesn't have")),
                 qPrintable(notes.first()));
        QVERIFY(
            info.value(QStringLiteral("secrets")).toString().contains(QStringLiteral("DynDNS")));

        // Only the ticked key; the port stays.
        QSignalSpy imported(&app, &App::qbtImportFinished);
        app.applyQbtImport({{QStringLiteral("keys"), QStringList{QStringLiteral("max_uploads")}},
                            {QStringLiteral("categories"), true},
                            {QStringLiteral("tagsRss"), true},
                            {QStringLiteral("folders"), false}});
        QTRY_COMPARE(imported.size(), 1); // the demo answers at once
        QVERIFY2(imported.at(0).at(1).toBool(), qPrintable(imported.at(0).at(0).toString()));
        QCOMPARE(imported.at(0).at(0).toString(), QStringLiteral("Imported into home: 1 setting."));
        QCOMPARE(app.serverPrefs()->value(QStringLiteral("max_uploads")).toInt(), 77);
        QCOMPARE(app.serverPrefs()->value(QStringLiteral("listen_port")).toInt(), ports);
        QVERIFY(!app.qbtBackupBusy());

        // Read once, imported once: a second apply needs a new read.
        app.applyQbtImport({{QStringLiteral("keys"), QStringList{QStringLiteral("max_uploads")}}});
        QCOMPARE(imported.size(), 2);
        QVERIFY(!imported.at(1).at(1).toBool());
    }

    void otherFilesAreRefusedBeforeTheSheet()
    {
        App app;
        app.startDemo(16);
        const QString path = m_dir.filePath(QStringLiteral("not.json"));
        QVERIFY(writeJson(path, QJsonObject{{QStringLiteral("hello"), 1}}));
        QCOMPARE(app.readQbtSettings(path),
                 QStringLiteral("This file is neither a qBittorrent settings export nor an "
                                "omaseed backup."));
        QVERIFY(app.readQbtSettings(m_dir.filePath(QStringLiteral("missing.json")))
                    .contains(QStringLiteral("Cannot read")));
        // docs/34 D8 a: a torrents export, whatever it is called, is sent to its own row.
        const QString torrents = m_dir.filePath(QStringLiteral("torrents.json"));
        QFile tar(torrents);
        QVERIFY(tar.open(QIODevice::WriteOnly));
        tar.write(TarArchive::write(
            {{QStringLiteral("manifest.json"),
              QJsonDocument(
                  QJsonObject{{QStringLiteral("format"), QStringLiteral("omaseed-torrents")},
                              {QStringLiteral("version"), 1}})
                  .toJson(),
              0}}));
        tar.close();
        QCOMPARE(app.readQbtSettings(torrents),
                 QStringLiteral("This is a torrents export: import it with Import torrents."));
    }

    // docs/34 D8 a: the contents decide, never the name: an export renamed .omaseed is read as
    // an export, and a backup renamed .json as a backup.
    void theContentsDecideNotTheName()
    {
        App app;
        app.startDemo(16);
        QSignalSpy exported(&app, &App::qbtExportFinished);
        const QString path = m_dir.filePath(QStringLiteral("renamed.omaseed"));
        app.exportQbtSettings(path);
        QVERIFY(exported.wait(5000));
        QSignalSpy read(&app, &App::qbtImportRead);
        QCOMPARE(app.readQbtSettings(path), QString());
        QVERIFY(read.wait(5000));
        QVERIFY(read.at(0).at(0).toMap().value(QStringLiteral("ok")).toBool());
        QVERIFY(read.at(0)
                    .at(0)
                    .toMap()
                    .value(QStringLiteral("sub"))
                    .toString()
                    .startsWith(QStringLiteral("From home's export of ")));

        OmaseedBackup::Sources sources;
        sources.statePath = m_dir.filePath(QStringLiteral("none/state.json"));
        sources.serversPath = m_dir.filePath(QStringLiteral("none/servers.json"));
        const QByteArray json = readFile(path);
        sources.extras = [json] {
            return QList<TarArchive::Entry>{{QStringLiteral("qbittorrent/x.json"), json, 0}};
        };
        const QString backup = m_dir.filePath(QStringLiteral("backup.json"));
        QString error;
        QVERIFY2(OmaseedBackup::createFile(sources, backup, 1759500000, &error), qPrintable(error));
        QCOMPARE(app.readQbtSettings(backup), QString());
        QVERIFY(read.wait(5000));
        const QString sub = read.at(1).at(0).toMap().value(QStringLiteral("sub")).toString();
        QVERIFY2(sub.startsWith(QStringLiteral("From home's settings in backup.json, read ")),
                 qPrintable(sub));
    }

    // docs/34 D6 a: copy from another server, here the demo's fiction of one: the sheet is the
    // import's, titled for the copy, and nothing moves the app.
    void copyFromAnotherServer()
    {
        App app;
        app.startDemo(16, true);
        const QString home = app.serverList()->idAt(0);
        const QString other =
            app.serverList()->add({{QStringLiteral("name"), QStringLiteral("private")},
                                   {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:8091")},
                                   {QStringLiteral("authMode"), QStringLiteral("none")}});
        QVERIFY(!other.isEmpty());

        QVERIFY(app.copyQbtSettingsFrom(home).contains(QStringLiteral("omaseed is on home")));
        QCOMPARE(app.copyQbtSettingsFrom(QStringLiteral("no-such-id")),
                 QStringLiteral("That server is no longer in the list."));

        QSignalSpy read(&app, &App::qbtImportRead);
        QCOMPARE(app.copyQbtSettingsFrom(other), QString());
        QVERIFY(read.wait(5000));
        const QVariantMap info = read.at(0).at(0).toMap();
        QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
                 qPrintable(info.value(QStringLiteral("error")).toString()));
        QCOMPARE(info.value(QStringLiteral("copy")).toBool(), true);
        QCOMPARE(info.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Copy settings from private into home"));
        const QString sub = info.value(QStringLiteral("sub")).toString();
        QVERIFY2(sub.startsWith(QStringLiteral("Read from private just now (qBittorrent ")),
                 qPrintable(sub));
        QVERIFY2(sub.contains(QStringLiteral("1 setting differs from what home has now")),
                 qPrintable(sub));
        QVERIFY2(sub.endsWith(QStringLiteral("private is not changed.")), qPrintable(sub));
        QCOMPARE(info.value(QStringLiteral("nowHead")).toString(), QStringLiteral("home now"));
        QCOMPARE(info.value(QStringLiteral("fromHead")).toString(), QStringLiteral("private"));
        QVERIFY(!rowOf(info, QStringLiteral("max_active_downloads")).isEmpty());
        // The app is still on home.
        QCOMPARE(app.serverList()->defaultId(), home);
        QCOMPARE(app.serverName(), QStringLiteral("home"));

        QSignalSpy imported(&app, &App::qbtImportFinished);
        const int before = app.serverPrefs()->value(QStringLiteral("max_active_downloads")).toInt();
        app.applyQbtImport(
            {{QStringLiteral("keys"), QStringList{QStringLiteral("max_active_downloads")}}});
        QTRY_COMPARE(imported.size(), 1);
        QCOMPARE(imported.at(0).at(0).toString(), QStringLiteral("Imported into home: 1 setting."));
        QCOMPARE(app.serverPrefs()->value(QStringLiteral("max_active_downloads")).toInt(),
                 before + 2);
    }

    // An omaseed backup (an automatic copy) holds each server's settings under the ids of the
    // machine that made it: this server's is found by its address.
    void anOmaseedBackupsCopyIsFoundByAddress()
    {
        App app;
        app.startDemo(16);
        QSignalSpy exported(&app, &App::qbtExportFinished);
        const QString json = m_dir.filePath(QStringLiteral("demo.json"));
        app.exportQbtSettings(json);
        QVERIFY(exported.wait(5000));
        QJsonObject root = readJson(json);
        QJsonObject server = root.value(QStringLiteral("server")).toObject();
        server.insert(QStringLiteral("url"), QStringLiteral("http://demo.example.org"));
        root.insert(QStringLiteral("server"), server);
        QJsonObject other = root;
        other.insert(QStringLiteral("server"),
                     QJsonObject{{QStringLiteral("url"), QStringLiteral("http://10.0.0.9:8080")}});

        OmaseedBackup::Sources sources;
        sources.statePath = m_dir.filePath(QStringLiteral("none/state.json"));
        sources.serversPath = m_dir.filePath(QStringLiteral("none/servers.json"));
        sources.extras = [root, other] {
            return QList<TarArchive::Entry>{
                {QStringLiteral("qbittorrent/elsewhere-id.json"), QJsonDocument(other).toJson(), 0},
                {QStringLiteral("qbittorrent/another-machines-id.json"),
                 QJsonDocument(root).toJson(),
                 0}};
        };
        const QString backup = m_dir.filePath(QStringLiteral("auto.omaseed"));
        QString error;
        QVERIFY2(OmaseedBackup::createFile(sources, backup, 1759500000, &error), qPrintable(error));

        QSignalSpy read(&app, &App::qbtImportRead);
        QCOMPARE(app.readQbtSettings(backup), QString());
        QVERIFY(read.wait(5000));
        QVERIFY(read.at(0).at(0).toMap().value(QStringLiteral("ok")).toBool());

        // A backup with none for this server says so.
        sources.extras = [other] {
            return QList<TarArchive::Entry>{
                {QStringLiteral("qbittorrent/a.json"), QJsonDocument(other).toJson(), 0},
                {QStringLiteral("qbittorrent/b.json"), QJsonDocument(other).toJson(), 0}};
        };
        QVERIFY(OmaseedBackup::createFile(sources, backup, 1759500000, &error));
        QCOMPARE(app.readQbtSettings(backup),
                 QStringLiteral("This backup holds no qBittorrent settings for home."));
    }

private:
    QTemporaryDir m_dir;
    QByteArray m_previousConfigHome;
};

QTEST_MAIN(TestAppQbtBackup)
#include "tst_app_qbtbackup.moc"
