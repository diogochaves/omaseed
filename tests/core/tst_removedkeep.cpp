// RemovedKeep (docs/DECISIONS.md C50 GT6; #295): a removed torrent kept as a one-torrent
// backup file under $XDG_DATA_HOME/omaseed/removed/<yyyymmdd>-<hash8>/, private, listed with
// its server and old place, pruned 30 days on, read back stopped for adding back. A folder that
// isn't one torrent is skipped, and pruned by the day in its name.

#include "core/RemovedKeep.hpp"
#include "core/TorrentFile.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace omaseed::core;

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

TorrentsManifest manifestAt(const QDateTime &at)
{
    TorrentsManifest m;
    m.created = at;
    m.appVersion = QStringLiteral("0.17.0");
    m.serverName = QStringLiteral("attic");
    m.serverUrl = QStringLiteral("http://127.0.0.1:8085/transmission/rpc");
    m.engine = EngineKind::Transmission;
    m.version = QStringLiteral("4.1.3");
    return m;
}

TorrentRecord recordOf(const QByteArray &torrent)
{
    const TorrentFile parsed = TorrentFile::parse(torrent);
    TorrentRecord r;
    r.id = parsed.infoHashV1;
    r.infohashV1 = parsed.infoHashV1;
    r.name = parsed.name;
    r.savePath = QStringLiteral("/data/old place");
    r.state = QStringLiteral("started");
    r.tags = {QStringLiteral("keep")};
    return r;
}

const QDateTime kNov21 = QDateTime(QDate(2026, 11, 21), QTime(15, 20), QTimeZone::UTC);

} // namespace

