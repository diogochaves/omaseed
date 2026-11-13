// App's #236 block (board 56, DECISIONS C38): the renames the details pane's story tells.
// omaseed's own renames are written here as the server answers them OK (R1, R3), into one
// core::RenameHistory per server that lives, loads and saves as the move history does (R9,
// C18 D8). A torrent's name seen changed in a poll is a line too (R4), unless it is the echo of
// omaseed's own rename; a file rename the log says failed takes its line back (R3, C2 D2). The
// declarations sit at the end of App.hpp.
#include "ui/app/App.hpp"

#include "core/ProblemLog.hpp"
#include "core/SyncEngine.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

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
    connect(m_engine, &core::SyncEngine::torrentRenamed, this, &App::recordSeenRename);
    connect(m_engine->problems(),
            &core::ProblemLog::renameWatchFailed,
            this,
            &App::onRenameWatchFailed);
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
    m_sentRenames.clear();
    m_renameWatchRuns.clear();
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

int App::writeRename(const QString &serverId, const RenameRecord &rename)
{
    if (serverId.isEmpty() || serverId != m_renamesServerId)
        return -1;
    RenameRecord line = rename;
    if (!line.at.isValid())
        line.at = QDateTime::currentDateTimeUtc();
    const int id = m_renames.record(line);
    // A fold back to the name it began with took a line away: saved all the same.
    saveRenames();
    return id;
}

int App::recordRename(const QString &serverId, RenameRecord rename)
{
    rename.by = RenameRecord::By::Omaseed;
    const int id = writeRename(serverId, rename);
    if (id < 0)
        return 0;
    if (id > 0)
        m_renameLineOf.insert(rename.hash, id);
    else
        m_renameLineOf.remove(rename.hash);
    return id;
}

void App::recordSeenRename(const QString &hash, const QString &was, const QString &now)
{
    if (m_renamesServerId.isEmpty() || m_renamesServerId != renamesServerId())
        return;
    if (isEchoOfSentRename(hash, now))
        return;
    RenameRecord rename;
    rename.hash = hash;
    rename.what = RenameRecord::What::Torrent;
    rename.by = RenameRecord::By::Elsewhere;
    rename.at = QDateTime::currentDateTimeUtc(); // R4: the poll that saw it
    rename.was = was;
    rename.now = now;
    rename.disk = m_features.renameOnDisk; // R7: Transmission renamed its folder too (K28)
    writeRename(m_renamesServerId, rename);
}

// --- R4's echo ---------------------------------------------------------------------------------

int App::noteSentRename(const QString &hash, const QString &name)
{
    SentRename sent;
    sent.token = m_nextSentRename++;
    sent.name = name;
    m_sentRenames[hash].append(sent);
    return sent.token;
}

void App::settleSentRename(const QString &hash, int token, bool ok)
{
    const auto found = m_sentRenames.find(hash);
    if (found == m_sentRenames.end())
        return; // its echo was seen already, or another server's
    QList<SentRename> &sent = found.value();
    for (qsizetype index = 0; index < sent.size(); ++index) {
        if (sent.at(index).token != token)
            continue;
        if (ok)
            sent[index].answered.start();
        else
            sent.removeAt(index);
        break;
    }
    if (sent.isEmpty())
        m_sentRenames.erase(found);
}

bool App::isEchoOfSentRename(const QString &hash, const QString &name)
{
    const auto found = m_sentRenames.find(hash);
    if (found == m_sentRenames.end())
        return false;
    QList<SentRename> &sent = found.value();
    sent.removeIf([](const SentRename &one) {
        return one.answered.isValid() && one.answered.elapsed() > qint64(kEchoSeconds) * 1000;
    });
    bool echo = false;
    for (qsizetype index = 0; index < sent.size(); ++index) {
        if (sent.at(index).name == name) {
            sent.removeAt(index);
            echo = true;
            break;
        }
    }
    if (sent.isEmpty())
        m_sentRenames.erase(found);
    return echo;
}

// --- R3's take-back ----------------------------------------------------------------------------

void App::noteRenameWatch(int watchId,
                          const QString &hash,
                          const QList<std::pair<QString, QString>> &moves,
                          bool folder)
{
    if (watchId <= 0)
        return;
    // A watch reads for 30 s (C2 D2): what is older has nothing more to say.
    m_renameWatchRuns.removeIf([](const std::pair<const int &, RenameWatchRun &> &entry) {
        return entry.second.age.elapsed() > 60 * 1000;
    });
    RenameWatchRun run;
    run.hash = hash;
    run.moves = moves;
    run.folder = folder;
    run.age.start();
    m_renameWatchRuns.insert(watchId, run);
}

void App::settleRenameWatch(int watchId, int line)
{
    const auto found = m_renameWatchRuns.find(watchId);
    if (found == m_renameWatchRuns.end())
        return;
    if (line <= 0) {
        m_renameWatchRuns.erase(found); // no line to take back
        return;
    }
    if (found->failed) {
        // The log was read before the OK landed: the line goes as soon as it is written.
        const QString hash = found->hash;
        m_renameWatchRuns.erase(found);
        takeBackRename(hash, line);
        return;
    }
    found->line = line;
}

void App::onRenameWatchFailed(int watchId, const QString &hash, const QStringList &files)
{
    const auto found = m_renameWatchRuns.find(watchId);
    if (found == m_renameWatchRuns.end() || found->hash != hash)
        return;
    // qBittorrent names the file by the path it kept (the old one); either is the run's.
    const auto isRuns = [&run = *found](const QString &file) {
        return std::any_of(run.moves.cbegin(), run.moves.cend(), [&](const auto &move) {
            if (file == move.first || file == move.second)
                return true;
            return run.folder && (file.startsWith(move.first + QLatin1Char('/')) ||
                                  file.startsWith(move.second + QLatin1Char('/')));
        });
    };
    if (!files.isEmpty() && std::none_of(files.cbegin(), files.cend(), isRuns))
        return; // another run's failure on the same torrent
    if (found->line <= 0) {
        found->failed = true; // its OK hasn't landed yet
        return;
    }
    const QString runHash = found->hash;
    const int line = found->line;
    m_renameWatchRuns.erase(found);
    takeBackRename(runHash, line);
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
