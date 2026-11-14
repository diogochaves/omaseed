// App's #236/#248 block (boards 56 and 58, DECISIONS C38, C43): the record the details pane's
// story tells from. omaseed's own renames are written here as the server answers them OK (R1,
// R3), into one core::StoryRecord per server that lives, loads and saves as the move history
// does (R9, C18 D8). A torrent's name seen changed in a poll is a line too (R4), unless it is
// the echo of omaseed's own rename; a file rename the log says failed takes its line back (R3,
// C2 D2). omaseed's own category and tag edits are lines at their OK too (C43 Y2, Y3), noted for
// their echo the way renames are (Y8). A filing or place edit a poll sees is a line unless it is
// such an echo or omaseed's own move (Y4–Y8). The declarations sit at the end of App.hpp.
#include "ui/app/App.hpp"

#include "core/CategoryModel.hpp"
#include "core/MoveHistory.hpp"
#include "core/ProblemLog.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/MoveController.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <memory>

namespace omaseed::app {

using core::StoryEntry;
using core::StoryRecord;

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

void App::setUpStory()
{
    connect(this, &App::serverChanged, this, &App::loadStory);
    // R9: dropped when it leaves the server; a full update is the whole library.
    connect(m_engine, &core::SyncEngine::synced, this, [this](bool fullUpdate) {
        if (fullUpdate)
            pruneStory();
    });
    connect(m_engine, &core::SyncEngine::torrentRenamed, this, &App::recordSeenRename);
    connect(m_engine, &core::SyncEngine::torrentsEdited, this, &App::recordSeenEdits);
    connect(m_engine->problems(),
            &core::ProblemLog::renameWatchFailed,
            this,
            &App::onRenameWatchFailed);
    loadStory();
}

QString App::storyServerId() const
{
    // As the move history's (updateMoveServer()): demo and first run keep no record.
    return m_hasServer && !m_demoMode ? m_server.id : QString();
}

void App::loadStory()
{
    const QString serverId = storyServerId();
    if (serverId == m_storyServerId && !serverId.isEmpty())
        return;
    m_storyServerId = serverId;
    m_story = serverId.isEmpty()
                  ? StoryRecord()
                  : StoryRecord::fromJson(
                        stateSection(kSection).value(serverId).toObject().value(kKey).toArray());
    m_renameLineOf.clear();
    m_sentRenames.clear();
    m_sentCategories.clear();
    m_sentTags.clear();
    m_renameWatchRuns.clear();
    ++m_storyRevision;
    Q_EMIT storyChanged();
}

void App::saveStory()
{
    if (!m_storyServerId.isEmpty()) {
        QJsonObject section = stateSection(kSection);
        QJsonObject server = section.value(m_storyServerId).toObject();
        server.insert(kKey, m_story.toJson());
        section.insert(m_storyServerId, server);
        writeStateSection(kSection, section);
    }
    ++m_storyRevision;
    Q_EMIT storyChanged();
}

void App::pruneStory()
{
    if (m_storyServerId.isEmpty() || m_storyServerId != storyServerId() || m_story.isEmpty())
        return;
    const QStringList hashes = m_engine->torrents()->hashes();
    if (!m_story.prune(QSet<QString>(hashes.cbegin(), hashes.cend())))
        return;
    m_renameLineOf.removeIf([&hashes](const std::pair<const QString &, int &> &entry) {
        return !hashes.contains(entry.first);
    });
    saveStory();
}

int App::writeEntry(const QString &serverId, const StoryEntry &entry)
{
    if (serverId.isEmpty() || serverId != m_storyServerId)
        return -1;
    StoryEntry line = entry;
    if (!line.at.isValid())
        line.at = QDateTime::currentDateTimeUtc();
    const int id = m_story.record(line);
    // A fold back to the name it began with took a line away: saved all the same.
    saveStory();
    return id;
}

int App::recordRename(const QString &serverId, StoryEntry rename)
{
    rename.by = StoryEntry::By::Omaseed;
    const int id = writeEntry(serverId, rename);
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
    if (m_storyServerId.isEmpty() || m_storyServerId != storyServerId())
        return;
    if (isEchoOfSentRename(hash, now))
        return;
    StoryEntry rename;
    rename.hash = hash;
    rename.what = StoryEntry::What::Torrent;
    rename.by = StoryEntry::By::Elsewhere;
    rename.at = QDateTime::currentDateTimeUtc(); // R4: the poll that saw it
    rename.was = was;
    rename.now = now;
    rename.disk = m_features.renameOnDisk; // R7: Transmission renamed its folder too (K28)
    writeEntry(m_storyServerId, rename);
}

// --- R4's echo ---------------------------------------------------------------------------------

int App::noteSent(SentEdits &notes, const QString &hash, SentEdit edit)
{
    edit.token = m_nextSentEdit++;
    edit.answered.invalidate();
    const int token = edit.token;
    notes[hash].append(std::move(edit));
    return token;
}

void App::settleSent(SentEdits &notes, const QString &hash, int token, bool ok)
{
    const auto found = notes.find(hash);
    if (found == notes.end())
        return; // its echo was seen already, or another server's
    QList<SentEdit> &sent = found.value();
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
        notes.erase(found);
}

void App::dropStaleSent(SentEdits &notes, const QString &hash)
{
    const auto found = notes.find(hash);
    if (found == notes.end())
        return;
    found.value().removeIf([](const SentEdit &one) {
        return one.answered.isValid() && one.answered.elapsed() > qint64(kEchoSeconds) * 1000;
    });
    if (found.value().isEmpty())
        notes.erase(found);
}

int App::noteSentRename(const QString &hash, const QString &name)
{
    SentEdit sent;
    sent.name = name;
    return noteSent(m_sentRenames, hash, std::move(sent));
}

void App::settleSentRename(const QString &hash, int token, bool ok)
{
    settleSent(m_sentRenames, hash, token, ok);
}

bool App::isEchoOfSentRename(const QString &hash, const QString &name)
{
    dropStaleSent(m_sentRenames, hash);
    const auto found = m_sentRenames.find(hash);
    if (found == m_sentRenames.end())
        return false;
    QList<SentEdit> &sent = found.value();
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

// --- C43 Y2, Y3: omaseed's own filing edits ----------------------------------------------------

void App::writeEntries(const QString &serverId, const QList<StoryEntry> &entries)
{
    if (entries.isEmpty() || serverId.isEmpty() || serverId != m_storyServerId)
        return;
    for (const StoryEntry &entry : entries)
        m_story.record(entry);
    saveStory();
}

void App::sendCategory(const QStringList &hashes, const QString &name, EditAnswer answer)
{
    // C25 K15: Deluge keeps its labels lower case; the line says the name it kept.
    const QString target = m_features.categoriesAreLabels ? name.trimmed().toLower() : name;
    const core::TorrentModel *model = m_engine->torrents();
    QList<std::pair<QString, QString>> had; // hash → its category when sent
    QHash<QString, int> tokens;
    for (const QString &hash : hashes) {
        if (model->indexOfHash(hash) >= 0)
            had.append({hash, model->torrent(hash).category});
        // Y8's echo: the next poll shows this edit, maybe before its OK lands.
        SentEdit sent;
        sent.name = target;
        tokens.insert(hash, noteSent(m_sentCategories, hash, std::move(sent)));
    }
    m_client->setCategory(hashes, name)
        ->then(this,
               [this, answer, had, tokens, target, serverId = storyServerId()](
                   const core::Result<core::Done> &result) {
                   if (answer)
                       answer(result);
                   const bool ok = !result.isError();
                   if (serverId == storyServerId())
                       for (auto it = tokens.cbegin(); it != tokens.cend(); ++it)
                           settleSent(m_sentCategories, it.key(), it.value(), ok);
                   if (!ok)
                       return;
                   // Y2: what it was in when sent; a torrent already in it is no line.
                   const QDateTime at = QDateTime::currentDateTimeUtc();
                   QList<StoryEntry> lines;
                   for (const auto &[hash, was] : had) {
                       if (was == target)
                           continue;
                       StoryEntry line;
                       line.hash = hash;
                       line.what = StoryEntry::What::Category;
                       line.by = StoryEntry::By::Omaseed;
                       line.at = at;
                       line.was = was;
                       line.now = target;
                       lines.append(line);
                   }
                   writeEntries(serverId, lines);
               });
}

void App::sendTags(const QStringList &hashes,
                   const QStringList &added,
                   const QStringList &removed,
                   EditAnswer addAnswer,
                   EditAnswer removeAnswer)
{
    if (hashes.isEmpty() || (added.isEmpty() && removed.isEmpty()))
        return;
    // Y3: per torrent, the tags it didn't have and those it had; a tag already in place is no
    // part of its line.
    struct Edit
    {
        QString hash;
        QStringList tagged;
        QStringList untagged;
        int addToken = 0;
        int removeToken = 0;
    };
    struct Pending
    {
        QList<Edit> edits;
        int calls = 0;
        bool addOk = false;
        bool removeOk = false;
    };
    auto pending = std::make_shared<Pending>();
    const core::TorrentModel *model = m_engine->torrents();
    for (const QString &hash : hashes) {
        Edit edit;
        edit.hash = hash;
        if (model->indexOfHash(hash) >= 0) {
            const QStringList had = model->torrent(hash).tags;
            for (const QString &tag : added)
                if (!had.contains(tag) && !edit.tagged.contains(tag))
                    edit.tagged.append(tag);
            for (const QString &tag : removed)
                if (had.contains(tag) && !edit.untagged.contains(tag))
                    edit.untagged.append(tag);
        }
        // Y8's echo, as sendCategory's: one note per request that changes this torrent.
        if (!edit.tagged.isEmpty()) {
            SentEdit sent;
            sent.tagged = edit.tagged;
            edit.addToken = noteSent(m_sentTags, hash, std::move(sent));
        }
        if (!edit.untagged.isEmpty()) {
            SentEdit sent;
            sent.untagged = edit.untagged;
            edit.removeToken = noteSent(m_sentTags, hash, std::move(sent));
        }
        pending->edits.append(edit);
    }
    pending->calls = (added.isEmpty() ? 0 : 1) + (removed.isEmpty() ? 0 : 1);
    const QString serverId = storyServerId();
    // When the last answer is in: one line per torrent, with the parts that went (Y3).
    const auto done = [this, pending, serverId]() {
        if (--pending->calls > 0)
            return;
        const QDateTime at = QDateTime::currentDateTimeUtc();
        QList<StoryEntry> lines;
        for (const Edit &edit : std::as_const(pending->edits)) {
            StoryEntry line;
            line.hash = edit.hash;
            line.what = StoryEntry::What::Tags;
            line.by = StoryEntry::By::Omaseed;
            line.at = at;
            if (pending->addOk)
                line.tagged = edit.tagged;
            if (pending->removeOk)
                line.untagged = edit.untagged;
            if (!line.isNoChange())
                lines.append(line);
        }
        writeEntries(serverId, lines);
    };
    if (!added.isEmpty())
        m_client->addTags(hashes, added)
            ->then(
                this,
                [this, pending, serverId, addAnswer, done](const core::Result<core::Done> &result) {
                    if (addAnswer)
                        addAnswer(result);
                    pending->addOk = !result.isError();
                    if (serverId == storyServerId())
                        for (const Edit &edit : std::as_const(pending->edits))
                            if (edit.addToken > 0)
                                settleSent(m_sentTags, edit.hash, edit.addToken, pending->addOk);
                    done();
                });
    if (!removed.isEmpty())
        m_client->removeTags(hashes, removed)
            ->then(this,
                   [this, pending, serverId, removeAnswer, done](
                       const core::Result<core::Done> &result) {
                       if (removeAnswer)
                           removeAnswer(result);
                       pending->removeOk = !result.isError();
                       if (serverId == storyServerId())
                           for (const Edit &edit : std::as_const(pending->edits))
                               if (edit.removeToken > 0)
                                   settleSent(
                                       m_sentTags, edit.hash, edit.removeToken, pending->removeOk);
                       done();
                   });
}

bool App::isEchoOfSentCategory(const QString &hash, const QString &now)
{
    dropStaleSent(m_sentCategories, hash);
    const auto found = m_sentCategories.find(hash);
    if (found == m_sentCategories.end())
        return false;
    QList<SentEdit> &sent = found.value();
    bool echo = false;
    for (qsizetype index = 0; index < sent.size(); ++index) {
        if (sent.at(index).name == now) {
            sent.removeAt(index);
            echo = true;
            break;
        }
    }
    if (sent.isEmpty())
        m_sentCategories.erase(found);
    return echo;
}

bool App::takeEchoOfSentTags(const QString &hash, QStringList *tagged, QStringList *untagged)
{
    dropStaleSent(m_sentTags, hash);
    const auto found = m_sentTags.find(hash);
    if (found == m_sentTags.end())
        return false;
    bool taken = false;
    // A name both seen and noted leaves both: the rest is someone else's (C43 Y8).
    const auto take = [&taken](QStringList *seen, QStringList &noted) {
        if (seen == nullptr)
            return;
        seen->removeIf([&noted, &taken](const QString &name) {
            if (!noted.contains(name))
                return false;
            noted.removeAll(name);
            taken = true;
            return true;
        });
    };
    QList<SentEdit> &sent = found.value();
    for (SentEdit &note : sent) {
        take(tagged, note.tagged);
        take(untagged, note.untagged);
    }
    sent.removeIf(
        [](const SentEdit &note) { return note.tagged.isEmpty() && note.untagged.isEmpty(); });
    if (sent.isEmpty())
        m_sentTags.erase(found);
    return taken;
}

// --- C43 Y2–Y8: what a poll sees ---------------------------------------------------------------

void App::recordSeenEdits(const QList<core::SeenEdit> &edits)
{
    using core::SeenEdit;
    if (m_storyServerId.isEmpty() || m_storyServerId != storyServerId())
        return;
    const QDateTime at = QDateTime::currentDateTimeUtc(); // R4: the poll that saw them, one time
    const qint64 nowMs = at.toMSecsSinceEpoch();
    const bool labels = m_features.categoriesAreLabels;

    // The main session's call (#248 D): a Deluge label off an unfinished torrent puts its save
    // path back to where it downloads; no place line. The label-off is this poll's, a recent
    // one's, or omaseed's own still noted.
    m_labelOffSeenAt.removeIf([nowMs](const std::pair<const QString &, qint64 &> &entry) {
        return nowMs - entry.second > qint64(kEchoSeconds) * 1000;
    });
    QSet<QString> labelOff;
    if (labels) {
        for (const SeenEdit &edit : edits) {
            if (edit.what == SeenEdit::What::Category && edit.now.isEmpty()) {
                labelOff.insert(edit.hash);
                m_labelOffSeenAt.insert(edit.hash, nowMs);
            }
        }
        for (const SeenEdit &edit : edits) {
            if (edit.what != SeenEdit::What::Place || labelOff.contains(edit.hash))
                continue;
            dropStaleSent(m_sentCategories, edit.hash);
            const QList<SentEdit> sent = m_sentCategories.value(edit.hash);
            if (m_labelOffSeenAt.contains(edit.hash) ||
                std::any_of(sent.cbegin(), sent.cend(), [](const SentEdit &note) {
                    return note.name.isEmpty();
                }))
                labelOff.insert(edit.hash);
        }
    }

    QList<StoryEntry> entries;
    QHash<QString, qsizetype> tagsEntryOf; // hash → its ordinary Tags entry in `entries`
    for (const SeenEdit &edit : edits) {
        StoryEntry entry;
        entry.hash = edit.hash;
        entry.at = at;
        entry.by = StoryEntry::By::Elsewhere;
        switch (edit.what) {
        case SeenEdit::What::Category:
            // Y8: omaseed's own, pending or answered within the minute, is its OK's line.
            if (isEchoOfSentCategory(edit.hash, edit.now))
                continue;
            entry.what = StoryEntry::What::Category;
            entry.was = edit.was;
            entry.now = edit.now;
            entry.removed = edit.removed;
            break;
        case SeenEdit::What::Tags: {
            QStringList tagged = edit.tagged;
            QStringList untagged = edit.untagged;
            takeEchoOfSentTags(edit.hash, &tagged, &untagged);
            if (tagged.isEmpty() && untagged.isEmpty())
                continue;
            // Transmission's labels leave its list whenever the last torrent drops one: that
            // says nothing was removed (C43 Y8), so it is an ordinary untag.
            const bool removed = edit.removed && !m_features.tagsAreLabels;
            if (!removed) {
                const auto found = tagsEntryOf.constFind(edit.hash);
                if (found != tagsEntryOf.constEnd()) {
                    StoryEntry &same = entries[*found];
                    same.tagged += tagged;
                    same.untagged += untagged;
                    continue;
                }
                tagsEntryOf.insert(edit.hash, entries.size());
            }
            entry.what = StoryEntry::What::Tags;
            entry.tagged = tagged;
            entry.untagged = untagged;
            entry.removed = removed;
            break;
        }
        case SeenEdit::What::Place: {
            // Y6: omaseed's own move, running or ended within the minute, is the move's story.
            if (m_move != nullptr && m_move->ownsMoveOf(edit.hash))
                continue;
            const core::Torrent &t = m_engine->torrents()->torrent(edit.hash);
            // With the label's move_completed gone the torrent has no waiting folder any more:
            // the path reads landed, though nothing moved (it is still unfinished).
            if (labelOff.contains(edit.hash) && !t.isComplete())
                continue;
            entry.what = StoryEntry::What::Place;
            entry.was = edit.was;
            entry.now = edit.now;
            entry.landed = edit.landed;
            // Y7: the server's own move. qBittorrent's automatic management (a set location
            // turns it off first: scripts/dev/research/248-story-edits); Deluge's label folder.
            if (m_features.autoManagement && t.autoTmm) {
                entry.by = StoryEntry::By::Server;
                entry.cause = QStringLiteral("category");
                entry.causeName = t.category;
            } else if (labels && m_features.categories && !t.category.isEmpty()) {
                QString folder = m_engine->categories()->savePathOf(t.category);
                while (folder.size() > 1 && folder.endsWith(QLatin1Char('/')))
                    folder.chop(1);
                if (!folder.isEmpty() && folder == edit.now) {
                    entry.by = StoryEntry::By::Server;
                    entry.cause = QStringLiteral("label");
                    entry.causeName = t.category;
                }
            }
            break;
        }
        }
        entries.append(entry);
    }
    writeEntries(m_storyServerId, entries);
}

bool App::placeMovedSinceLastMove(const QString &hash) const
{
    if (m_move == nullptr)
        return false;
    const core::MoveRecord *last = m_move->history().lastFor(hash);
    if (last == nullptr || !last->finishedAt.isValid())
        return false;
    const QList<StoryEntry> lines = m_story.recordsFor(hash);
    return std::any_of(lines.cbegin(), lines.cend(), [last](const StoryEntry &line) {
        return line.what == StoryEntry::What::Place && line.by != StoryEntry::By::Omaseed &&
               line.at > last->finishedAt;
    });
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
    if (id <= 0 || !m_story.remove(hash, id))
        return false;
    if (m_renameLineOf.value(hash) == id)
        m_renameLineOf.remove(hash);
    saveStory();
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
    StoryEntry rename;
    rename.hash = hash;
    rename.what = done == 1 ? StoryEntry::What::File : StoryEntry::What::Files;
    // R3: the first file renamed stands for the run.
    rename.was = ownName(moves.constFirst().first);
    rename.now = ownName(moves.constFirst().second);
    rename.count = done;
    rename.in = folders.size() == 1 ? folders.constFirst() : QString();
    rename.folders = int(folders.size());
    return recordRename(serverId, rename);
}

} // namespace omaseed::app
