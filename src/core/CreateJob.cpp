#include "core/CreateJob.hpp"

#include "core/AnnounceMask.hpp"
#include "core/CreatePlan.hpp"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QTimer>

namespace omaseed::core {

namespace {

qint64 monotonicMs()
{
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return timer.elapsed();
}

/// qBittorrent's failure words without its "Create new torrent file failed. Reason: " lead.
QString reasonOf(const QString &message)
{
    static const QString lead = QStringLiteral("Reason: ");
    const qsizetype at = message.indexOf(lead);
    return (at >= 0 ? message.mid(at + lead.size()) : message).trimmed();
}

} // namespace

CreateJob::CreateJob(QbtClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &CreateJob::poll);
}

void CreateJob::setPollInterval(std::chrono::milliseconds interval)
{
    m_timer->setInterval(interval);
}

void CreateJob::setClock(std::function<qint64()> clock)
{
    m_clock = std::move(clock);
}

qint64 CreateJob::now() const
{
    return m_clock ? m_clock() : monotonicMs();
}

qint64 CreateJob::elapsedMs() const
{
    return m_state == State::Idle ? 0 : now() - m_startedAt;
}

bool CreateJob::isActive() const
{
    return m_state == State::Starting || m_state == State::Queued || m_state == State::Hashing ||
           m_state == State::Fetching || m_state == State::Adding;
}

bool CreateJob::isOver() const
{
    return m_state == State::Done || m_state == State::Failed || m_state == State::Cancelled;
}

void CreateJob::start(const Request &request)
{
    ++m_generation;
    m_timer->stop();
    m_request = request;
    m_failure = Failure::None;
    m_taskId.clear();
    m_errorText.clear();
    m_bytes.clear();
    m_torrent = TorrentFile();
    m_progress = 0;
    m_pieceSize = 0;
    m_tookMs = 0;
    m_cancelAsked = false;
    m_inFlight = false;
    m_startedAt = now();
    setState(State::Starting);
    if (m_client.isNull()) {
        fail(Failure::Refused, tr("no server"));
        return;
    }
    const int generation = m_generation;
    m_client->torrentCreatorAddTask(request.task)
        ->then(this, [this, generation](const QbtResult<QString> &result) {
            if (generation != m_generation)
                return;
            if (result.isError()) {
                fail(Failure::Refused, result.error().message());
                return;
            }
            m_taskId = result.value();
            Q_EMIT taskStarted(m_taskId);
            if (m_cancelAsked) {
                cancel();
                return;
            }
            setState(State::Queued);
            poll();
        });
}

void CreateJob::resume(const Request &request, const QString &taskId, qint64 startedAtMs)
{
    ++m_generation;
    m_timer->stop();
    m_request = request;
    m_failure = Failure::None;
    m_taskId = taskId;
    m_errorText.clear();
    m_bytes.clear();
    m_torrent = TorrentFile();
    m_progress = 0;
    m_pieceSize = 0;
    m_cancelAsked = false;
    m_inFlight = false;
    m_startedAt = startedAtMs;
    setState(State::Queued);
    poll();
}

void CreateJob::cancel()
{
    if (!isActive())
        return;
    if (m_taskId.isEmpty()) {
        // addTask is out: the id it brings is deleted at once.
        m_cancelAsked = true;
        return;
    }
    ++m_generation;
    m_timer->stop();
    if (!m_client.isNull())
        m_client->torrentCreatorDeleteTask(m_taskId);
    setState(State::Cancelled);
    Q_EMIT finished();
}

void CreateJob::arm()
{
    if (m_state == State::Queued || m_state == State::Hashing)
        m_timer->start();
}

