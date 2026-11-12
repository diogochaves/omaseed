// DelugeTorrentsImport (#88, docs/DECISIONS.md C40 X4–X7, C9): a TorrentsBackup file laid onto
// a Deluge, against the tests' fake deluge-web (FakeDeluge) given a small state of its own: the
// torrents it was handed, their files as it names them, renames that show a few reads later, a
// check that runs only when asked and takes a poll. The plan, each add option, priorities only
// in the add, the label made once then set, the Label plugin off, a duplicate, a torrent the
// daemon refuses, renames sent and waited for before the recheck (a top folder, no top folder,
// a single file in a folder), the rename that would replace a file, one that never shows,
// resume only when complete and started, "leave them stopped", the queue, a link and its later
// finishing, stop and resume through the journal, the daemon gone mid-run, and what can't be
// kept in the outcome.

#include "FakeDeluge.hpp"

#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/deluge/DelugeRpc.hpp"
#include "core/engines/deluge/DelugeTorrentsImport.hpp"

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
using namespace fakedeluge;
using namespace omaseed::core;

namespace {

using Import = DelugeTorrentsImport;

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

QStringList namesOf(const char *fixtureName, const QString &layout = QStringLiteral("Original"))
{
    return TorrentsBackup::namesUnder(TorrentFile::parse(torrentBytes(fixtureName)), layout);
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
    QStringList pending; ///< a rename on its way: the names it will have
    int lag = 0;         ///< reads of its files before `pending` shows
    QString label;
    QJsonObject options; ///< its add's
    QJsonArray priorities;
    bool paused = true;
    bool checked = false; ///< a check was asked and ran
    int checks = 0;       ///< polls it still shows as checking
    bool complete = true; ///< what its check finds
};

} // namespace

class TestDelugeTorrentsImport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void thePlanListsTheLabelsToMake();
    void theAddsOptions();
    void prioritiesInDelugesNumbers();
    void renamesForAFolderAndFiles();

    void addsPausedOneAtATimeThenChecksAndResumesTheComplete();
    void theLabelIsMadeOnceThenSet();
    void withThePluginOffNoLabelIsSet();
    void aDuplicateIsAlreadyThere();
    void aTorrentTheDaemonRefusesIsNamed();
    void renamesAreWaitedForBeforeTheRecheck();
    void aTorrentWithoutItsTopFolderIsRenamedFileByFile();
    void aRenameThatWouldReplaceAFileIsNotSent();
    void aRenameThatNeverShowsLeavesItStopped();
    void leaveThemStoppedChecksNothing();
    void theQueueIsPutBackInOrder();
    void aLinkWaitsInTheJournalAndIsFinishedLater();
    void aFinishedLinkNeverWantsASkippedFileAgain();
    void stopAndResumeThroughTheJournal();
    void aDaemonGoneKeepsTheJournal();
    void whatCantBeKeptIsInTheOutcome();

private:
    QJsonValue answer(const QString &method, const QJsonArray &params);
    /// Writes the file and reads it back; `torrents` are fixture names by record id.
    TorrentsBackup::Contents write(const QList<TorrentRecord> &records,
                                   const QHash<QString, const char *> &torrents,
                                   EngineKind engine = EngineKind::Deluge);
    Import::Options options() const;
    Import::Outcome run(const TorrentsBackup::Contents &contents,
                        const Import::Plan &plan,
                        const Import::Options &options,
                        const std::function<void(Import &, const QString &, int)> &onProgress = {});
    Import::Outcome finishLinks(const Import::Options &options);
    /// The calls about `hash`, reads aside.
    QStringList methodsFor(const QString &hash) const;

    std::unique_ptr<FakeServer> m_server;
    std::unique_ptr<FakeDeluge> m_daemon;
    std::unique_ptr<DelugeEngine> m_engine;
    QTemporaryDir m_dir;
    QHash<QString, Held> m_held;
    QStringList m_order;      ///< the daemon's torrents as added, then as queued
    QStringList m_labels;     ///< the Label plugin's
    QStringList m_incomplete; ///< hashes whose check finds files missing
    QStringList m_refuse;     ///< torrent names whose add the daemon refuses
    int m_renameLag = 2;      ///< reads before a rename shows; -1 never
    int m_adding = 0;         ///< adds answered so far, for "one at a time"
    bool m_plugin = true;
    bool m_down = false; ///< every request answers 500
};

void TestDelugeTorrentsImport::init()
{
    QVERIFY(m_dir.isValid());
    QDir(m_dir.filePath(QStringLiteral("imports"))).removeRecursively();
    m_held.clear();
    m_order.clear();
    m_labels = QStringList{QStringLiteral("old")};
    m_incomplete.clear();
    m_refuse.clear();
    m_renameLag = 2;
    m_plugin = true;
    m_down = false;
    m_server = std::make_unique<FakeServer>();
    m_daemon = std::make_unique<FakeDeluge>();
    m_daemon->answer = [this](const QString &method, const QJsonArray &params) {
        return answer(method, params);
    };
    const FakeServer::Handler daemon = m_daemon->handler();
    m_server->setHandler([this, daemon](const Recorded &request, int n) {
        if (m_down)
            return httpResponse(500, "Internal Server Error", "down");
        return daemon(request, n);
    });
    m_engine = std::make_unique<DelugeEngine>();
    m_engine->setBaseUrl(m_server->url());
    m_engine->setCredentials(QString(), m_daemon->password);
    m_engine->setTimeout(std::chrono::seconds(5));
    m_engine->rpc()->setMaxRetries(0);
}

void TestDelugeTorrentsImport::cleanup()
{
    m_engine.reset();
    m_daemon.reset();
    m_server.reset();
}

