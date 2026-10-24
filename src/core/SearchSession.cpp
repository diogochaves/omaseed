#include "core/SearchSession.hpp"

#include "core/QbtClient.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QVariantMap>

namespace omaseed::core {

namespace {

bool runs(const QString &state)
{
    return state == QLatin1String("asking") || state == QLatin1String("slow");
}

} // namespace

SearchSession::SearchSession(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_poll.setInterval(m_pollMs);
    connect(&m_poll, &QTimer::timeout, this, &SearchSession::poll);
}

void SearchSession::setClient(QbtClient *client)
{
    m_client = client;
}

void SearchSession::setTimings(int pollMs, int deadlineMs, int maxRunning)
{
    m_pollMs = pollMs;
    m_deadlineMs = deadlineMs;
    m_maxRunning = maxRunning;
    m_poll.setInterval(pollMs);
}

QString SearchSession::stateName() const
{
    switch (m_state) {
    case State::Asking:
        return QStringLiteral("asking");
    case State::Done:
        return QStringLiteral("done");
    case State::NoPython:
        return QStringLiteral("nopython");
    case State::Error:
        return QStringLiteral("error");
    case State::Idle:
        break;
    }
    return QStringLiteral("idle");
}

QVariantList SearchSession::pluginRows() const
{
    if (m_jobs.isEmpty())
        return m_restoredPlugins;
    QVariantList rows;
    for (const Job &job : m_jobs) {
        QVariantMap row;
        row.insert(QStringLiteral("name"), job.plugin.name);
        row.insert(QStringLiteral("fullName"),
                   job.plugin.fullName.isEmpty() ? job.plugin.name : job.plugin.fullName);
        // A job whose log is still being read reads as asking.
        row.insert(QStringLiteral("state"), job.pendingLog ? QStringLiteral("asking") : job.state);
        row.insert(QStringLiteral("count"), job.count);
        row.insert(QStringLiteral("seconds"),
                   job.answeredMs >= 0 && job.startedMs >= 0
                       ? int((job.answeredMs - job.startedMs + 500) / 1000)
                       : -1);
        row.insert(QStringLiteral("reason"), job.reason);
        row.insert(QStringLiteral("logTime"), job.logTime);
        rows.append(row);
    }
    return rows;
}

int SearchSession::asked() const
{
    if (m_jobs.isEmpty())
        return int(std::count_if(
            m_restoredPlugins.cbegin(), m_restoredPlugins.cend(), [](const QVariant &row) {
                return row.toMap().value(QStringLiteral("state")).toString() !=
                       QLatin1String("skipped");
            }));
    return int(std::count_if(m_jobs.cbegin(), m_jobs.cend(), [](const Job &job) {
        return job.state != QLatin1String("skipped");
    }));
}

int SearchSession::answered() const
{
    const QVariantList rows = pluginRows();
    return int(std::count_if(rows.cbegin(), rows.cend(), [](const QVariant &row) {
        return row.toMap().value(QStringLiteral("state")).toString() == QLatin1String("answered");
    }));
}

int SearchSession::didntAnswer() const
{
    const QVariantList rows = pluginRows();
    return int(std::count_if(rows.cbegin(), rows.cend(), [](const QVariant &row) {
        const QString state = row.toMap().value(QStringLiteral("state")).toString();
        return state == QLatin1String("failed") || state == QLatin1String("slow") ||
               state == QLatin1String("stopped");
    }));
}

QList<int> SearchSession::jobIds() const
{
    QList<int> ids;
    for (const Job &job : m_jobs)
        if (job.id >= 0 && !job.deleted)
            ids.append(job.id);
    return ids;
}

void SearchSession::start(const QString &pattern,
                          const QString &category,
                          const QList<Plugin> &plugins)
{
    // SR12: a new search replaces the last; its jobs go from the server.
    deleteJobs(jobIds());
    ++m_generation;
    m_jobs.clear();
    m_restoredPlugins.clear();
    m_hits.clear();
    m_error.clear();
    m_pattern = pattern;
    m_category = category.isEmpty() ? QStringLiteral("all") : category;
    m_startedSecs = QDateTime::currentSecsSinceEpoch();
    for (const Plugin &plugin : plugins) {
        Job job;
        job.plugin = plugin;
        // SR6: a plugin without the chosen type is skipped, and named so.
        const bool hasType =
            m_category == QLatin1String("all") || plugin.categories.contains(m_category);
        job.state = hasType ? QStringLiteral("queued") : QStringLiteral("skipped");
        m_jobs.append(job);
    }
    m_state = State::Asking;
    Q_EMIT hitsChanged();
    pump();
    updateDone();
    if (m_state == State::Asking)
        m_poll.start();
    Q_EMIT changed();
}

void SearchSession::askAgain(const QString &plugin)
{
    Job *job = jobFor(plugin);
    if (job == nullptr || m_client.isNull())
        return;
    if (job->id >= 0 && !job->deleted)
        m_client->searchDelete(job->id);
    const QString name = job->plugin.name;
    const Plugin keep = job->plugin;
    *job = Job();
    job->plugin = keep;
    job->state = QStringLiteral("queued");
    m_hits.removeIf([&name](const SearchHit &hit) { return hit.plugin == name; });
    m_state = State::Asking;
    m_error.clear();
    Q_EMIT hitsChanged();
    pump();
    m_poll.start();
    Q_EMIT changed();
}

void SearchSession::stopAsking(const QString &plugin)
{
    Job *job = jobFor(plugin);
    if (job == nullptr || !runs(job->state))
        return;
    if (job->id >= 0 && !m_client.isNull())
        m_client->searchStop(job->id);
    job->state = QStringLiteral("stopped");
    job->answeredMs = now();
    updateDone();
    Q_EMIT changed();
}

void SearchSession::shutdown()
{
    m_poll.stop();
    for (Job &job : m_jobs) {
        if (job.id >= 0 && !job.deleted && !m_client.isNull()) {
            m_client->searchDelete(job.id);
            job.deleted = true;
        }
    }
}

void SearchSession::forget()
{
    ++m_generation;
    m_poll.stop();
    m_jobs.clear();
    m_restoredPlugins.clear();
    m_hits.clear();
    m_pattern.clear();
    m_category = QStringLiteral("all");
    m_error.clear();
    m_state = State::Idle;
    m_logPending = false;
    Q_EMIT hitsChanged();
    Q_EMIT changed();
}

void SearchSession::deleteJobs(const QList<int> &ids)
{
    if (m_client.isNull())
        return;
    for (const int id : ids)
        m_client->searchDelete(id);
}

SearchSession::Snapshot SearchSession::snapshot() const
{
    Snapshot snapshot;
    snapshot.pattern = m_pattern;
    snapshot.category = m_category;
    snapshot.state = m_state == State::Asking ? State::Done : m_state;
    snapshot.hits = m_hits;
    // A plugin still asked when the window left this server is no longer heard.
    for (const QVariant &value : pluginRows()) {
        QVariantMap row = value.toMap();
        const QString state = row.value(QStringLiteral("state")).toString();
        if (state == QLatin1String("asking") || state == QLatin1String("queued") ||
            state == QLatin1String("slow"))
            row.insert(QStringLiteral("state"), QStringLiteral("stopped"));
        snapshot.plugins.append(row);
    }
    return snapshot;
}

void SearchSession::restore(const Snapshot &snapshot)
{
    ++m_generation;
    m_poll.stop();
    m_jobs.clear();
    m_pattern = snapshot.pattern;
    m_category = snapshot.category;
    m_state = snapshot.state;
    m_hits = snapshot.hits;
    m_restoredPlugins = snapshot.plugins;
    m_error.clear();
    Q_EMIT hitsChanged();
    Q_EMIT changed();
}

QVariantMap
SearchSession::failureFromLog(const QJsonArray &lines, const QString &plugin, qint64 sinceSecs)
{
    const QString quoted = QStringLiteral("\"%1\"").arg(plugin);
    const QString file = QStringLiteral("engines/%1.py").arg(plugin);
    for (qsizetype i = lines.size() - 1; i >= 0; --i) {
        const QJsonObject line = lines.at(i).toObject();
        if (line.value(QStringLiteral("timestamp")).toInteger() < sinceSecs)
            continue;
        const QString message = line.value(QStringLiteral("message")).toString();
        if (!message.contains(QStringLiteral("search engine"), Qt::CaseInsensitive))
            continue;
        const qsizetype engines = message.indexOf(QStringLiteral("Engines:"));
        const QString named =
            engines >= 0
                ? message.mid(engines,
                              message.indexOf(QStringLiteral(". Error:"), engines) - engines)
                : QString();
        if (!named.contains(quoted) && !named.contains(plugin) && !message.contains(file))
            continue;
        QString reason = message;
        const qsizetype error = message.indexOf(QStringLiteral("Error: \""));
        if (error >= 0) {
            reason = message.mid(error + 8);
            while (reason.endsWith(QLatin1Char('.')) || reason.endsWith(QLatin1Char('"')))
                reason.chop(1);
            const QStringList parts = reason.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (qsizetype j = parts.size() - 1; j >= 0; --j) {
                if (!parts.at(j).trimmed().isEmpty()) {
                    reason = parts.at(j).trimmed();
                    break;
                }
            }
        }
        const qint64 when = line.value(QStringLiteral("timestamp")).toInteger();
        return {{QStringLiteral("reason"), reason},
                {QStringLiteral("time"),
                 QDateTime::fromSecsSinceEpoch(when).toString(QStringLiteral("HH:mm"))}};
    }
    return {};
}

void SearchSession::pump()
{
    if (m_client.isNull() || m_state != State::Asking)
        return;
    for (int i = 0; i < m_jobs.size() && running() < m_maxRunning; ++i)
        if (m_jobs.at(i).state == QLatin1String("queued"))
            startJob(i);
}

int SearchSession::running() const
{
    return int(std::count_if(m_jobs.cbegin(), m_jobs.cend(), [](const Job &job) {
        return runs(job.state) && !job.deleted;
    }));
}

SearchSession::Job *SearchSession::jobFor(const QString &plugin)
{
    for (Job &job : m_jobs)
        if (job.plugin.name == plugin)
            return &job;
    return nullptr;
}

void SearchSession::startJob(int index)
{
    Job &job = m_jobs[index];
    job.state = QStringLiteral("asking");
    job.startedMs = now();
    const quint64 generation = m_generation;
    const QString name = job.plugin.name;
    m_client->searchStart(m_pattern, m_category, {name})
        ->then(this, [this, generation, name](const QbtResult<int> &result) {
            if (generation != m_generation)
                return;
            Job *job = jobFor(name);
            if (job == nullptr)
                return;
            if (result.isError()) {
                const ApiError &error = result.error();
                if (error.kind() == ApiError::Kind::Conflict &&
                    error.message().contains(QStringLiteral("Python"))) {
                    // SR13: the server's machine has no Python: nothing can be asked.
                    m_state = State::NoPython;
                    m_error = error.message();
                    m_poll.stop();
                    for (Job &each : m_jobs)
                        if (each.state != QLatin1String("skipped"))
                            each.state = QStringLiteral("stopped");
                } else if (error.kind() == ApiError::Kind::Conflict) {
                    // Five searches run on the server already (other clients'): wait for a slot.
                    job->state = QStringLiteral("queued");
                    job->startedMs = -1;
                } else {
                    job->state = QStringLiteral("failed");
                    job->reason = error.message();
                    job->answeredMs = now();
                    if (std::all_of(m_jobs.cbegin(), m_jobs.cend(), [](const Job &each) {
                            return each.state == QLatin1String("failed") ||
                                   each.state == QLatin1String("skipped");
                        })) {
                        m_state = State::Error;
                        m_error = error.message();
                        m_poll.stop();
                    }
                }
                updateDone();
                Q_EMIT changed();
                return;
            }
            job->id = result.value();
            Q_EMIT changed();
        });
}

void SearchSession::poll()
{
    if (m_client.isNull()) {
        m_poll.stop();
        return;
    }
    bool moved = false;
    for (int i = 0; i < m_jobs.size(); ++i) {
        Job &job = m_jobs[i];
        if (!runs(job.state) || job.pendingLog)
            continue;
        // SR2: no answer in the deadline is "slow"; it is still heard.
        if (job.state == QLatin1String("asking") && job.startedMs >= 0 &&
            now() - job.startedMs >= m_deadlineMs) {
            job.state = QStringLiteral("slow");
            moved = true;
        }
        if (job.id < 0 || job.polling)
            continue;
        job.polling = true;
        const quint64 generation = m_generation;
        const int id = job.id;
        const QString name = job.plugin.name;
        m_client->searchResults(id, 0, job.offset)
            ->then(this, [this, generation, id, name](const QbtResult<QJsonObject> &result) {
                if (generation != m_generation)
                    return;
                Job *job = jobFor(name);
                if (job == nullptr || job->id != id)
                    return;
                job->polling = false;
                if (result.isError()) {
                    if (result.error().kind() == ApiError::Kind::NotFound) {
                        // Gone from the server (deleted by someone else, or qBittorrent restarted).
                        job->deleted = true;
                        finishJob(int(job - m_jobs.data()), job->count);
                        Q_EMIT changed();
                    }
                    return;
                }
                const QJsonArray results =
                    result.value().value(QStringLiteral("results")).toArray();
                for (const QJsonValue &value : results)
                    m_hits.append(SearchHit::fromJson(value.toObject(), name));
                job->offset += int(results.size());
                job->count += int(results.size());
                const bool stopped = result.value().value(QStringLiteral("status")).toString() ==
                                     QLatin1String("Stopped");
                if (!results.isEmpty())
                    Q_EMIT hitsChanged();
                if (stopped && runs(job->state))
                    finishJob(int(job - m_jobs.data()),
                              result.value().value(QStringLiteral("total")).toInt(job->count));
                Q_EMIT changed();
            });
    }
    pump();
    if (moved)
        updateDone();
    if (m_logPending)
        checkFailures();
    const bool live = std::any_of(m_jobs.cbegin(), m_jobs.cend(), [](const Job &job) {
        return runs(job.state) || job.state == QLatin1String("queued") || job.pendingLog ||
               job.recheckPending;
    });
    if (!live)
        m_poll.stop();
    if (moved)
        Q_EMIT changed();
}

void SearchSession::finishJob(int index, int total)
{
    Job &job = m_jobs[index];
    job.answeredMs = now();
    if (job.count == 0 && total == 0) {
        // Nothing found, or failed: the log says which (SR2).
        job.pendingLog = true;
        m_logPending = true;
        job.state = QStringLiteral("answered");
        checkFailures();
    } else {
        job.state = QStringLiteral("answered");
    }
    updateDone();
}

void SearchSession::checkFailures()
{
    if (m_client.isNull() || m_logChecking)
        return;
    m_logChecking = true;
    m_logPending = false;
    const quint64 generation = m_generation;
    const qint64 since = m_startedSecs - 1;
    m_client->logMainProblems(-1)->then(
        this, [this, generation, since](const QbtResult<QJsonArray> &result) {
            m_logChecking = false;
            if (generation != m_generation)
                return;
            for (Job &job : m_jobs) {
                if (!job.pendingLog && !job.recheckPending)
                    continue;
                job.pendingLog = false;
                job.recheckPending = false;
                if (result.isError())
                    continue;
                const QVariantMap failure = failureFromLog(result.value(), job.plugin.name, since);
                if (!failure.isEmpty()) {
                    job.state = QStringLiteral("failed");
                    job.reason = failure.value(QStringLiteral("reason")).toString();
                    job.logTime = failure.value(QStringLiteral("time")).toString();
                }
            }
            // Asked again meanwhile: read once more.
            if (m_logPending)
                checkFailures();
            updateDone();
            Q_EMIT changed();
        });
}

void SearchSession::updateDone()
{
    if (m_state != State::Asking)
        return;
    const bool waiting = std::any_of(m_jobs.cbegin(), m_jobs.cend(), [](const Job &job) {
        return job.state == QLatin1String("queued") || job.state == QLatin1String("asking") ||
               job.pendingLog || job.recheckPending;
    });
    if (waiting)
        return;
    // A job that found nothing is read against the log once more as the search ends: its
    // warning may have come after it stopped.
    bool recheck = false;
    for (Job &job : m_jobs) {
        if (job.state == QLatin1String("answered") && job.count == 0 && !job.logRechecked) {
            job.logRechecked = true;
            job.recheckPending = true;
            recheck = true;
        }
    }
    if (recheck) {
        m_logPending = true;
        checkFailures();
        return;
    }
    m_state = State::Done;
}

} // namespace omaseed::core
