// TransmissionTorrentsImport (#88, docs/DECISIONS.md C40 X4–X7, C9): a TorrentsBackup file laid
// onto a Transmission, against the tests' fake daemon (FakeTransmission) given a small state
// of its own: the torrents it was handed, their files as it names them, a check that takes a
// poll. The plan, each torrent_add argument, the torrent_set that follows, labels from a
// qBittorrent's file and from a Transmission's, a duplicate, renames before a verify, the
// rename that would replace a file, names Transmission can't give, start only when complete
// and started, "leave them stopped", a link and its later finishing, stop and resume through
// the journal, the daemon gone mid-run, and what can't be kept in the outcome.

#include "FakeTransmission.hpp"

#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/transmission/TransmissionEngine.hpp"
#include "core/engines/transmission/TransmissionRpc.hpp"
#include "core/engines/transmission/TransmissionTorrentsImport.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace fakehttp;
using namespace faketransmission;
using namespace omaseed::core;

namespace {

using Import = TransmissionTorrentsImport;

QByteArray torrentBytes(const char *name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

const char *const kPack = "multi_file_v1_unicode.torrent";
const char *const kSingle = "single_file_v1.torrent";
const char *const kNested = "nested_library_v1.torrent";
const QString kLinkHash = QStringLiteral("88").repeated(20);

/// A record for the torrent of `fixtureName`: complete and started, in /data.
TorrentRecord recordOf(const char *fixtureName)
{
    const TorrentFile parsed = TorrentFile::parse(torrentBytes(fixtureName));
    if (!parsed.isValid())
        qFatal("fixture %s does not parse", fixtureName);
    TorrentRecord r;
    r.id = parsed.torrentId();
    r.infohashV1 = parsed.infoHashV1;
    r.name = parsed.name;
    r.size = parsed.totalSize;
    r.progress = 1;
    r.savePath = QStringLiteral("/data");
    r.state = QStringLiteral("started");
    return r;
}

TorrentRecord linkRecord()
{
    TorrentRecord r;
    r.id = kLinkHash;
    r.infohashV1 = kLinkHash;
    r.name = QStringLiteral("a link");
    r.savePath = QStringLiteral("/data");
    r.state = QStringLiteral("started");
    r.magnet = QStringLiteral("magnet:?xt=urn:btih:%1&dn=a+link").arg(kLinkHash);
    return r;
}

QStringList namesOf(const char *fixtureName)
{
    return TorrentsBackup::namesUnder(TorrentFile::parse(torrentBytes(fixtureName)),
                                      QStringLiteral("Original"));
}

QStringList strings(const QJsonValue &array)
{
    QStringList out;
    for (const QJsonValue &v : array.toArray())
        out.append(v.toString());
    return out;
}

QList<int> ints(const QJsonValue &array)
{
    QList<int> out;
    for (const QJsonValue &v : array.toArray())
        out.append(v.toInt());
    return out;
}

/// One torrent as the fake daemon holds it.
struct Held
{
    QString name;
    QStringList files;
    QString downloadDir;
    QStringList labels;
    bool metadata = true;
    bool running = false;
    int checks = 1;     ///< polls it still shows as checking: one on a paused add
    qint64 left = 0;    ///< left_until_done once checked
    QJsonObject params; ///< its torrent_add
};

} // namespace

class TestTransmissionTorrentsImport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void thePlanHasNothingToCreate();
    void labelsTheCategoryFirst();
    void theAddsParams();
    void whatTheAddIgnoresGoesInASet();
    void renameStepsTopFolderFirst();
    void namesTransmissionCantGive();

    void addsPausedThenSetsLimitsAndStartsTheComplete();
    void aQbittorrentsCategoryIsTheFirstLabel();
    void aDuplicateIsAlreadyThere();
    void aTorrentTheDaemonRefusesIsNamed();
    void renamesAreSentBeforeAVerify();
    void aRenameThatWouldReplaceAFileIsNotSent();
    void aTorrentWithoutItsTopFolderIsLeftStopped();
    void leaveThemStoppedChecksNothing();
    void theQueueIsPutBackInOrder();
    void aLinkWaitsInTheJournalAndIsFinishedLater();
    void stopAndResumeThroughTheJournal();
    void aDaemonGoneKeepsTheJournal();

private:
    QJsonObject answer(const QString &method, const QJsonObject &params);
    /// Writes the file and reads it back; `torrents` are fixture names by record id.
    TorrentsBackup::Contents write(const QList<TorrentRecord> &records,
                                   const QHash<QString, const char *> &torrents,
                                   EngineKind engine = EngineKind::Transmission);
    Import::Options options() const;
    Import::Outcome run(const TorrentsBackup::Contents &contents,
                        const Import::Plan &plan,
                        const Import::Options &options,
                        const std::function<void(Import &, const QString &, int)> &onProgress = {});
    Import::Outcome finishLinks(const Import::Options &options);
    QList<Call> callsOf(const QString &method) const;
    QStringList methodsFor(const QString &hash) const;

    std::unique_ptr<FakeServer> m_server;
    std::unique_ptr<FakeTransmission> m_daemon;
    std::unique_ptr<TransmissionEngine> m_engine;
    QTemporaryDir m_dir;
    QHash<QString, Held> m_held;
    QStringList m_order;             ///< the daemon's queue
    QHash<QString, qint64> m_leftOf; ///< hash -> left_until_done its check finds
    QStringList m_refuse;            ///< torrent names whose add the daemon refuses
    bool m_down = false;             ///< every request answers 500
};

void TestTransmissionTorrentsImport::init()
{
    QVERIFY(m_dir.isValid());
    QDir(m_dir.filePath(QStringLiteral("imports"))).removeRecursively();
    m_held.clear();
    m_order.clear();
    m_leftOf.clear();
    m_refuse.clear();
    m_down = false;
    m_server = std::make_unique<FakeServer>();
    m_daemon = std::make_unique<FakeTransmission>();
    m_daemon->answer = [this](const QString &method, const QJsonObject &params) {
        return answer(method, params);
    };
    const FakeServer::Handler daemon = m_daemon->handler();
    m_server->setHandler([this, daemon](const Recorded &request, int n) {
        if (m_down)
            return httpResponse(500, "Internal Server Error", "down");
        return daemon(request, n);
    });
    m_engine = std::make_unique<TransmissionEngine>();
    m_engine->setBaseUrl(m_server->url());
    m_engine->setTimeout(std::chrono::seconds(5));
    m_engine->rpc()->setMaxRetries(0);
}