QJsonValue TestDelugeTorrentsImport::answer(const QString &method, const QJsonArray &params)
{
    if (method == QLatin1String("core.get_enabled_plugins"))
        return m_plugin ? QJsonArray{QStringLiteral("Label")} : QJsonArray();
    if (method.startsWith(QLatin1String("label."))) {
        if (!m_plugin)
            return errorOf(2, QStringLiteral("Unknown method"));
        if (method == QLatin1String("label.get_labels"))
            return QJsonArray::fromStringList(m_labels);
        if (method == QLatin1String("label.add")) {
            if (m_labels.contains(params.at(0).toString()))
                return errorOf(4, QStringLiteral("Label already exists"));
            m_labels.append(params.at(0).toString());
            return QJsonValue();
        }
        if (method == QLatin1String("label.set_torrent")) {
            if (!m_labels.contains(params.at(1).toString()))
                return errorOf(4, QStringLiteral("Unknown Label"));
            m_held[params.at(0).toString()].label = params.at(1).toString();
        }
        return QJsonValue();
    }
    const bool file = method == QLatin1String("core.add_torrent_file");
    if (file || method == QLatin1String("core.add_torrent_magnet")) {
        Held held;
        QString hash;
        if (file) {
            const TorrentFile parsed =
                TorrentFile::parse(QByteArray::fromBase64(params.at(1).toString().toLatin1()));
            hash = parsed.infoHashV1;
            held.name = parsed.name;
            held.files = TorrentsBackup::namesUnder(parsed, QStringLiteral("Original"));
        } else {
            hash = params.at(0).toString().section(QStringLiteral("btih:"), 1).left(40);
            held.name = hash;
        }
        if (m_refuse.contains(held.name))
            return errorOf(4, QStringLiteral("Unable to add torrent, decoding filedump failed"));
        if (m_held.contains(hash))
            return errorOf(4, QStringLiteral("Torrent already in session (%1).").arg(hash));
        held.options = params.at(file ? 2 : 1).toObject();
        held.priorities = held.options.value(QStringLiteral("file_priorities")).toArray();
        held.complete = !m_incomplete.contains(hash);
        m_held.insert(hash, held);
        m_order.append(hash);
        return hash;
    }
    if (method == QLatin1String("core.get_torrents_status")) {
        const QJsonObject filter = params.at(0).toObject();
        const QStringList ids = strings(filter.value(QStringLiteral("id")));
        const QStringList keys = strings(params.at(1));
        QJsonObject out;
        int queue = 0;
        for (const QString &hash : std::as_const(m_order)) {
            Held &held = m_held[hash];
            const bool checking = held.checks > 0;
            const bool finished = held.checked && held.complete && !checking;
            const int place = finished ? -1 : queue++;
            if (filter.contains(QStringLiteral("id")) && !ids.contains(hash))
                continue;
            QJsonObject row;
            if (keys.contains(QStringLiteral("files"))) {
                if (!held.pending.isEmpty() && held.lag >= 0 && held.lag-- == 0) {
                    held.files = held.pending;
                    held.pending.clear();
                }
                QJsonArray files;
                for (int i = 0; i < held.files.size(); ++i)
                    files.append(QJsonObject{{QStringLiteral("index"), i},
                                             {QStringLiteral("path"), held.files.at(i)}});
                row.insert(QStringLiteral("files"), files);
            }
            if (keys.contains(QStringLiteral("file_priorities")))
                row.insert(QStringLiteral("file_priorities"), held.priorities);
            if (keys.contains(QStringLiteral("queue")))
                row.insert(QStringLiteral("queue"), place);
            if (keys.contains(QStringLiteral("state"))) {
                if (checking)
                    --held.checks;
                row.insert(QStringLiteral("state"),
                           checking      ? QStringLiteral("Checking")
                           : held.paused ? QStringLiteral("Paused")
                                         : QStringLiteral("Seeding"));
            }
            if (keys.contains(QStringLiteral("is_finished")))
                row.insert(QStringLiteral("is_finished"), finished);
            out.insert(hash, row);
        }
        return out;
    }
    if (method == QLatin1String("core.rename_folder")) {
        Held &held = m_held[params.at(0).toString()];
        const QString from = params.at(1).toString();
        held.pending = held.files;
        for (QString &name : held.pending) {
            if (name.startsWith(from))
                name = params.at(2).toString() + name.mid(from.size());
        }
        held.lag = m_renameLag;
        return QJsonValue();
    }
    if (method == QLatin1String("core.rename_files")) {
        Held &held = m_held[params.at(0).toString()];
        held.pending = held.files;
        for (const QJsonValue &pair : params.at(1).toArray())
            held.pending[pair.toArray().at(0).toInt()] = pair.toArray().at(1).toString();
        held.lag = m_renameLag;
        return QJsonValue();
    }
    for (const QString &hash : strings(params.at(0))) {
        Held &held = m_held[hash];
        if (method == QLatin1String("core.force_recheck")) {
            held.checks = 1;
            held.checked = true;
        } else if (method == QLatin1String("core.resume_torrents")) {
            held.paused = false;
        } else if (method == QLatin1String("core.set_torrent_options")) {
            const QJsonObject set = params.at(1).toObject();
            if (set.contains(QStringLiteral("file_priorities")))
                held.priorities = set.value(QStringLiteral("file_priorities")).toArray();
        } else if (method == QLatin1String("core.queue_bottom")) {
            m_order.removeAll(hash);
            m_order.append(hash);
        }
    }
    return QJsonValue();
}

