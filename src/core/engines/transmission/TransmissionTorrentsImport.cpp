#include "core/engines/transmission/TransmissionTorrentsImport.hpp"

#include "core/ApiError.hpp"
#include "core/TorrentFile.hpp"
#include "core/engines/transmission/TransmissionEngine.hpp"
#include "core/engines/transmission/TransmissionRpc.hpp"
#include "core/engines/transmission/TransmissionTranslate.hpp"

#include <QJsonArray>

#include <algorithm>

namespace omaseed::core {

namespace TT = TransmissionTranslate;

namespace {

using Retry = TransmissionRpc::Retry;

/// tr_torrent_activity: waiting for its check, and in it.
constexpr int kStatusCheckWait = 1;
constexpr int kStatusChecking = 2;
/// seed_ratio_mode / seed_idle_mode: the session's · the torrent's own · none.
constexpr int kModeOwn = 1;
constexpr int kModeNone = 2;
constexpr int kQuietPolls = 3; ///< polls with nothing checking before an unseen one settles

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
    const int status = torrent.value(QLatin1String("status")).toInt();
    return status == kStatusCheckWait || status == kStatusChecking;
}

bool hasMetadata(const QJsonObject &torrent)
{
    return torrent.value(QLatin1String("metadata_percent_complete")).toDouble(1) >= 1;
}

/// Nothing left of its wanted files.
bool isComplete(const QJsonObject &torrent)
{
    return hasMetadata(torrent) &&
           torrent.value(QLatin1String("left_until_done")).toInteger(1) == 0;
}

QJsonObject forTorrent(const QString &hash, QJsonObject params = {})
{
    params.insert(QStringLiteral("ids"), QJsonArray{hash});
    return params;
}

QJsonObject torrentGet(const QStringList &hashes, const QStringList &fields)
{
    QJsonObject params{{QStringLiteral("fields"), QJsonArray::fromStringList(fields)}};
    if (!hashes.isEmpty())
        params.insert(QStringLiteral("ids"), QJsonArray::fromStringList(hashes));
    return params;
}

QStringList fileNames(const QJsonObject &torrent)
{
    QStringList names;
    for (const QJsonValue &file : torrent.value(QLatin1String("files")).toArray())
        names.append(file.toObject().value(QLatin1String("name")).toString());
    return names;
}

/// The daemon can't be reached or won't talk to us: the run ends (docs/24 §7). Anything else
/// is its answer about one torrent.
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
        return true;
    default:
        return false;
    }
}

QJsonArray numbers(const QList<int> &list)
{
    QJsonArray out;
    for (const int n : list)
        out.append(n);
    return out;
}

} // namespace

// ---- the pure half --------------------------------------------------------------------------

TransmissionTorrentsImport::Plan TransmissionTorrentsImport::plan(
    const TorrentsBackup::Contents &contents, const Here &here, const Journal &journal)
{
    // Nothing to create first: a label is there once a torrent carries it.
    return TorrentsImportPlan::plan(contents, here, journal);
}

QStringList TransmissionTorrentsImport::labelsHere(const TorrentsManifest &manifest,
                                                   const TorrentRecord &record)
{
    // docs/DECISIONS.md C40 X4: the category first, then the tags.
    const LibraryCrossing::Grouping g =
        LibraryCrossing::grouping(manifest.engine, EngineKind::Transmission, record);
    QStringList labels;
    for (QString label : g.tags) {
        // "labels cannot contain comma (,) character", and it keeps no empty one.
        label = label.replace(QLatin1Char(','), QLatin1Char(' ')).simplified();
        if (!label.isEmpty() && !labels.contains(label))
            labels.append(label);
    }
    return labels;
}

QList<LibraryCrossing::Loss>
TransmissionTorrentsImport::losses(const TorrentsBackup::Contents &contents, const Plan &plan)
{
    QList<TorrentRecord> records;
    for (const QList<int> *list : {&plan.add, &plan.magnets, &plan.finish}) {
        for (const int i : *list)
            records.append(contents.records.at(i));
    }
    return LibraryCrossing::losses(contents.manifest.engine, EngineKind::Transmission, records);
}

