#include "core/SkippedJobs.hpp"

#include "core/Format.hpp"

#include <QDebug>
#include <QSet>
#include <QTimer>

#include <algorithm>

namespace omaseed::core {

namespace {

bool isStoppedPhase(TorrentPhase phase)
{
    // Stopped, and an errored or missing-files torrent writes nothing either.
    return phase == TorrentPhase::Stopped || phase == TorrentPhase::Errored ||
           phase == TorrentPhase::Missing;
}

void log(const QString &text)
{
    qInfo().noquote() << QStringLiteral("free space: %1").arg(text);
}

} // namespace

// ---- FreeSkippedJob --------------------------------------------------------------------------

FreeSkippedJob::FreeSkippedJob(EngineClient *client, QObject *parent)
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
    m_client->stop({m_input.hash})->then(this, [this](const Result<Done> &r) {
        if (r.isError()) {
            finish(tr("couldn't stop it: %1").arg(r.error().toString()));
            return;
        }
        waitStopped(0);
    });
}

void FreeSkippedJob::waitStopped(int waited)
{
    m_client->torrents({m_input.hash})
        ->then(this, [this, waited](const Result<QList<TorrentPatch>> &r) {
            if (!r.isError() && r.value().isEmpty()) {
                finish(tr("the torrent is gone"));
                return;
            }
            const bool known = !r.isError() && r.value().first().has(TorrentField::Status);
            if (known && isStoppedPhase(r.value().first().values.status.phase)) {
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
    m_client->files(m_input.hash)->then(this, [this](const Result<QList<ContentFile>> &files) {
        if (files.isError()) {
            finish(tr("couldn't read its files: %1").arg(files.error().toString()));
            return;
        }
        const QList<ContentFile> list = files.value();
        m_client->pieceStates(m_input.hash)
            ->then(this, [this, list](const Result<QByteArray> &pieces) {
                const QByteArray states = pieces.isError() ? QByteArray() : pieces.value();
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
                    m_client->start({m_input.hash});
                    m_client->stop({m_input.hash})->then(this, [this](const Result<Done> &) {
                        waitStopped(0);
                    });
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
    m_client->start({m_input.hash})->then(this, [this](const Result<Done> &r) {
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
    m_client->recheck({m_input.hash})->then(this, [this](const Result<Done> &r) {
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

} // namespace omaseed::core