TorrentsBackup::Contents
TestDelugeTorrentsImport::write(const QList<TorrentRecord> &records,
                                const QHash<QString, const char *> &torrents,
                                EngineKind engine)
{
    TorrentsManifest manifest;
    manifest.created = QDateTime(QDate(2026, 11, 12), QTime(10, 0), QTimeZone::UTC);
    manifest.appVersion = QStringLiteral("test");
    manifest.serverName = QStringLiteral("source");
    manifest.serverUrl = QStringLiteral("http://source:8112");
    manifest.engine = engine;
    manifest.version = QStringLiteral("2.2.0");
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

Import::Options TestDelugeTorrentsImport::options() const
{
    Import::Options o;
    o.journalPath = m_dir.filePath(QStringLiteral("imports/fake.json"));
    o.pollMs = 10;
    return o;
}

Import::Outcome
TestDelugeTorrentsImport::run(const TorrentsBackup::Contents &contents,
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

Import::Outcome TestDelugeTorrentsImport::finishLinks(const Import::Options &options)
{
    Import import(m_engine.get());
    QSignalSpy finished(&import, &Import::finished);
    import.finishLinks(options);
    if (!finished.wait(20000))
        qFatal("finishLinks never finished");
    return finished.at(0).at(0).value<Import::Outcome>();
}

QStringList TestDelugeTorrentsImport::methodsFor(const QString &hash) const
{
    QStringList out;
    for (const Call &call : m_daemon->calls) {
        if (call.method == QLatin1String("core.get_torrents_status"))
            continue;
        const QJsonValue first = call.params.at(0);
        if (first.toString() == hash || first.toArray().contains(hash))
            out.append(call.method);
    }
    return out;
}

// ---- the pure half --------------------------------------------------------------------------

void TestDelugeTorrentsImport::thePlanListsTheLabelsToMake()
{
    TorrentRecord pack = recordOf(kPack);
    pack.category = QStringLiteral("Field Recordings");
    pack.tags = {QStringLiteral("hd")};
    TorrentRecord single = recordOf(kSingle);
    single.category = QStringLiteral("old");
    TorrentRecord tagged = recordOf(kNested);
    tagged.tags = {QStringLiteral("more")};
    TorrentRecord nested = recordOf(kNested);
    const TorrentsBackup::Contents contents =
        write({pack, single, nested, linkRecord()},
              {{pack.id, kPack}, {single.id, kSingle}, {nested.id, kNested}},
              EngineKind::Qbittorrent);

    // C40 X4: one label each, the category in Deluge's spelling; a tag never stands in.
    QCOMPARE(Import::labelHere(contents.manifest, pack), QStringLiteral("field-recordings"));
    QCOMPARE(Import::labelHere(contents.manifest, single), QStringLiteral("old"));
    QCOMPARE(Import::labelHere(contents.manifest, tagged), QString());
    QCOMPARE(Import::labelHere(contents.manifest, nested), QString());
    // A Transmission's first label; a Deluge's own as it is.
    TorrentsManifest other = contents.manifest;
    other.engine = EngineKind::Transmission;
    QCOMPARE(Import::labelHere(other, tagged), QStringLiteral("more"));
    other.engine = EngineKind::Deluge;
    QCOMPARE(Import::labelHere(other, single), QStringLiteral("old"));

    Import::Here here;
    here.ids = {nested.infohashV1};
    here.categories = {QStringLiteral("old")};
    const Import::Plan plan = Import::plan(contents, here);
    QCOMPARE(plan.add, (QList<int>{0, 1}));
    QCOMPARE(plan.magnets, QList<int>{3});
    QCOMPARE(plan.skipped, QList<int>{2});
    // Only the label the daemon lacks; a Deluge has no tags.
    QCOMPARE(plan.categories, QStringList({"field-recordings"}));
    QVERIFY(plan.tags.isEmpty());
}

void TestDelugeTorrentsImport::theAddsOptions()
{
    TorrentRecord r = recordOf(kPack);
    // Nothing of its own: paused, no speed limit, the server's ratio limit, every file.
    QJsonObject o = Import::addOptions(r, QStringLiteral("/there"));
    QCOMPARE(o.value(QStringLiteral("add_paused")).toBool(), true);
    QCOMPARE(o.value(QStringLiteral("download_location")).toString(), QStringLiteral("/there"));
    QCOMPARE(o.value(QStringLiteral("max_download_speed")).toDouble(), -1.0);
    QCOMPARE(o.value(QStringLiteral("max_upload_speed")).toDouble(), -1.0);
    for (const char *key : {"file_priorities",
                            "stop_at_ratio",
                            "stop_ratio",
                            "remove_at_ratio",
                            "sequential_download",
                            "prioritize_first_last_pieces",
                            "seed_mode",
                            "label"})
        QVERIFY2(!o.contains(QLatin1String(key)), key);

    r.dlLimit = 2048 * 1024;
    r.upLimit = 512;
    r.ratioLimit = 1.5;
    r.shareLimitAction = QStringLiteral("Remove");
    r.sequential = true;
    r.firstLastPiecePrio = true;
    r.priorities = {1, 0, 7};
    o = Import::addOptions(r, QString());
    QVERIFY(!o.contains(QStringLiteral("download_location")));
    QCOMPARE(o.value(QStringLiteral("max_download_speed")).toDouble(), 2048.0); // KiB/s
    QCOMPARE(o.value(QStringLiteral("max_upload_speed")).toDouble(), 0.5);
    QCOMPARE(o.value(QStringLiteral("stop_at_ratio")).toBool(), true);
    QCOMPARE(o.value(QStringLiteral("stop_ratio")).toDouble(), 1.5);
    QCOMPARE(o.value(QStringLiteral("remove_at_ratio")).toBool(), true);
    QCOMPARE(o.value(QStringLiteral("sequential_download")).toBool(), true);
    QCOMPARE(o.value(QStringLiteral("prioritize_first_last_pieces")).toBool(), true);
    QCOMPARE(ints(o.value(QStringLiteral("file_priorities"))), (QList<int>{4, 0, 7}));
    QVERIFY(!o.contains(QStringLiteral("seed_mode"))); // C40 X6: never

    // Any action but Remove stops there; a link's add has no files to choose among.
    for (const char *action : {"Stop", "Default", "EnableSuperSeeding", ""}) {
        r.shareLimitAction = QLatin1String(action);
        o = Import::addOptions(r, QString(), false);
        QCOMPARE(o.value(QStringLiteral("stop_at_ratio")).toBool(), true);
        QCOMPARE(o.value(QStringLiteral("remove_at_ratio")).toBool(), false);
        QVERIFY(!o.contains(QStringLiteral("file_priorities")));
    }
    // No limit of its own, whatever the server's is.
    r.ratioLimit = -1;
    o = Import::addOptions(r, QString());
    QVERIFY(o.contains(QStringLiteral("stop_at_ratio")));
    QCOMPARE(o.value(QStringLiteral("stop_at_ratio")).toBool(), false);
    QVERIFY(!o.contains(QStringLiteral("stop_ratio")));
}

void TestDelugeTorrentsImport::prioritiesInDelugesNumbers()
{
    // A record's: 0 not wanted, 1 normal, 2 low, 6 high, 7 maximal (Deluge's highest is high).
    QCOMPARE(ints(Import::filePriorities({0, 1, 2, 6, 7, 99})), (QList<int>{0, 4, 1, 7, 7, 4}));
    QVERIFY(Import::filePriorities({}).isEmpty());
}

void TestDelugeTorrentsImport::renamesForAFolderAndFiles()
{
    const QStringList current{"Top/a.bin", "Top/sub/b.bin", "Top/sub/c.txt"};
    // Nothing to do.
    QVERIFY(Import::renamesFor(current, current)->isEmpty());
    // The top folder alone: one core.rename_folder.
    auto r = Import::renamesFor(current, {"New/a.bin", "New/sub/b.bin", "New/sub/c.txt"});
    QVERIFY(r.has_value());
    QCOMPARE(r->folderFrom, QStringLiteral("Top/"));
    QCOMPARE(r->folderTo, QStringLiteral("New/"));
    QVERIFY(r->files.isEmpty());
    // The folder, then the file that is still elsewhere, by its whole path.
    r = Import::renamesFor(current, {"New/a.bin", "New/other/B.bin", "New/sub/c.txt"});
    QCOMPARE(r->folderTo, QStringLiteral("New/"));
    QCOMPARE(r->afterFolder, QStringList({"New/a.bin", "New/sub/b.bin", "New/sub/c.txt"}));
    QCOMPARE(r->files, (QList<QPair<int, QString>>{{1, QStringLiteral("New/other/B.bin")}}));
    // Without its top folder (qBittorrent's NoSubfolder): every file, no folder rename.
    r = Import::renamesFor(current, {"a.bin", "sub/b.bin", "sub/c.txt"});
    QVERIFY(r->folderFrom.isEmpty());
    QCOMPARE(r->files.size(), 3);
    QCOMPARE(r->files.at(1), (QPair<int, QString>{1, QStringLiteral("sub/b.bin")}));
    // A single file into a folder of its own (Subfolder).
    r = Import::renamesFor({"a.bin"}, {"a/a.bin"});
    QVERIFY(r->folderFrom.isEmpty());
    QCOMPARE(r->files, (QList<QPair<int, QString>>{{0, QStringLiteral("a/a.bin")}}));
    // What can't be: another count, an empty name or part, one name twice.
    QVERIFY(!Import::renamesFor(current, {"a.bin"}).has_value());
    QVERIFY(!Import::renamesFor(current, {"Top/a.bin", "", "Top/c"}).has_value());
    QVERIFY(!Import::renamesFor(current, {"Top/a.bin", "Top//b", "Top/c"}).has_value());
    QVERIFY(!Import::renamesFor(current, {"Top/a.bin", "Top/x", "Top/x"}).has_value());
}

// ---- the run --------------------------------------------------------------------------------

void TestDelugeTorrentsImport::addsPausedOneAtATimeThenChecksAndResumesTheComplete()
{
    TorrentRecord pack = recordOf(kPack); // complete, started, with limits and priorities
    pack.priorities = QList<int>(namesOf(kPack).size(), 1);
    pack.priorities[0] = 0;
    pack.dlLimit = 300 * 1024;
    pack.ratioLimit = 2;
    pack.sequential = true;
    TorrentRecord single = recordOf(kSingle); // complete here, but stopped at export
    single.state = QStringLiteral("stopped");
    const TorrentRecord nested = recordOf(kNested); // started, and its files are not all here
    const TorrentsBackup::Contents contents = write(
        {pack, single, nested}, {{pack.id, kPack}, {single.id, kSingle}, {nested.id, kNested}});
    m_incomplete.append(nested.infohashV1);

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
    QCOMPARE(outcome.labelsNotSet, 0);
    QVERIFY(outcome.losses.isEmpty() && outcome.misplaced.isEmpty() && outcome.refused.isEmpty());
    QCOMPARE(phases, QStringList({"adding", "checking"}));

    // One core.add_torrent_file each (never the batch call), each sent after the answer to
    // the one before: with FakeHttp answering in order, the three are the first three calls
    // after the plugin's two.
    QVERIFY(m_daemon->callsOf(QStringLiteral("core.add_torrent_files")).isEmpty());
    QCOMPARE(m_daemon->callsOf(QStringLiteral("core.add_torrent_file")).size(), 3);
    const QJsonObject added = m_held.value(pack.infohashV1).options;
    QCOMPARE(added.value(QStringLiteral("add_paused")).toBool(), true);
    QCOMPARE(added.value(QStringLiteral("download_location")).toString(),
             QStringLiteral("/mnt/there"));
    QCOMPARE(added.value(QStringLiteral("max_download_speed")).toDouble(), 300.0);
    QCOMPARE(added.value(QStringLiteral("stop_ratio")).toDouble(), 2.0);
    QCOMPARE(added.value(QStringLiteral("sequential_download")).toBool(), true);
    QCOMPARE(ints(added.value(QStringLiteral("file_priorities"))).mid(0, 2), (QList<int>{0, 4}));
    QVERIFY(!added.contains(QStringLiteral("seed_mode")));
    const Call first = m_daemon->callsOf(QStringLiteral("core.add_torrent_file")).at(0);
    QCOMPARE(QByteArray::fromBase64(first.params.at(1).toString().toLatin1()), torrentBytes(kPack));

    // Priorities only in the add (C40 X6): nothing sets them afterwards.
    QVERIFY(m_daemon->callsOf(QStringLiteral("core.set_torrent_options")).isEmpty());
    // Deluge checks nothing by itself: each one is asked. Only the complete one that was
    // started is resumed.
    QCOMPARE(m_daemon->callsOf(QStringLiteral("core.force_recheck")).size(), 3);
    QCOMPARE(methodsFor(pack.infohashV1),
             QStringList({"core.force_recheck", "core.resume_torrents"}));
    QCOMPARE(methodsFor(single.infohashV1), QStringList({"core.force_recheck"}));
    QVERIFY(!m_held.value(pack.infohashV1).paused);
    QVERIFY(m_held.value(single.infohashV1).paused);
    QVERIFY(m_held.value(nested.infohashV1).paused);
    // Nothing left for a next run.
    QVERIFY(!QFile::exists(o.journalPath));
}

void TestDelugeTorrentsImport::theLabelIsMadeOnceThenSet()
{
    TorrentRecord pack = recordOf(kPack);
    pack.category = QStringLiteral("Films");
    pack.tags = {QStringLiteral("hd")};
    TorrentRecord single = recordOf(kSingle);
    single.category = QStringLiteral("films");
    TorrentRecord nested = recordOf(kNested);
    nested.category = QStringLiteral("old"); // one the daemon has
    const TorrentsBackup::Contents contents =
        write({pack, single, nested},
              {{pack.id, kPack}, {single.id, kSingle}, {nested.id, kNested}},
              EngineKind::Qbittorrent);
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    // label.add once, for the label the daemon lacked, before the first torrent is given it.
    const QList<Call> adds = m_daemon->callsOf(QStringLiteral("label.add"));
    QCOMPARE(adds.size(), 1);
    QCOMPARE(adds.at(0).params.at(0).toString(), QStringLiteral("films"));
    QCOMPARE(outcome.categories, 1);
    QCOMPARE(m_daemon->callsOf(QStringLiteral("label.set_torrent")).size(), 3);
    QCOMPARE(m_held.value(pack.infohashV1).label, QStringLiteral("films"));
    QCOMPARE(m_held.value(single.infohashV1).label, QStringLiteral("films"));
    QCOMPARE(m_held.value(nested.infohashV1).label, QStringLiteral("old"));
    QCOMPARE(outcome.labelsNotSet, 0);
    // The label is never an add option (Deluge ignores it there), and is set before the check.
    QVERIFY(!m_held.value(pack.infohashV1).options.contains(QStringLiteral("label")));
    QCOMPARE(methodsFor(pack.infohashV1),
             QStringList({"label.set_torrent", "core.force_recheck", "core.resume_torrents"}));
    // The tag a Deluge has no place for is a loss the result repeats (C40 X5).
    QCOMPARE(outcome.losses,
             (QList<LibraryCrossing::Loss>{{LibraryCrossing::LossKind::GroupingNames, 1}}));

    // A label someone made since the labels were read: "already exists" is fine.
    init();
    m_daemon->answer = [this](const QString &method, const QJsonArray &params) {
        if (method == QLatin1String("label.get_labels"))
            return QJsonValue(QJsonArray());
        return answer(method, params);
    };
    m_labels.append(QStringLiteral("films"));
    const Import::Outcome raced = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(raced.complete && raced.errors.isEmpty(), qPrintable(raced.errors.join(u"; ")));
    QCOMPARE(raced.categories, 0);
    QCOMPARE(raced.labelsNotSet, 0);
    QCOMPARE(m_held.value(pack.infohashV1).label, QStringLiteral("films"));
}

void TestDelugeTorrentsImport::withThePluginOffNoLabelIsSet()
{
    TorrentRecord pack = recordOf(kPack);
    pack.category = QStringLiteral("films");
    TorrentRecord single = recordOf(kSingle);
    single.category = QStringLiteral("films");
    const TorrentRecord nested = recordOf(kNested); // no label: not counted
    const TorrentsBackup::Contents contents = write(
        {pack, single, nested}, {{pack.id, kPack}, {single.id, kSingle}, {nested.id, kNested}});
    m_plugin = false;
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.added, 3);
    QCOMPARE(outcome.started, 3);
    QCOMPARE(outcome.labelsNotSet, 2);
    QCOMPARE(outcome.categories, 0);
    for (const Call &call : std::as_const(m_daemon->calls))
        QVERIFY2(!call.method.startsWith(QLatin1String("label.")), qPrintable(call.method));

    // Turned off under the run: the first label call says so, and no more are made.
    init();
    m_daemon->answer = [this](const QString &method, const QJsonArray &params) {
        if (method == QLatin1String("label.add"))
            m_plugin = false;
        return answer(method, params);
    };
    const Import::Outcome under = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(under.complete && under.errors.isEmpty(), qPrintable(under.errors.join(u"; ")));
    QCOMPARE(under.labelsNotSet, 2);
    QCOMPARE(under.started, 3);
    QCOMPARE(m_daemon->callsOf(QStringLiteral("label.add")).size(), 1);
    QVERIFY(m_daemon->callsOf(QStringLiteral("label.set_torrent")).isEmpty());
}

void TestDelugeTorrentsImport::aDuplicateIsAlreadyThere()
{
    const TorrentRecord pack = recordOf(kPack);
    const TorrentRecord single = recordOf(kSingle);
    const TorrentsBackup::Contents contents =
        write({pack, single}, {{pack.id, kPack}, {single.id, kSingle}});
    // By any of its hashes the plan skips it.
    Import::Here here;
    here.ids = {pack.infohashV1};
    QCOMPARE(Import::plan(contents, here).skipped, QList<int>{0});

    // It came to the daemon after the plan was made: "Torrent already in session".
    Held there;
    there.name = pack.name;
    there.paused = false;
    m_held.insert(pack.infohashV1, there);
    m_order.append(pack.infohashV1);
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.started, 1);
    // Left as it is: nothing is asked of it.
    QCOMPARE(methodsFor(pack.infohashV1), QStringList());
    QVERIFY(!QFile::exists(options().journalPath));
}