void TestTransmissionTorrentsImport::cleanup()
{
    m_engine.reset();
    m_daemon.reset();
    m_server.reset();
}

QJsonObject TestTransmissionTorrentsImport::answer(const QString &method, const QJsonObject &params)
{
    const QStringList ids = strings(params.value(QStringLiteral("ids")));
    if (method == QLatin1String("session_get"))
        return {{QStringLiteral("units"), QJsonObject{{QStringLiteral("speed_bytes"), 1000}}}};
    if (method == QLatin1String("torrent_add")) {
        Held held;
        QString hash;
        if (params.contains(QStringLiteral("metainfo"))) {
            const TorrentFile parsed = TorrentFile::parse(QByteArray::fromBase64(
                params.value(QStringLiteral("metainfo")).toString().toLatin1()));
            hash = parsed.infoHashV1;
            held.name = parsed.name;
            held.files = TorrentsBackup::namesUnder(parsed, QStringLiteral("Original"));
        } else {
            const QString link = params.value(QStringLiteral("filename")).toString();
            hash = link.section(QStringLiteral("btih:"), 1).left(40);
            held.name = hash;
            held.metadata = false;
            held.checks = 0;
        }
        if (m_refuse.contains(held.name))
            return {{QStringLiteral("__error"),
                     fixture("error_not_absolute.json").value(QStringLiteral("error"))}};
        const QJsonObject said{{QStringLiteral("hash_string"), hash},
                               {QStringLiteral("id"), 1},
                               {QStringLiteral("name"), held.name}};
        if (m_held.contains(hash))
            return {{QStringLiteral("torrent_duplicate"), said}};
        held.downloadDir = params.value(QStringLiteral("download_dir")).toString();
        held.labels = strings(params.value(QStringLiteral("labels")));
        held.left = m_leftOf.value(hash, 0);
        held.params = params;
        m_held.insert(hash, held);
        m_order.append(hash);
        return {{QStringLiteral("torrent_added"), said}};
    }
    if (method == QLatin1String("torrent_get")) {
        const QStringList fields = strings(params.value(QStringLiteral("fields")));
        QJsonArray rows;
        for (const QString &hash : std::as_const(m_order)) {
            if (params.contains(QStringLiteral("ids")) && !ids.contains(hash))
                continue;
            Held &held = m_held[hash];
            QJsonObject row{{QStringLiteral("hash_string"), hash}};
            if (fields.contains(QStringLiteral("files"))) {
                QJsonArray files;
                for (const QString &name : std::as_const(held.files))
                    files.append(QJsonObject{{QStringLiteral("name"), name}});
                row.insert(QStringLiteral("files"), files);
            }
            if (fields.contains(QStringLiteral("metadata_percent_complete")))
                row.insert(QStringLiteral("metadata_percent_complete"), held.metadata ? 1 : 0);
            if (fields.contains(QStringLiteral("status"))) {
                const bool checking = held.checks > 0;
                if (checking)
                    --held.checks;
                row.insert(QStringLiteral("status"), checking ? 2 : held.running ? 6 : 0);
                row.insert(QStringLiteral("left_until_done"), checking ? 1000 : held.left);
            }
            rows.append(row);
        }
        return {{QStringLiteral("torrents"), rows}};
    }
    if (method == QLatin1String("torrent_rename_path")) {
        const QString path = params.value(QStringLiteral("path")).toString();
        const QString name = params.value(QStringLiteral("name")).toString();
        const QString to = path.left(path.lastIndexOf(QLatin1Char('/')) + 1) + name;
        for (const QString &hash : ids) {
            for (QString &file : m_held[hash].files) {
                if (file == path)
                    file = to;
                else if (file.startsWith(path + QLatin1Char('/')))
                    file = to + file.mid(path.size());
            }
        }
        return {{QStringLiteral("path"), path}, {QStringLiteral("name"), name}};
    }
    if (method == QLatin1String("torrent_verify")) {
        for (const QString &hash : ids)
            m_held[hash].checks = 1;
    } else if (method == QLatin1String("torrent_start")) {
        for (const QString &hash : ids)
            m_held[hash].running = true;
    } else if (method == QLatin1String("torrent_set") &&
               params.contains(QStringLiteral("queue_position"))) {
        for (const QString &hash : ids) {
            m_order.removeAll(hash);
            m_order.insert(
                qBound(0, params.value(QStringLiteral("queue_position")).toInt(), m_order.size()),
                hash);
        }
    }
    return {};
}

TorrentsBackup::Contents
TestTransmissionTorrentsImport::write(const QList<TorrentRecord> &records,
                                      const QHash<QString, const char *> &torrents,
                                      EngineKind engine)
{
    TorrentsManifest manifest;
    manifest.created = QDateTime(QDate(2026, 11, 11), QTime(10, 0), QTimeZone::UTC);
    manifest.appVersion = QStringLiteral("test");
    manifest.serverName = QStringLiteral("source");
    manifest.serverUrl = QStringLiteral("http://source:9091");
    manifest.engine = engine;
    manifest.version = QStringLiteral("4.1.3");
    manifest.savePath = QStringLiteral("/data");
    manifest.count = int(records.size());
    const QString path = m_dir.filePath(QStringLiteral("t.omaseed-torrents"));
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            qFatal("cannot write %s", qPrintable(path));
        TarWriter writer(&file);
        bool ok =
            writer.add(TorrentsBackup::kManifest, QJsonDocument(manifest.toJson()).toJson(), 0);
        for (const TorrentRecord &r : records) {
            ok = ok && writer.add(TorrentsBackup::recordEntry(r.id),
                                  QJsonDocument(r.toJson()).toJson(),
                                  0);
            if (!r.isMagnet())
                ok = ok && writer.add(TorrentsBackup::torrentEntry(r.id),
                                      torrentBytes(torrents.value(r.id)),
                                      0);
        }
        if (!ok || !writer.finish())
            qFatal("cannot write %s", qPrintable(path));
    }
    QString error;
    const auto contents = TorrentsBackup::read(path, &error);
    if (!contents)
        qFatal("cannot read the file back: %s", qPrintable(error));
    return *contents;
}

