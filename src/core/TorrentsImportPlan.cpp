#include "core/TorrentsImportPlan.hpp"

#include "core/RenameDisk.hpp"
#include "core/TorrentFile.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

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

} // namespace

// ---- save paths -----------------------------------------------------------------------------

QString TorrentsImportPlan::mapPath(const QString &path, const QList<PathRow> &rows)
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

bool TorrentsImportPlan::covers(const QList<PathRow> &rows, const QString &path)
{
    const QString clean = cleanFolder(path);
    return std::any_of(rows.cbegin(), rows.cend(), [&clean](const PathRow &row) {
        return !cleanFolder(row.to).isEmpty() && under(clean, cleanFolder(row.from));
    });
}

QList<TorrentsImportPlan::PathRow>
TorrentsImportPlan::prefill(const TorrentsBackup::Contents &contents,
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

QString TorrentsImportPlan::Journal::keyFor(const TorrentsManifest &manifest)
{
    return manifest.created.toUTC().toString(Qt::ISODate) + QLatin1Char('|') + manifest.serverUrl +
           QLatin1Char('|') + QString::number(manifest.count);
}

QString TorrentsImportPlan::Journal::pathFor(const QString &serverId)
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

TorrentsImportPlan::Journal TorrentsImportPlan::Journal::load(const QString &path)
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
    const QJsonObject links = o.value(QStringLiteral("links")).toObject();
    for (auto it = links.constBegin(); it != links.constEnd(); ++it) {
        if (TorrentsBackup::isTorrentId(it.key()) && it.value().isObject())
            journal.links.insert(it.key(), it.value().toObject());
    }
    return journal;
}

bool TorrentsImportPlan::Journal::save(const QString &path) const
{
    if (path.isEmpty())
        return true;
    if (stages.isEmpty() && links.isEmpty())
        return !QFile::exists(path) || QFile::remove(path);
    QJsonObject torrents;
    for (auto it = stages.constBegin(); it != stages.constEnd(); ++it)
        torrents.insert(it.key(), it.value());
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QJsonObject journal{{QStringLiteral("key"), key}, {QStringLiteral("torrents"), torrents}};
    if (!links.isEmpty()) {
        QJsonObject waiting;
        for (auto it = links.constBegin(); it != links.constEnd(); ++it)
            waiting.insert(it.key(), it.value());
        journal.insert(QStringLiteral("links"), waiting);
    }
    file.write(QJsonDocument(journal).toJson(QJsonDocument::Compact));
    return file.commit();
}

// ---- the plan -------------------------------------------------------------------------------

TorrentsImportPlan::Plan TorrentsImportPlan::plan(const TorrentsBackup::Contents &contents,
                                                  const Here &here,
                                                  const Journal &journal,
                                                  const GroupingOf &grouping)
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
        if (!grouping)
            continue;
        const LibraryCrossing::Grouping g = grouping(r);
        if (!g.category.isEmpty() && !here.categories.contains(g.category) &&
            !categories.contains(g.category))
            categories.append(g.category);
        for (const QString &tag : g.tags) {
            if (!here.tags.contains(tag) && !tags.contains(tag))
                tags.append(tag);
        }
    }
    p.categories = categories;
    p.tags = tags;
    return p;
}

int TorrentsImportPlan::uncovered(const TorrentsBackup::Contents &contents,
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

bool TorrentsImportPlan::renameWouldReplace(const QString &localBase,
                                            const QString &from,
                                            const QString &to)
{
    if (localBase.isEmpty())
        return false;
    const RenameDisk disk(localBase, {});
    return disk.fileTaken(from) && disk.fileTaken(to);
}

std::optional<QStringList> TorrentsImportPlan::namesWanted(const TorrentRecord &record,
                                                           const TorrentFile *parsed,
                                                           const QStringList &current)
{
    const QString kOriginal = QStringLiteral("Original");
    QStringList original;
    QStringList wanted;
    if (parsed != nullptr && parsed->isValid()) {
        original = TorrentsBackup::namesUnder(*parsed, kOriginal);
        wanted = TorrentsBackup::namesUnder(*parsed, record.layout);
    } else {
        // A link: only the daemon knows the .torrent's names, and only under its own layout.
        if (record.layout != kOriginal)
            return std::nullopt;
        original = current;
        wanted = current;
    }
    for (const TorrentRecord::Rename &rename : record.renamed) {
        if (rename.index < 0 || rename.index >= wanted.size())
            return std::nullopt;
        wanted[rename.index] = rename.to;
    }
    // The daemon's files are the record's, one for one: an earlier run may have renamed some.
    if (current.size() == wanted.size())
        return wanted;
    // Or it lists files the record leaves out (padding): by the .torrent's names.
    QStringList out;
    for (const QString &name : current) {
        const qsizetype at = original.indexOf(name);
        out.append(at < 0 ? name : wanted.at(at));
    }
    return out;
}

} // namespace omaseed::core
