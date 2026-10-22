#include "core/SkippedJobs.hpp"

#include "core/Format.hpp"
#include "core/PieceStates.hpp"

#include <QDebug>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>

namespace omaseed::core {

namespace {

bool isStoppedState(const QString &state)
{
    // 5.x says stopped, 4.x paused; an errored or missing-files torrent writes nothing either.
    return state.startsWith(QLatin1String("stopped")) ||
           state.startsWith(QLatin1String("paused")) || state == QLatin1String("error") ||
           state == QLatin1String("missingFiles");
}

void log(const QString &text)
{
    qInfo().noquote() << QStringLiteral("free space: %1").arg(text);
}

} // namespace

// ---- FreeSkippedJob --------------------------------------------------------------------------

FreeSkippedJob::FreeSkippedJob(QbtClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{}

void FreeSkippedJob::start(const Input &input)
{
    if (m_busy || m_client.isNull())
        return;
    m_busy = true;
    m_input = input;
    m_outcome = Outcome();
    m_plan = FreePlan();
    m_cycled = false;
    if (!m_input.running) {
        readAndDelete();
        return;
    }
    Q_EMIT step(QStringLiteral("stopping"));
    log(QStringLiteral("stopping %1").arg(m_input.name));
    m_client->torrentsStop({m_input.hash})->then(this, [this](const QbtResult<QByteArray> &r) {
        if (r.isError()) {
            finish(tr("couldn't stop it: %1").arg(r.error().toString()));
            return;
        }
        waitStopped(0);
    });
}

void FreeSkippedJob::waitStopped(int waited)
{
    TorrentsInfoQuery query;
    query.hashes = {m_input.hash};
    m_client->torrentsInfo(query)->then(this, [this, waited](const QbtResult<QJsonArray> &r) {
        const QJsonObject info =
            r.isError() || r.value().isEmpty() ? QJsonObject() : r.value().first().toObject();
        if (!r.isError() && r.value().isEmpty()) {
            finish(tr("the torrent is gone"));
            return;
        }
        if (isStoppedState(info.value(QStringLiteral("state")).toString())) {
            readAndDelete();
            return;
        }
        if (waited >= m_input.stopTimeoutMs) {
            finish(tr("it didn't stop within %1 s").arg(m_input.stopTimeoutMs / 1000));
            return;
        }
        QTimer::singleShot(
            m_input.pollMs, this, [this, waited]() { waitStopped(waited + m_input.pollMs); });
    });
}

void FreeSkippedJob::readAndDelete()
{
    // D3 at delete time: the files' priorities and pieces as they are now, and what is open.
    m_client->torrentsFiles(m_input.hash)->then(this, [this](const QbtResult<QJsonArray> &files) {
        if (files.isError()) {
            finish(tr("couldn't read its files: %1").arg(files.error().toString()));
            return;
        }
        const QJsonArray list = files.value();
        m_client->torrentsPieceStates(m_input.hash)
            ->then(this, [this, list](const QbtResult<QJsonArray> &pieces) {
                const QByteArray states =
                    pieces.isError() ? QByteArray() : PieceStates::fromJson(pieces.value());
                const SkippedReading reading = SkippedOnDisk::read(m_input.hash,
                                                                   list,
                                                                   states,
                                                                   m_input.torrentSize,
                                                                   m_input.localPathOf,
                                                                   m_input.reader);
                if (!reading.seen) {
                    finish(tr("this machine can't see its files"));
                    return;
                }
                QSet<QString> paths;
                for (const SkippedFile &file : reading.files) {
                    if (file.holdsSpace())
                        paths.insert(file.localPath);
                }
                m_plan =
                    SkippedOnDisk::plan(reading,
                                        m_input.hybrid,
                                        m_input.contentLocal,
                                        m_input.sharedRoots,
                                        SkippedOnDisk::openFileHolders(paths, m_input.procRoot));
                // A tick on a stopped torrent leaves libtorrent holding the files it created
                // (seen on ntfs3, #118): only a stop releases them, and a stopped torrent
                // doesn't stop again. Started and stopped once, qBittorrent lets go.
                const bool heldByQbt =
                    std::any_of(m_plan.items.cbegin(), m_plan.items.cend(), [](const FreeItem &i) {
                        return i.verdict == FreeItem::Verdict::Open &&
                               i.openBy.contains(QLatin1String("qbittorrent"), Qt::CaseInsensitive);
                    });
                if (heldByQbt && !m_cycled) {
                    m_cycled = true;
                    log(QStringLiteral("%1: qBittorrent still holds its skipped files; starting "
                                       "and stopping it so it lets go")
                            .arg(m_input.name));
                    Q_EMIT step(QStringLiteral("stopping"));
                    m_client->torrentsStart({m_input.hash});
                    m_client->torrentsStop({m_input.hash})
                        ->then(this, [this](const QbtResult<QByteArray> &) { waitStopped(0); });
                    return;
                }
                Q_EMIT step(QStringLiteral("deleting"));
                bool piecesWent = false;
                for (const FreeItem &item : std::as_const(m_plan.items)) {
                    if (item.verdict != FreeItem::Verdict::Delete) {
                        ++m_outcome.kept;
                        if (!item.openBy.isEmpty())
                            m_outcome.openBy.append(item.openBy);
                        continue;
                    }
                    if (m_input.remove(item.file.localPath)) {
                        ++m_outcome.deleted;
                        m_outcome.freed += item.file.allocated;
                        piecesWent = piecesWent || item.file.holdsPieces;
                    } else {
                        m_outcome.failed.append(item.file.localPath);
                    }
                }
                m_plan.recheck = piecesWent && !m_input.hybrid;
                log(QStringLiteral("deleted %1 skipped files of %2 (%3), %4 stay")
                        .arg(m_outcome.deleted)
                        .arg(m_input.name, Format::size(m_outcome.freed))
                        .arg(m_outcome.kept));
                startAgain();
            });
    });
}

void FreeSkippedJob::startAgain()
{
    if (!m_input.running) {
        recheck();
        return;
    }
    Q_EMIT step(QStringLiteral("starting"));
    m_client->torrentsStart({m_input.hash})->then(this, [this](const QbtResult<QByteArray> &r) {
        if (r.isError()) {
            finish(tr("couldn't start it again: %1").arg(r.error().toString()));
            return;
        }
        m_outcome.started = true;
        log(QStringLiteral("started %1 again").arg(m_input.name));
        recheck();
    });
}

void FreeSkippedJob::recheck()
{
    if (!m_plan.recheck) {
        finish();
        return;
    }
    // C17 D3: only a v1-only or v2-only torrent gets here; a hybrid never does (#115).
    Q_EMIT step(QStringLiteral("rechecking"));
    m_client->torrentsRecheck({m_input.hash})->then(this, [this](const QbtResult<QByteArray> &r) {
        if (r.isError()) {
            finish(tr("couldn't recheck it: %1").arg(r.error().toString()));
            return;
        }
        m_outcome.rechecked = true;
        log(QStringLiteral("rechecking %1").arg(m_input.name));
        finish();
    });
}

void FreeSkippedJob::finish(const QString &error)
{
    m_outcome.error = error;
    m_outcome.ok = error.isEmpty();
    if (!error.isEmpty())
        log(QStringLiteral("%1: %2").arg(m_input.name, error));
    m_busy = false;
    Q_EMIT finished(m_outcome);
}

// ---- AddAgainJob -----------------------------------------------------------------------------

AddAgainJob::AddAgainJob(QbtClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{}

QbtClient::AddOptions AddAgainJob::addOptions(const TorrentRecord &record)
{
    QbtClient::AddOptions o;
    o.savepath = record.savePath;
    o.useDownloadPath = !record.downloadPath.isEmpty();
    if (!record.downloadPath.isEmpty())
        o.downloadPath = record.downloadPath;
    if (!record.category.isEmpty())
        o.category = record.category;
    o.tags = record.tags;
    // Stopped, so nothing is written before its names and priorities are put back.
    o.stopped = true;
    o.autoTMM = false;
    o.contentLayout =
        record.layout == QLatin1String("Subfolder")     ? QbtClient::ContentLayout::Subfolder
        : record.layout == QLatin1String("NoSubfolder") ? QbtClient::ContentLayout::NoSubfolder
                                                        : QbtClient::ContentLayout::Original;
    if (record.upLimit > 0)
        o.upLimit = record.upLimit;
    if (record.dlLimit > 0)
        o.dlLimit = record.dlLimit;
    if (record.sequential)
        o.sequentialDownload = true;
    if (record.firstLastPiecePrio)
        o.firstLastPiecePrio = true;
    return o;
}

QMap<int, QList<int>> AddAgainJob::priorityGroups(const TorrentRecord &record)
{
    QMap<int, QList<int>> groups;
    for (int i = 0; i < record.priorities.size(); ++i) {
        if (record.priorities.at(i) != 1)
            groups[record.priorities.at(i)].append(i);
    }
    return groups;
}

void AddAgainJob::start(const Input &input)
{
    if (m_busy || m_client.isNull())
        return;
    m_busy = true;
    m_input = input;
    m_outcome = Outcome();
    m_record = TorrentRecord();
    m_torrent.clear();
    Q_EMIT step(QStringLiteral("reading"));
    m_client->webApiVersion()->then(this, [this](const QbtResult<QString> &r) {
        m_apiVersion = r.isError() ? QString() : r.value();
        readRecord();
    });
}

void AddAgainJob::readRecord()
{
    TorrentsInfoQuery query;
    query.hashes = {m_input.hash};
    m_client->torrentsInfo(query)->then(this, [this](const QbtResult<QJsonArray> &info) {
        if (info.isError() || info.value().isEmpty()) {
            finish(info.isError() ? info.error().toString() : tr("the torrent is gone"));
            return;
        }
        const QJsonObject object = info.value().first().toObject();
        m_client->torrentsFiles(m_input.hash)
            ->then(this, [this, object](const QbtResult<QJsonArray> &files) {
                if (files.isError()) {
                    finish(files.error().toString());
                    return;
                }
                const QJsonArray fileList = files.value();
                m_client->torrentsTrackers(m_input.hash)
                    ->then(this, [this, object, fileList](const QbtResult<QJsonArray> &trackers) {
                        const QJsonArray trackerList =
                            trackers.isError() ? QJsonArray() : trackers.value();
                        QUrlQuery query;
                        query.addQueryItem(QStringLiteral("hash"), m_input.hash);
                        m_client->get(QStringLiteral("torrents/export"), query)
                            ->then(this,
                                   [this, object, fileList, trackerList](
                                       const QbtResult<QByteArray> &exported) {
                                       if (exported.isError() || exported.value().isEmpty()) {
                                           finish(tr("couldn't export its .torrent"));
                                           return;
                                       }
                                       m_torrent = exported.value();
                                       m_record = TorrentsBackup::recordFrom(
                                           object, fileList, trackerList, m_torrent);
                                       Q_EMIT step(QStringLiteral("removing"));
                                       log(QStringLiteral("adding %1 again: removing it, "
                                                          "keeping its files")
                                               .arg(m_record.name));
                                       m_client->torrentsDelete({m_input.hash}, false)
                                           ->then(this, [this](const QbtResult<QByteArray> &r) {
                                               if (r.isError()) {
                                                   finish(r.error().toString());
                                                   return;
                                               }
                                               waitGone(0);
                                           });
                                   });
                    });
            });
    });
}

void AddAgainJob::waitGone(int waited)
{
    TorrentsInfoQuery query;
    query.hashes = {m_input.hash};
    m_client->torrentsInfo(query)->then(this, [this, waited](const QbtResult<QJsonArray> &r) {
        if (!r.isError() && r.value().isEmpty()) {
            if (!m_input.deletePaths.isEmpty())
                Q_EMIT step(QStringLiteral("deleting"));
            for (const QString &path : std::as_const(m_input.deletePaths)) {
                if (m_input.remove(path))
                    ++m_outcome.deleted;
            }
            add();
            return;
        }
        if (waited >= m_input.waitMs) {
            finish(tr("qBittorrent didn't remove it"));
            return;
        }
        QTimer::singleShot(
            m_input.pollMs, this, [this, waited]() { waitGone(waited + m_input.pollMs); });
    });
}

void AddAgainJob::add()
{
    Q_EMIT step(QStringLiteral("adding"));
    const QString fileName = m_record.id + QStringLiteral(".torrent");
    m_client->torrentsAddFile(fileName, m_torrent, addOptions(m_record))
        ->then(this, [this](const QbtResult<QByteArray> &r) {
            if (r.isError()) {
                // It is gone from the server now: say what to add back, and from where.
                finish(tr("qBittorrent refused the add: %1").arg(r.error().toString()));
                return;
            }
            waitAdded(0);
        });
}

void AddAgainJob::waitAdded(int waited)
{
    TorrentsInfoQuery query;
    query.hashes = {m_record.id};
    m_client->torrentsInfo(query)->then(this, [this, waited](const QbtResult<QJsonArray> &r) {
        if (!r.isError() && !r.value().isEmpty()) {
            m_outcome.hash = r.value().first().toObject().value(QStringLiteral("hash")).toString();
            restore();
            return;
        }
        if (waited >= m_input.waitMs) {
            finish(tr("the torrent didn't come back"));
            return;
        }
        QTimer::singleShot(
            m_input.pollMs, this, [this, waited]() { waitAdded(waited + m_input.pollMs); });
    });
}

void AddAgainJob::restore()
{
    Q_EMIT step(QStringLiteral("restoring"));
    const QString hash = m_outcome.hash;
    // One step after another; a refusal is a warning, the rest still runs.
    auto steps = std::make_shared<QList<std::function<void(std::function<void()>)>>>();
    auto warn = [this](const QString &what, const QbtResult<QByteArray> &r) {
        if (r.isError())
            m_outcome.warnings.append(QStringLiteral("%1: %2").arg(what, r.error().toString()));
    };
    for (const TorrentRecord::Rename &rename : std::as_const(m_record.renamed)) {
        steps->append([this, hash, rename, warn](std::function<void()> go) {
            m_client->torrentsRenameFile(hash, rename.from, rename.to)
                ->then(this, [warn, go](const QbtResult<QByteArray> &r) {
                    warn(QStringLiteral("rename"), r);
                    go();
                });
        });
    }
    const QMap<int, QList<int>> groups = priorityGroups(m_record);
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const int priority = it.key();
        const QList<int> files = it.value();
        steps->append([this, hash, priority, files, warn](std::function<void()> go) {
            m_client->torrentsSetFilePrio(hash, files, priority)
                ->then(this, [warn, go](const QbtResult<QByteArray> &r) {
                    warn(QStringLiteral("file priorities"), r);
                    go();
                });
        });
    }
    const TorrentRecord &r = m_record;
    if (r.ratioLimit != -2 || r.seedingTimeLimit != -2 || r.inactiveSeedingTimeLimit != -2 ||
        (!r.shareLimitAction.isEmpty() && r.shareLimitAction != QLatin1String("Default"))) {
        steps->append([this, hash, warn](std::function<void()> go) {
            std::optional<QbtClient::ShareLimitAction> action;
            if (QbtClient::supportsShareLimitAction(m_apiVersion))
                action = QbtClient::shareLimitActionFromName(m_record.shareLimitAction);
            m_client
                ->torrentsSetShareLimits({hash},
                                         m_record.ratioLimit,
                                         m_record.seedingTimeLimit,
                                         m_record.inactiveSeedingTimeLimit,
                                         action)
                ->then(this, [warn, go](const QbtResult<QByteArray> &x) {
                    warn(QStringLiteral("share limits"), x);
                    go();
                });
        });
    }
    if (r.started()) {
        steps->append([this, hash, warn](std::function<void()> go) {
            Q_EMIT step(QStringLiteral("starting"));
            m_client->torrentsStart({hash})->then(this, [warn, go](const QbtResult<QByteArray> &x) {
                warn(QStringLiteral("start"), x);
                go();
            });
        });
    }
    auto next = std::make_shared<std::function<void()>>();
    *next = [this, steps, next]() {
        if (steps->isEmpty()) {
            log(QStringLiteral("%1 is back").arg(m_record.name));
            finish();
            return;
        }
        auto first = steps->takeFirst();
        // The chain holds itself until its last step; a weak copy breaks the cycle then.
        std::weak_ptr<std::function<void()>> weak = next;
        first([weak]() {
            if (auto strong = weak.lock())
                (*strong)();
        });
    };
    // Kept alive while its replies are out.
    m_chain = next;
    (*next)();
}

void AddAgainJob::finish(const QString &error)
{
    m_outcome.error = error;
    m_outcome.ok = error.isEmpty();
    if (!error.isEmpty())
        log(QStringLiteral("adding %1 again: %2").arg(m_record.name, error));
    m_busy = false;
    m_chain.reset();
    Q_EMIT finished(m_outcome);
}

} // namespace omaseed::core
