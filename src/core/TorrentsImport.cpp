#include "core/TorrentsImport.hpp"

#include "core/QbtClient.hpp"
#include "core/RenameDisk.hpp"
#include "core/TorrentFile.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrlQuery>

#include <algorithm>

namespace omaseed::core {

namespace {

/// A folder as rows compare it: '/' separated, without a trailing '/' (but "/" itself).
QString cleanFolder(const QString &path)
{
    QString out = path.trimmed();
    out.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (out.size() > 1 && out.endsWith(QLatin1Char('/')))
        out.chop(1);
    return out;
}

/// `path` lies in `folder` (or is it), at a folder boundary: "/data" holds "/data/x", not
/// "/database".
bool under(const QString &path, const QString &folder)
{
    if (folder.isEmpty())
        return false;
    if (folder == QLatin1String("/"))
        return path.startsWith(QLatin1Char('/'));
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}

bool isChecking(const QString &state)
{
    return state.startsWith(QLatin1String("checking")) || state == QLatin1String("moving") ||
           state == QLatin1String("queuedForChecking") || state == QLatin1String("allocating");
}

/// A request that may already be done (a category that exists) answers 409: not a failure.
bool fine(const ApiError &error)
{
    return !error.isError() || error.kind() == ApiError::Kind::Conflict;
}

constexpr int kChunk = 100; ///< hashes per recheck, start or info request

} // namespace

// ---- save paths -----------------------------------------------------------------------------

QString TorrentsImport::mapPath(const QString &path, const QList<PathRow> &rows)
{
    const QString clean = cleanFolder(path);
    int best = -1;
    qsizetype bestLength = -1;
    for (int i = 0; i < rows.size(); ++i) {
        const QString from = cleanFolder(rows.at(i).from);
        const QString to = cleanFolder(rows.at(i).to);
        if (from.isEmpty() || to.isEmpty() || !under(clean, from))
            continue;
        if (from.size() > bestLength) {
            best = i;
            bestLength = from.size();
        }
    }
    if (best < 0)
        return path;
    const QString from = cleanFolder(rows.at(best).from);
    const QString to = cleanFolder(rows.at(best).to);
    QString rest = from == QLatin1String("/") ? clean.mid(1) : clean.mid(from.size() + 1);
    if (clean == from)
        return to;
    return to == QLatin1String("/") ? QLatin1Char('/') + rest : to + QLatin1Char('/') + rest;
}

bool TorrentsImport::covers(const QList<PathRow> &rows, const QString &path)
{
    const QString clean = cleanFolder(path);
    return std::any_of(rows.cbegin(), rows.cend(), [&clean](const PathRow &row) {
        return !cleanFolder(row.to).isEmpty() && under(clean, cleanFolder(row.from));
    });
}

QList<TorrentsImport::PathRow> TorrentsImport::prefill(const TorrentsBackup::Contents &contents,
                                                       const std::optional<PathMap> &source,
                                                       const QString &sourceName,
                                                       const PathMap &target,
                                                       const QString &targetName)
{
    QStringList paths;
    for (const TorrentRecord &r : contents.records) {
        for (const QString &p : {r.savePath, r.downloadPath}) {
            const QString clean = cleanFolder(p);
            if (!clean.isEmpty() && !paths.contains(clean))
                paths.append(clean);
        }
    }
    QList<PathRow> rows;
    const auto offer = [&rows, &paths](const QString &from, const QString &to, const QString &who) {
        const QString f = cleanFolder(from);
        const QString t = cleanFolder(to);
        if (f.isEmpty() || t.isEmpty() || f == t)
            return;
        if (std::any_of(rows.cbegin(), rows.cend(), [&f](const PathRow &row) {
                return cleanFolder(row.from) == f;
            }))
            return;
        if (std::none_of(
                paths.cbegin(), paths.cend(), [&f](const QString &p) { return under(p, f); }))
            return;
        rows.append({f, t, who});
    };
    if (source) {
        // The exporting server's remote folders are where this machine sees them; the target
        // sees that place through its own map, or as it is when it has none.
        for (const PathMap::Mapping &m : source->mappings()) {
            if (target.count() == 0)
                offer(m.remote, m.local, sourceName);
            else if (target.coversLocal(m.local))
                offer(m.remote, target.toRemote(m.local), sourceName);
        }
    }
    // A file whose paths are this machine's: the target's map, read backwards.
    for (const PathMap::Mapping &m : target.mappings())
        offer(m.local, m.remote, targetName);
    return rows;
}

// ---- the journal ----------------------------------------------------------------------------

QString TorrentsImport::Journal::keyFor(const TorrentsManifest &manifest)
{
    return manifest.created.toUTC().toString(Qt::ISODate) + QLatin1Char('|') + manifest.serverUrl +
           QLatin1Char('|') + QString::number(manifest.count);
}

QString TorrentsImport::Journal::pathFor(const QString &serverId)
{
    QString base = qEnvironmentVariable("XDG_DATA_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    QString safe = serverId;
    for (QChar &c : safe) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    }
    if (safe.isEmpty())
        safe = QStringLiteral("default");
    return base + QStringLiteral("/omaseed/imports/") + safe + QStringLiteral(".json");
}

TorrentsImport::Journal TorrentsImport::Journal::load(const QString &path)
{
    Journal journal;
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        return journal;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    journal.key = o.value(QStringLiteral("key")).toString();
    const QJsonObject stages = o.value(QStringLiteral("torrents")).toObject();
    for (auto it = stages.constBegin(); it != stages.constEnd(); ++it) {
        if (TorrentsBackup::isTorrentId(it.key()))
            journal.stages.insert(it.key(), it.value().toString());
    }
    return journal;
}

bool TorrentsImport::Journal::save(const QString &path) const
{
    if (path.isEmpty())
        return true;
    if (stages.isEmpty())
        return !QFile::exists(path) || QFile::remove(path);
    QJsonObject torrents;
    for (auto it = stages.constBegin(); it != stages.constEnd(); ++it)
        torrents.insert(it.key(), it.value());
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("key"), key},
                                         {QStringLiteral("torrents"), torrents}})
                   .toJson(QJsonDocument::Compact));
    return file.commit();
}

