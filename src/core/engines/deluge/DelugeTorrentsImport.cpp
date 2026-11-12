#include "core/engines/deluge/DelugeTorrentsImport.hpp"

#include "core/ApiError.hpp"
#include "core/TorrentFile.hpp"
#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/deluge/DelugeRpc.hpp"
#include "core/engines/deluge/DelugeTranslate.hpp"

#include <algorithm>

namespace omaseed::core {

namespace DT = DelugeTranslate;

namespace {

using Retry = DelugeRpc::Retry;

constexpr int kQuietPolls = 3;     ///< polls with nothing checking before an unseen one settles
constexpr int kRenamePollMs = 200; ///< between two reads of a torrent's files after a rename
constexpr int kRenamePolls = 50;   ///< reads before a rename counts as not applied

const QString kOriginal = QStringLiteral("Original");
const QString kStageLink = QStringLiteral("link");
const QString kStageAdded = QStringLiteral("added");
const QString kStageCheck = QStringLiteral("check");
const QString kStageHold = QStringLiteral("hold");

QString text(const QJsonObject &json, const char *name)
{
    return json.value(QLatin1String(name)).toString();
}

bool isChecking(const QJsonObject &torrent)
{
    const QString state = text(torrent, "state");
    return state == QLatin1String("Checking") || state == QLatin1String("Allocating");
}

/// Nothing left of its wanted files.
bool isComplete(const QJsonObject &torrent)
{
    return torrent.value(QLatin1String("is_finished")).toBool();
}

/// core.get_torrents_status for `ids`: an answer keyed by id, none for one that is gone.
QJsonArray statusOf(const QStringList &ids, const QStringList &keys)
{
    return {QJsonObject{{QStringLiteral("id"), QJsonArray::fromStringList(ids)}},
            QJsonArray::fromStringList(keys)};
}

/// Its files' paths, in index order.
QStringList fileNames(const QJsonObject &torrent)
{
    QList<QPair<int, QString>> files;
    int at = 0;
    for (const QJsonValue &value : torrent.value(QLatin1String("files")).toArray()) {
        const QJsonObject file = value.toObject();
        files.append({file.value(QLatin1String("index")).toInt(at), text(file, "path")});
        ++at;
    }
    std::stable_sort(
        files.begin(), files.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList names;
    for (const auto &file : std::as_const(files))
        names.append(file.second);
    return names;
}

/// The folder every one of `names` is in; empty when one isn't in a folder or they differ.
QString topFolder(const QStringList &names)
{
    QString top;
    for (const QString &name : names) {
        const qsizetype slash = name.indexOf(QLatin1Char('/'));
        if (slash <= 0)
            return {};
        const QString first = name.left(slash);
        if (!top.isEmpty() && first != top)
            return {};
        top = first;
    }
    return top;
}

/// deluge-web or its daemon can't be reached or won't talk to us: the run ends (docs/24 §7).
/// "Unknown method" is a deluge-web that lost its daemon. Anything else is the daemon's
/// answer about one torrent.
bool gone(const ApiError &error)
{
    switch (error.kind()) {
    case ApiError::Kind::Network:
    case ApiError::Kind::Certificate:
    case ApiError::Kind::Timeout:
    case ApiError::Kind::Unauthorized:
    case ApiError::Kind::Forbidden:
    case ApiError::Kind::Server:
    case ApiError::Kind::Cancelled:
    case ApiError::Kind::NotFound:
        return true;
    default:
        return false;
    }
}

bool isDuplicate(const ApiError &error)
{
    return error.kind() == ApiError::Kind::Conflict &&
           error.message().contains(QLatin1String("already in session"));
}

} // namespace

// ---- the pure half --------------------------------------------------------------------------

DelugeTorrentsImport::Plan DelugeTorrentsImport::plan(const TorrentsBackup::Contents &contents,
                                                      const Here &here,
                                                      const Journal &journal)
{
    const EngineKind from = contents.manifest.engine;
    return TorrentsImportPlan::plan(contents, here, journal, [from](const TorrentRecord &record) {
        return LibraryCrossing::grouping(from, EngineKind::Deluge, record);
    });
}

QString DelugeTorrentsImport::labelHere(const TorrentsManifest &manifest,
                                        const TorrentRecord &record)
{
    // docs/DECISIONS.md C40 X4: one label, the category or the first label.
    return LibraryCrossing::grouping(manifest.engine, EngineKind::Deluge, record).category;
}

QList<LibraryCrossing::Loss> DelugeTorrentsImport::losses(const TorrentsBackup::Contents &contents,
                                                          const Plan &plan)
{
    QList<TorrentRecord> records;
    for (const QList<int> *list : {&plan.add, &plan.magnets, &plan.finish}) {
        for (const int i : *list)
            records.append(contents.records.at(i));
    }
    return LibraryCrossing::losses(contents.manifest.engine, EngineKind::Deluge, records);
}

QJsonArray DelugeTorrentsImport::filePriorities(const QList<int> &priorities)
{
    QJsonArray out;
    for (const int priority : priorities) {
        switch (priority) {
        case int(FilePriority::Skip):
        case int(FilePriority::Low):
        case int(FilePriority::High):
        case int(FilePriority::Maximal):
            out.append(DT::delugePriority(FilePriority(priority)));
            break;
        default:
            out.append(DT::delugePriority(FilePriority::Normal));
            break;
        }
    }
    return out;
}

QJsonObject DelugeTorrentsImport::addOptions(const TorrentRecord &record,
                                             const QString &savePath,
                                             bool withFiles)
{
    // C40 X6: always paused; the check and the resume come after its names are right.
    QJsonObject options{{QStringLiteral("add_paused"), true},
                        {QStringLiteral("max_download_speed"), DT::speedKiB(record.dlLimit)},
                        {QStringLiteral("max_upload_speed"), DT::speedKiB(record.upLimit)}};
    if (!savePath.isEmpty())
        options.insert(QStringLiteral("download_location"), savePath);
    // C40 X6: in the add, never after a check (the header's trap).
    if (withFiles && !record.priorities.isEmpty())
        options.insert(QStringLiteral("file_priorities"), filePriorities(record.priorities));
    // -2 is the server's limit, which a new torrent goes by anyway.
    if (record.ratioLimit >= 0) {
        // Deluge stops at its ratio, or removes: any other action stops (C40 X5).
        options.insert(QStringLiteral("stop_at_ratio"), true);
        options.insert(QStringLiteral("stop_ratio"), record.ratioLimit);
        options.insert(QStringLiteral("remove_at_ratio"),
                       record.shareLimitAction == QLatin1String("Remove"));
    } else if (record.ratioLimit == -1) {
        options.insert(QStringLiteral("stop_at_ratio"), false);
    }
    if (record.sequential)
        options.insert(QStringLiteral("sequential_download"), true);
    if (record.firstLastPiecePrio)
        options.insert(QStringLiteral("prioritize_first_last_pieces"), true);
    return options;
}

std::optional<DelugeTorrentsImport::Renames>
DelugeTorrentsImport::renamesFor(const QStringList &current, const QStringList &wanted)
{
    if (current.size() != wanted.size())
        return std::nullopt;
    QSet<QString> seen;
    for (const QString &name : wanted) {
        // A path Deluge would take as it is and libtorrent would not: no empty part.
        if (name.isEmpty() || name.split(QLatin1Char('/')).contains(QString()) ||
            seen.contains(name))
            return std::nullopt;
        seen.insert(name);
    }
    Renames renames;
    renames.afterFolder = current;
    const QString from = topFolder(current);
    const QString to = topFolder(wanted);
    if (!from.isEmpty() && !to.isEmpty() && from != to) {
        renames.folderFrom = from + QLatin1Char('/');
        renames.folderTo = to + QLatin1Char('/');
        for (QString &name : renames.afterFolder)
            name = to + name.mid(from.size());
    }
    for (int i = 0; i < int(wanted.size()); ++i) {
        if (renames.afterFolder.at(i) != wanted.at(i))
            renames.files.append({i, wanted.at(i)});
    }
    return renames;
}

int DelugeTorrentsImport::linksWaiting(const QString &journalPath)
{
    return int(Journal::load(journalPath).links.size());
}

// ---- the run --------------------------------------------------------------------------------

DelugeTorrentsImport::DelugeTorrentsImport(DelugeEngine *engine, QObject *parent)
    : QObject(parent)
    , m_rpc(engine != nullptr ? engine->rpc() : nullptr)
{
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &DelugeTorrentsImport::poll);
}

DelugeTorrentsImport::~DelugeTorrentsImport() = default;

void DelugeTorrentsImport::begin(const Options &options)
{
    m_busy = true;
    ++m_generation;
    m_contents = TorrentsBackup::Contents();
    m_plan = Plan();
    m_options = options;
    m_options.batch = std::max(1, m_options.batch);
    m_outcome = Outcome();
    m_order.clear();
    m_batch.clear();
    m_items.clear();
    m_byHash.clear();
    m_queued.clear();
    m_checking.clear();
    m_seenChecking.clear();
    m_labels.clear();
    m_file.reset();
    m_quietPolls = 0;
    m_checkTotal = 0;
    m_adding = false;
    m_labelPlugin = false;
    m_linksOnly = false;
    m_step = 0;
    m_next = 0;
    m_journal = Journal::load(m_options.journalPath);
}

void DelugeTorrentsImport::start(const TorrentsBackup::Contents &contents,
                                 const Plan &plan,
                                 const Options &options)
{
    if (m_busy)
        return;
    begin(options);
    m_contents = contents;
    m_plan = plan;
    m_outcome.losses = losses(contents, plan); // C40 X5: the result repeats them

    // Another file's unfinished torrents are not this run's; the links waiting for their
    // details stay whatever file they came from (C40 X7).
    const QString key = Journal::keyFor(contents.manifest);
    if (m_journal.key != key) {
        m_journal.key = key;
        m_journal.stages.clear();
    }

    // The exported queue order first, then the rest as the file lists them.
    m_order = plan.add + plan.magnets;
    std::stable_sort(m_order.begin(), m_order.end(), [this](int a, int b) {
        const int qa = m_contents.records.at(a).queuePosition;
        const int qb = m_contents.records.at(b).queuePosition;
        if ((qa > 0) != (qb > 0))
            return qa > 0;
        return qa > 0 && qa < qb;
    });

    m_file = std::make_unique<QFile>(contents.path);
    if (!m_file->open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot open %1").arg(contents.path));
    next();
}

void DelugeTorrentsImport::finishLinks(const Options &options)
{
    if (m_busy)
        return;
    begin(options);
    m_linksOnly = true;
    // Never from inside the call: whoever asked hears `finished` after it returns.
    const int generation = m_generation;
    QMetaObject::invokeMethod(
        this,
        [this, generation] {
            if (live(generation))
                readLinks();
        },
        Qt::QueuedConnection);
}

void DelugeTorrentsImport::cancel()
{
    if (!m_busy)
        return;
    m_outcome.cancelled = true;
    emitFinished();
}

void DelugeTorrentsImport::fail(const QString &error)
{
    m_outcome.errors.append(error);
    emitFinished();
}

void DelugeTorrentsImport::done()
{
    m_outcome.complete = true;
    emitFinished();
}

void DelugeTorrentsImport::emitFinished()
{
    m_pollTimer.stop();
    ++m_generation; // answers still on their way are dropped
    m_busy = false;
    m_file.reset();
    m_outcome.linksWaiting = int(std::count_if(
        m_journal.links.cbegin(), m_journal.links.cend(), [](const QJsonObject &entry) {
            return entry.value(QLatin1String("stage")).toString() == kStageLink;
        }));
    saveJournal();
    Q_EMIT finished(m_outcome);
}

bool DelugeTorrentsImport::failed(const ApiError &error, const QString &what)
{
    if (gone(error)) {
        fail(error.toString());
        return true;
    }
    m_outcome.errors.append(QStringLiteral("%1: %2").arg(what, error.toString()));
    return false;
}

void DelugeTorrentsImport::saveJournal()
{
    if (!m_journal.save(m_options.journalPath))
        qWarning("DelugeTorrentsImport: cannot write %s", qPrintable(m_options.journalPath));
}

void DelugeTorrentsImport::runSteps(QList<Step> steps, std::function<void()> then)
{
    if (steps.isEmpty()) {
        then();
        return;
    }
    const Step first = steps.takeFirst();
    const int generation = m_generation;
    first([this, generation, steps, then]() {
        if (live(generation))
            runSteps(steps, then);
    });
}

DelugeTorrentsImport::Item DelugeTorrentsImport::itemFor(int index) const
{
    Item item;
    item.record = m_contents.records.at(index);
    const TorrentRecord &r = item.record;
    // Deluge keys a torrent by its v1 info hash, a hybrid's too; its add says which.
    item.hash = r.infohashV1.isEmpty() ? r.id : r.infohashV1;
    item.savePath = TorrentsImportPlan::mapPath(r.savePath, m_options.rows);
    item.label = labelHere(m_contents.manifest, r);
    item.link = r.isMagnet();
    item.namesAsked = !r.renamed.isEmpty() || r.layout != kOriginal;
    return item;
}

QJsonObject DelugeTorrentsImport::linkEntry(const Item &item, const QString &stage)
{
    // What finishLinks needs once the file is gone. The link itself and its trackers stay
    // out: a private tracker's announce URL carries a passkey, and the daemon has them.
    TorrentRecord kept = item.record;
    kept.magnet.clear(); // and so its layout and renames are written
    QJsonObject record = kept.toJson();
    record.remove(QStringLiteral("trackers"));
    return {{QStringLiteral("stage"), stage},
            {QStringLiteral("hash"), item.hash},
            {QStringLiteral("savePath"), item.savePath},
            {QStringLiteral("record"), record}};
}

void DelugeTorrentsImport::next()
{
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    switch (m_step++) {
    case 0:
        // 1. The Label plugin and its labels.
        return readLabels();
    case 1:
        // 2. The torrents, a batch at a time.
        Q_EMIT progress(QStringLiteral("adding"), 0, m_plan.count());
        if (!m_busy) // stopped from a progress handler
            return;
        m_adding = true;
        addBatch();
        return;
    case 2: {
        // Those an earlier run added: their steps again (each starts from what the daemon
        // has now), or straight to the check, which that run asked for.
        m_items.clear();
        for (const int i : std::as_const(m_plan.finish)) {
            Item item = itemFor(i);
            const QString id = item.record.id;
            const QString stage = m_journal.stages.value(id);
            item.keepStopped = stage == kStageHold;
            ++m_outcome.finished;
            if (stage == kStageAdded) {
                m_items.append(item);
            } else if (m_options.check) {
                track(item);
                m_checking.append(item.hash);
            } else {
                m_journal.stages.remove(id);
            }
        }
        followUps(0);
        return;
    }
    case 3:
        moveQueue();
        return;
    case 4:
        if (m_options.check && !m_checking.isEmpty())
            return startCheck();
        return done();
    default:
        return;
    }
}

void DelugeTorrentsImport::readLabels()
{
    const int generation = m_generation;
    m_rpc->call(QStringLiteral("core.get_enabled_plugins"), {}, Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonValue> &plugins) {
            if (!live(generation))
                return;
            if (plugins.isError())
                return fail(plugins.error().toString());
            m_labelPlugin = plugins.value().toArray().contains(QStringLiteral("Label"));
            if (!m_labelPlugin)
                return next();
            m_rpc->call(QStringLiteral("label.get_labels"), {}, Retry::OnTransient)
                ->then(this, [this, generation](const Result<QJsonValue> &labels) {
                    if (!live(generation))
                        return;
                    if (labels.isError())
                        return fail(labels.error().toString());
                    for (const QJsonValue &label : labels.value().toArray())
                        m_labels.insert(label.toString());
                    next();
                });
        });
}

void DelugeTorrentsImport::addBatch()
{
    if (m_next >= m_order.size()) {
        m_adding = false;
        return next();
    }
    const int end = std::min<int>(m_next + m_options.batch, int(m_order.size()));
    m_batch = m_order.mid(m_next, end - m_next);
    m_next = end;
    m_items.clear();
    sendAdd();
}

void DelugeTorrentsImport::sendAdd()
{
    if (m_batch.isEmpty()) {
        // The batch is in: the journal says so before its steps begin.
        saveJournal();
        return followUps(0);
    }
    // C40 X6: one torrent per call, and the next only after its answer.
    const auto item = std::make_shared<Item>(itemFor(m_batch.takeFirst()));
    const TorrentRecord &r = item->record;
    Reply<QJsonValue> *reply = nullptr;
    if (item->link) {
        reply = m_rpc->call(QStringLiteral("core.add_torrent_magnet"),
                            {r.magnet, addOptions(r, item->savePath, false)});
    } else {
        QString error;
        QByteArray bytes = TorrentsBackup::torrentBytes(m_file.get(), m_contents, r.id, &error);
        if (bytes.isEmpty())
            return fail(error);
        // A file a qBittorrent exported before #71 may carry piece layers libtorrent's
        // readers refuse (DECISIONS C9).
        bytes = TorrentFile::repairPieceLayers(bytes);
        reply = m_rpc->call(QStringLiteral("core.add_torrent_file"),
                            {r.id + QStringLiteral(".torrent"),
                             QString::fromLatin1(bytes.toBase64()),
                             addOptions(r, item->savePath)});
    }
    const int generation = m_generation;
    reply->then(this, [this, generation, item](const Result<QJsonValue> &answer) {
        if (!live(generation))
            return;
        const TorrentRecord &rec = item->record;
        if (answer.isError()) {
            // "Torrent already in session": it came to the daemon since the plan was made.
            // It is already there, as it is. A .torrent or a folder the daemon refuses is
            // one torrent's trouble.
            if (!isDuplicate(answer.error()) &&
                failed(answer.error(), QStringLiteral("%1 was not added").arg(rec.name)))
                return;
        } else if (answer.value().isString()) {
            const QString id = answer.value().toString().toLower();
            if (!id.isEmpty())
                item->hash = id;
            if (item->link) {
                // C40 X7: counted apart; its journal entry waits for its details.
                m_journal.links.insert(rec.id, linkEntry(*item, kStageLink));
                ++m_outcome.magnets;
            } else {
                m_journal.stages.insert(rec.id, kStageAdded);
                ++m_outcome.added;
            }
            m_items.append(*item);
        }
        // No id and no error: an older daemon's word for a torrent already there.
        sendAdd();
    });
}

void DelugeTorrentsImport::followUps(int at)
{
    if (at >= m_items.size()) {
        // The batch is through: what is left goes on, the journal says where it stands.
        saveJournal();
        m_items.clear();
        if (m_adding) {
            Q_EMIT progress(QStringLiteral("adding"), m_next, m_plan.count());
            if (!m_busy)
                return;
            return addBatch();
        }
        return next();
    }
    const auto item = std::make_shared<Item>(m_items.at(at));
    const int generation = m_generation;
    QList<Step> steps;

    // Its label (a link finished later got it at its add).
    if (!m_linksOnly)
        steps.append([this, item](std::function<void()> go) { setLabel(item, go); });
    // A link has no files yet: its priorities and names wait for finishLinks (C40 X7).
    const bool waits = item->link && !m_linksOnly;
    if (!waits) {
        // The file priorities a torrent with its .torrent had in its add: a link's, now
        // that it has files, and before its check (the header's trap).
        if (m_linksOnly && !item->priorities.isEmpty()) {
            steps.append([this, item, generation](std::function<void()> go) {
                const QJsonObject options{{QStringLiteral("file_priorities"), item->priorities}};
                m_rpc
                    ->call(QStringLiteral("core.set_torrent_options"),
                           {QJsonArray{item->hash}, options})
                    ->then(this, [this, go, generation](const Result<QJsonValue> &x) {
                        if (!live(generation))
                            return;
                        if (x.isError() && failed(x.error(), QStringLiteral("file priorities")))
                            return;
                        go();
                    });
            });
        }
        steps.append([this, item](std::function<void()> go) { applyNames(item, go); });
        // C40 X6: Deluge never checks a paused add; it is asked, once the names are right.
        // Nothing is asked when all stay stopped.
        steps.append([this, item, generation](std::function<void()> go) {
            if (!m_options.check)
                return go();
            m_rpc->call(QStringLiteral("core.force_recheck"), {QJsonValue(QJsonArray{item->hash})})
                ->then(this, [this, go, generation](const Result<QJsonValue> &x) {
                    if (!live(generation))
                        return;
                    if (x.isError() && failed(x.error(), QStringLiteral("check")))
                        return;
                    go();
                });
        });
    }
    runSteps(steps, [this, item, at] {
        settleFollowUp(*item);
        followUps(at + 1);
    });
}

void DelugeTorrentsImport::setLabel(const std::shared_ptr<Item> &item, std::function<void()> then)
{
    const QString label = item->label;
    if (label.isEmpty())
        return then();
    if (!m_labelPlugin) {
        // Deluge's Label plugin is off: nothing to set it with. The result says how many.
        ++m_outcome.labelsNotSet;
        return then();
    }
    const int generation = m_generation;
    // The plugin turned off under the run answers "Unknown method"; so does a deluge-web
    // that lost its daemon, which the torrent's next call then finds.
    const auto unset = [this, then](const ApiError &error, const QString &what) {
        ++m_outcome.labelsNotSet;
        if (error.kind() == ApiError::Kind::NotFound) {
            m_labelPlugin = false;
            return then();
        }
        if (!failed(error, what))
            then();
    };
    const auto set = [this, item, label, then, unset, generation] {
        m_rpc->call(QStringLiteral("label.set_torrent"), {item->hash, label})
            ->then(this, [this, label, then, unset, generation](const Result<QJsonValue> &x) {
                if (!live(generation))
                    return;
                if (x.isError())
                    return unset(x.error(), QStringLiteral("label %1").arg(label));
                then();
            });
    };
    if (m_labels.contains(label))
        return set();
    // "Unknown Label" otherwise: made first, once. A label made here has no options of its
    // own, so setting it moves nothing and limits nothing.
    m_rpc->call(QStringLiteral("label.add"), {label})
        ->then(this, [this, label, set, unset, generation](const Result<QJsonValue> &x) {
            if (!live(generation))
                return;
            if (x.isError() && !x.error().message().contains(QLatin1String("exists")))
                return unset(x.error(), QStringLiteral("label %1").arg(label));
            if (!x.isError())
                ++m_outcome.categories;
            m_labels.insert(label);
            set();
        });
}

void DelugeTorrentsImport::misplace(Item &item)
{
    item.keepStopped = true;
    if (!m_outcome.misplaced.contains(item.record.name))
        m_outcome.misplaced.append(item.record.name);
}

void DelugeTorrentsImport::applyNames(const std::shared_ptr<Item> &item, std::function<void()> then)
{
    if (!item->namesAsked)
        return then();
    const int generation = m_generation;
    const auto rename = [this, item, then, generation](const QStringList &current) {
        if (current.isEmpty())
            return then(); // removed meanwhile
        const TorrentRecord &r = item->record;
        TorrentFile parsed;
        if (!item->link) {
            QString error;
            const QByteArray bytes =
                TorrentsBackup::torrentBytes(m_file.get(), m_contents, r.id, &error);
            if (bytes.isEmpty())
                return fail(error);
            parsed = TorrentFile::parse(bytes);
        }
        const std::optional<QStringList> wanted =
            TorrentsImportPlan::namesWanted(r, item->link ? nullptr : &parsed, current);
        const std::optional<Renames> renames = wanted ? renamesFor(current, *wanted) : std::nullopt;
        if (!renames) {
            // Names that can't be (a link under another layout, one name twice): left as
            // they are, stopped.
            misplace(*item);
            return then();
        }
        if (renames->isEmpty())
            return then(); // an earlier run did them
        // DECISIONS C9: both names on disk. Deluge would take the rename and the check
        // would then read the other file, so none of its renames is sent.
        const QString local =
            m_options.localPathFor ? m_options.localPathFor(item->savePath) : QString();
        for (qsizetype i = 0; i < current.size(); ++i) {
            if (current.at(i) != wanted->at(i) &&
                TorrentsImportPlan::renameWouldReplace(local, current.at(i), wanted->at(i))) {
                item->keepStopped = true;
                if (!m_outcome.refused.contains(r.name))
                    m_outcome.refused.append(r.name);
                return then();
            }
        }
        // Each rename is answered at once and done in the background: the next step waits
        // until the daemon's files say it.
        const auto sent = [this, item, generation](const QStringList &expected,
                                                   const QString &what,
                                                   std::function<void()> go) {
            return [this, item, expected, what, go, generation](const Result<QJsonValue> &x) {
                if (!live(generation))
                    return;
                if (x.isError()) {
                    if (failed(x.error(), what))
                        return;
                    misplace(*item);
                    return go();
                }
                awaitNames(item, expected, 0, [this, item, go](bool there) {
                    if (!there)
                        misplace(*item);
                    go();
                });
            };
        };
        QList<Step> steps;
        if (!renames->folderFrom.isEmpty()) {
            steps.append([this, item, renames, sent](std::function<void()> go) {
                m_rpc
                    ->call(QStringLiteral("core.rename_folder"),
                           {item->hash, renames->folderFrom, renames->folderTo})
                    ->then(this,
                           sent(renames->afterFolder,
                                QStringLiteral("rename %1").arg(renames->folderTo),
                                go));
            });
        }
        if (!renames->files.isEmpty()) {
            steps.append([this, item, renames, wanted, sent](std::function<void()> go) {
                if (item->keepStopped)
                    return go(); // the folder's rename never showed: its paths are not these
                QJsonArray files;
                for (const auto &file : renames->files)
                    files.append(QJsonArray{file.first, file.second});
                m_rpc->call(QStringLiteral("core.rename_files"), {item->hash, files})
                    ->then(
                        this,
                        sent(*wanted, QStringLiteral("rename in %1").arg(item->record.name), go));
            });
        }
        runSteps(steps, then);
    };
    if (!item->current.isEmpty())
        return rename(item->current);
    // The files as the daemon names them now: what a rename starts from (an earlier run may
    // have done some).
    m_rpc
        ->call(QStringLiteral("core.get_torrents_status"),
               statusOf({item->hash}, {QStringLiteral("files")}),
               Retry::OnTransient)
        ->then(this, [this, rename, generation](const Result<QJsonValue> &x) {
            if (!live(generation))
                return;
            if (x.isError())
                return fail(x.error().toString());
            const QList<QJsonObject> rows = DT::rows(x.value());
            rename(rows.isEmpty() ? QStringList() : fileNames(rows.constFirst()));
        });
}

void DelugeTorrentsImport::awaitNames(const std::shared_ptr<Item> &item,
                                      const QStringList &expected,
                                      int polls,
                                      std::function<void(bool)> then)
{
    const int generation = m_generation;
    m_rpc
        ->call(QStringLiteral("core.get_torrents_status"),
               statusOf({item->hash}, {QStringLiteral("files")}),
               Retry::OnTransient)
        ->then(this, [=, this](const Result<QJsonValue> &x) {
            if (!live(generation))
                return;
            if (x.isError())
                return fail(x.error().toString());
            const QList<QJsonObject> rows = DT::rows(x.value());
            if (rows.isEmpty() || fileNames(rows.constFirst()) == expected)
                return then(!rows.isEmpty()); // there, or removed meanwhile
            if (polls + 1 >= kRenamePolls)
                return then(false);
            QTimer::singleShot(std::min(kRenamePollMs, m_options.pollMs), this, [=, this] {
                if (live(generation))
                    awaitNames(item, expected, polls + 1, then);
            });
        });
}

void DelugeTorrentsImport::track(const Item &item)
{
    m_byHash.insert(item.hash,
                    Tracked{item.record.id, item.record.started(), item.keepStopped, item.link});
}

void DelugeTorrentsImport::settleFollowUp(const Item &item)
{
    const TorrentRecord &r = item.record;
    if (item.link && !m_linksOnly)
        return; // waits in the journal for its details
    if (m_linksOnly)
        ++m_outcome.finished;
    else if (r.queuePosition > 0)
        m_queued.append({r.queuePosition, item.hash});
    if (m_options.check) {
        const QString stage = item.keepStopped ? kStageHold : kStageCheck;
        if (item.link)
            m_journal.links.insert(r.id, linkEntry(item, stage));
        else
            m_journal.stages.insert(r.id, stage);
        track(item);
        m_checking.append(item.hash);
    } else {
        if (item.link)
            m_journal.links.remove(r.id);
        else
            m_journal.stages.remove(r.id);
        ++m_outcome.leftStopped;
    }
}

void DelugeTorrentsImport::readLinks()
{
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    if (m_journal.links.isEmpty())
        return done();
    QStringList hashes;
    for (const QJsonObject &entry : std::as_const(m_journal.links))
        hashes.append(text(entry, "hash"));
    const QStringList keys{
        QStringLiteral("files"), QStringLiteral("file_priorities"), QStringLiteral("is_finished")};
    const int generation = m_generation;
    m_rpc
        ->call(
            QStringLiteral("core.get_torrents_status"), statusOf(hashes, keys), Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonValue> &x) {
            if (!live(generation))
                return;
            if (x.isError())
                return fail(x.error().toString());
            QHash<QString, QJsonObject> rows;
            for (const QJsonObject &row : DT::rows(x.value()))
                rows.insert(text(row, "hash").toLower(), row);
            const QStringList ids = m_journal.links.keys();
            for (const QString &id : ids) {
                const QJsonObject entry = m_journal.links.value(id);
                const std::optional<TorrentRecord> record =
                    TorrentRecord::fromJson(entry.value(QLatin1String("record")).toObject());
                const QString hash = text(entry, "hash");
                if (!record || !rows.contains(hash)) {
                    m_journal.links.remove(id); // removed from the daemon since
                    continue;
                }
                const QString stage = text(entry, "stage");
                Item item;
                item.record = *record;
                item.hash = hash;
                item.savePath = text(entry, "savePath");
                item.link = true;
                item.namesAsked = !record->renamed.isEmpty() || record->layout != kOriginal;
                if (stage == kStageCheck || stage == kStageHold) {
                    // An earlier finishLinks got as far as its check.
                    item.keepStopped = stage == kStageHold;
                    ++m_outcome.finished;
                    if (m_options.check) {
                        track(item);
                        m_checking.append(hash);
                    } else {
                        m_journal.links.remove(id);
                        ++m_outcome.leftStopped;
                    }
                    continue;
                }
                const QJsonObject row = rows.value(hash);
                item.current = fileNames(row);
                if (item.current.isEmpty())
                    continue; // still waiting for its details
                // Its priorities, unless it is finished and they would want a skipped file
                // again (the header's trap), or they are the daemon's already.
                const QJsonArray now = row.value(QLatin1String("file_priorities")).toArray();
                const QJsonArray wanted = record->priorities.size() == item.current.size()
                                              ? filePriorities(record->priorities)
                                              : QJsonArray();
                bool wakes = false;
                for (qsizetype i = 0; i < wanted.size() && i < now.size(); ++i)
                    wakes = wakes || (now.at(i).toInt() == 0 && wanted.at(i).toInt() != 0);
                if (!wanted.isEmpty() && wanted != now && !(isComplete(row) && wakes))
                    item.priorities = wanted;
                m_items.append(item);
            }
            m_step = 4; // then the check
            followUps(0);
        });
}