void TestDelugeTorrentsImport::aTorrentTheDaemonRefusesIsNamed()
{
    const TorrentRecord pack = recordOf(kPack);
    const TorrentRecord single = recordOf(kSingle);
    const TorrentsBackup::Contents contents =
        write({pack, single}, {{pack.id, kPack}, {single.id, kSingle}});
    m_refuse.append(pack.name);
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY(outcome.complete); // one torrent's trouble: the run goes on
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.started, 1);
    QCOMPARE(outcome.errors.size(), 1);
    QVERIFY2(outcome.errors.at(0).startsWith(pack.name + QStringLiteral(" was not added")),
             qPrintable(outcome.errors.at(0)));
    QVERIFY(!m_held.contains(pack.infohashV1));
}

void TestDelugeTorrentsImport::renamesAreWaitedForBeforeTheRecheck()
{
    const QStringList own = namesOf(kPack);
    const QString top = own.at(0).section(QLatin1Char('/'), 0, 0);
    QVERIFY(own.size() > 1 && !top.isEmpty());
    // Its top folder renamed, and one file below it.
    TorrentRecord r = recordOf(kPack);
    QStringList wanted;
    for (int i = 0; i < own.size(); ++i) {
        QString name = QStringLiteral("Top Renamed") + own.at(i).mid(top.size());
        if (i == 1)
            name = QStringLiteral("Top Renamed/moved/B renamed.bin");
        wanted.append(name);
        r.renamed.append({i, own.at(i), name});
    }
    const TorrentsBackup::Contents contents = write({r}, {{r.id, kPack}});
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QVERIFY(outcome.misplaced.isEmpty() && outcome.refused.isEmpty());
    QCOMPARE(outcome.started, 1);
    QCOMPARE(m_held.value(r.infohashV1).files, wanted);

    // The folder, then the file by its whole path, then the check, then the resume.
    QCOMPARE(methodsFor(r.infohashV1),
             QStringList({"core.rename_folder",
                          "core.rename_files",
                          "core.force_recheck",
                          "core.resume_torrents"}));
    const Call folder = m_daemon->callsOf(QStringLiteral("core.rename_folder")).at(0);
    QCOMPARE(folder.params.at(1).toString(), top + QLatin1Char('/'));
    QCOMPARE(folder.params.at(2).toString(), QStringLiteral("Top Renamed/"));
    const QJsonArray files =
        m_daemon->callsOf(QStringLiteral("core.rename_files")).at(0).params.at(1).toArray();
    QCOMPARE(files, QJsonArray{QJsonValue(QJsonArray{1, wanted.at(1)})});

    // Deluge renames in the background (here: two reads late). Each rename is read back
    // until it shows before the next call about the torrent goes out.
    const QStringList all = m_daemon->methods();
    const auto readsBetween = [&all](const QString &from, const QString &to) {
        return all.mid(all.indexOf(from) + 1, all.indexOf(to) - all.indexOf(from) - 1)
            .count(QStringLiteral("core.get_torrents_status"));
    };
    QCOMPARE(
        readsBetween(QStringLiteral("core.rename_folder"), QStringLiteral("core.rename_files")), 3);
    QCOMPARE(
        readsBetween(QStringLiteral("core.rename_files"), QStringLiteral("core.force_recheck")), 3);
}