// ---- the plan -------------------------------------------------------------------------------

TorrentsImport::Plan TorrentsImport::plan(const TorrentsBackup::Contents &contents,
                                          const Here &here,
                                          const Journal &journal)
{
    Plan p;
    const bool resumes = journal.key == Journal::keyFor(contents.manifest);
    QStringList categories;
    QStringList tags;
    for (int i = 0; i < contents.records.size(); ++i) {
        const TorrentRecord &r = contents.records.at(i);
        const QStringList ids = r.ids();
        const bool present = std::any_of(
            ids.cbegin(), ids.cend(), [&here](const QString &id) { return here.ids.contains(id); });
        if (present) {
            if (resumes && journal.stages.contains(r.id)) {
                p.finish.append(i);
            } else {
                p.skipped.append(i);
                p.skippedBytes += r.size;
            }
            continue;
        }
        if (r.isMagnet()) {
            p.magnets.append(i);
        } else {
            p.add.append(i);
            p.addBytes += r.size;
        }
        if (!r.category.isEmpty() && !here.categories.contains(r.category) &&
            !categories.contains(r.category))
            categories.append(r.category);
        for (const QString &tag : r.tags) {
            if (!here.tags.contains(tag) && !tags.contains(tag))
                tags.append(tag);
        }
    }
    p.categories = categories;
    p.tags = tags;
    return p;
}

int TorrentsImport::uncovered(const TorrentsBackup::Contents &contents,
                              const Plan &plan,
                              const QList<PathRow> &rows)
{
    int n = 0;
    for (const QList<int> *list : {&plan.add, &plan.magnets}) {
        for (const int i : *list) {
            if (!covers(rows, contents.records.at(i).savePath))
                ++n;
        }
    }
    return n;
}

bool TorrentsImport::renameWouldReplace(const QString &localBase,
                                        const QString &from,
                                        const QString &to)
{
    if (localBase.isEmpty())
        return false;
    const RenameDisk disk(localBase, {});
    return disk.fileTaken(from) && disk.fileTaken(to);
}

// ---- the run --------------------------------------------------------------------------------

TorrentsImport::TorrentsImport(QbtClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &TorrentsImport::poll);
}

TorrentsImport::~TorrentsImport() = default;

