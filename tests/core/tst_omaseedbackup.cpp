#include "core/AutoBackup.hpp"
#include "core/OmaseedBackup.hpp"
#include "core/SeedLedger.hpp"
#include "core/ServerStore.hpp"
#include "core/StateFile.hpp"
#include "core/TarArchive.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::AutoBackup;
using omaseed::core::OmaseedBackup;
using omaseed::core::SeedLedger;
using omaseed::core::Server;
using omaseed::core::ServerStore;
using omaseed::core::StateFile;
using omaseed::core::TarArchive;

namespace {

constexpr qint64 kNow = 1759500000; // 2025-10-03, a fixed "now"
constexpr qint64 kDay = 86400;

/// A ledger at `path` that began at `start` and has a minute of upload.
void makeLedger(const QString &path, qint64 start, bool keepOpen = false, SeedLedger *ledger = nullptr)
{
    SeedLedger own;
    SeedLedger &l = ledger ? *ledger : own;
    QVERIFY(l.open(path));
    l.observe(start, 0, 0, {});
    l.observe(start + 60, 6000, 0, {});
    l.flush(start + 60);
    if (!keepOpen)
        l.close();
}

Server server(const QString &id, const QString &name, const QString &url)
{
    Server s;
    s.id = id;
    s.name = name;
    s.url = QUrl(url);
    return s;
}

} // namespace

class TstOmaseedBackup : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;
    OmaseedBackup::Sources m_sources;

    /// A fresh, empty omaseed (config and data) under its own folder, the real paths pointed
    /// there by XDG_CONFIG_HOME / XDG_DATA_HOME.
    void freshHome(const QString &name)
    {
        const QString home = m_root.filePath(name);
        QDir().mkpath(home);
        qputenv("XDG_CONFIG_HOME", (home + QStringLiteral("/config")).toUtf8());
        qputenv("XDG_DATA_HOME", (home + QStringLiteral("/data")).toUtf8());
        m_sources = OmaseedBackup::defaultSources();
        m_sources.machine = QStringLiteral("desk");
        m_sources.appVersion = QStringLiteral("0.1.0");
    }

    /// Settings, two servers, history for the first: the backup tests' library.
    QByteArray backupOfALibrary(qint64 historyStart)
    {
        StateFile::writeSection(QStringLiteral("appearance"),
                                QJsonObject{{QStringLiteral("speedsAlignment"), QStringLiteral("right")}});
        ServerStore store;
        Server home = server(QStringLiteral("home-id"), QStringLiteral("home"), QStringLiteral("http://192.168.1.10:8080/"));
        home.pathMap = {{QStringLiteral("/data"), QStringLiteral("/mnt/media")}};
        store.add(home);
        store.add(server(QStringLiteral("box-id"), QStringLiteral("seedbox"), QStringLiteral("https://box.example:443")));
        store.setDefault(QStringLiteral("home-id"));
        makeLedger(m_sources.ledgerPath(QStringLiteral("home-id")), historyStart);
        QString error;
        const QByteArray archive = OmaseedBackup::create(m_sources, kNow, &error);
        [&] { QVERIFY2(!archive.isEmpty(), qPrintable(error)); }();
        return archive;
    }