void TestDelugeTorrentsImport::aTorrentWithoutItsTopFolderIsRenamedFileByFile()
{
    // A qBittorrent's "NoSubfolder", and a single file it put in a folder of its own
    // ("Subfolder"): Deluge names each file by its whole path, so both are reached.
    TorrentRecord r = recordOf(kPack);
    r.layout = QStringLiteral("NoSubfolder");
    TorrentRecord single = recordOf(kSingle);
    single.layout = QStringLiteral("Subfolder");
    const TorrentsBackup::Contents contents =
        write({r, single}, {{r.id, kPack}, {single.id, kSingle}}, EngineKind::Qbittorrent);
    const QStringList flat = namesOf(kPack, QStringLiteral("NoSubfolder"));
    const QStringList inFolder = namesOf(kSingle, QStringLiteral("Subfolder"));
    QVERIFY(flat != namesOf(kPack));
    QVERIFY(inFolder.at(0).contains(QLatin1Char('/')));

    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.added, 2);
    QVERIFY(outcome.misplaced.isEmpty());
    QCOMPARE(outcome.started, 2);
    QCOMPARE(m_held.value(r.infohashV1).files, flat);
    QCOMPARE(m_held.value(single.infohashV1).files, inFolder);
    // No folder rename can drop a folder: every file, in one call.
    QVERIFY(m_daemon->callsOf(QStringLiteral("core.rename_folder")).isEmpty());
    QCOMPARE(methodsFor(r.infohashV1),
             QStringList({"core.rename_files", "core.force_recheck", "core.resume_torrents"}));
    const QJsonArray sent =
        m_daemon->callsOf(QStringLiteral("core.rename_files")).at(0).params.at(1).toArray();
    QCOMPARE(sent.size(), flat.size());
    QCOMPARE(sent.at(0).toArray(), (QJsonArray{0, flat.at(0)}));
    QCOMPARE(m_daemon->callsOf(QStringLiteral("core.rename_files")).at(1).params.at(1).toArray(),
             QJsonArray{QJsonValue(QJsonArray{0, inFolder.at(0)})});
}