QJsonObject TransmissionTorrentsImport::fileChoices(const QList<int> &priorities)
{
    QList<int> unwanted, low, high;
    for (int i = 0; i < priorities.size(); ++i) {
        switch (priorities.at(i)) {
        case int(FilePriority::Skip):
            unwanted.append(i);
            break;
        case int(FilePriority::Low):
            low.append(i);
            break;
        case int(FilePriority::High):
        case int(FilePriority::Maximal):
            high.append(i);
            break;
        default:
            break;
        }
    }
    // An empty list means every file to Transmission: only lists with files go.
    QJsonObject params;
    if (!unwanted.isEmpty())
        params.insert(QStringLiteral("files_unwanted"), numbers(unwanted));
    if (!high.isEmpty())
        params.insert(QStringLiteral("priority_high"), numbers(high));
    if (!low.isEmpty())
        params.insert(QStringLiteral("priority_low"), numbers(low));
    return params;
}

QJsonObject TransmissionTorrentsImport::addParams(const TorrentRecord &record,
                                                  const QString &savePath,
                                                  const QStringList &labels,
                                                  const QByteArray &metainfo)
{
    // C40 X6: always paused; the check and the start come after its names are right.
    QJsonObject params{{QStringLiteral("paused"), true}};
    if (metainfo.isEmpty()) {
        params.insert(QStringLiteral("filename"), record.magnet);
    } else {
        params = fileChoices(record.priorities);
        params.insert(QStringLiteral("paused"), true);
        params.insert(QStringLiteral("metainfo"), QString::fromLatin1(metainfo.toBase64()));
    }
    if (!savePath.isEmpty())
        params.insert(QStringLiteral("download_dir"), savePath);
    if (!labels.isEmpty())
        params.insert(QStringLiteral("labels"), QJsonArray::fromStringList(labels));
    if (record.sequential)
        params.insert(QStringLiteral("sequential_download"), true);
    return params;
}

QJsonObject TransmissionTorrentsImport::limitParams(const TorrentRecord &record, qint64 speedBytes)
{
    QJsonObject params;
    if (record.dlLimit > 0) {
        params.insert(QStringLiteral("download_limit"), TT::speedUnits(record.dlLimit, speedBytes));
        params.insert(QStringLiteral("download_limited"), true);
    }
    if (record.upLimit > 0) {
        params.insert(QStringLiteral("upload_limit"), TT::speedUnits(record.upLimit, speedBytes));
        params.insert(QStringLiteral("upload_limited"), true);
    }
    // -2 is the session's limit, which a new torrent goes by anyway.
    if (record.ratioLimit >= 0) {
        params.insert(QStringLiteral("seed_ratio_mode"), kModeOwn);
        params.insert(QStringLiteral("seed_ratio_limit"), record.ratioLimit);
    } else if (record.ratioLimit == -1) {
        params.insert(QStringLiteral("seed_ratio_mode"), kModeNone);
    }
    if (record.inactiveSeedingTimeLimit >= 0) {
        params.insert(QStringLiteral("seed_idle_mode"), kModeOwn);
        params.insert(QStringLiteral("seed_idle_limit"), record.inactiveSeedingTimeLimit);
    } else if (record.inactiveSeedingTimeLimit == -1) {
        params.insert(QStringLiteral("seed_idle_mode"), kModeNone);
    }
    return params;
}