private slots:
    void initTestCase() { QVERIFY(m_root.isValid()); }

    void roundTrip()
    {
        freshHome(QStringLiteral("roundtrip"));
        // A plain-file secret must never travel.
        QDir().mkpath(m_root.filePath(QStringLiteral("roundtrip/config/omaseed/secrets")));
        QFile secret(m_root.filePath(QStringLiteral("roundtrip/config/omaseed/secrets/home-id")));
        QVERIFY(secret.open(QIODevice::WriteOnly));
        secret.write("hunter2");
        secret.close();
        m_sources.extras = [] {
            return QList<TarArchive::Entry>{{QStringLiteral("qbittorrent/home-id.json"), "{}", kNow}};
        };

        const QByteArray archive = backupOfALibrary(kNow - 200 * kDay);
        QVERIFY(!archive.contains("hunter2"));
        QString error;
        const auto contents = OmaseedBackup::parse(archive, &error);
        QVERIFY2(contents.has_value(), qPrintable(error));
        QCOMPARE(contents->machine, QStringLiteral("desk"));
        QCOMPARE(contents->appVersion, QStringLiteral("0.1.0"));
        QCOMPARE(contents->created.toSecsSinceEpoch(), kNow);
        QVERIFY(contents->hasSettings);
        QCOMPARE(contents->state.value(QStringLiteral("appearance")).toObject()
                     .value(QStringLiteral("speedsAlignment")).toString(),
                 QStringLiteral("right"));
        QCOMPARE(contents->servers.size(), 2);
        QCOMPARE(contents->defaultId, QStringLiteral("home-id"));
        QVERIFY(contents->servers.at(0).hasLedger);
        QCOMPARE(contents->servers.at(0).history.start, kNow - 200 * kDay);
        QCOMPARE(contents->servers.at(0).history.days(kNow), 200);
        QVERIFY(!contents->servers.at(1).hasLedger);
        QCOMPARE(contents->servers.at(0).server.pathMap.size(), 1);
        QCOMPARE(contents->extras.size(), 1);
        QCOMPARE(contents->extras.constFirst().name, QStringLiteral("qbittorrent/home-id.json"));
    }

    void copiesAnOpenLedger()
    {
        // The app keeps its ledger open and writing; VACUUM INTO copies it whole anyway.
        freshHome(QStringLiteral("open"));
        ServerStore store;
        store.add(server(QStringLiteral("home-id"), QStringLiteral("home"), QStringLiteral("http://10.0.0.2:8080")));
        SeedLedger ledger;
        makeLedger(m_sources.ledgerPath(QStringLiteral("home-id")), kNow - 3 * kDay, true, &ledger);
        const auto contents = OmaseedBackup::parse(OmaseedBackup::create(m_sources, kNow));
        QVERIFY(contents.has_value());
        const QString copy = m_root.filePath(QStringLiteral("open/copy.sqlite"));
        QFile out(copy);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(contents->ledgers.value(QStringLiteral("home-id")));
        out.close();
        QCOMPARE(OmaseedBackup::historyOf(copy).start, kNow - 3 * kDay);
        ledger.close();
    }

    void refusesWhatIsNotABackup()
    {
        freshHome(QStringLiteral("refuse"));
        const QByteArray good = backupOfALibrary(kNow - 10 * kDay);
        QString error;

        QVERIFY(!OmaseedBackup::parse(QByteArray("just some bytes"), &error));
        QVERIFY(error.contains(QStringLiteral("not an omaseed backup")));

        // Another app's tar.
        QVERIFY(!OmaseedBackup::parse(TarArchive::write({{QStringLiteral("readme.txt"), "hi", 0}}), &error));
        QVERIFY(error.contains(QStringLiteral("no manifest")));
        QVERIFY(!OmaseedBackup::parse(
            TarArchive::write({{QStringLiteral("manifest.json"), R"({"format":"something-else","version":1})", 0}}),
            &error));

        // A newer omaseed's.
        QVERIFY(!OmaseedBackup::parse(
            TarArchive::write({{QStringLiteral("manifest.json"), R"({"format":"omaseed-backup","version":9})", 0}}),
            &error));
        QVERIFY(error.contains(QStringLiteral("newer")));

        // Damaged: cut short, and a history that is not SQLite.
        QVERIFY(!OmaseedBackup::parse(good.left(good.size() / 2), &error));
        auto entries = *TarArchive::read(good);
        for (auto &entry : entries) {
            if (entry.name.startsWith(QStringLiteral("ledger/")))
                entry.data = "garbage";
        }
        QVERIFY(!OmaseedBackup::parse(TarArchive::write(entries), &error));
        QVERIFY(error.contains(QStringLiteral("damaged")));
    }

    void addressKeys()
    {
        QCOMPARE(OmaseedBackup::addressKey(QUrl(QStringLiteral("http://192.168.1.10:8080/"))),
                 OmaseedBackup::addressKey(QUrl(QStringLiteral("http://192.168.1.10:8080"))));
        QCOMPARE(OmaseedBackup::addressKey(QUrl(QStringLiteral("https://Box.Example/"))),
                 OmaseedBackup::addressKey(QUrl(QStringLiteral("https://box.example:443"))));
        QVERIFY(OmaseedBackup::addressKey(QUrl(QStringLiteral("http://a:8080")))
                != OmaseedBackup::addressKey(QUrl(QStringLiteral("http://a:8081"))));
        QVERIFY(OmaseedBackup::addressKey(QUrl(QStringLiteral("http://a/qbt")))
                != OmaseedBackup::addressKey(QUrl(QStringLiteral("http://a/"))));
    }

    void restoresIntoAnotherMachine()
    {
        // Made on "desk", restored on a machine that knows home under another id and has 4 days
        // of its history: home is kept and takes the backup's path maps, seedbox is added, the
        // backup's 200 days replace the 4.
        freshHome(QStringLiteral("desk"));
        const QByteArray archive = backupOfALibrary(kNow - 200 * kDay);
        const auto contents = OmaseedBackup::parse(archive);
        QVERIFY(contents.has_value());

        freshHome(QStringLiteral("laptop"));
        StateFile::writeSection(QStringLiteral("appearance"),
                                QJsonObject{{QStringLiteral("speedsAlignment"), QStringLiteral("left")}});
        ServerStore store;
        store.add(server(QStringLiteral("laptop-home"), QStringLiteral("my home"), QStringLiteral("http://192.168.1.10:8080")));
        makeLedger(m_sources.ledgerPath(QStringLiteral("laptop-home")), kNow - 4 * kDay);

        const auto matches = OmaseedBackup::match(*contents, store.servers(), m_sources);
        QCOMPARE(matches.size(), 2);
        QCOMPARE(matches.at(0).localId, QStringLiteral("laptop-home"));
        QCOMPARE(matches.at(0).local.days(kNow), 4);
        QVERIFY(matches.at(0).backupLonger);
        QVERIFY(matches.at(1).localId.isEmpty());

        OmaseedBackup::Plan plan;
        plan.history = {QStringLiteral("home-id")};
        QString error;
        const auto outcome = OmaseedBackup::restore(*contents, plan, store, m_sources, kNow + 60, &error);
        QVERIFY2(outcome.has_value(), qPrintable(error));

        // The safety copy came first and holds what was here.
        QVERIFY(QFileInfo::exists(outcome->safetyCopy));
        QVERIFY(QFileInfo(outcome->safetyCopy).fileName().startsWith(QStringLiteral("before-restore-")));
        const auto before = OmaseedBackup::parseFile(outcome->safetyCopy);
        QVERIFY(before.has_value());
        QCOMPARE(before->servers.size(), 1);
        QCOMPARE(before->servers.constFirst().history.start, kNow - 4 * kDay);

        // Settings replaced.
        QCOMPARE(StateFile::readSection(QStringLiteral("appearance")).value(QStringLiteral("speedsAlignment")).toString(),
                 QStringLiteral("right"));
        // Servers: one kept (name kept, path map taken), one added under its own id.
        QCOMPARE(outcome->keptServers, QStringList{QStringLiteral("laptop-home")});
        QCOMPARE(outcome->addedServers, QStringList{QStringLiteral("box-id")});
        QCOMPARE(store.count(), 2);
        QCOMPARE(store.byId(QStringLiteral("laptop-home"))->name, QStringLiteral("my home"));
        QCOMPARE(store.byId(QStringLiteral("laptop-home"))->pathMap.size(), 1);
        QVERIFY(store.byId(QStringLiteral("box-id")).has_value());
        // History: the longer one won and opens as a ledger.
        QCOMPARE(outcome->historyRestored, QStringList{QStringLiteral("laptop-home")});
        SeedLedger reopened;
        QVERIFY(reopened.open(m_sources.ledgerPath(QStringLiteral("laptop-home"))));
        QCOMPARE(reopened.start(), kNow - 200 * kDay);
    }

    void longerHistoryHereStays()
    {
        freshHome(QStringLiteral("short-backup"));
        const auto contents = OmaseedBackup::parse(backupOfALibrary(kNow - 5 * kDay));
        QVERIFY(contents.has_value());

        freshHome(QStringLiteral("long-here"));
        ServerStore store;
        store.add(server(QStringLiteral("home-id"), QStringLiteral("home"), QStringLiteral("http://192.168.1.10:8080")));
        makeLedger(m_sources.ledgerPath(QStringLiteral("home-id")), kNow - 300 * kDay);
        const auto matches = OmaseedBackup::match(*contents, store.servers(), m_sources);
        QVERIFY(!matches.at(0).backupLonger);

        OmaseedBackup::Plan plan;
        plan.history = {QStringLiteral("home-id")};
        const auto outcome = OmaseedBackup::restore(*contents, plan, store, m_sources, kNow);
        QVERIFY(outcome.has_value());
        QCOMPARE(outcome->historyKept, QStringList{QStringLiteral("home-id")});
        QCOMPARE(OmaseedBackup::historyOf(m_sources.ledgerPath(QStringLiteral("home-id"))).start, kNow - 300 * kDay);
    }

    void onlyTheTickedParts()
    {
        freshHome(QStringLiteral("parts-from"));
        const auto contents = OmaseedBackup::parse(backupOfALibrary(kNow - 50 * kDay));
        QVERIFY(contents.has_value());

        freshHome(QStringLiteral("parts-to"));
        StateFile::writeSection(QStringLiteral("appearance"),
                                QJsonObject{{QStringLiteral("speedsAlignment"), QStringLiteral("left")}});
        ServerStore store;
        OmaseedBackup::Plan plan;
        plan.settings = false;
        plan.servers = false;
        plan.history = {QStringLiteral("home-id")}; // its server is neither here nor restored
        const auto outcome = OmaseedBackup::restore(*contents, plan, store, m_sources, kNow);
        QVERIFY(outcome.has_value());
        QCOMPARE(store.count(), 0);
        QVERIFY(outcome->historyRestored.isEmpty());
        QCOMPARE(StateFile::readSection(QStringLiteral("appearance")).value(QStringLiteral("speedsAlignment")).toString(),
                 QStringLiteral("left"));
        QVERIFY(!QFileInfo::exists(m_sources.ledgerPath(QStringLiteral("home-id"))));
    }

    void anIdTakenHereGetsANewOne()
    {
        freshHome(QStringLiteral("ids-from"));
        const auto contents = OmaseedBackup::parse(backupOfALibrary(kNow - 50 * kDay));
        QVERIFY(contents.has_value());

        freshHome(QStringLiteral("ids-to"));
        ServerStore store;
        store.add(server(QStringLiteral("home-id"), QStringLiteral("other"), QStringLiteral("http://elsewhere:9090")));
        OmaseedBackup::Plan plan;
        plan.history = {QStringLiteral("home-id")};
        const auto outcome = OmaseedBackup::restore(*contents, plan, store, m_sources, kNow);
        QVERIFY(outcome.has_value());
        QCOMPARE(store.count(), 3);
        QCOMPARE(store.byId(QStringLiteral("home-id"))->name, QStringLiteral("other"));
        QCOMPARE(outcome->addedServers.size(), 2);
        const QString newHome = outcome->addedServers.constFirst();
        QVERIFY(newHome != QStringLiteral("home-id"));
        QCOMPARE(outcome->historyRestored, QStringList{newHome});
        // The existing server's own history file was left alone.
        QVERIFY(!QFileInfo::exists(m_sources.ledgerPath(QStringLiteral("home-id"))));
    }

    void nothingChangesWhenTheSafetyCopyFails()
    {
        freshHome(QStringLiteral("nosafety-from"));
        const auto contents = OmaseedBackup::parse(backupOfALibrary(kNow - 50 * kDay));
        QVERIFY(contents.has_value());

        freshHome(QStringLiteral("nosafety-to"));
        StateFile::writeSection(QStringLiteral("appearance"),
                                QJsonObject{{QStringLiteral("speedsAlignment"), QStringLiteral("left")}});
        // The backups "folder" is a file: no copy can be written there.
        QDir().mkpath(QFileInfo(m_sources.backupsDir).absolutePath());
        QFile blocker(m_sources.backupsDir);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        ServerStore store;
        QString error;
        QVERIFY(!OmaseedBackup::restore(*contents, OmaseedBackup::Plan{}, store, m_sources, kNow, &error));
        QVERIFY(error.contains(QStringLiteral("Nothing was restored")));
        QCOMPARE(store.count(), 0);
        QCOMPARE(StateFile::readSection(QStringLiteral("appearance")).value(QStringLiteral("speedsAlignment")).toString(),
                 QStringLiteral("left"));
    }

    void automaticCopiesKeepFour()
    {
        freshHome(QStringLiteral("auto"));
        backupOfALibrary(kNow - 30 * kDay);
        AutoBackup copies;
        copies.setSources(m_sources);
        QCOMPARE(copies.mode(), QStringLiteral("weekly"));
        int flushed = 0;
        copies.setBeforeCopy([&flushed] { ++flushed; });
        QSignalSpy copied(&copies, &AutoBackup::copied);

        // A safety copy is not one of the automatic ones and is never removed.
        QDir().mkpath(m_sources.backupsDir);
        const QString safety = OmaseedBackup::datedName(m_sources.backupsDir, QStringLiteral("before-restore"), kNow);
        QFile safetyFile(safety);
        QVERIFY(safetyFile.open(QIODevice::WriteOnly));
        safetyFile.close();

        QVERIFY(copies.runIfDue(kNow));
        QVERIFY(!copies.runIfDue(kNow + kDay)); // weekly: not yet
        qint64 t = kNow;
        for (int i = 0; i < 5; ++i) {
            t += 7 * kDay;
            QVERIFY(copies.runIfDue(t));
        }
        QCOMPARE(copied.count(), 6);
        QCOMPARE(flushed, 6);
        const QStringList kept = copies.copies();
        QCOMPARE(kept.size(), AutoBackup::kKeep);
        QCOMPARE(copies.lastCopyTime().toSecsSinceEpoch(), t);
        QVERIFY(QFileInfo(kept.constFirst()).fileName() > QFileInfo(kept.constLast()).fileName());
        QVERIFY(QFileInfo::exists(safety));
        QVERIFY(OmaseedBackup::parseFile(kept.constFirst()).has_value());

        copies.setMode(QStringLiteral("daily"));
        QVERIFY(copies.runIfDue(t + kDay));
        copies.setMode(QStringLiteral("off"));
        QVERIFY(!copies.runIfDue(t + 30 * kDay));
        copies.setMode(QStringLiteral("hourly")); // unknown: weekly
        QCOMPARE(copies.mode(), QStringLiteral("weekly"));
    }

    void aFailedCopyIsReportedOnceAndCostsNothing()
    {
        freshHome(QStringLiteral("autofail"));
        backupOfALibrary(kNow - 30 * kDay);
        AutoBackup copies;
        copies.setSources(m_sources);
        QVERIFY(copies.runIfDue(kNow));
        const QStringList before = copies.copies();

        // The folder turns read-only (as good as a full disk for QSaveFile).
        QFile::setPermissions(m_sources.backupsDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        QSignalSpy failed(&copies, &AutoBackup::failed);
        QVERIFY(!copies.runIfDue(kNow + 8 * kDay));
        QVERIFY(!copies.runIfDue(kNow + 9 * kDay));
        QCOMPARE(failed.count(), 1);
        QFile::setPermissions(m_sources.backupsDir,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QCOMPARE(copies.copies(), before);

        // A copy that works clears the report, so the next failure is told again.
        QVERIFY(copies.runIfDue(kNow + 10 * kDay));
        QFile::setPermissions(m_sources.backupsDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        QVERIFY(!copies.runIfDue(kNow + 18 * kDay));
        QCOMPARE(failed.count(), 2);
        QFile::setPermissions(m_sources.backupsDir,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
};

QTEST_GUILESS_MAIN(TstOmaseedBackup)
#include "tst_omaseedbackup.moc"