Import::Options TestTransmissionTorrentsImport::options() const
{
    Import::Options o;
    o.journalPath = m_dir.filePath(QStringLiteral("imports/fake.json"));
    o.pollMs = 10;
    return o;
}

Import::Outcome TestTransmissionTorrentsImport::run(
    const TorrentsBackup::Contents &contents,
    const Import::Plan &plan,
    const Import::Options &options,
    const std::function<void(Import &, const QString &, int)> &onProgress)
{
    Import import(m_engine.get());
    QSignalSpy finished(&import, &Import::finished);
    if (onProgress)
        connect(&import, &Import::progress, &import, [&](const QString &phase, int done, int) {
            onProgress(import, phase, done);
        });
    import.start(contents, plan, options);
    if (finished.isEmpty() && !finished.wait(20000))
        qFatal("the import never finished");
    return finished.at(0).at(0).value<Import::Outcome>();
}

Import::Outcome TestTransmissionTorrentsImport::finishLinks(const Import::Options &options)
{
    Import import(m_engine.get());
    QSignalSpy finished(&import, &Import::finished);
    import.finishLinks(options);
    if (!finished.wait(20000))
        qFatal("finishLinks never finished");
    return finished.at(0).at(0).value<Import::Outcome>();
}

QList<Call> TestTransmissionTorrentsImport::callsOf(const QString &method) const
{
    QList<Call> out;
    for (const Call &call : m_daemon->calls) {
        if (call.method == method)
            out.append(call);
    }
    return out;
}

QStringList TestTransmissionTorrentsImport::methodsFor(const QString &hash) const
{
    QStringList out;
    for (const Call &call : m_daemon->calls) {
        if (call.method != QLatin1String("torrent_get") &&
            strings(call.params.value(QStringLiteral("ids"))).contains(hash))
            out.append(call.method);
    }
    return out;
}

// ---- the pure half --------------------------------------------------------------------------

void TestTransmissionTorrentsImport::thePlanHasNothingToCreate()
{
    TorrentRecord pack = recordOf(kPack);
    pack.category = QStringLiteral("films");
    pack.tags = {QStringLiteral("hd")};
    pack.seedingTimeLimit = 600;
    pack.autoTmm = true;
    TorrentRecord single = recordOf(kSingle);
    TorrentRecord nested = recordOf(kNested);
    nested.shareLimitAction = QStringLiteral("Remove");
    const TorrentsBackup::Contents contents =
        write({pack, single, nested, linkRecord()},
              {{pack.id, kPack}, {single.id, kSingle}, {nested.id, kNested}},
              EngineKind::Qbittorrent);

    Import::Here here;
    here.ids = {single.infohashV1, nested.infohashV1};
    Import::Journal journal;
    journal.key = Import::Journal::keyFor(contents.manifest);
    journal.stages.insert(nested.id, QStringLiteral("check"));
    const Import::Plan plan = Import::plan(contents, here, journal);
    QCOMPARE(plan.add, QList<int>{0});
    QCOMPARE(plan.skipped, QList<int>{1});
    QCOMPARE(plan.finish, QList<int>{2});
    QCOMPARE(plan.magnets, QList<int>{3});
    QCOMPARE(plan.addBytes, pack.size);
    // A label is there once a torrent carries it.
    QVERIFY(plan.categories.isEmpty());
    QVERIFY(plan.tags.isEmpty());

    // C40 X5: what a Transmission can't keep of those it sends or finishes.
    using Loss = LibraryCrossing::Loss;
    using Kind = LibraryCrossing::LossKind;
    QCOMPARE(
        Import::losses(contents, plan),
        QList<Loss>(
            {{Kind::SeedingTimeLimit, 1}, {Kind::ShareLimitAction, 1}, {Kind::AutoManagement, 1}}));
    // Nothing is lost between two Transmissions.
    TorrentsBackup::Contents own = contents;
    own.manifest.engine = EngineKind::Transmission;
    QVERIFY(Import::losses(own, plan).isEmpty());
}

void TestTransmissionTorrentsImport::labelsTheCategoryFirst()
{
    TorrentRecord r = recordOf(kSingle);
    r.category = QStringLiteral("films");
    r.tags = {QStringLiteral("hd"), QStringLiteral("films"), QStringLiteral("a, b")};
    TorrentsManifest manifest;
    // A qBittorrent's file (C40 X4): the category, then the tags; no label twice, no comma.
    QCOMPARE(Import::labelsHere(manifest, r), QStringList({"films", "hd", "a b"}));
    // A Transmission's: its labels, in its order.
    manifest.engine = EngineKind::Transmission;
    r.category.clear();
    r.tags = {QStringLiteral("second"), QStringLiteral("first")};
    QCOMPARE(Import::labelsHere(manifest, r), QStringList({"second", "first"}));
    // A Deluge's one label.
    manifest.engine = EngineKind::Deluge;
    r.category = QStringLiteral("music");
    r.tags.clear();
    QCOMPARE(Import::labelsHere(manifest, r), QStringList({"music"}));
}

void TestTransmissionTorrentsImport::theAddsParams()
{
    TorrentRecord r = recordOf(kPack);
    r.priorities = {0, 6, 2, 1, 7};
    r.sequential = true;
    r.dlLimit = 5000; // the add ignores it: not sent there
    const QByteArray bytes = torrentBytes(kPack);
    const QJsonObject p =
        Import::addParams(r, QStringLiteral("/there"), {QStringLiteral("x")}, bytes);
    QCOMPARE(p.value(QStringLiteral("paused")).toBool(), true);
    QCOMPARE(QByteArray::fromBase64(p.value(QStringLiteral("metainfo")).toString().toLatin1()),
             bytes);
    QCOMPARE(p.value(QStringLiteral("download_dir")).toString(), QStringLiteral("/there"));
    QCOMPARE(strings(p.value(QStringLiteral("labels"))), QStringList({"x"}));
    QCOMPARE(ints(p.value(QStringLiteral("files_unwanted"))), QList<int>({0}));
    QCOMPARE(ints(p.value(QStringLiteral("priority_high"))), QList<int>({1, 4}));
    QCOMPARE(ints(p.value(QStringLiteral("priority_low"))), QList<int>({2}));
    QCOMPARE(p.value(QStringLiteral("sequential_download")).toBool(), true);
    QVERIFY(!p.contains(QStringLiteral("filename")));
    QVERIFY(!p.contains(QStringLiteral("download_limit")));

    // Nothing said where the record says nothing: an empty list is every file to Transmission.
    const QJsonObject plain = Import::addParams(recordOf(kSingle), QString(), {}, bytes);
    QCOMPARE(plain.keys(), QStringList({"metainfo", "paused"}));

    // A link: by its magnet, with no file choices (it has no files yet).
    TorrentRecord link = linkRecord();
    link.priorities = {0, 1};
    const QJsonObject l = Import::addParams(link, QStringLiteral("/there"), {}, QByteArray());
    QCOMPARE(l.value(QStringLiteral("filename")).toString(), link.magnet);
    QCOMPARE(l.value(QStringLiteral("paused")).toBool(), true);
    QVERIFY(!l.contains(QStringLiteral("metainfo")));
    QVERIFY(!l.contains(QStringLiteral("files_unwanted")));
}

