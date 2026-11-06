// App's #236 block (board 56, DECISIONS C38): the renames the details pane's story tells.
// omaseed's own renames are written here as the server answers them OK (R1, R3), into one
// core::RenameHistory per server that lives, loads and saves as the move history does (R9,
// C18 D8). The declarations sit at the end of App.hpp.
//
// Not here yet (#236's next step): a torrent's name seen changed in a poll (R4), the echo of
// omaseed's own rename in that poll, and the log's failed file rename taking its line back
// (R3, C2 D2). takeBackRename() and lastRenameLineOf() are their seam.
#include "ui/app/App.hpp"

#include "core/SyncEngine.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

namespace omaseed::app {

using core::RenameHistory;
using core::RenameRecord;

namespace {

const QString kSection = QStringLiteral("moves");
const QString kKey = QStringLiteral("renames");

// The paths inside the torrent are the files tab's own ("Pack/Disc 1/a.mkv"): R3's OLD and
// NEW are the last segment, its FOLDER everything before it, the torrent's top folder
// included, as the files tab's tree shows it. "" at the top.
QString ownName(const QString &path)
{
    return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
}

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

} // namespace

void App::setUpRenames()
{
    connect(this, &App::serverChanged, this, &App::loadRenames);
    // R9: dropped when it leaves the server; a full update is the whole library.
    connect(m_engine, &core::SyncEngine::synced, this, [this](bool fullUpdate) {
        if (fullUpdate)
            pruneRenames();
    });
    loadRenames();
}

QString App::renamesServerId() const
{
    // As the move history's (updateMoveServer()): demo and first run keep no record.
    return m_hasServer && !m_demoMode ? m_server.id : QString();
}

void App::loadRenames()
{
    const QString serverId = renamesServerId();
    if (serverId == m_renamesServerId && !serverId.isEmpty())
        return;
    m_renamesServerId = serverId;
    m_renames = serverId.isEmpty()
                    ? RenameHistory()
                    : RenameHistory::fromJson(
                          stateSection(kSection).value(serverId).toObject().value(kKey).toArray());
    m_renameLineOf.clear();
    ++m_renamesRevision;
    Q_EMIT renamesChanged();
}

void App::saveRenames()
{
    if (!m_renamesServerId.isEmpty()) {
        QJsonObject section = stateSection(kSection);
        QJsonObject server = section.value(m_renamesServerId).toObject();
        server.insert(kKey, m_renames.toJson());
        section.insert(m_renamesServerId, server);
        writeStateSection(kSection, section);
    }
    ++m_renamesRevision;
    Q_EMIT renamesChanged();
}

void App::pruneRenames()
{
    if (m_renamesServerId.isEmpty() || m_renamesServerId != renamesServerId() ||
        m_renames.isEmpty())
        return;
    const QStringList hashes = m_engine->torrents()->hashes();
    if (!m_renames.prune(QSet<QString>(hashes.cbegin(), hashes.cend())))
        return;
    m_renameLineOf.removeIf([&hashes](const std::pair<const QString &, int &> &entry) {
        return !hashes.contains(entry.first);
    });
    saveRenames();
}

int App::recordRename(const QString &serverId, RenameRecord rename)
{
    if (serverId.isEmpty() || serverId != m_renamesServerId)
        return 0;
    rename.by = RenameRecord::By::Omaseed;
    if (!rename.at.isValid())
        rename.at = QDateTime::currentDateTimeUtc();
    const int id = m_renames.record(rename);
    // A fold back to the name it began with took a line away: saved all the same.
    if (id > 0)
        m_renameLineOf.insert(rename.hash, id);
    else
        m_renameLineOf.remove(rename.hash);
    saveRenames();
    return id;
}

bool App::takeBackRename(const QString &hash, int id)
{
    if (id <= 0 || !m_renames.remove(hash, id))
        return false;
    if (m_renameLineOf.value(hash) == id)
        m_renameLineOf.remove(hash);
    saveRenames();
    return true;
}

int App::recordFileRun(const QString &serverId,
                       const QString &hash,
                       const QList<std::pair<QString, QString>> &moves,
                       int done)
{
    done = std::min(done, int(moves.size()));
    if (done <= 0)
        return 0; // R3: a run that renamed nothing is no line
    QStringList folders;
    for (int index = 0; index < done; ++index) {
        const QString folder = parentOf(moves.at(index).first);
        if (!folders.contains(folder))
            folders.append(folder);
    }
    RenameRecord rename;
    rename.hash = hash;
    rename.what = done == 1 ? RenameRecord::What::File : RenameRecord::What::Files;
    // R3: the first file renamed stands for the run.
    rename.was = ownName(moves.constFirst().first);
    rename.now = ownName(moves.constFirst().second);
    rename.count = done;
    rename.in = folders.size() == 1 ? folders.constFirst() : QString();
    rename.folders = int(folders.size());
    return recordRename(serverId, rename);
}

} // namespace omaseed::app