void TorrentsImport::start(const TorrentsBackup::Contents &contents,
                           const Plan &plan,
                           const Options &options)
{
    if (m_busy)
        return;
    m_busy = true;
    ++m_generation;
    m_contents = contents;
    m_plan = plan;
    m_options = options;
    m_options.batch = std::max(1, m_options.batch);
    m_outcome = Outcome();
    m_items.clear();
    m_byId.clear();
    m_queued.clear();
    m_checking.clear();
    m_seenChecking.clear();
    m_polls = 0;
    m_checkTotal = 0;
    m_adding = false;
    m_step = 0;
    m_next = 0;

    m_journal = Journal::load(m_options.journalPath);
    const QString key = Journal::keyFor(contents.manifest);
    if (m_journal.key != key)
        m_journal = Journal{key, {}};

    // The exported queue order first (what qBittorrent queues comes back in that order), then
    // the rest as the file lists them.
    m_order = plan.add + plan.magnets;
    std::stable_sort(m_order.begin(), m_order.end(), [this](int a, int b) {
        const int qa = record(a).queuePosition;
        const int qb = record(b).queuePosition;
        if ((qa > 0) != (qb > 0))
            return qa > 0;
        return qa > 0 && qa < qb;
    });

    m_file = std::make_unique<QFile>(contents.path);
    if (!m_file->open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot open %1").arg(contents.path));
    next();
}

void TorrentsImport::cancel()
{
    if (!m_busy)
        return;
    m_outcome.cancelled = true;
    m_pollTimer.stop();
    ++m_generation;
    m_busy = false;
    saveJournal();
    Q_EMIT finished(m_outcome);
}

void TorrentsImport::fail(const QString &error)
{
    m_outcome.errors.append(error);
    m_pollTimer.stop();
    ++m_generation;
    m_busy = false;
    saveJournal();
    Q_EMIT finished(m_outcome);
}

void TorrentsImport::saveJournal()
{
    if (!m_journal.save(m_options.journalPath))
        qWarning("TorrentsImport: cannot write %s", qPrintable(m_options.journalPath));
}

void TorrentsImport::runSteps(QList<std::function<void(std::function<void()>)>> steps,
                              std::function<void()> then)
{
    if (steps.isEmpty()) {
        then();
        return;
    }
    auto first = steps.takeFirst();
    const int generation = m_generation;
    first([this, generation, steps, then]() {
        if (live(generation))
            runSteps(steps, then);
    });
}

QString TorrentsImport::categoryPathHere(const QString &category) const
{
    QString path =
        m_categoriesHere.value(category).toObject().value(QStringLiteral("savePath")).toString();
    if (path.isEmpty())
        path = category.isEmpty() ? m_savePathHere : m_savePathHere + QLatin1Char('/') + category;
    else if (!QDir::isAbsolutePath(path))
        path = m_savePathHere + QLatin1Char('/') + path;
    return QDir::cleanPath(path);
}

void TorrentsImport::next()
{
    if (m_client.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    switch (m_step++) {
    case 0:
        m_client->webApiVersion()->then(this, [this, generation](const QbtResult<QString> &r) {
            if (!live(generation))
                return;
            if (r.isError())
                return fail(r.error().toString());
            m_apiVersion = r.value();
            next();
        });
        return;
    case 1: {
        // 1. Categories, their folders through the rows (§6: a category's folder goes through
        // the same "from → to" rows as the torrents).
        QList<std::function<void(std::function<void()>)>> steps;
        for (const QString &name : std::as_const(m_plan.categories)) {
            steps.append([this, name](std::function<void()> go) {
                const QString savePath = m_contents.manifest.categories.value(name)
                                             .toObject()
                                             .value(QStringLiteral("savePath"))
                                             .toString();
                const QString mapped =
                    savePath.isEmpty() ? QString() : mapPath(savePath, m_options.rows);
                const int generation = m_generation;
                m_client->torrentsCreateCategory(name, mapped)
                    ->then(this, [this, generation, name, go](const QbtResult<QByteArray> &r) {
                        if (!live(generation))
                            return;
                        if (r.isError() && !fine(r.error()))
                            m_outcome.errors.append(
                                QStringLiteral("category %1: %2").arg(name, r.error().toString()));
                        else if (r.isValid())
                            ++m_outcome.categories;
                        go();
                    });
            });
        }
        runSteps(steps, [this] { next(); });
        return;
    }
    case 2:
        if (m_plan.tags.isEmpty())
            return next();
        m_client->torrentsCreateTags(m_plan.tags)
            ->then(this, [this, generation](const QbtResult<QByteArray> &r) {
                if (!live(generation))
                    return;
                if (r.isError() && !fine(r.error()))
                    m_outcome.errors.append(QStringLiteral("tags: %1").arg(r.error().toString()));
                else
                    m_outcome.tags = int(m_plan.tags.size());
                next();
            });
        return;
    case 3:
        // What automatic management would make of a torrent's folder here (step 2's last part).
        m_client->torrentsCategories()->then(this,
                                             [this, generation](const QbtResult<QJsonObject> &r) {
                                                 if (!live(generation))
                                                     return;
                                                 if (r.isError())
                                                     return fail(r.error().toString());
                                                 m_categoriesHere = r.value();
                                                 next();
                                             });
        return;
    case 4:
        m_client->appPreferences()->then(this, [this, generation](const QbtResult<QJsonObject> &r) {
            if (!live(generation))
                return;
            if (r.isError())
                return fail(r.error().toString());
            m_savePathHere = r.value().value(QStringLiteral("save_path")).toString();
            next();
        });
        return;
    case 5:
        // 2. The torrents, a batch at a time.
        Q_EMIT progress(QStringLiteral("adding"), 0, m_plan.count());
        m_adding = true;
        addBatch();
        return;
    case 6: {
        // Those an earlier run added: their steps again (each one checks what is done), or
        // straight to the check.
        m_items.clear();
        for (const int i : std::as_const(m_plan.finish)) {
            const TorrentRecord &r = record(i);
            const QString stage = m_journal.stages.value(r.id);
            Item item;
            item.index = i;
            item.savePath = mapPath(r.savePath, m_options.rows);
            item.downloadPath =
                r.downloadPath.isEmpty() ? QString() : mapPath(r.downloadPath, m_options.rows);
            item.keepStopped = stage == QLatin1String("hold");
            m_byId.insert(r.id, item);
            if (stage == QLatin1String("added"))
                m_items.append(item);
            else if (m_options.check)
                m_checking.append(r.id);
            else
                m_journal.stages.remove(r.id);
            ++m_outcome.finished;
        }
        followUps(0);
        return;
    }
    case 7:
        moveQueue(0);
        return;
    case 8:
        if (m_options.check && !m_checking.isEmpty())
            return startCheck();
        return done();
    default:
        return;
    }
}

void TorrentsImport::addBatch()
{
    if (m_next >= m_order.size()) {
        m_adding = false;
        return next();
    }
    const int generation = m_generation;
    const int end = std::min<int>(m_next + m_options.batch, int(m_order.size()));
    m_items.clear();

    struct Group
    {
        QbtClient::AddOptions options;
        QList<QbtClient::AddFile> files;
        QStringList urls;
        QList<Item> items;
    };
    QList<Group> groups;
    QHash<QString, int> groupOf;
    for (int at = m_next; at < end; ++at) {
        const int i = m_order.at(at);
        const TorrentRecord &r = record(i);
        Item item;
        item.index = i;
        item.savePath = mapPath(r.savePath, m_options.rows);
        item.downloadPath =
            r.downloadPath.isEmpty() ? QString() : mapPath(r.downloadPath, m_options.rows);
        QByteArray bytes;
        if (!r.isMagnet()) {
            QString error;
            bytes = TorrentsBackup::torrentBytes(m_file.get(), m_contents, r.id, &error);
            if (bytes.isEmpty())
                return fail(error);
            const QStringList own = TorrentFile::parse(bytes).trackers();
            for (const QString &url : r.trackers) {
                if (!own.contains(url))
                    item.extraTrackers.append(url);
            }
        }
        QbtClient::AddOptions o;
        o.savepath = item.savePath;
        o.useDownloadPath = !item.downloadPath.isEmpty();
        if (!item.downloadPath.isEmpty())
            o.downloadPath = item.downloadPath;
        if (!r.category.isEmpty())
            o.category = r.category;
        o.tags = r.tags;
        // Every torrent with metadata comes in stopped, to be checked first (D6 a). A link
        // can't be checked: it starts as it was exported, unless all stay stopped.
        o.stopped = r.isMagnet() ? !(r.started() && m_options.check) : true;
        o.autoTMM = false;
        if (!r.isMagnet()) {
            o.contentLayout =
                r.layout == QLatin1String("Subfolder")     ? QbtClient::ContentLayout::Subfolder
                : r.layout == QLatin1String("NoSubfolder") ? QbtClient::ContentLayout::NoSubfolder
                                                           : QbtClient::ContentLayout::Original;
        }
        if (r.upLimit > 0)
            o.upLimit = r.upLimit;
        if (r.dlLimit > 0)
            o.dlLimit = r.dlLimit;
        if (r.sequential)
            o.sequentialDownload = true;
        if (r.firstLastPiecePrio)
            o.firstLastPiecePrio = true;
        const QString key = QStringList{item.savePath,
                                        item.downloadPath,
                                        r.category,
                                        r.tags.join(QLatin1Char(',')),
                                        r.isMagnet() ? QStringLiteral("link") : r.layout,
                                        QString::number(*o.stopped),
                                        QString::number(r.upLimit),
                                        QString::number(r.dlLimit),
                                        QString::number(r.sequential),
                                        QString::number(r.firstLastPiecePrio)}
                                .join(QChar(0x1f));
        int g = groupOf.value(key, -1);
        if (g < 0) {
            g = int(groups.size());
            groupOf.insert(key, g);
            groups.append(Group{o, {}, {}, {}});
        }
        if (r.isMagnet())
            groups[g].urls.append(r.magnet);
        else
            groups[g].files.append({r.id + QStringLiteral(".torrent"), bytes});
        groups[g].items.append(item);
    }
    m_next = end;

    auto waiting = std::make_shared<int>(int(groups.size()));
    for (const Group &group : std::as_const(groups)) {
        const auto answer = [this, generation, waiting, items = group.items](
                                const QbtResult<QByteArray> &r) {
            if (!live(generation))
                return;
            if (r.isError())
                return fail(r.error().toString());
            // 5.x answers {"added_torrent_ids": […], …}; older ones "Ok." or "Fails.".
            const QByteArray body = r.value().trimmed();
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            QSet<QString> ids;
            bool knows = false;
            if (doc.isObject() && doc.object().contains(QStringLiteral("added_torrent_ids"))) {
                knows = true;
                for (const QJsonValue &id :
                     doc.object().value(QStringLiteral("added_torrent_ids")).toArray())
                    ids.insert(id.toString().toLower());
            }
            const bool refused = body == "Fails.";
            for (const Item &item : items) {
                const TorrentRecord &rec = record(item.index);
                const QStringList recIds = rec.ids();
                const bool in =
                    !refused &&
                    (!knows || std::any_of(recIds.cbegin(),
                                           recIds.cend(),
                                           [&ids](const QString &id) { return ids.contains(id); }));
                if (!in) {
                    m_outcome.errors.append(QStringLiteral("%1 was not added").arg(rec.name));
                    continue;
                }
                m_journal.stages.insert(rec.id, QStringLiteral("added"));
                m_byId.insert(rec.id, item);
                m_items.append(item);
                if (rec.isMagnet())
                    ++m_outcome.magnets;
                else
                    ++m_outcome.added;
            }
            if (--*waiting > 0)
                return;
            saveJournal();
            awaitAdded(0);
        };
        QbtReply<QByteArray> *reply =
            group.urls.isEmpty() ? m_client->torrentsAddMany(group.files, {}, group.options)
                                 : m_client->torrentsAddUrls(group.urls, group.options);
        reply->then(this, answer);
    }
}

void TorrentsImport::awaitAdded(int attempt)
{
    // qBittorrent answers an add before the torrent is in its session: torrents/files says 404
    // for a moment. The batch's steps wait until every one of them is listed.
    QStringList ids;
    for (const Item &item : std::as_const(m_items))
        ids.append(record(item.index).id);
    if (ids.isEmpty())
        return followUps(0);
    const int generation = m_generation;
    TorrentsInfoQuery query;
    query.hashes = ids;
    m_client->torrentsInfo(query)->then(
        this, [this, generation, attempt, ids](const QbtResult<QJsonArray> &r) {
            if (!live(generation))
                return;
            if (r.isError())
                return fail(r.error().toString());
            QSet<QString> listed;
            for (const QJsonValue &v : r.value())
                listed.insert(v.toObject().value(QStringLiteral("hash")).toString().toLower());
            if (listed.size() >= ids.size())
                return followUps(0);
            if (attempt >= 150) { // 30 s
                for (const QString &id : ids) {
                    if (!listed.contains(id))
                        return fail(QStringLiteral("%1 did not show up after its add")
                                        .arg(record(m_byId.value(id).index).name));
                }
            }
            QTimer::singleShot(200, this, [this, generation, attempt] {
                if (live(generation))
                    awaitAdded(attempt + 1);
            });
        });
}

void TorrentsImport::followUps(int at)
{
    const int generation = m_generation;
    if (at >= m_items.size()) {
        // The batch is through: what is left goes on, the journal says where it stands.
        saveJournal();
        m_items.clear();
        if (m_adding) {
            Q_EMIT progress(QStringLiteral("adding"), m_next, m_plan.count());
            return addBatch();
        }
        return next();
    }
    Item item = m_items.at(at);
    const TorrentRecord &r = record(item.index);
    const QString id = r.id;
    QList<std::function<void(std::function<void()>)>> steps;

    // The files as the server names them now: what a rename starts from (an earlier run may
    // have done some).
    auto names = std::make_shared<QStringList>();
    if (!r.isMagnet() && (!r.renamed.isEmpty() || !r.priorities.isEmpty())) {
        steps.append([this, id, names, generation](std::function<void()> go) {
            m_client->torrentsFiles(id)->then(
                this, [this, names, go, generation](const QbtResult<QJsonArray> &res) {
                    if (!live(generation))
                        return;
                    if (res.isError())
                        return fail(res.error().toString());
                    for (const QJsonValue &f : res.value())
                        names->append(f.toObject().value(QStringLiteral("name")).toString());
                    go();
                });
        });
    }
    if (!r.isMagnet() && !r.renamed.isEmpty()) {
        const QString base =
            r.progress < 1 && !item.downloadPath.isEmpty() ? item.downloadPath : item.savePath;
        const QString local = m_options.localPathFor ? m_options.localPathFor(base) : QString();
        steps.append([this, id, names, local, generation, &r](std::function<void()> go) {
            QList<std::function<void(std::function<void()>)>> renames;
            bool refused = false;
            for (const TorrentRecord::Rename &rename : r.renamed) {
                const QString now = names->value(rename.index);
                if (now != rename.from)
                    continue; // done already, or not the file it was
                if (renameWouldReplace(local, rename.from, rename.to)) {
                    refused = true;
                    continue;
                }
                renames.append([this, id, rename, generation](std::function<void()> on) {
                    m_client->torrentsRenameFile(id, rename.from, rename.to)
                        ->then(this,
                               [this, on, generation, rename](const QbtResult<QByteArray> &x) {
                                   if (!live(generation))
                                       return;
                                   if (x.isError())
                                       m_outcome.errors.append(
                                           QStringLiteral("rename %1: %2")
                                               .arg(rename.to, x.error().toString()));
                                   on();
                               });
                });
            }
            if (refused) {
                m_byId[id].keepStopped = true;
                if (!m_outcome.refused.contains(r.name))
                    m_outcome.refused.append(r.name);
            }
            runSteps(renames, go);
        });
    }
    if (!r.isMagnet() && !r.priorities.isEmpty()) {
        QHash<int, QList<int>> byPriority;
        for (int i = 0; i < r.priorities.size(); ++i) {
            if (r.priorities.at(i) != 1)
                byPriority[r.priorities.at(i)].append(i);
        }
        for (auto it = byPriority.constBegin(); it != byPriority.constEnd(); ++it) {
            const int priority = it.key();
            const QList<int> files = it.value();
            steps.append([this, id, priority, files, generation](std::function<void()> go) {
                m_client->torrentsSetFilePrio(id, files, priority)
                    ->then(this, [this, go, generation](const QbtResult<QByteArray> &x) {
                        if (!live(generation))
                            return;
                        if (x.isError())
                            m_outcome.errors.append(
                                QStringLiteral("file priorities: %1").arg(x.error().toString()));
                        go();
                    });
            });
        }
    }
    if (!item.extraTrackers.isEmpty()) {
        const QStringList urls = item.extraTrackers;
        steps.append([this, id, urls, generation](std::function<void()> go) {
            m_client->torrentsAddTrackers(id, urls)->then(
                this, [this, go, generation](const QbtResult<QByteArray> &x) {
                    if (!live(generation))
                        return;
                    if (x.isError())
                        m_outcome.errors.append(
                            QStringLiteral("trackers: %1").arg(x.error().toString()));
                    go();
                });
        });
    }
    const bool shareLimits =
        r.ratioLimit != -2 || r.seedingTimeLimit != -2 || r.inactiveSeedingTimeLimit != -2 ||
        (!r.shareLimitAction.isEmpty() && r.shareLimitAction != QLatin1String("Default"));
    if (shareLimits) {
        steps.append([this, id, generation, &r](std::function<void()> go) {
            std::optional<QbtClient::ShareLimitAction> action;
            if (QbtClient::supportsShareLimitAction(m_apiVersion))
                action = QbtClient::shareLimitActionFromName(r.shareLimitAction);
            m_client
                ->torrentsSetShareLimits(
                    {id}, r.ratioLimit, r.seedingTimeLimit, r.inactiveSeedingTimeLimit, action)
                ->then(this, [this, go, generation](const QbtResult<QByteArray> &x) {
                    if (!live(generation))
                        return;
                    if (x.isError())
                        m_outcome.errors.append(
                            QStringLiteral("share limits: %1").arg(x.error().toString()));
                    go();
                });
        });
    }
    if (r.autoTmm) {
        // Automatic management moves files to the category's folder: turned on only where that
        // folder is the one the files are in, and nothing sits in an incomplete folder.
        if (r.downloadPath.isEmpty() &&
            categoryPathHere(r.category) == QDir::cleanPath(item.savePath)) {
            steps.append([this, id, generation](std::function<void()> go) {
                m_client->torrentsSetAutoManagement({id}, true)
                    ->then(this, [this, go, generation](const QbtResult<QByteArray> &x) {
                        if (!live(generation))
                            return;
                        if (x.isError())
                            m_outcome.errors.append(QStringLiteral("automatic management: %1")
                                                        .arg(x.error().toString()));
                        go();
                    });
            });
        } else {
            ++m_outcome.keptManual;
        }
    }
    if (r.isMagnet() && r.state == QLatin1String("forced") && m_options.check) {
        steps.append([this, id, generation](std::function<void()> go) {
            m_client->torrentsSetForceStart({id}, true)
                ->then(this, [this, go, generation](const QbtResult<QByteArray> &x) {
                    if (!live(generation))
                        return;
                    if (x.isError())
                        m_outcome.errors.append(
                            QStringLiteral("force start: %1").arg(x.error().toString()));
                    go();
                });
        });
    }

    runSteps(steps, [this, at, id, &r] {
        if (r.queuePosition > 0 && !r.isMagnet())
            m_queued.append(m_byId.value(id).index);
        if (r.isMagnet()) {
            m_journal.stages.remove(id);
            if (r.started() && m_options.check)
                ++m_outcome.started;
        } else if (m_options.check) {
            m_journal.stages.insert(id,
                                    m_byId.value(id).keepStopped ? QStringLiteral("hold")
                                                                 : QStringLiteral("check"));
            m_checking.append(id);
        } else {
            m_journal.stages.remove(id);
            ++m_outcome.leftStopped;
        }
        followUps(at + 1);
    });
}

void TorrentsImport::moveQueue(int at)
{
    // 3. The queue: qBittorrent puts each add at the bottom, and a multipart add in no fixed
    // order, so the queued ones go to the bottom again one by one in their exported order.
    if (at == 0)
        std::stable_sort(m_queued.begin(), m_queued.end(), [this](int a, int b) {
            return record(a).queuePosition < record(b).queuePosition;
        });
    if (at >= m_queued.size() || m_queued.size() < 2)
        return next();
    const int generation = m_generation;
    m_client->torrentsBottomPrio({record(m_queued.at(at)).id})
        ->then(this, [this, at, generation](const QbtResult<QByteArray> &) {
            // 409: queueing is off on this server; the order then means nothing.
            if (live(generation))
                moveQueue(at + 1);
        });
}

void TorrentsImport::startCheck()
{
    // 4. D6 a: qBittorrent checks them all, then the complete ones start.
    m_checkTotal = int(m_checking.size());
    Q_EMIT progress(QStringLiteral("checking"), 0, m_checkTotal);
    const int generation = m_generation;
    auto waiting = std::make_shared<int>(0);
    for (int at = 0; at < m_checking.size(); at += kChunk) {
        ++*waiting;
        m_client->torrentsRecheck(m_checking.mid(at, kChunk))
            ->then(this, [this, generation, waiting](const QbtResult<QByteArray> &r) {
                if (!live(generation))
                    return;
                if (r.isError())
                    return fail(r.error().toString());
                if (--*waiting == 0)
                    m_pollTimer.start(m_options.pollMs);
            });
    }
}

void TorrentsImport::poll()
{
    if (!m_busy)
        return;
    ++m_polls;
    const int generation = m_generation;
    auto waiting = std::make_shared<int>(0);
    auto toStart = std::make_shared<QStringList>();
    auto toForce = std::make_shared<QStringList>();
    auto seen = std::make_shared<QSet<QString>>();
    const QStringList checking = m_checking;
    const auto settleAll = [this, generation, toStart, toForce] {
        saveJournal();
        Q_EMIT progress(
            QStringLiteral("checking"), m_checkTotal - int(m_checking.size()), m_checkTotal);
        QList<std::function<void(std::function<void()>)>> steps;
        for (int at = 0; at < toStart->size(); at += kChunk) {
            const QStringList chunk = toStart->mid(at, kChunk);
            steps.append([this, chunk, generation](std::function<void()> go) {
                m_client->torrentsStart(chunk)->then(
                    this, [this, go, generation](const QbtResult<QByteArray> &x) {
                        if (!live(generation))
                            return;
                        if (x.isError())
                            m_outcome.errors.append(
                                QStringLiteral("start: %1").arg(x.error().toString()));
                        go();
                    });
            });
        }
        if (!toForce->isEmpty()) {
            const QStringList forced = *toForce;
            steps.append([this, forced, generation](std::function<void()> go) {
                m_client->torrentsSetForceStart(forced, true)
                    ->then(this, [this, go, generation](const QbtResult<QByteArray> &x) {
                        if (!live(generation))
                            return;
                        if (x.isError())
                            m_outcome.errors.append(
                                QStringLiteral("force start: %1").arg(x.error().toString()));
                        go();
                    });
            });
        }
        runSteps(steps, [this] {
            if (m_checking.isEmpty())
                return done();
            m_pollTimer.start(m_options.pollMs);
        });
    };
    for (int at = 0; at < checking.size(); at += kChunk) {
        ++*waiting;
        TorrentsInfoQuery query;
        query.hashes = checking.mid(at, kChunk);
        m_client->torrentsInfo(query)->then(
            this,
            [this, generation, waiting, toStart, toForce, seen, settleAll](
                const QbtResult<QJsonArray> &r) {
                if (!live(generation))
                    return;
                if (r.isError())
                    return fail(r.error().toString());
                QList<QJsonObject> settled;
                for (const QJsonValue &v : r.value()) {
                    const QJsonObject t = v.toObject();
                    const QString id = t.value(QStringLiteral("hash")).toString().toLower();
                    seen->insert(id);
                    const QString state = t.value(QStringLiteral("state")).toString();
                    if (isChecking(state)) {
                        m_seenChecking.insert(id);
                        continue;
                    }
                    const bool complete =
                        t.value(QStringLiteral("amount_left")).toInteger(1) == 0 &&
                        t.value(QStringLiteral("progress")).toDouble() >= 0.9999;
                    if (!m_seenChecking.contains(id) && !complete && m_polls < 3)
                        continue; // its check may not have begun yet
                    settled.append(t);
                }
                for (const QJsonObject &t : std::as_const(settled)) {
                    const QString id = t.value(QStringLiteral("hash")).toString().toLower();
                    m_checking.removeAll(id);
                    m_journal.stages.remove(id);
                    const Item item = m_byId.value(id);
                    const bool complete =
                        t.value(QStringLiteral("amount_left")).toInteger(1) == 0 &&
                        t.value(QStringLiteral("progress")).toDouble() >= 0.9999;
                    const bool known = item.index >= 0;
                    const QString exported = known ? record(item.index).state : QString();
                    if (complete && known && exported != QLatin1String("stopped") &&
                        !item.keepStopped) {
                        toStart->append(id);
                        if (exported == QLatin1String("forced"))
                            toForce->append(id);
                        ++m_outcome.started;
                    } else {
                        ++m_outcome.leftStopped;
                    }
                }
                if (--*waiting == 0) {
                    // A torrent deleted while it was checked: nothing to start.
                    for (const QString &id : QStringList(m_checking)) {
                        if (!seen->contains(id)) {
                            m_checking.removeAll(id);
                            m_journal.stages.remove(id);
                            ++m_outcome.leftStopped;
                        }
                    }
                    settleAll();
                }
            });
    }
    if (*waiting == 0)
        settleAll();
}

void TorrentsImport::done()
{
    m_busy = false;
    ++m_generation;
    m_outcome.complete = true;
    saveJournal();
    Q_EMIT finished(m_outcome);
}

} // namespace omaseed::core