void TestTransmissionTorrentsImport::whatTheAddIgnoresGoesInASet()
{
    TorrentRecord r = recordOf(kSingle);
    QVERIFY(Import::limitParams(r, 1000).isEmpty()); // -2: the session's, as a new torrent is
    r.dlLimit = 300000;
    r.upLimit = 1500;
    r.ratioLimit = 1.5;
    r.inactiveSeedingTimeLimit = 44;
    r.seedingTimeLimit = 600; // no place in a Transmission
    QJsonObject p = Import::limitParams(r, 1000);
    QCOMPARE(p.value(QStringLiteral("download_limit")).toInt(), 300);
    QCOMPARE(p.value(QStringLiteral("download_limited")).toBool(), true);
    QCOMPARE(p.value(QStringLiteral("upload_limit")).toInt(), 2); // rounded up: still a limit
    QCOMPARE(p.value(QStringLiteral("upload_limited")).toBool(), true);
    QCOMPARE(p.value(QStringLiteral("seed_ratio_mode")).toInt(), 1);
    QCOMPARE(p.value(QStringLiteral("seed_ratio_limit")).toDouble(), 1.5);
    QCOMPARE(p.value(QStringLiteral("seed_idle_mode")).toInt(), 1);
    QCOMPARE(p.value(QStringLiteral("seed_idle_limit")).toInt(), 44);
    QCOMPARE(p.size(), 8);
    // The daemon's own unit.
    QCOMPARE(Import::limitParams(r, 1024).value(QStringLiteral("download_limit")).toInt(), 293);

    r = recordOf(kSingle);
    r.ratioLimit = -1;
    r.inactiveSeedingTimeLimit = -1;
    p = Import::limitParams(r, 1000);
    QCOMPARE(p.value(QStringLiteral("seed_ratio_mode")).toInt(), 2);
    QCOMPARE(p.value(QStringLiteral("seed_idle_mode")).toInt(), 2);
    QCOMPARE(p.size(), 2);
}

void TestTransmissionTorrentsImport::renameStepsTopFolderFirst()
{
    using Step = Import::RenameStep;
    const QStringList current{"Top/a.bin", "Top/sub/b.bin", "Top/sub/c.bin"};
    // The research's case: the top folder, then a file under its new name.
    QCOMPARE(Import::renameSteps(
                 current, {"Top Renamed/a.bin", "Top Renamed/sub/B.bin", "Top Renamed/sub/c.bin"}),
             QList<Step>({{"Top", "Top Renamed"}, {"Top Renamed/sub/b.bin", "B.bin"}}));
    // A folder below, once for all its files.
    QCOMPARE(Import::renameSteps(current, {"Top/a.bin", "Top/disc/b.bin", "Top/disc/c.bin"}),
             QList<Step>({{"Top/sub", "disc"}}));
    QCOMPARE(Import::renameSteps(current, current), QList<Step>());
    QCOMPARE(Import::renameSteps({"one.bin"}, {"other.bin"}),
             QList<Step>({{"one.bin", "other.bin"}}));

    // What torrent_rename_path can't do: another depth, a folder split in two, two made one,
    // a name already another file's.
    QVERIFY(!Import::renameSteps(current, {"a.bin", "sub/b.bin", "sub/c.bin"}));
    QVERIFY(!Import::renameSteps({"one.bin"}, {"one/one.bin"}));
    QVERIFY(!Import::renameSteps(current, {"Top/a.bin", "Top/x/b.bin", "Top/y/c.bin"}));
    QVERIFY(!Import::renameSteps({"T/x/a", "T/y/b"}, {"T/z/a", "T/z/b"}));
    QVERIFY(!Import::renameSteps(current, {"Top/a.bin", "Top/sub/c.bin", "Top/sub/b.bin"}));
    QVERIFY(!Import::renameSteps(current, {"Top/a.bin"}));
}

void TestTransmissionTorrentsImport::namesTransmissionCantGive()
{
    const TorrentFile pack = TorrentFile::parse(torrentBytes(kPack));
    const QStringList own = namesOf(kPack);
    QVERIFY(pack.multiFile && own.size() >= 2);
    TorrentRecord r = recordOf(kPack);

    // Its own layout and one rename: that file's new name, the rest as they are.
    const QString renamed =
        own.at(1).section(QLatin1Char('/'), 0, -2) + QStringLiteral("/new name");
    r.renamed = {{1, own.at(1), renamed}};
    QStringList wanted = own;
    wanted[1] = renamed;
    QCOMPARE(Import::namesWanted(r, &pack, own), wanted);
    // Read again after the rename was made (a resumed run): nothing left to do.
    QCOMPARE(Import::renameSteps(wanted, *Import::namesWanted(r, &pack, wanted)),
             QList<Import::RenameStep>());

    // qBittorrent's "NoSubfolder": the files without the top folder. Transmission has no
    // such layout, and no rename moves a file up.
    r.renamed.clear();
    r.layout = QStringLiteral("NoSubfolder");
    const auto flat = Import::namesWanted(r, &pack, own);
    QVERIFY(flat.has_value());
    QCOMPARE(flat->at(0), own.at(0).section(QLatin1Char('/'), 1));
    QVERIFY(!Import::renameSteps(own, *flat));

    // A single file in a folder of its own ("Subfolder"): the same.
    const TorrentFile single = TorrentFile::parse(torrentBytes(kSingle));
    TorrentRecord s = recordOf(kSingle);
    s.layout = QStringLiteral("Subfolder");
    QVERIFY(
        !Import::renameSteps(namesOf(kSingle), *Import::namesWanted(s, &single, namesOf(kSingle))));

    // A link has no .torrent: the daemon's names with the record's renames, under the
    // .torrent's own layout only.
    TorrentRecord link = linkRecord();
    link.renamed = {{0, QStringLiteral("x/a"), QStringLiteral("x/b")}};
    QCOMPARE(Import::namesWanted(link, nullptr, {"x/a", "x/c"}), QStringList({"x/b", "x/c"}));
    link.layout = QStringLiteral("NoSubfolder");
    QVERIFY(!Import::namesWanted(link, nullptr, {"x/a", "x/c"}));
}