void DelugeTorrentsImport::moveQueue()
{
    // 3. The queue: each add goes to its end, and a finished torrent has no place in it. Of
    // those unfinished at export, the ones the daemon still queues go to the end once more,
    // one by one in their exported order (an earlier run may have added some out of order).
    if (m_queued.size() < 2)
        return next();
    std::stable_sort(m_queued.begin(), m_queued.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });
    QStringList ids;
    for (const auto &queued : std::as_const(m_queued))
        ids.append(queued.second);
    const int generation = m_generation;
    m_rpc
        ->call(QStringLiteral("core.get_torrents_status"),
               statusOf(ids, {QStringLiteral("queue")}),
               Retry::OnTransient)
        ->then(this, [this, ids, generation](const Result<QJsonValue> &x) {
            if (!live(generation))
                return;
            if (x.isError())
                return fail(x.error().toString());
            const QJsonObject byId = x.value().toObject();
            QList<Step> steps;
            for (const QString &id : ids) {
                // -1: checked and finished already, or gone. Deluge has no queue place for it.
                if (byId.value(id).toObject().value(QLatin1String("queue")).toInt(-1) < 0)
                    continue;
                steps.append([this, id, generation](std::function<void()> go) {
                    m_rpc->call(QStringLiteral("core.queue_bottom"), {QJsonValue(QJsonArray{id})})
                        ->then(this, [this, go, generation](const Result<QJsonValue> &y) {
                            if (!live(generation))
                                return;
                            if (y.isError() && failed(y.error(), QStringLiteral("queue")))
                                return;
                            go();
                        });
                });
            }
            runSteps(steps, [this] { next(); });
        });
}