void TestDelugeTorrentsImport::aRenameThatWouldReplaceAFileIsNotSent()
{
    const QStringList own = namesOf(kPack);
    TorrentRecord r = recordOf(kPack);
    const QString to = own.at(0).section(QLatin1Char('/'), 0, -2) + QStringLiteral("/taken.bin");
    r.renamed = {{0, own.at(0), to}, {1, own.at(1), own.at(1) + QStringLiteral(".new")}};
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
    QVERIFY(outcome.misplaced.isEmpty());
    QCOMPARE(outcome.started, 1);
    QCOMPARE(outcome.leftStopped, 1);
    // None of its renames is sent, its other one neither; it is checked, and never started
    // though the daemon found it complete.
    QCOMPARE(methodsFor(r.infohashV1), QStringList({"core.force_recheck"}));
    QCOMPARE(m_held.value(r.infohashV1).files, own);
    QVERIFY(m_held.value(r.infohashV1).paused);
    QCOMPARE(methodsFor(free.infohashV1),
             QStringList({"core.rename_files", "core.force_recheck", "core.resume_torrents"}));

    // Where the disk can't be seen the renames are sent (#64's rule).
    init();
    o.localPathFor = {};
    const Import::Outcome blind = run(contents, Import::plan(contents, {}), o);
    QVERIFY(blind.refused.isEmpty());
    QCOMPARE(blind.started, 2);
}