// ---- the run --------------------------------------------------------------------------------

void TestTransmissionTorrentsImport::addsPausedThenSetsLimitsAndStartsTheComplete()
{
    TorrentRecord pack = recordOf(kPack); // complete, started, with limits and choices
    pack.tags = {QStringLiteral("films"), QStringLiteral("hd")};
    pack.priorities = QList<int>(namesOf(kPack).size(), 1);
    pack.priorities[0] = 0;
    pack.dlLimit = 300000;
    pack.ratioLimit = 2;
    pack.sequential = true;
    TorrentRecord single = recordOf(kSingle); // complete here, but stopped at export
    single.state = QStringLiteral("stopped");
    TorrentRecord nested = recordOf(kNested); // started, and its files are not all here
    const TorrentsBackup::Contents contents = write(
        {pack, single, nested}, {{pack.id, kPack}, {single.id, kSingle}, {nested.id, kNested}});
    m_leftOf.insert(nested.infohashV1, 4096);

    Import::Options o = options();
    o.rows = {{QStringLiteral("/data"), QStringLiteral("/mnt/there"), QString()}};
    QStringList phases;
    const Import::Outcome outcome = run(
        contents, Import::plan(contents, {}), o, [&phases](Import &, const QString &phase, int) {
            if (!phases.contains(phase))
                phases.append(phase);
        });
    QVERIFY2(outcome.complete, qPrintable(outcome.errors.join(u"; ")));
    QVERIFY2(outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.added, 3);
    QCOMPARE(outcome.started, 1);
    QCOMPARE(outcome.leftStopped, 2);
    QCOMPARE(outcome.magnets, 0);
    QVERIFY(outcome.losses.isEmpty());
    QCOMPARE(phases, QStringList({"adding", "checking"}));

    // One torrent_add each, paused, into the folder the rows give.
    QCOMPARE(callsOf(QStringLiteral("torrent_add")).size(), 3);
    const QJsonObject added = m_held.value(pack.infohashV1).params;
    QCOMPARE(added.value(QStringLiteral("paused")).toBool(), true);
    QCOMPARE(added.value(QStringLiteral("download_dir")).toString(), QStringLiteral("/mnt/there"));
    QCOMPARE(strings(added.value(QStringLiteral("labels"))), QStringList({"films", "hd"}));
    QCOMPARE(ints(added.value(QStringLiteral("files_unwanted"))), QList<int>({0}));
    QCOMPARE(added.value(QStringLiteral("sequential_download")).toBool(), true);
    QCOMPARE(QByteArray::fromBase64(added.value(QStringLiteral("metainfo")).toString().toLatin1()),
             torrentBytes(kPack));
    QVERIFY(m_held.value(single.infohashV1).params.value(QStringLiteral("paused")).toBool());

    // What the add ignores, in a torrent_set of its own; none for a record without limits.
    const QList<Call> sets = callsOf(QStringLiteral("torrent_set"));
    QCOMPARE(sets.size(), 1);
    QCOMPARE(strings(sets.at(0).params.value(QStringLiteral("ids"))),
             QStringList({pack.infohashV1}));
    QCOMPARE(sets.at(0).params.value(QStringLiteral("download_limit")).toInt(), 300);
    QCOMPARE(sets.at(0).params.value(QStringLiteral("seed_ratio_limit")).toDouble(), 2.0);

    // Transmission's own check is the check: none asked. Only the complete one that was
    // started starts.
    QVERIFY(callsOf(QStringLiteral("torrent_verify")).isEmpty());
    QVERIFY(callsOf(QStringLiteral("torrent_rename_path")).isEmpty());
    QVERIFY(m_held.value(pack.infohashV1).running);
    QVERIFY(!m_held.value(single.infohashV1).running);
    QVERIFY(!m_held.value(nested.infohashV1).running);
    // Nothing left for a next run.
    QVERIFY(!QFile::exists(o.journalPath));
}

void TestTransmissionTorrentsImport::aQbittorrentsCategoryIsTheFirstLabel()
{
    TorrentRecord r = recordOf(kSingle);
    r.category = QStringLiteral("films");
    r.tags = {QStringLiteral("hd"), QStringLiteral("a,b")};
    r.seedingTimeLimit = 600;
    r.downloadPath = QStringLiteral("/incomplete");
    const TorrentsBackup::Contents contents =
        write({r}, {{r.id, kSingle}}, EngineKind::Qbittorrent);
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(m_held.value(r.infohashV1).labels, QStringList({"films", "hd", "a b"}));
    // C40 X5: the result repeats what the sheet said can't be kept; none of it was sent.
    using Loss = LibraryCrossing::Loss;
    using Kind = LibraryCrossing::LossKind;
    QCOMPARE(outcome.losses,
             QList<Loss>({{Kind::SeedingTimeLimit, 1}, {Kind::IncompleteFolder, 1}}));
    QCOMPARE(m_held.value(r.infohashV1).downloadDir, QStringLiteral("/data"));
    QVERIFY(callsOf(QStringLiteral("torrent_set")).isEmpty());
}

void TestTransmissionTorrentsImport::aDuplicateIsAlreadyThere()
{
    const TorrentRecord pack = recordOf(kPack);
    const TorrentRecord single = recordOf(kSingle);
    const TorrentsBackup::Contents contents =
        write({pack, single}, {{pack.id, kPack}, {single.id, kSingle}});
    // It came to the daemon after the plan was made.
    Held there;
    there.name = QStringLiteral("there already");
    there.labels = {QStringLiteral("its own")};
    m_held.insert(pack.infohashV1, there);
    m_order.append(pack.infohashV1);

    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.started, 1);
    // Nothing of the import touched it.
    QCOMPARE(methodsFor(pack.infohashV1), QStringList());
    QCOMPARE(m_held.value(pack.infohashV1).labels, QStringList({"its own"}));
    QVERIFY(!m_held.value(pack.infohashV1).running);
    QVERIFY(!QFile::exists(options().journalPath));
}