void DelugeTorrentsImport::startCheck()
{
    // 4. C40 X6: each torrent's check was asked once its names were right; what is left is
    // to wait for them, then the complete ones are resumed.
    m_checkTotal = int(m_checking.size());
    Q_EMIT progress(QStringLiteral("checking"), 0, m_checkTotal);
    if (m_busy)
        m_pollTimer.start(m_options.pollMs);
}

void DelugeTorrentsImport::poll()
{
    if (!m_busy)
        return;
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    const QStringList keys{QStringLiteral("state"), QStringLiteral("is_finished")};
    m_rpc
        ->call(QStringLiteral("core.get_torrents_status"),
               statusOf(m_checking, keys),
               Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonValue> &x) {
            if (!live(generation))
                return;
            if (x.isError())
                return fail(x.error().toString());
            const QList<QJsonObject> polled = DT::rows(x.value());
            // One never seen checking and not finished settles only after kQuietPolls polls
            // in a row with none of these checking: its check may have ended before the
            // first, or not have begun.
            const bool anyChecking = std::any_of(polled.cbegin(), polled.cend(), isChecking);
            m_quietPolls = anyChecking ? 0 : m_quietPolls + 1;
            QSet<QString> seen;
            QStringList toStart;
            const auto forget = [this](const QString &hash) {
                m_checking.removeAll(hash);
                const Tracked tracked = m_byHash.value(hash);
                if (tracked.link)
                    m_journal.links.remove(tracked.id);
                else
                    m_journal.stages.remove(tracked.id);
                return tracked;
            };
            for (const QJsonObject &t : polled) {
                const QString hash = text(t, "hash").toLower();
                seen.insert(hash);
                if (!m_checking.contains(hash))
                    continue;
                if (isChecking(t)) {
                    m_seenChecking.insert(hash);
                    continue;
                }
                const bool complete = isComplete(t);
                if (!m_seenChecking.contains(hash) && !complete && m_quietPolls < kQuietPolls)
                    continue; // its check may not have begun yet
                // Deluge resumes a paused torrent to check it and pauses it again when the
                // check's alert is handled, a moment after the state stops reading Checking:
                // a resume sent in between is undone by that pause (seen on 2.2.0, where a
                // complete torrent that was started came back Paused). Wait for the pause.
                if (text(t, "state") != QLatin1String("Paused") && m_quietPolls < kQuietPolls)
                    continue;
                const Tracked tracked = forget(hash);
                // "Complete ones start", if they were started (DECISIONS C9, C40 X6).
                if (complete && tracked.started && !tracked.keepStopped) {
                    toStart.append(hash);
                    ++m_outcome.started;
                } else {
                    ++m_outcome.leftStopped;
                }
            }
            // A torrent removed while it was checked: nothing to start.
            for (const QString &hash : QStringList(m_checking)) {
                if (!seen.contains(hash)) {
                    forget(hash);
                    ++m_outcome.leftStopped;
                }
            }
            saveJournal();
            Q_EMIT progress(
                QStringLiteral("checking"), m_checkTotal - int(m_checking.size()), m_checkTotal);
            if (!live(generation))
                return;
            const auto onward = [this] {
                if (m_checking.isEmpty())
                    return done();
                m_pollTimer.start(m_options.pollMs);
            };
            if (toStart.isEmpty())
                return onward();
            // A check leaves a torrent paused, as it was added.
            m_rpc
                ->call(QStringLiteral("core.resume_torrents"),
                       {QJsonValue(QJsonArray::fromStringList(toStart))})
                ->then(this, [this, onward, generation](const Result<QJsonValue> &y) {
                    if (!live(generation))
                        return;
                    if (y.isError() && failed(y.error(), QStringLiteral("start")))
                        return;
                    onward();
                });
        });
}

} // namespace omaseed::core