void TestDelugeTorrentsImport::aRenameThatNeverShowsLeavesItStopped()
{
    TorrentRecord r = recordOf(kSingle);
    r.renamed = {{0, namesOf(kSingle).at(0), QStringLiteral("renamed.bin")}};
    const TorrentsBackup::Contents contents = write({r}, {{r.id, kSingle}});
    m_renameLag = -1; // the daemon answers, and its files never say the new name
    Import::Options o = options();
    o.pollMs = 1;
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), o);
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.misplaced, QStringList({r.name}));
    QCOMPARE(outcome.started, 0);
    QCOMPARE(outcome.leftStopped, 1);
    QVERIFY(m_held.value(r.infohashV1).paused);

    // A rename the daemon refuses outright: the same.
    init();
    m_daemon->answer = [this](const QString &method, const QJsonArray &params) {
        if (method == QLatin1String("core.rename_files"))
            return errorOf(4, QStringLiteral("rename refused"));
        return answer(method, params);
    };
    const Import::Outcome refused = run(contents, Import::plan(contents, {}), o);
    QVERIFY(refused.complete);
    QCOMPARE(refused.misplaced, QStringList({r.name}));
    QCOMPARE(refused.errors.size(), 1);
    QCOMPARE(refused.started, 0);
}

void TestDelugeTorrentsImport::leaveThemStoppedChecksNothing()
{
    TorrentRecord r = recordOf(kSingle);
    r.renamed = {{0, namesOf(kSingle).at(0), QStringLiteral("renamed.bin")}};
    const TorrentRecord pack = recordOf(kPack);
    const TorrentsBackup::Contents contents = write({r, pack}, {{r.id, kSingle}, {pack.id, kPack}});
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
    // The names are still put right; no check is asked and nothing is resumed.
    QCOMPARE(methodsFor(r.infohashV1), QStringList({"core.rename_files"}));
    QCOMPARE(methodsFor(pack.infohashV1), QStringList());
    QVERIFY(m_held.value(r.infohashV1).paused);
    QVERIFY(!QFile::exists(o.journalPath));
}

void TestDelugeTorrentsImport::theQueueIsPutBackInOrder()
{
    // The file lists them out of queue order; the complete one has no place in it.
    TorrentRecord third = recordOf(kPack);
    third.queuePosition = 3;
    TorrentRecord first = recordOf(kSingle);
    first.queuePosition = 1;
    TorrentRecord done = recordOf(kNested);
    const TorrentsBackup::Contents contents =
        write({third, done, first}, {{third.id, kPack}, {done.id, kNested}, {first.id, kSingle}});
    m_incomplete = QStringList{third.infohashV1, first.infohashV1};
    const Import::Outcome outcome = run(contents, Import::plan(contents, {}), options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    // Added in the exported queue order, then the rest.
    QStringList added;
    for (const Call &call : m_daemon->callsOf(QStringLiteral("core.add_torrent_file")))
        added.append(call.params.at(0).toString().left(40));
    QCOMPARE(added, QStringList({first.id, third.id, done.id}));
    // And those still queued go to the end once more, in that order.
    QStringList bottoms;
    for (const Call &call : m_daemon->callsOf(QStringLiteral("core.queue_bottom")))
        bottoms.append(strings(call.params.at(0)));
    QCOMPARE(bottoms, QStringList({first.infohashV1, third.infohashV1}));
    QCOMPARE(m_order.mid(1), QStringList({first.infohashV1, third.infohashV1}));
}

void TestDelugeTorrentsImport::aLinkWaitsInTheJournalAndIsFinishedLater()
{
    TorrentRecord link = linkRecord();
    link.category = QStringLiteral("m");
    link.priorities = {1, 0};
    link.renamed = {{0, QStringLiteral("it/a.bin"), QStringLiteral("it/A renamed.bin")}};
    link.upLimit = 2048;
    const TorrentRecord single = recordOf(kSingle);
    TorrentsBackup::Contents contents = write({link, single}, {{single.id, kSingle}});
    contents.records[0] = link; // its renames only a record made in memory has
    const Import::Options o = options();
    const Import::Plan plan = Import::plan(contents, {});
    QCOMPARE(plan.magnets, QList<int>{0});

    const Import::Outcome outcome = run(contents, plan, o);
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    // C40 X7: added by its link, paused, counted apart.
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.magnets, 1);
    QCOMPARE(outcome.linksWaiting, 1);
    QCOMPARE(outcome.started, 1); // the other torrent
    const Call add = m_daemon->callsOf(QStringLiteral("core.add_torrent_magnet")).at(0);
    QCOMPARE(add.params.at(0).toString(), link.magnet);
    const Held held = m_held.value(kLinkHash);
    QCOMPARE(held.options.value(QStringLiteral("add_paused")).toBool(), true);
    QCOMPARE(held.options.value(QStringLiteral("download_location")).toString(),
             QStringLiteral("/data"));
    QCOMPARE(held.options.value(QStringLiteral("max_upload_speed")).toDouble(), 2.0);
    QVERIFY(!held.options.contains(QStringLiteral("file_priorities")));
    QVERIFY(held.paused);
    // Its label is set at once; its priorities, names and check have to wait.
    QCOMPARE(held.label, QStringLiteral("m"));
    QCOMPARE(methodsFor(kLinkHash), QStringList({"label.set_torrent"}));

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
    QCOMPARE(Import::plan(contents, here, journal).count(), 0);

    // Its details have not arrived: nothing is done, it keeps waiting.
    m_daemon->calls.clear();
    Import::Outcome later = finishLinks(o);
    QVERIFY(later.complete);
    QCOMPARE(later.finished, 0);
    QCOMPARE(later.linksWaiting, 1);
    QCOMPARE(methodsFor(kLinkHash), QStringList());

    // They arrive, with every file wanted; its data turns out complete on disk.
    m_held[kLinkHash].files = QStringList{"it/a.bin", "it/b.bin"};
    m_held[kLinkHash].priorities = QJsonArray{4, 4};
    m_daemon->calls.clear();
    later = finishLinks(o);
    QVERIFY2(later.complete && later.errors.isEmpty(), qPrintable(later.errors.join(u"; ")));
    QCOMPARE(later.finished, 1);
    QCOMPARE(later.linksWaiting, 0);
    QCOMPARE(later.started, 1);
    // Its priorities first and before its check, its rename, its check, then its resume: it
    // was started at export.
    QCOMPARE(methodsFor(kLinkHash),
             QStringList({"core.set_torrent_options",
                          "core.rename_files",
                          "core.force_recheck",
                          "core.resume_torrents"}));
    QCOMPARE(m_held.value(kLinkHash).priorities, (QJsonArray{4, 0}));
    QCOMPARE(m_held.value(kLinkHash).files, QStringList({"it/A renamed.bin", "it/b.bin"}));
    QVERIFY(!m_held.value(kLinkHash).paused);
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

void TestDelugeTorrentsImport::aFinishedLinkNeverWantsASkippedFileAgain()
{
    // The trap (scripts/dev/research/88-engines-backup): a finished torrent whose skipped
    // file is wanted again can't be removed until the daemon restarts.
    TorrentRecord link = linkRecord();
    link.priorities = {1, 6};
    TorrentsBackup::Contents contents = write({link}, {});
    const Import::Options o = options();
    QCOMPARE(run(contents, Import::plan(contents, {}), o).linksWaiting, 1);
    // Meanwhile someone started it, skipped its second file, and it finished.
    Held &held = m_held[kLinkHash];
    held.files = QStringList{"it/a.bin", "it/b.bin"};
    held.priorities = QJsonArray{4, 0};
    held.checked = true;
    m_daemon->calls.clear();
    const Import::Outcome later = finishLinks(o);
    QVERIFY2(later.complete && later.errors.isEmpty(), qPrintable(later.errors.join(u"; ")));
    QCOMPARE(later.finished, 1);
    QVERIFY(m_daemon->callsOf(QStringLiteral("core.set_torrent_options")).isEmpty());
    QCOMPARE(m_held.value(kLinkHash).priorities, (QJsonArray{4, 0}));

    // Not finished, the same choices are set: nothing was checked yet.
    init();
    QCOMPARE(run(contents, Import::plan(contents, {}), o).linksWaiting, 1);
    m_held[kLinkHash].files = QStringList{"it/a.bin", "it/b.bin"};
    m_held[kLinkHash].priorities = QJsonArray{4, 0};
    finishLinks(o);
    QCOMPARE(m_held.value(kLinkHash).priorities, (QJsonArray{4, 7}));
}

void TestDelugeTorrentsImport::stopAndResumeThroughTheJournal()
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
    QVERIFY(m_held.value(first.infohashV1).paused);
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
    QCOMPARE(m_daemon->callsOf(QStringLiteral("core.add_torrent_file")).size(), 2);
    // Its check was asked by the first run: only its resume is left.
    QCOMPARE(methodsFor(first.infohashV1), QStringList({"core.resume_torrents"}));
    QVERIFY(!m_held.value(first.infohashV1).paused);
    QVERIFY(!QFile::exists(o.journalPath));

    // Stopped between an add and its steps (the journal says "added"): the steps run again,
    // each from what the daemon has by then.
    init();
    journal = Import::Journal{Import::Journal::keyFor(contents.manifest), {}};
    journal.stages.insert(first.id, QStringLiteral("added"));
    QVERIFY(journal.save(o.journalPath));
    Held half;
    half.files = own;
    m_held.insert(first.infohashV1, half);
    m_order.append(first.infohashV1);
    const Import::Outcome finished = run(contents, Import::plan(contents, here, journal), o);
    QVERIFY2(finished.complete && finished.errors.isEmpty(),
             qPrintable(finished.errors.join(u"; ")));
    QCOMPARE(finished.finished, 1);
    QCOMPARE(methodsFor(first.infohashV1),
             QStringList({"core.rename_files", "core.force_recheck", "core.resume_torrents"}));
    QCOMPARE(m_held.value(first.infohashV1).files, QStringList({"renamed.bin"}));

    // Another file's journal is not this file's: nothing of it is "unfinished" here.
    Import::Journal other = journal;
    other.key = QStringLiteral("another export");
    QCOMPARE(Import::plan(contents, here, other).skipped, QList<int>{0});
}

