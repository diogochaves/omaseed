// docs/34 D8 a and D6 a (#70), without a server: a file is known by what is inside it, never
// by its name (BackupFormat); another server's settings come from omaseed's own copies when
// that server can't be read (QbtSettingsFetch::fromBackup, newestSaved), and a read that can't
// reach the server says why in plain words. The live half is tst_qbtsettingsfetch_live.

#include "core/ApiError.hpp"
#include "core/BackupFormat.hpp"
#include "core/OmaseedBackup.hpp"
#include "core/Server.hpp"
#include "core/TarArchive.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/qbt/QbtSettingsExport.hpp"
#include "core/engines/qbt/QbtSettingsFetch.hpp"

#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::ApiError;
using omaseed::core::AuthMode;
using omaseed::core::OmaseedBackup;
using omaseed::core::QbtSettingsExport;
using omaseed::core::QbtSettingsFetch;
using omaseed::core::QbtSettingsSnapshot;
using omaseed::core::Server;
using omaseed::core::TarArchive;
using omaseed::core::TorrentsBackup;
namespace BackupFormat = omaseed::core::BackupFormat;
using Kind = omaseed::core::BackupFormat::Kind;

namespace {

QbtSettingsSnapshot
settingsOf(const QString &name, const QString &url, const QDateTime &created, int maxActive)
{
    QbtSettingsSnapshot s;
    s.created = created;
    s.appVersion = QStringLiteral("0.2.0");
    s.serverName = name;
    s.serverUrl = url;
    s.version = QStringLiteral("v5.2.3");
    s.apiVersion = QStringLiteral("2.15.1");
    s.preferences = QJsonObject{{QStringLiteral("max_active_downloads"), maxActive}};
    return s;
}

QByteArray manifest(const QString &format)
{
    return QJsonDocument(
               QJsonObject{{QStringLiteral("format"), format}, {QStringLiteral("version"), 1}})
        .toJson();
}

/// An omaseed backup holding `extras` (no settings, no servers): enough for OmaseedBackup::parse.
QByteArray backupWith(const QList<TarArchive::Entry> &extras)
{
    QList<TarArchive::Entry> entries{
        {QStringLiteral("manifest.json"), manifest(OmaseedBackup::kFormat), 1760000000}};
    entries.append(extras);
    return TarArchive::write(entries);
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

Server server(const QString &id, const QString &name, const QString &url)
{
    Server s;
    s.id = id;
    s.name = name;
    s.url = QUrl(url);
    return s;
}

const QDateTime kMonday(QDate(2026, 10, 12), QTime(21, 4), QTimeZone::UTC);
const QDateTime kThursday(QDate(2026, 10, 15), QTime(23, 30), QTimeZone::UTC);

} // namespace

class TestBackupFormat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // D8 a: the three files, each under every name, and what is none of them.
    void detectsByContent_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("kind");

        const QByteArray settings = QbtSettingsExport::toJson(settingsOf(
            QStringLiteral("private"), QStringLiteral("http://127.0.0.1:8091"), kThursday, 3));
        const QByteArray torrents = TarArchive::write(
            {{TorrentsBackup::kManifest, manifest(TorrentsBackup::kFormat), 1760000000},
             {QStringLiteral("torrents/abc.json"), QByteArray("{}"), 1760000000}});
        // A tar rebuilt by hand, its manifest not first.
        const QByteArray reordered = TarArchive::write(
            {{QStringLiteral("state.json"), QByteArray("{}"), 1760000000},
             {QStringLiteral("manifest.json"), manifest(OmaseedBackup::kFormat), 1760000000}});