void TestTransmissionTorrentsImport::aTorrentTheDaemonRefusesIsNamed()
{
    const TorrentRecord pack = recordOf(kPack);
    const TorrentRecord single = recordOf(kSingle);
    const TorrentsBackup::Contents contents =
        write({pack, single}, {{pack.id, kPack}, {single.id, kSingle}});
    m_refuse = {pack.name};
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    // One torrent's trouble: named, and the run goes on (docs/24 §7).
    QVERIFY(outcome.complete);
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.errors.size(), 1);
    QVERIFY2(outcome.errors.at(0).startsWith(pack.name + QStringLiteral(" was not added")),
             qPrintable(outcome.errors.at(0)));
    QVERIFY(m_held.value(single.infohashV1).running);
    QVERIFY(!QFile::exists(options().journalPath));
}

void TestTransmissionTorrentsImport::renamesAreSentBeforeAVerify()
{
    const QStringList own = namesOf(kPack);
    const QString top = own.at(0).section(QLatin1Char('/'), 0, 0);
    TorrentRecord r = recordOf(kPack);
    // The top folder renamed (every file's name changes), and one file in it.
    for (int i = 0; i < own.size(); ++i) {
        QString to = QStringLiteral("Top Renamed/") + own.at(i).section(QLatin1Char('/'), 1);
        if (i == 0)
            to = to.section(QLatin1Char('/'), 0, -2) + QStringLiteral("/first renamed");
        r.renamed.append({i, own.at(i), to});
    }
    const TorrentsBackup::Contents contents = write({r}, {{r.id, kPack}});
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.started, 1);
    QVERIFY(outcome.refused.isEmpty() && outcome.misplaced.isEmpty());

    // Added, renamed (the top folder first, the file by the path it has by then), checked
    // once more, and only then started.
    QCOMPARE(
        methodsFor(r.infohashV1),
        QStringList(
            {"torrent_rename_path", "torrent_rename_path", "torrent_verify", "torrent_start"}));
    const QList<Call> renames = callsOf(QStringLiteral("torrent_rename_path"));
    QCOMPARE(renames.at(0).params.value(QStringLiteral("path")).toString(), top);
    QCOMPARE(renames.at(0).params.value(QStringLiteral("name")).toString(),
             QStringLiteral("Top Renamed"));
    QCOMPARE(renames.at(1).params.value(QStringLiteral("path")).toString(),
             QStringLiteral("Top Renamed/") + own.at(0).section(QLatin1Char('/'), 1));
    QCOMPARE(renames.at(1).params.value(QStringLiteral("name")).toString(),
             QStringLiteral("first renamed"));
    QStringList wanted;
    for (const TorrentRecord::Rename &rename : std::as_const(r.renamed))
        wanted.append(rename.to);
    QCOMPARE(m_held.value(r.infohashV1).files, wanted);
}

void TestTransmissionTorrentsImport::aRenameThatWouldReplaceAFileIsNotSent()
{
    const QStringList own = namesOf(kPack);
    TorrentRecord r = recordOf(kPack);
    const QString to = own.at(0).section(QLatin1Char('/'), 0, -2) + QStringLiteral("/taken.bin");
    r.renamed = {{0, own.at(0), to}};
    r.savePath = QStringLiteral("/remote/data");
    TorrentRecord free = recordOf(kSingle);
    free.renamed = {{0, namesOf(kSingle).at(0), QStringLiteral("free.bin")}};
    free.savePath = QStringLiteral("/remote/data");
    const TorrentsBackup::Contents contents = write({r, free}, {{r.id, kPack}, {free.id, kSingle}});

    // This machine sees the daemon's folder, and both the old and the new name are in it
    // (DECISIONS C9). Of the other torrent only the new name is: the expected case.
    const QString local = m_dir.filePath(QStringLiteral("seen"));
    for (const QString &name : {own.at(0), to, QStringLiteral("free.bin")}) {
        QVERIFY(QDir().mkpath(QFileInfo(local + QLatin1Char('/') + name).absolutePath()));
        QFile file(local + QLatin1Char('/') + name);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }
    Import::Options o = options();
    o.localPathFor = [local](const QString &path) {
        return path == QLatin1String("/remote/data") ? local : QString();
    };
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), o);
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.refused, QStringList({r.name}));
    QCOMPARE(outcome.started, 1);
    QCOMPARE(outcome.leftStopped, 1);
    // Not sent, and never started, though the daemon found it complete.
    QCOMPARE(methodsFor(r.infohashV1), QStringList());
    QCOMPARE(m_held.value(r.infohashV1).files, own);
    QVERIFY(!m_held.value(r.infohashV1).running);
    QCOMPARE(methodsFor(free.infohashV1),
             QStringList({"torrent_rename_path", "torrent_verify", "torrent_start"}));

    // Where the disk can't be seen the rename is sent (#64's rule).
    init();
    o.localPathFor = {};
    const Import::Outcome blind = run(contents, Import::plan(contents, {}), o);
    QVERIFY(blind.refused.isEmpty());
    QCOMPARE(blind.started, 2);
}

void TestTransmissionTorrentsImport::aTorrentWithoutItsTopFolderIsLeftStopped()
{
    TorrentRecord r = recordOf(kPack);
    r.layout = QStringLiteral("NoSubfolder"); // a qBittorrent's, moved out of its folder
    const TorrentRecord single = recordOf(kSingle);
    const TorrentsBackup::Contents contents =
        write({r, single}, {{r.id, kPack}, {single.id, kSingle}}, EngineKind::Qbittorrent);
    // Its files are not under the names Transmission looks for: the check finds none.
    m_leftOf.insert(r.infohashV1, r.size);
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.added, 2);
    QCOMPARE(outcome.misplaced, QStringList({r.name}));
    QCOMPARE(outcome.started, 1);
    QCOMPARE(outcome.leftStopped, 1);
    // Added, nothing guessed: no rename, no start.
    QCOMPARE(methodsFor(r.infohashV1), QStringList());
    QVERIFY(m_held.contains(r.infohashV1));

    // Even were it found complete, it stays stopped: its files are not where the file says.
    init();
    const Import::Outcome again = run(contents, Import::plan(contents, {}), options());
    QCOMPARE(again.misplaced, QStringList({r.name}));
    QVERIFY(!m_held.value(r.infohashV1).running);
}