void TestDelugeTorrentsImport::aDaemonGoneKeepsTheJournal()
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

    // A deluge-web that lost its daemon ("Unknown method") ends the run the same way.
    init();
    m_daemon->answer = [this](const QString &method, const QJsonArray &params) {
        if (method == QLatin1String("core.force_recheck"))
            return errorOf(2, QStringLiteral("Unknown method"));
        return answer(method, params);
    };
    const Import::Outcome lost = run(contents, Import::plan(contents, {}), o);
    QVERIFY(!lost.complete);
    QCOMPARE(lost.added, 1);
    QCOMPARE(lost.errors.size(), 1);
    QCOMPARE(Import::Journal::load(o.journalPath).stages.keys(), QStringList({first.id}));
}

void TestDelugeTorrentsImport::whatCantBeKeptIsInTheOutcome()
{
    // A qBittorrent's torrent with what a Deluge has no place for (C40 X5).
    TorrentRecord r = recordOf(kPack);
    r.category = QStringLiteral("films");
    r.tags = {QStringLiteral("hd"), QStringLiteral("x")};
    r.seedingTimeLimit = 600;
    r.inactiveSeedingTimeLimit = 60;
    r.autoTmm = true;
    r.downloadPath = QStringLiteral("/incomplete");
    const TorrentRecord plain = recordOf(kSingle);
    const TorrentsBackup::Contents contents =
        write({r, plain}, {{r.id, kPack}, {plain.id, kSingle}}, EngineKind::Qbittorrent);
    const Import::Plan plan = Import::plan(contents, {});
    const QList<LibraryCrossing::Loss> expected =
        LibraryCrossing::losses(EngineKind::Qbittorrent, EngineKind::Deluge, {r, plain});
    QVERIFY(expected.size() >= 4);
    QCOMPARE(Import::losses(contents, plan), expected);
    const Import::Outcome outcome = run(contents, plan, options());
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.losses, expected);
    // Nothing of it is sent.
    const QJsonObject sent = m_held.value(r.infohashV1).options;
    for (const char *key : {"move_completed", "move_completed_path", "auto_managed", "seed_mode"})
        QVERIFY2(!sent.contains(QLatin1String(key)), key);
    // A Deluge's own file loses nothing.
    const TorrentsBackup::Contents own = write({r}, {{r.id, kPack}});
    QVERIFY(Import::losses(own, Import::plan(own, {})).isEmpty());
}

QTEST_GUILESS_MAIN(TestDelugeTorrentsImport)
#include "tst_delugetorrentsimport.moc"