        QTest::newRow("omaseed backup") << backupWith({}) << int(Kind::OmaseedBackup);
        QTest::newRow("manifest not first") << reordered << int(Kind::OmaseedBackup);
        QTest::newRow("torrents export") << torrents << int(Kind::OmaseedTorrents);
        QTest::newRow("settings export") << settings << int(Kind::QbtSettings);
        QTest::newRow("another app's JSON")
            << QByteArray(R"({"format": "something-else", "version": 1})") << int(Kind::Unknown);
        QTest::newRow("a JSON array") << QByteArray("[1, 2]") << int(Kind::Unknown);
        // A manifest alone is not one of omaseed's files.
        QTest::newRow("a backup's manifest on its own")
            << manifest(OmaseedBackup::kFormat) << int(Kind::Unknown);
        QTest::newRow("a tar without a manifest")
            << TarArchive::write({{QStringLiteral("notes.txt"), QByteArray("hi"), 1760000000}})
            << int(Kind::Unknown);
        QTest::newRow("a tar of another format")
            << TarArchive::write(
                   {{QStringLiteral("manifest.json"), manifest(QStringLiteral("x")), 1760000000}})
            << int(Kind::Unknown);
        QTest::newRow("empty") << QByteArray() << int(Kind::Unknown);
        QTest::newRow("text") << QByteArray("hello, world\n") << int(Kind::Unknown);
    }

    void detectsByContent()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(int, kind);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // The same bytes under each of omaseed's names, and none: the name never decides.
        for (const QString &name : {QStringLiteral("file.omaseed"),
                                    QStringLiteral("file.omaseed-torrents"),
                                    QStringLiteral("file.json"),
                                    QStringLiteral("file")}) {
            const QString path = dir.filePath(name);
            QVERIFY(writeFile(path, bytes));
            QCOMPARE(int(BackupFormat::detectFile(path)), kind);
        }
    }

    void detectLeavesTheDeviceWhereItWas()
    {
        QByteArray bytes = backupWith({});
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        QVERIFY(buffer.seek(100));
        QCOMPARE(BackupFormat::detect(&buffer), Kind::OmaseedBackup);
        QCOMPARE(buffer.pos(), 100);
        QCOMPARE(BackupFormat::detectFile(QStringLiteral("/nonexistent/file.omaseed")),
                 Kind::Unknown);
    }

    void formatNames()
    {
        QCOMPARE(BackupFormat::formatName(Kind::OmaseedBackup), QStringLiteral("omaseed-backup"));
        QCOMPARE(BackupFormat::formatName(Kind::OmaseedTorrents),
                 QStringLiteral("omaseed-torrents"));
        QCOMPARE(BackupFormat::formatName(Kind::QbtSettings),
                 QStringLiteral("omaseed-qbittorrent-settings"));
        QVERIFY(BackupFormat::formatName(Kind::Unknown).isEmpty());
    }

    // D6 a's fallback: a backup gives a server's settings by this machine's id for it, then by
    // its address, and never another server's because it is the only one.
    void fromBackupFindsTheServer()
    {
        const QByteArray privateJson = QbtSettingsExport::toJson(settingsOf(
            QStringLiteral("private"), QStringLiteral("http://127.0.0.1:8091"), kThursday, 7));
        const QByteArray publicJson = QbtSettingsExport::toJson(settingsOf(
            QStringLiteral("public"), QStringLiteral("http://127.0.0.1:8090"), kThursday, 3));
        const std::optional<OmaseedBackup::Contents> contents = OmaseedBackup::parse(
            backupWith({{QStringLiteral("qbittorrent/private-id.json"), privateJson, 1760000000},
                        {QStringLiteral("qbittorrent/other-id.json"), publicJson, 1760000000}}));
        QVERIFY(contents);

        // By id.
        std::optional<QbtSettingsSnapshot> s =
            QbtSettingsFetch::fromBackup(*contents,
                                         server(QStringLiteral("private-id"),
                                                QStringLiteral("private"),
                                                QStringLiteral("http://10.0.0.2:8091")));
        QVERIFY(s);
        QCOMPARE(s->serverName, QStringLiteral("private"));
        // By address, when this machine gave the server another id.
        s = QbtSettingsFetch::fromBackup(*contents,
                                         server(QStringLiteral("new-id"),
                                                QStringLiteral("public"),
                                                QStringLiteral("http://127.0.0.1:8090/")));
        QVERIFY(s);
        QCOMPARE(s->serverName, QStringLiteral("public"));
        // Neither: nothing, even though the backup has settings.
        s = QbtSettingsFetch::fromBackup(*contents,
                                         server(QStringLiteral("seedbox-id"),
                                                QStringLiteral("seedbox"),
                                                QStringLiteral("https://box.example")));
        QVERIFY(!s);
    }

    // The newest of what this run read and the newest copy that holds the server.
    void newestSavedPicksTheNewest()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Server privateServer = server(QStringLiteral("private-id"),
                                            QStringLiteral("private"),
                                            QStringLiteral("http://127.0.0.1:8091"));
        const auto copyWith = [&](const QString &name, const QDateTime &when, int maxActive) {
            const QString path = dir.filePath(name);
            const QByteArray json = QbtSettingsExport::toJson(settingsOf(
                QStringLiteral("private"), privateServer.url.toString(), when, maxActive));
            [&] {
                QVERIFY(writeFile(
                    path,
                    backupWith(
                        {{QStringLiteral("qbittorrent/private-id.json"), json, 1760000000}})));
            }();
            return path;
        };
        const QString newer = copyWith(QStringLiteral("omaseed-2026-10-15.omaseed"), kThursday, 7);
        const QString older = copyWith(QStringLiteral("omaseed-2026-10-12.omaseed"), kMonday, 5);
        const QString empty = dir.filePath(QStringLiteral("omaseed-2026-10-16.omaseed"));
        QVERIFY(writeFile(empty, backupWith({})));

        // No session read: the newest copy that has it (the empty one passed over).
        std::optional<QbtSettingsSnapshot> s =
            QbtSettingsFetch::newestSaved(privateServer, {}, {empty, newer, older});
        QVERIFY(s);
        QCOMPARE(s->preferences.value(QStringLiteral("max_active_downloads")).toInt(), 7);
        QCOMPARE(s->created, kThursday);

        // A session read older than the copy loses to it; a newer one wins.
        QHash<QString, QByteArray> session{
            {QStringLiteral("private-id"),
             QbtSettingsExport::toJson(
                 settingsOf(QStringLiteral("private"), privateServer.url.toString(), kMonday, 9))}};
        s = QbtSettingsFetch::newestSaved(privateServer, session, {newer, older});
        QCOMPARE(s->preferences.value(QStringLiteral("max_active_downloads")).toInt(), 7);
        session[QStringLiteral("private-id")] = QbtSettingsExport::toJson(settingsOf(
            QStringLiteral("private"), privateServer.url.toString(), kThursday.addDays(1), 9));
        s = QbtSettingsFetch::newestSaved(privateServer, session, {newer, older});
        QCOMPARE(s->preferences.value(QStringLiteral("max_active_downloads")).toInt(), 9);

        // Nothing kept of a server: nothing.
        QVERIFY(!QbtSettingsFetch::newestSaved(
            server(QStringLiteral("x"), QStringLiteral("x"), QStringLiteral("http://x:1")),
            session,
            {newer, older}));
    }

    void reasonsInPlainWords()
    {
        QCOMPARE(QbtSettingsFetch::reasonText(
                     ApiError(ApiError::Kind::Network, QStringLiteral("Connection refused")),
                     AuthMode::None),
                 QStringLiteral("connection refused"));
        QCOMPARE(QbtSettingsFetch::reasonText(ApiError(ApiError::Kind::Timeout, QString()),
                                              AuthMode::Cookie),
                 QStringLiteral("no answer in time"));
        QCOMPARE(QbtSettingsFetch::reasonText(ApiError::fromHttpStatus(401), AuthMode::Cookie),
                 QStringLiteral("wrong username or password"));
        QCOMPARE(QbtSettingsFetch::reasonText(ApiError::fromHttpStatus(403), AuthMode::ApiKey),
                 QStringLiteral("the server refused this API key"));
    }

    // A server that isn't there: the read fails once, says why, and leaves nothing running.
    void fetchFromNowhereFails()
    {
        // A port nothing listens on: taken, then let go.
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        const quint16 port = probe.serverPort();
        probe.close();

        QbtSettingsFetch fetch;
        QSignalSpy fetched(&fetch, &QbtSettingsFetch::fetched);
        QSignalSpy failed(&fetch, &QbtSettingsFetch::failed);
        fetch.fetch(server(QStringLiteral("gone"),
                           QStringLiteral("gone"),
                           QStringLiteral("http://127.0.0.1:%1").arg(port)),
                    QString(),
                    QStringLiteral("test"));
        QVERIFY(fetch.isBusy());
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 15000);
        QCOMPARE(fetched.size(), 0);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("connection refused"));
        QVERIFY(!fetch.isBusy());
    }
};

QTEST_GUILESS_MAIN(TestBackupFormat)
#include "tst_backupformat.moc"
