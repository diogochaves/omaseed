// DECISIONS C47 I8–I10 on #271's facts: each engine's temp folder in one shape, where an add
// ends by default, and which servers take a torrent's room at once.

#include "core/TempFolder.hpp"

#include <QTest>

using omaseed::core::EngineKind;
using omaseed::core::TempFolder;
namespace TempFolders = omaseed::core::TempFolders;

class TestTempFolder : public QObject
{
    Q_OBJECT

private slots:
    void qbittorrentOnAndOff();
    void transmissionIsSessionWideOnly();
    void delugeDownloadsToDownloadLocation();
    void preallocatesPerEngine();
};

void TestTempFolder::qbittorrentOnAndOff()
{
    QVariantMap prefs{{QStringLiteral("save_path"), QStringLiteral("/data/done")},
                      {QStringLiteral("temp_path_enabled"), true},
                      {QStringLiteral("temp_path"), QStringLiteral("/fast/temp")}};
    QCOMPARE(TempFolders::fromPreferences(prefs, EngineKind::Qbittorrent),
             (TempFolder{true, QStringLiteral("/fast/temp"), true}));
    QCOMPARE(TempFolders::defaultSavePath(prefs, EngineKind::Qbittorrent),
             QStringLiteral("/data/done"));

    prefs.insert(QStringLiteral("temp_path_enabled"), false);
    const TempFolder off = TempFolders::fromPreferences(prefs, EngineKind::Qbittorrent);
    QVERIFY(!off.on);
    QVERIFY(off.perTorrent); // #271 (1): an add can still ask for one of its own

    // On with no folder said is no folder.
    prefs.insert(QStringLiteral("temp_path_enabled"), true);
    prefs.remove(QStringLiteral("temp_path"));
    QVERIFY(!TempFolders::fromPreferences(prefs, EngineKind::Qbittorrent).on);
    // An engine not known yet has none.
    QCOMPARE(TempFolders::fromPreferences(prefs, EngineKind::Unknown), TempFolder());
}

void TestTempFolder::transmissionIsSessionWideOnly()
{
    // incomplete_dir(_enabled) arrive translated into qBittorrent's keys; #271 (5): torrent_add
    // ignores one, so no add can change it.
    const QVariantMap prefs{{QStringLiteral("save_path"), QStringLiteral("/srv/films")},
                            {QStringLiteral("temp_path_enabled"), true},
                            {QStringLiteral("temp_path"), QStringLiteral("/srv/incomplete")}};
    QCOMPARE(TempFolders::fromPreferences(prefs, EngineKind::Transmission),
             (TempFolder{true, QStringLiteral("/srv/incomplete"), false}));
    QCOMPARE(TempFolders::defaultSavePath(prefs, EngineKind::Transmission),
             QStringLiteral("/srv/films"));
}

void TestTempFolder::delugeDownloadsToDownloadLocation()
{
    // #271 (4): download_location (save_path here) is where it downloads; with move_completed on,
    // it ends in move_completed_path.
    QVariantMap prefs{{QStringLiteral("save_path"), QStringLiteral("/srv/deluge/downloads")},
                      {QStringLiteral("move_completed"), true},
                      {QStringLiteral("move_completed_path"), QStringLiteral("/srv/deluge/done")}};
    QCOMPARE(TempFolders::fromPreferences(prefs, EngineKind::Deluge),
             (TempFolder{true, QStringLiteral("/srv/deluge/downloads"), true}));
    QCOMPARE(TempFolders::defaultSavePath(prefs, EngineKind::Deluge),
             QStringLiteral("/srv/deluge/done"));

    prefs.insert(QStringLiteral("move_completed"), false);
    QVERIFY(!TempFolders::fromPreferences(prefs, EngineKind::Deluge).on);
    QCOMPARE(TempFolders::defaultSavePath(prefs, EngineKind::Deluge),
             QStringLiteral("/srv/deluge/downloads"));
}

void TestTempFolder::preallocatesPerEngine()
{
    const QVariantMap on{{QStringLiteral("preallocate_all"), true},
                         {QStringLiteral("pre_allocate_storage"), true}};
    QVERIFY(TempFolders::preallocates(on, EngineKind::Qbittorrent));
    QVERIFY(TempFolders::preallocates(on, EngineKind::Deluge));
    QVERIFY(!TempFolders::preallocates(on, EngineKind::Transmission)); // #271 (6)
    QVERIFY(!TempFolders::preallocates(QVariantMap(), EngineKind::Qbittorrent));
    QVERIFY(!TempFolders::preallocates({{QStringLiteral("preallocate_all"), true}},
                                       EngineKind::Deluge)); // each engine its own key
}

QTEST_APPLESS_MAIN(TestTempFolder)

#include "tst_tempfolder.moc"