std::optional<QList<TransmissionTorrentsImport::RenameStep>>
TransmissionTorrentsImport::renameSteps(const QStringList &current, const QStringList &wanted)
{
    if (current.size() != wanted.size())
        return std::nullopt;
    QList<QStringList> now;
    QList<QStringList> then;
    qsizetype depth = 0;
    for (qsizetype i = 0; i < current.size(); ++i) {
        now.append(current.at(i).split(QLatin1Char('/')));
        then.append(wanted.at(i).split(QLatin1Char('/')));
        // torrent_rename_path names one part of a path: it moves nothing up or down.
        if (now.at(i).size() != then.at(i).size())
            return std::nullopt;
        depth = std::max(depth, now.at(i).size());
    }
    QList<RenameStep> steps;
    // The top folder first, then what is in it: a path is named as it reads by then.
    for (qsizetype d = 0; d < depth; ++d) {
        for (qsizetype i = 0; i < now.size(); ++i) {
            if (d >= now.at(i).size() || now.at(i).at(d) == then.at(i).at(d))
                continue;
            const QStringList from = now.at(i).mid(0, d + 1);
            const QString name = then.at(i).at(d);
            if (name.isEmpty())
                return std::nullopt;
            QStringList to = from;
            to[d] = name;
            for (qsizetype j = 0; j < now.size(); ++j) {
                if (now.at(j).size() <= d)
                    continue;
                const QStringList head = now.at(j).mid(0, d + 1);
                // The name is another file's or folder's now: two would become one.
                if (head == to)
                    return std::nullopt;
                if (head != from)
                    continue;
                // One folder can't become two.
                if (then.at(j).at(d) != name)
                    return std::nullopt;
                now[j][d] = name;
            }
            steps.append({from.join(QLatin1Char('/')), name});
        }
    }
    return steps;
}

std::optional<QStringList> TransmissionTorrentsImport::namesWanted(const TorrentRecord &record,
                                                                   const TorrentFile *parsed,
                                                                   const QStringList &current)
{
    return TorrentsImportPlan::namesWanted(record, parsed, current); // shared with Deluge's
}

int TransmissionTorrentsImport::linksWaiting(const QString &journalPath)
{
    return int(Journal::load(journalPath).links.size());
}

// ---- the run --------------------------------------------------------------------------------

TransmissionTorrentsImport::TransmissionTorrentsImport(TransmissionEngine *engine, QObject *parent)
    : QObject(parent)
    , m_rpc(engine != nullptr ? engine->rpc() : nullptr)
{
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &TransmissionTorrentsImport::poll);
}

TransmissionTorrentsImport::~TransmissionTorrentsImport() = default;

void TransmissionTorrentsImport::begin(const Options &options)
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
    m_file.reset();
    m_quietPolls = 0;
    m_checkTotal = 0;
    m_inFlight = 0;
    m_adding = false;
    m_linksOnly = false;
    m_step = 0;
    m_next = 0;
    m_journal = Journal::load(m_options.journalPath);
}