void TestTransmissionTorrentsImport::leaveThemStoppedChecksNothing()
{
    const QStringList own = namesOf(kSingle);
    TorrentRecord renamed = recordOf(kSingle);
    renamed.renamed = {{0, own.at(0), QStringLiteral("other name")}};
    const TorrentRecord pack = recordOf(kPack);
    const TorrentsBackup::Contents contents =
        write({renamed, pack}, {{renamed.id, kSingle}, {pack.id, kPack}});
    Import::Options o = options();
    o.check = false;
    QStringList phases;
    const Import::Outcome outcome = run(
        contents, Import::plan(contents, {}), o, [&phases](Import &, const QString &phase, int) {
            if (!phases.contains(phase))
                phases.append(phase);
        });
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.added, 2);
    QCOMPARE(outcome.started, 0);
    QCOMPARE(outcome.leftStopped, 2);
    QCOMPARE(phases, QStringList({"adding"}));
    // The names are still put right; nothing is verified, waited for or started.
    QCOMPARE(m_held.value(renamed.infohashV1).files, QStringList({"other name"}));
    QVERIFY(callsOf(QStringLiteral("torrent_verify")).isEmpty());
    QVERIFY(callsOf(QStringLiteral("torrent_start")).isEmpty());
    for (const Call &call : callsOf(QStringLiteral("torrent_get")))
        QVERIFY(!strings(call.params.value(QStringLiteral("fields")))
                     .contains(QStringLiteral("status")));
    QVERIFY(!QFile::exists(o.journalPath));
}

void TestTransmissionTorrentsImport::theQueueIsPutBackInOrder()
{
    // Three unfinished ones, exported as second, first and third in the queue, and one that
    // was complete (no place in it).
    TorrentRecord a = recordOf(kPack);
    a.queuePosition = 2;
    TorrentRecord b = recordOf(kSingle);
    b.queuePosition = 1;
    TorrentRecord c = recordOf(kNested);
    c.queuePosition = 3;
    TorrentRecord d = recordOf("open_movies_v1.torrent");
    const TorrentsBackup::Contents contents =
        write({a, d, b, c},
              {{a.id, kPack}, {b.id, kSingle}, {c.id, kNested}, {d.id, "open_movies_v1.torrent"}});
    for (const TorrentRecord &r : {a, b, c})
        m_leftOf.insert(r.infohashV1, 100);
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    // Each to the end, in the exported order: the complete one first, then b, a, c.
    QStringList moved;
    for (const Call &call : callsOf(QStringLiteral("torrent_set"))) {
        QCOMPARE(call.params.value(QStringLiteral("queue_position")).toInt(), 3);
        moved += strings(call.params.value(QStringLiteral("ids")));
    }
    QCOMPARE(moved, QStringList({b.infohashV1, a.infohashV1, c.infohashV1}));
    QCOMPARE(m_order, QStringList({d.infohashV1, b.infohashV1, a.infohashV1, c.infohashV1}));
}

void TestTransmissionTorrentsImport::aLinkWaitsInTheJournalAndIsFinishedLater()
{
    TorrentRecord link = linkRecord();
    link.tags = {QStringLiteral("m")};
    link.priorities = {1, 0};
    link.renamed = {{0, QStringLiteral("it/a.bin"), QStringLiteral("it/A renamed.bin")}};
    link.upLimit = 2000;
    const TorrentRecord single = recordOf(kSingle);
    TorrentsBackup::Contents contents = write({link, single}, {{single.id, kSingle}});
    // A link's file choices come through the file (a Transmission writes them when it can't
    // read the .torrent); its renames only a record made in memory has.
    QCOMPARE(contents.records.at(0).priorities, link.priorities);
    QVERIFY(contents.records.at(0).renamed.isEmpty());
    contents.records[0] = link;
    const Import::Options o = options();
    const Import::Plan plan = Import::plan(contents, {});
    QCOMPARE(plan.magnets, QList<int>{0});

    const Import::Outcome outcome = run(contents, plan, o);
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    // C40 X7: added by its link, stopped, counted apart.
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.magnets, 1);
    QCOMPARE(outcome.linksWaiting, 1);
    QCOMPARE(outcome.started, 1); // the other torrent
    const Held held = m_held.value(kLinkHash);
    QCOMPARE(held.params.value(QStringLiteral("filename")).toString(), link.magnet);
    QCOMPARE(held.params.value(QStringLiteral("paused")).toBool(), true);
    QCOMPARE(held.labels, QStringList({"m"}));
    QCOMPARE(held.downloadDir, QStringLiteral("/data"));
    QVERIFY(!held.running);
    // Its limits are set at once; its file choices and names have to wait.
    QCOMPARE(methodsFor(kLinkHash), QStringList({"torrent_set"}));
    QCOMPARE(callsOf(QStringLiteral("torrent_set"))
                 .at(0)
                 .params.value(QStringLiteral("upload_limit"))
                 .toInt(),
             2);

    // The journal keeps it past the run, without its link (a passkey may be in it).
    QCOMPARE(Import::linksWaiting(o.journalPath), 1);
    const Import::Journal journal = Import::Journal::load(o.journalPath);
    QVERIFY(journal.stages.isEmpty());
    QVERIFY(journal.links.contains(kLinkHash));
    QFile file(o.journalPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!file.readAll().contains("magnet:"));
    // The same file read again: it is there already, and stays waiting.
    Import::Here here;
    here.ids = {kLinkHash, single.infohashV1};
    const Import::Plan again = Import::plan(contents, here, journal);
    QCOMPARE(again.count(), 0);

    // Its details have not arrived: nothing is done, it keeps waiting.
    m_daemon->calls.clear();
    Import::Outcome later = finishLinks(o);
    QVERIFY(later.complete);
    QCOMPARE(later.finished, 0);
    QCOMPARE(later.linksWaiting, 1);
    QCOMPARE(methodsFor(kLinkHash), QStringList());

    // They arrive (someone started it), and it turns out complete on disk.
    m_held[kLinkHash].metadata = true;
    m_held[kLinkHash].files = QStringList{"it/a.bin", "it/b.bin"};
    m_daemon->calls.clear();
    later = finishLinks(o);
    QVERIFY2(later.complete && later.errors.isEmpty(), qPrintable(later.errors.join(u"; ")));
    QCOMPARE(later.finished, 1);
    QCOMPARE(later.linksWaiting, 0);
    QCOMPARE(later.started, 1);
    // Its file choices, its rename, its check, then its start: it was started at export.
    QCOMPARE(
        methodsFor(kLinkHash),
        QStringList({"torrent_set", "torrent_rename_path", "torrent_verify", "torrent_start"}));
    QCOMPARE(ints(callsOf(QStringLiteral("torrent_set"))
                      .at(0)
                      .params.value(QStringLiteral("files_unwanted"))),
             QList<int>({1}));
    QCOMPARE(m_held.value(kLinkHash).files, QStringList({"it/A renamed.bin", "it/b.bin"}));
    QVERIFY(m_held.value(kLinkHash).running);
    QVERIFY(!QFile::exists(o.journalPath));
    QCOMPARE(Import::linksWaiting(o.journalPath), 0);

    // A link removed from the daemon before its details came leaves the journal.
    init();
    QCOMPARE(run(contents, plan, o).linksWaiting, 1);
    m_held.remove(kLinkHash);
    m_order.removeAll(kLinkHash);
    later = finishLinks(o);
    QVERIFY(later.complete);
    QCOMPARE(later.linksWaiting, 0);
    QVERIFY(!QFile::exists(o.journalPath));
}