class TestRemovedKeep : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("XDG_DATA_HOME", m_home->path().toUtf8());
        m_torrent = fixture(QStringLiteral("single_file_v1.torrent"));
        QVERIFY(!m_torrent.isEmpty());
    }

    void keepsOneTorrentPrivately()
    {
        QCOMPARE(RemovedKeep::root(), m_home->path() + QStringLiteral("/omaseed/removed"));
        const TorrentRecord record = recordOf(m_torrent);
        QString error;
        const auto kept = RemovedKeep::keep(manifestAt(kNov21), record, m_torrent, &error);
        QVERIFY2(kept.has_value(), qPrintable(error));
        QCOMPARE(kept->id, QStringLiteral("20261121-") + record.id.left(8));
        QCOMPARE(kept->path,
                 RemovedKeep::root() + QLatin1Char('/') + kept->id + QStringLiteral("/") +
                     RemovedKeep::kFile);
        QCOMPARE(kept->hash, record.id);
        QCOMPARE(kept->name, record.name);
        QCOMPARE(kept->savePath, QStringLiteral("/data/old place"));
        QCOMPARE(kept->serverName, QStringLiteral("attic"));
        QCOMPARE(kept->serverUrl, QStringLiteral("http://127.0.0.1:8085/transmission/rpc"));
        QCOMPARE(kept->engine, EngineKind::Transmission);
        QCOMPARE(kept->removedAt, kNov21);
        QCOMPARE(kept->keptUntil, kNov21.addDays(30));
        QVERIFY(!kept->link);
        // C9: a passkey may be in it.
        QCOMPARE(QFileInfo(kept->path).permissions() &
                     (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup |
                      QFileDevice::WriteOther),
                 QFileDevice::Permissions());
        QCOMPARE(QFileInfo(QFileInfo(kept->path).path()).permissions() &
                     (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::ExeGroup |
                      QFileDevice::ExeOther),
                 QFileDevice::Permissions());
        // The same format Backup writes: one record, its .torrent beside it.
        const auto contents = TorrentsBackup::read(kept->path, &error);
        QVERIFY2(contents.has_value(), qPrintable(error));
        QCOMPARE(contents->manifest.count, 1);
        QCOMPARE(contents->records.size(), 1);
        QVERIFY(contents->torrents.contains(record.id));

        // The same torrent again that day: a folder of its own.
        const auto again = RemovedKeep::keep(manifestAt(kNov21.addSecs(60)), record, m_torrent);
        QVERIFY(again.has_value());
        QCOMPARE(again->id, kept->id + QStringLiteral("-2"));
        const QList<RemovedKeep::Entry> all = RemovedKeep::list();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all.at(0).id, again->id); // newest first
        QCOMPARE(all.at(1), *kept);
    }

    void aLinkKeepsItsMagnet()
    {
        TorrentRecord record = recordOf(m_torrent);
        record.magnet = QStringLiteral("magnet:?xt=urn:btih:%1").arg(record.id);
        const auto kept = RemovedKeep::keep(manifestAt(kNov21), record, {});
        QVERIFY(kept.has_value());
        QVERIFY(kept->link);
        // Without a magnet, no .torrent is no way back.
        record.magnet.clear();
        QString error;
        QVERIFY(!RemovedKeep::keep(manifestAt(kNov21), record, {}, &error).has_value());
        QVERIFY(!error.isEmpty());
        QCOMPARE(RemovedKeep::list().size(), 1);
    }

    void prunesAfterThirtyDays()
    {
        const TorrentRecord record = recordOf(m_torrent);
        const auto old = RemovedKeep::keep(manifestAt(kNov21.addDays(-31)), record, m_torrent);
        const auto edge = RemovedKeep::keep(manifestAt(kNov21.addDays(-29)), record, m_torrent);
        const auto fresh = RemovedKeep::keep(manifestAt(kNov21), record, m_torrent);
        QVERIFY(old && edge && fresh);
        QCOMPARE(RemovedKeep::prune(kNov21), 1);
        QStringList ids;
        for (const RemovedKeep::Entry &e : RemovedKeep::list())
            ids << e.id;
        QCOMPARE(ids, (QStringList{fresh->id, edge->id}));
        QCOMPARE(RemovedKeep::prune(kNov21.addDays(30).addSecs(1)), 2);
        QCOMPARE(RemovedKeep::list().size(), 0);
    }

    void aBrokenFolderIsSkippedAndPrunedByItsName()
    {
        const TorrentRecord record = recordOf(m_torrent);
        QVERIFY(RemovedKeep::keep(manifestAt(kNov21), record, m_torrent).has_value());
        QDir root(RemovedKeep::root());
        // Damaged, from long ago; damaged, recent; a folder that was never ours.
        for (const QString &id : {QStringLiteral("20261001-deadbeef"),
                                  QStringLiteral("20261120-deadbeef"),
                                  QStringLiteral("mine")}) {
            QVERIFY(root.mkdir(id));
            QFile junk(root.filePath(id + QLatin1Char('/') + RemovedKeep::kFile));
            QVERIFY(junk.open(QIODevice::WriteOnly));
            junk.write("not a tar");
        }
        QCOMPARE(RemovedKeep::list().size(), 1);
        QVERIFY(!RemovedKeep::entry(QStringLiteral("20261120-deadbeef")).has_value());
        QVERIFY(!RemovedKeep::take(QStringLiteral("20261120-deadbeef")).has_value());
        QCOMPARE(RemovedKeep::prune(kNov21), 1);
        QVERIFY(!root.exists(QStringLiteral("20261001-deadbeef")));
        QVERIFY(root.exists(QStringLiteral("20261120-deadbeef")));
        QVERIFY(root.exists(QStringLiteral("mine")));
        QVERIFY(!RemovedKeep::forget(QStringLiteral("mine")));
        QVERIFY(!RemovedKeep::forget(QStringLiteral("../omaseed")));
    }

    void takeReadsItStoppedAtItsPlace()
    {
        const TorrentRecord record = recordOf(m_torrent);
        QVERIFY(record.started());
        const auto kept = RemovedKeep::keep(manifestAt(kNov21), record, m_torrent);
        QVERIFY(kept.has_value());
        QString error;
        const auto contents = RemovedKeep::take(kept->id, &error);
        QVERIFY2(contents.has_value(), qPrintable(error));
        QCOMPARE(contents->records.size(), 1);
        QCOMPARE(contents->records.at(0).state, QStringLiteral("stopped"));
        QCOMPARE(contents->records.at(0).savePath, QStringLiteral("/data/old place"));
        QCOMPARE(contents->records.at(0).tags, QStringList{QStringLiteral("keep")});
        QCOMPARE(contents->manifest.serverName, QStringLiteral("attic"));
        // Taking reads; forgetting deletes.
        QVERIFY(RemovedKeep::entry(kept->id).has_value());
        QVERIFY(RemovedKeep::forget(kept->id));
        QVERIFY(!RemovedKeep::take(kept->id, &error).has_value());
        QVERIFY(!QFileInfo::exists(QFileInfo(kept->path).path()));
    }

    void prepareGivesAnEmptyFolderForAnExporter()
    {
        const QString hash = QString(40, QLatin1Char('a'));
        QString error;
        const QString path = RemovedKeep::prepare(hash.toUpper(), kNov21, &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QCOMPARE(QFileInfo(path).fileName(), RemovedKeep::kFile);
        QCOMPARE(QFileInfo(path).dir().dirName(), QStringLiteral("20261121-aaaaaaaa"));
        QVERIFY(!QFileInfo::exists(path));
        // An empty folder is no entry until the exporter has written it.
        QVERIFY(RemovedKeep::list().isEmpty());
        QVERIFY(RemovedKeep::prepare(QStringLiteral("nothex"), kNov21, &error).isEmpty());
    }

private:
    std::unique_ptr<QTemporaryDir> m_home;
    QByteArray m_torrent;
};

QTEST_GUILESS_MAIN(TestRemovedKeep)
#include "tst_removedkeep.moc"