void TransmissionTorrentsImport::start(const TorrentsBackup::Contents &contents,
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

void TransmissionTorrentsImport::finishLinks(const Options &options)
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

void TransmissionTorrentsImport::cancel()
{
    if (!m_busy)
        return;
    m_outcome.cancelled = true;
    emitFinished();
}

void TransmissionTorrentsImport::fail(const QString &error)
{
    m_outcome.errors.append(error);
    emitFinished();
}

void TransmissionTorrentsImport::done()
{
    m_outcome.complete = true;
    emitFinished();
}

void TransmissionTorrentsImport::emitFinished()
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

bool TransmissionTorrentsImport::failed(const ApiError &error, const QString &what)
{
    if (gone(error)) {
        fail(error.toString());
        return true;
    }
    m_outcome.errors.append(QStringLiteral("%1: %2").arg(what, error.toString()));
    return false;
}

void TransmissionTorrentsImport::saveJournal()
{
    if (!m_journal.save(m_options.journalPath))
        qWarning("TransmissionTorrentsImport: cannot write %s", qPrintable(m_options.journalPath));
}

void TransmissionTorrentsImport::runSteps(QList<Step> steps, std::function<void()> then)
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

TransmissionTorrentsImport::Item TransmissionTorrentsImport::itemFor(int index) const
{
    Item item;
    item.record = m_contents.records.at(index);
    const TorrentRecord &r = item.record;
    // Transmission keys a torrent by its v1 info hash, a hybrid's too.
    item.hash = r.infohashV1.isEmpty() ? r.id : r.infohashV1;
    item.savePath = TorrentsImportPlan::mapPath(r.savePath, m_options.rows);
    item.link = r.isMagnet();
    item.namesAsked = !r.renamed.isEmpty() || r.layout != kOriginal;
    return item;
}

QJsonObject TransmissionTorrentsImport::linkEntry(const Item &item, const QString &stage)
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

void TransmissionTorrentsImport::next()
{
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    switch (m_step++) {
    case 0: {
        // 1. The unit the daemon's speed limits are in.
        const QJsonObject params{{QStringLiteral("fields"), QJsonArray{QStringLiteral("units")}}};
        m_rpc->call(QStringLiteral("session_get"), params, Retry::OnTransient)
            ->then(this, [this, generation](const Result<QJsonObject> &r) {
                if (!live(generation))
                    return;
                if (r.isError())
                    return fail(r.error().toString());
                m_speedBytes = TT::session(r.value()).speedBytes;
                next();
            });
        return;
    }
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
        // has now), or straight to the check.
        m_items.clear();
        for (const int i : std::as_const(m_plan.finish)) {
            Item item = itemFor(i);
            const QString id = item.record.id;
            const QString stage = m_journal.stages.value(id);
            item.keepStopped = stage == kStageHold;
            item.verify = true;
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

void TransmissionTorrentsImport::addBatch()
{
    if (m_next >= m_order.size()) {
        m_adding = false;
        return next();
    }
    const int end = std::min<int>(m_next + m_options.batch, int(m_order.size()));
    m_batch = m_order.mid(m_next, end - m_next);
    m_next = end;
    m_items.clear();
    m_inFlight = 0;
    pumpAdds();
}

void TransmissionTorrentsImport::pumpAdds()
{
    // torrent_add takes one torrent: a few on their way at a time.
    while (m_inFlight < kInFlight && !m_batch.isEmpty()) {
        ++m_inFlight;
        sendAdd(m_batch.takeFirst());
        if (!m_busy)
            return;
    }
    if (m_inFlight > 0)
        return;
    // The batch is in: the journal says so before its steps begin.
    saveJournal();
    followUps(0);
}

void TransmissionTorrentsImport::sendAdd(int index)
{
    const auto item = std::make_shared<Item>(itemFor(index));
    const TorrentRecord &r = item->record;
    QByteArray bytes;
    if (!item->link) {
        QString error;
        bytes = TorrentsBackup::torrentBytes(m_file.get(), m_contents, r.id, &error);
        if (bytes.isEmpty())
            return fail(error);
        // A file a qBittorrent exported before #71 may carry piece layers libtorrent's
        // readers refuse (DECISIONS C9).
        bytes = TorrentFile::repairPieceLayers(bytes);
    }
    const QJsonObject params =
        addParams(r, item->savePath, labelsHere(m_contents.manifest, r), bytes);
    const int generation = m_generation;
    m_rpc->call(QStringLiteral("torrent_add"), params)
        ->then(this, [this, generation, item](const Result<QJsonObject> &answer) {
            if (!live(generation))
                return;
            --m_inFlight;
            const TorrentRecord &rec = item->record;
            if (answer.isError()) {
                // A .torrent or a folder the daemon refuses is one torrent's trouble.
                if (failed(answer.error(), QStringLiteral("%1 was not added").arg(rec.name)))
                    return;
            } else if (answer.value().contains(QStringLiteral("torrent_added"))) {
                const QString hash =
                    text(answer.value().value(QStringLiteral("torrent_added")).toObject(),
                         "hash_string");
                if (!hash.isEmpty())
                    item->hash = hash.toLower();
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
            // `torrent_duplicate`: it came to the daemon since the plan was made. It is
            // already there, as it is (the daemon drops what the add said).
            pumpAdds();
        });
}

void TransmissionTorrentsImport::followUps(int at)
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

    // What torrent_add ignores without a word (a link finished later had them at its add).
    if (!m_linksOnly) {
        const QJsonObject limits = limitParams(item->record, m_speedBytes);
        if (!limits.isEmpty()) {
            steps.append([this, item, limits, generation](std::function<void()> go) {
                m_rpc->call(QStringLiteral("torrent_set"), forTorrent(item->hash, limits))
                    ->then(this, [this, go, generation](const Result<QJsonObject> &x) {
                        if (!live(generation))
                            return;
                        if (x.isError() && failed(x.error(), QStringLiteral("limits")))
                            return;
                        go();
                    });
            });
        }
    }
    // A link has no files yet: its choices and names wait for finishLinks (C40 X7).
    const bool waits = item->link && !m_linksOnly;
    if (!waits) {
        if (m_linksOnly) {
            // The file choices a torrent with its .torrent had in its add.
            const QJsonObject choices = item->record.priorities.size() == item->current.size()
                                            ? fileChoices(item->record.priorities)
                                            : QJsonObject();
            if (!choices.isEmpty()) {
                steps.append([this, item, choices, generation](std::function<void()> go) {
                    m_rpc->call(QStringLiteral("torrent_set"), forTorrent(item->hash, choices))
                        ->then(this, [this, go, generation](const Result<QJsonObject> &x) {
                            if (!live(generation))
                                return;
                            if (x.isError() && failed(x.error(), QStringLiteral("file priorities")))
                                return;
                            go();
                        });
                });
            }
        }
        steps.append([this, item](std::function<void()> go) { applyNames(item, go); });
        // C40 X6: Transmission's own check ran over the old names; one more once they are
        // right. Nothing is asked when all stay stopped.
        steps.append([this, item, generation](std::function<void()> go) {
            if (!item->verify || !m_options.check)
                return go();
            m_rpc->call(QStringLiteral("torrent_verify"), forTorrent(item->hash))
                ->then(this, [this, go, generation](const Result<QJsonObject> &x) {
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

void TransmissionTorrentsImport::applyNames(const std::shared_ptr<Item> &item,
                                            std::function<void()> then)
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
            namesWanted(r, item->link ? nullptr : &parsed, current);
        const std::optional<QList<RenameStep>> renames =
            wanted ? renameSteps(current, *wanted) : std::nullopt;
        if (!renames) {
            // Names torrent_rename_path can't give (the header): left as they are, stopped.
            item->keepStopped = true;
            if (!m_outcome.misplaced.contains(r.name))
                m_outcome.misplaced.append(r.name);
            return then();
        }
        const QString local =
            m_options.localPathFor ? m_options.localPathFor(item->savePath) : QString();
        QList<Step> steps;
        for (const RenameStep &step : *renames) {
            steps.append([this, item, step, local, generation](std::function<void()> go) {
                if (item->refused)
                    return go(); // its paths would no longer be the daemon's
                const qsizetype slash = step.path.lastIndexOf(QLatin1Char('/'));
                const QString to = step.path.left(slash + 1) + step.name;
                // DECISIONS C9: both names on disk. Transmission would answer success and
                // point at the other file, so it is not sent.
                if (TorrentsImportPlan::renameWouldReplace(local, step.path, to)) {
                    item->refused = true;
                    item->keepStopped = true;
                    if (!m_outcome.refused.contains(item->record.name))
                        m_outcome.refused.append(item->record.name);
                    return go();
                }
                const QJsonObject params{{QStringLiteral("path"), step.path},
                                         {QStringLiteral("name"), step.name}};
                m_rpc->call(QStringLiteral("torrent_rename_path"), forTorrent(item->hash, params))
                    ->then(this, [this, item, to, go, generation](const Result<QJsonObject> &x) {
                        if (!live(generation))
                            return;
                        if (x.isError()) {
                            if (failed(x.error(), QStringLiteral("rename %1").arg(to)))
                                return;
                            // What follows would name paths the daemon doesn't have.
                            item->refused = true;
                            item->keepStopped = true;
                        } else {
                            item->verify = true;
                        }
                        go();
                    });
            });
        }
        runSteps(steps, then);
    };
    if (!item->current.isEmpty())
        return rename(item->current);
    // The files as the daemon names them now: what a rename starts from (an earlier run may
    // have done some).
    const QStringList fields{QStringLiteral("hash_string"), QStringLiteral("files")};
    m_rpc->call(QStringLiteral("torrent_get"), torrentGet({item->hash}, fields), Retry::OnTransient)
        ->then(this, [this, rename, generation](const Result<QJsonObject> &x) {
            if (!live(generation))
                return;
            if (x.isError())
                return fail(x.error().toString());
            const QList<QJsonObject> rows = TT::rows(x.value());
            rename(rows.isEmpty() ? QStringList() : fileNames(rows.constFirst()));
        });
}

void TransmissionTorrentsImport::track(const Item &item)
{
    m_byHash.insert(item.hash,
                    Tracked{item.record.id, item.record.started(), item.keepStopped, item.link});
}

void TransmissionTorrentsImport::settleFollowUp(const Item &item)
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

void TransmissionTorrentsImport::readLinks()
{
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    if (m_journal.links.isEmpty())
        return done();
    QStringList hashes;
    for (const QJsonObject &entry : std::as_const(m_journal.links))
        hashes.append(text(entry, "hash"));
    const QStringList fields{QStringLiteral("hash_string"),
                             QStringLiteral("metadata_percent_complete"),
                             QStringLiteral("files")};
    const int generation = m_generation;
    m_rpc->call(QStringLiteral("torrent_get"), torrentGet(hashes, fields), Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonObject> &x) {
            if (!live(generation))
                return;
            if (x.isError())
                return fail(x.error().toString());
            QHash<QString, QJsonObject> rows;
            for (const QJsonObject &row : TT::rows(x.value()))
                rows.insert(text(row, "hash_string").toLower(), row);
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
                item.verify = true; // C40 X7: its check is asked once its details are there
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
                if (!hasMetadata(row))
                    continue; // still waiting
                item.current = fileNames(row);
                if (item.current.isEmpty())
                    continue;
                m_items.append(item);
            }
            m_step = 4; // then the check
            followUps(0);
        });
}

void TransmissionTorrentsImport::moveQueue()
{
    // 3. The queue: torrent_add takes no place in it, and each add goes to the end. The
    // unfinished ones go to the end once more, one by one in their exported order.
    if (m_queued.size() < 2)
        return next();
    std::stable_sort(m_queued.begin(), m_queued.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });
    const int generation = m_generation;
    m_rpc
        ->call(QStringLiteral("torrent_get"),
               torrentGet({}, {QStringLiteral("hash_string")}),
               Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonObject> &x) {
            if (!live(generation))
                return;
            if (x.isError())
                return fail(x.error().toString());
            const int last = std::max(0, int(TT::rows(x.value()).size()) - 1);
            QList<Step> steps;
            for (const auto &queued : std::as_const(m_queued)) {
                const QString hash = queued.second;
                steps.append([this, hash, last, generation](std::function<void()> go) {
                    const QJsonObject params{{QStringLiteral("queue_position"), last}};
                    m_rpc->call(QStringLiteral("torrent_set"), forTorrent(hash, params))
                        ->then(this, [this, go, generation](const Result<QJsonObject> &y) {
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

void TransmissionTorrentsImport::startCheck()
{
    // 4. C40 X6: Transmission checks a paused add by itself, and those renamed were asked
    // again; what is left is to wait for them, then the complete ones start.
    m_checkTotal = int(m_checking.size());
    Q_EMIT progress(QStringLiteral("checking"), 0, m_checkTotal);
    if (m_busy)
        m_pollTimer.start(m_options.pollMs);
}

void TransmissionTorrentsImport::poll()
{
    if (!m_busy)
        return;
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    const QStringList fields{QStringLiteral("hash_string"),
                             QStringLiteral("status"),
                             QStringLiteral("left_until_done"),
                             QStringLiteral("metadata_percent_complete")};
    m_rpc->call(QStringLiteral("torrent_get"), torrentGet(m_checking, fields), Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonObject> &x) {
            if (!live(generation))
                return;
            if (x.isError())
                return fail(x.error().toString());
            const QList<QJsonObject> polled = TT::rows(x.value());
            // Transmission checks one torrent at a time; the others wait (status 1). One
            // never seen checking and not complete settles only after kQuietPolls polls in a
            // row with none of these checking: its check may have ended before the first.
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
                const QString hash = text(t, "hash_string").toLower();
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
            const QJsonObject params{{QStringLiteral("ids"), QJsonArray::fromStringList(toStart)}};
            m_rpc->call(QStringLiteral("torrent_start"), params)
                ->then(this, [this, onward, generation](const Result<QJsonObject> &y) {
                    if (!live(generation))
                        return;
                    if (y.isError() && failed(y.error(), QStringLiteral("start")))
                        return;
                    onward();
                });
        });
}

} // namespace omaseed::core