void TestTransmissionTorrentsImport::stopAndResumeThroughTheJournal()
{
    const QStringList own = namesOf(kSingle);
    TorrentRecord first = recordOf(kSingle);
    first.renamed = {{0, own.at(0), QStringLiteral("renamed.bin")}};
    first.queuePosition = 1; // sent first
    const TorrentRecord second = recordOf(kPack);
    const TorrentRecord third = recordOf(kNested);
    const TorrentsBackup::Contents contents = write(
        {first, second, third}, {{first.id, kSingle}, {second.id, kPack}, {third.id, kNested}});
    Import::Options o = options();
    o.batch = 1;

    // Stopped once the first batch is in and through its steps.
    const Import::Outcome stopped =
        run(contents, Import::plan(contents, {}), o, [](Import &import, const QString &, int done) {
            if (done == 1)
                import.cancel();
        });
    QVERIFY(stopped.cancelled);
    QVERIFY(!stopped.complete);
    QCOMPARE(stopped.added, 1);
    QCOMPARE(stopped.started, 0);
    QCOMPARE(m_held.size(), 1);
    QVERIFY(!m_held.value(first.infohashV1).running);
    // What was added stays, and the journal says how far it got.
    Import::Journal journal = Import::Journal::load(o.journalPath);
    QCOMPARE(journal.key, Import::Journal::keyFor(contents.manifest));
    QCOMPARE(journal.stages.value(first.id), QStringLiteral("check"));

    // The same file again: that one is finished, not skipped; the rest is added.
    Import::Here here;
    here.ids = {first.infohashV1};
    const Import::Plan plan = Import::plan(contents, here, journal);
    QCOMPARE(plan.finish, QList<int>{0});
    QCOMPARE(plan.add, (QList<int>{1, 2}));
    m_daemon->calls.clear();
    const Import::Outcome resumed = run(contents, plan, o);
    QVERIFY2(resumed.complete && resumed.errors.isEmpty(), qPrintable(resumed.errors.join(u"; ")));
    QCOMPARE(resumed.finished, 1);
    QCOMPARE(resumed.added, 2);
    QCOMPARE(resumed.started, 3);
    QCOMPARE(callsOf(QStringLiteral("torrent_add")).size(), 2);
    QVERIFY(m_held.value(first.infohashV1).running);
    QVERIFY(!QFile::exists(o.journalPath));

    // Stopped between an add and its steps (the journal says "added"): the steps run again,
    // each from what the daemon has by then.
    init();
    journal = Import::Journal{Import::Journal::keyFor(contents.manifest), {}};
    journal.stages.insert(first.id, QStringLiteral("added"));
    QVERIFY(journal.save(o.journalPath));
    Held half;
    half.files = own;
    half.checks = 0;
    m_held.insert(first.infohashV1, half);
    m_order.append(first.infohashV1);
    const Import::Outcome finished = run(contents, Import::plan(contents, here, journal), o);
    QVERIFY2(finished.complete && finished.errors.isEmpty(),
             qPrintable(finished.errors.join(u"; ")));
    QCOMPARE(finished.finished, 1);
    QCOMPARE(methodsFor(first.infohashV1),
             QStringList({"torrent_rename_path", "torrent_verify", "torrent_start"}));
    QCOMPARE(m_held.value(first.infohashV1).files, QStringList({"renamed.bin"}));

    // Another file's journal is not this file's: nothing of it is "unfinished" here.
    Import::Journal other = journal;
    other.key = QStringLiteral("another export");
    QCOMPARE(Import::plan(contents, here, other).skipped, QList<int>{0});
}

void TestTransmissionTorrentsImport::aDaemonGoneKeepsTheJournal()
{
    const TorrentRecord first = recordOf(kSingle);
    const TorrentRecord second = recordOf(kPack);
    const TorrentsBackup::Contents contents =
        write({first, second}, {{first.id, kSingle}, {second.id, kPack}});
    Import::Options o = options();
    o.batch = 1;
    const Import::Outcome outcome =
        run(contents, Import::plan(contents, {}), o, [this](Import &, const QString &, int done) {
            if (done == 1)
                m_down = true;
        });
    QVERIFY(!outcome.complete);
    QVERIFY(!outcome.cancelled);
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.errors.size(), 1);
    // The one that got in is kept for the next run of this file.
    const Import::Journal journal = Import::Journal::load(o.journalPath);
    QCOMPARE(journal.stages.keys(), QStringList({first.id}));

    m_down = false;
    Import::Here here;
    here.ids = {first.infohashV1};
    const Import::Outcome resumed = run(contents, Import::plan(contents, here, journal), o);
    QVERIFY2(resumed.complete && resumed.errors.isEmpty(), qPrintable(resumed.errors.join(u"; ")));
    QCOMPARE(resumed.finished, 1);
    QCOMPARE(resumed.added, 1);
    QCOMPARE(resumed.started, 2);
    QVERIFY(!QFile::exists(o.journalPath));
}

QTEST_GUILESS_MAIN(TestTransmissionTorrentsImport)
#include "tst_transmissiontorrentsimport.moc"