void CreateJob::poll()
{
    if (m_inFlight || m_client.isNull() || m_taskId.isEmpty())
        return;
    m_inFlight = true;
    const int generation = m_generation;
    m_client->torrentCreatorStatus(m_taskId)->then(
        this, [this, generation](const QbtResult<QJsonObject> &result) {
            if (generation != m_generation)
                return;
            m_inFlight = false;
            if (result.isError()) {
                if (result.error().httpStatus() == 404) {
                    fail(Failure::Restarted, QString());
                    return;
                }
                arm(); // a lost poll is asked again
                return;
            }
            const QJsonObject status = result.value();
            const QString word = status.value(QStringLiteral("status")).toString();
            const int progress = qBound(0, status.value(QStringLiteral("progress")).toInt(), 100);
            if (word == QLatin1String("Failed")) {
                fail(Failure::Hashing,
                     reasonOf(status.value(QStringLiteral("errorMessage")).toString()));
                return;
            }
            if (word == QLatin1String("Finished")) {
                m_progress = 100;
                m_pieceSize = qint64(status.value(QStringLiteral("pieceSize")).toDouble());
                fetch();
                return;
            }
            if (word == QLatin1String("Running")) {
                const bool moved = m_state != State::Hashing || progress != m_progress;
                m_progress = progress;
                if (m_state != State::Hashing)
                    setState(State::Hashing);
                else if (moved)
                    Q_EMIT changed();
            } else if (m_state != State::Queued) {
                setState(State::Queued);
            }
            arm();
        });
}

void CreateJob::fetch()
{
    setState(State::Fetching);
    const int generation = m_generation;
    m_client->torrentCreatorTorrentFile(m_taskId)->then(
        this, [this, generation](const QbtResult<QByteArray> &result) {
            if (generation != m_generation)
                return;
            if (result.isError()) {
                fail(Failure::Lost, result.error().message());
                return;
            }
            m_bytes = result.value();
            m_tookMs = now() - m_startedAt;
            m_torrent = TorrentFile::parse(m_bytes);
            if (m_pieceSize <= 0)
                m_pieceSize = m_torrent.pieceLength;
            // The task has done its work: qBittorrent keeps it until asked.
            m_client->torrentCreatorDeleteTask(m_taskId);
            if (!m_torrent.isValid()) {
                fail(Failure::Lost, tr("the .torrent qBittorrent made couldn't be read"));
                return;
            }
            if (m_request.addAfter) {
                add();
                return;
            }
            setState(State::Done);
            Q_EMIT finished();
        });
}

void CreateJob::add()
{
    setState(State::Adding);
    QbtClient::AddOptions options;
    // C27 CR12: seeding from where the files are: the folder's parent holds the folder (or the
    // one file), whatever the server's own layout and category folders say.
    options.savepath = CreatePlan::parentOf(m_request.task.sourcePath);
    options.autoTMM = false;
    options.contentLayout = QbtClient::ContentLayout::Original;
    options.skipChecking = true;
    options.stopped = false;
    options.apiVersion = m_request.apiVersion;
    if (!m_request.category.isEmpty())
        options.category = m_request.category;
    const int generation = m_generation;
    m_client->torrentsAddFile(m_request.name + QStringLiteral(".torrent"), m_bytes, options)
        ->then(this, [this, generation](const QbtResult<QByteArray> &result) {
            if (generation != m_generation)
                return;
            QString refused;
            if (result.isError())
                refused = result.error().message();
            else if (QString::fromUtf8(result.value())
                         .trimmed()
                         .compare(QStringLiteral("Fails."), Qt::CaseInsensitive) == 0)
                refused = tr("the server refused it");
            if (!refused.isEmpty()) {
                m_failure = Failure::AddFailed;
                m_errorText = AnnounceMask::maskText(refused);
            }
            setState(State::Done);
            Q_EMIT finished();
        });
}

void CreateJob::fail(Failure failure, const QString &text)
{
    ++m_generation;
    m_timer->stop();
    m_inFlight = false;
    m_failure = failure;
    m_errorText = AnnounceMask::maskText(text);
    // A failed task stays on the server until asked; one it forgot (404) is gone already.
    if (failure != Failure::Restarted && failure != Failure::Refused && !m_taskId.isEmpty() &&
        !m_client.isNull())
        m_client->torrentCreatorDeleteTask(m_taskId);
    setState(State::Failed);
    Q_EMIT finished();
}

void CreateJob::setState(State state)
{
    m_state = state;
    Q_EMIT changed();
}

} // namespace omaseed::core
