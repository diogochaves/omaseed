// App's #69/#72 block (docs/33, DECISIONS C12): servers in any state. What omaseed saw last
// on each server (D5), the one switch the rail, the server palette, `:`, the add sheet and
// Settings call (D2–D4, D14), a refused sign-in as a state of its own (D9), and ErrorWords'
// words for the window, the footer and the desktop notice (D23). Kept out of App.cpp so it
// reads as one piece; the declarations sit at the end of App.hpp.
#include "ui/app/App.hpp"

#include "core/ErrorWords.hpp"
#include "core/Format.hpp"
#include "integration/Notifier.hpp"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

namespace omaseed::app {

using namespace omaseed::core;

namespace {

/// The last-seen figures are written at most this often while the count does not move.
constexpr int kSeenEverySeconds = 60;

} // namespace

void App::setUpServers()
{
    m_seenSave = new QTimer(this);
    m_seenSave->setSingleShot(true);
    m_seenSave->setInterval(1000);
    connect(m_seenSave, &QTimer::timeout, this, [this]() {
        writeStateSection(QStringLiteral("servers"), m_serverList->lastSeenJson());
    });
    connect(
        m_serverList, &ServerListModel::lastSeenChanged, m_seenSave, qOverload<>(&QTimer::start));
    loadLastSeen();

    // The palette's rows follow the list, the live figures and the switch.
    connect(m_serverList, &QAbstractItemModel::dataChanged, this, &App::serverRowsChanged);
    connect(m_serverList, &QAbstractItemModel::modelReset, this, &App::serverRowsChanged);
    connect(this, &App::serverChanged, this, &App::serverRowsChanged);
    connect(
        m_engine->globalState(), &GlobalState::serverVersionChanged, this, &App::serverRowsChanged);

    connect(this, &App::serverChanged, this, &App::updateServerState);
    // `serverLost` follows m_serverLost, which App.cpp's own handlers set first.
    const auto noteLost = [this]() {
        if (m_lostShown == m_serverLost)
            return;
        m_lostShown = m_serverLost;
        Q_EMIT serverStateChanged();
    };
    connect(this, &App::serverChanged, this, noteLost);
    connect(m_engine, &SyncEngine::serverLost, this, noteLost);
    connect(m_engine, &SyncEngine::authRequired, this, noteLost);
    connect(m_engine, &SyncEngine::serverBack, this, noteLost);
    connect(m_engine, &SyncEngine::synced, this, noteLost);
    // docs/44 D11: whether the latest failure is a timeout, kept through the loss so the
    // notice after it says "answered again".
    connect(m_engine->globalState(), &GlobalState::lastErrorChanged, this, [this]() {
        const QString text = m_engine->globalState()->lastError();
        if (text.isEmpty())
            return;
        const bool waiting = ApiError::fromString(text).kind() == ApiError::Kind::Timeout;
        if (waiting == m_lostWaiting)
            return;
        m_lostWaiting = waiting;
        Q_EMIT serverStateChanged();
        updateServerState();
    });
    connect(m_engine->globalState(), &GlobalState::connectedChanged, this, &App::updateServerState);
    connect(m_engine->globalState(), &GlobalState::syncingChanged, this, &App::updateServerState);
    connect(m_engine, &SyncEngine::synced, this, [this]() {
        if (m_serverRefused) {
            m_serverRefused = false;
            Q_EMIT serverStateChanged();
        }
        rememberSeen();
        updateServerState();
    });
    connect(m_engine, &SyncEngine::serverLost, this, [this](const ApiError &error) {
        rememberFailure(error);
        updateServerState();
    });
    connect(m_engine, &SyncEngine::authRequired, this, [this](const ApiError &error) {
        // D9: polling has stopped (SyncEngine), and the window says why in its own words.
        m_serverRefused = true;
        rememberFailure(error);
        Q_EMIT serverStateChanged();
        updateServerState();
    });
    // A new start (a switch, "retry now", an edited login) is a new attempt.
    connect(m_engine, &SyncEngine::runningChanged, this, [this]() {
        if (m_engine->isRunning() && m_serverRefused) {
            m_serverRefused = false;
            Q_EMIT serverStateChanged();
            updateServerState();
        }
    });
    connect(m_engine->counts(), &FilterCounts::changed, this, &App::updateServerState);
}

void App::loadLastSeen()
{
    m_serverList->loadLastSeen(stateSection(QStringLiteral("servers")));
}

void App::rememberSeen()
{
    if (m_demoMode || !m_hasServer || m_server.id.isEmpty())
        return;
    const int count = m_engine->torrents()->count();
    const QDateTime now = QDateTime::currentDateTime();
    // Each poll would rewrite state.json every 1.5 s: once a minute is "last seen" enough,
    // and a count that moves is written straight away.
    if (count == m_seenCount && m_seenAt.isValid() && m_seenAt.secsTo(now) < kSeenEverySeconds)
        return;
    m_seenCount = count;
    m_seenAt = now;
    m_serverList->setLastSeen(m_server.id, count, now);
}

void App::rememberFailure(const ApiError &error)
{
    m_serverError = error;
    if (m_demoMode || !m_hasServer || m_server.id.isEmpty())
        return;
    const bool refused = ErrorWords::isRefusedSignIn(error, m_server.authMode);
    QString reason = ErrorWords::shortReason(error);
    if (!refused && error.kind() == ApiError::Kind::Forbidden)
        reason = tr("refused omaseed");
    QString detail = credentialRefusedText(error, m_server.authMode);
    if (error.kind() == ApiError::Kind::Certificate)
        detail = certificateRefusedText(m_server.certificateSha256);
    if (detail.isEmpty())
        detail = serverLostReasonText(error);
    m_serverList->setLastFailure(m_server.id, reason, detail, QDateTime::currentDateTime());
    // The next answer is written again, whatever its count.
    m_seenAt = QDateTime();
}

void App::updateServerState()
{
    if (!m_hasServer) {
        m_serverList->setCurrent(QString(), QString(), -1);
        return;
    }
    const GlobalState *state = m_engine->globalState();
    const int count = m_engine->torrents()->count();
    QString word = QStringLiteral("live");
    if (!m_demoMode) {
        if (m_serverRefused)
            word = QStringLiteral("refused");
        else if (m_serverLost && m_serverError.kind() == ApiError::Kind::Certificate)
            word = QStringLiteral("certificate");
        else if (serverWaiting())
            word = QStringLiteral("waiting"); // docs/44 D11: a timeout
        else if (m_serverLost || (!state->connected() && !state->syncing()))
            word = QStringLiteral("down");
        else if (!state->connected())
            word = QStringLiteral("syncing");
    }
    const bool counted = m_demoMode || state->connected();
    m_serverList->setCurrent(m_server.id, word, counted ? count : -1);
}

bool App::switchServer(const QString &id)
{
    if (id.isEmpty())
        return false;
    if (m_hasServer && m_server.id == id)
        return true;
    // Leaving a server: what it showed last is written now, not a minute later.
    if (m_hasServer && !m_demoMode && !m_server.id.isEmpty() &&
        m_engine->globalState()->connected()) {
        m_seenAt = QDateTime();
        rememberSeen();
    }
    m_seenCount = -1;
    m_seenAt = QDateTime();
    m_serverError = ApiError();
    m_lostWaiting = false;
    if (m_serverRefused) {
        m_serverRefused = false;
        Q_EMIT serverStateChanged();
    }
    return setDefaultServer(id);
}

ErrorWords::Subject App::errorSubject() const
{
    ErrorWords::Subject subject;
    subject.server = m_server.name;
    subject.address = serverHostPortText();
    subject.authMode = m_server.authMode;
    subject.pinnedCertificate = m_server.certificateSha256;
    return subject;
}

bool App::serverWaiting() const
{
    return m_serverLost && !m_serverRefused && m_lostWaiting;
}

QVariantMap
App::serverProblem(const QString &lastError, int retryInSeconds, int silentSeconds) const
{
    if (lastError.isEmpty() && !m_serverRefused)
        return {};
    ApiError error = m_serverError;
    if (error.toString() != lastError && !lastError.isEmpty())
        error = ApiError::fromString(lastError);
    if (!error.isError())
        return {};
    ErrorWords::Subject subject = errorSubject();
    // Q16: the engine's retry, counted down; none after a refused sign-in (D9).
    const bool refused = m_serverRefused || ErrorWords::isRefusedSignIn(error, m_server.authMode);
    subject.retrySeconds = refused ? -1 : retryInSeconds;
    subject.silentSeconds =
        silentSeconds >= 0 ? silentSeconds : m_engine->globalState()->silentSeconds();
    QVariantMap words = ErrorWords::forServer(error, subject).toMap();
    // docs/35 N5: the window draws the retry in a slot of its own, so the line without it,
    // the retry alone, and the slot's width (its longest form).
    ErrorWords::Subject base = subject;
    base.retrySeconds = -1;
    words.insert(QStringLiteral("lineBase"), ErrorWords::forServer(error, base).line);
    // docs/44 W1: a timeout shows how long it has waited where the countdown would be.
    const QString kind = words.value(QStringLiteral("kind")).toString();
    const bool waiting = kind == QLatin1String("waiting");
    const bool retried = waiting || kind == QLatin1String("unreachable");
    words.insert(QStringLiteral("retry"),
                 waiting   ? ErrorWords::waitingLine(subject.silentSeconds)
                 : retried ? ErrorWords::retryLine(subject.retrySeconds)
                           : QString());
    words.insert(QStringLiteral("retryChars"),
                 waiting   ? int(ErrorWords::waitingLine(888).size())
                 : retried ? int(ErrorWords::retryLine(88).size())
                           : 0);
    return words;
}

QString App::guardNote(const QString &key) const
{
    return ErrorWords::guard(m_server.name, key, m_serverRefused, serverWaiting());
}

void App::notifyServerLost(const ApiError &error, int retrySeconds)
{
    ErrorWords::Subject subject = errorSubject();
    subject.retrySeconds = retrySeconds;
    subject.silentSeconds = m_engine->globalState()->silentSeconds();
    const ErrorWords::Text text = ErrorWords::forServer(error, subject);
    m_notifier->notifyServerLost(m_server.id, text.headline, text.line);
}

QString App::actionFailureText(const ApiError &error, const QString &torrent, const QString &folder)
{
    ErrorWords::Subject subject = errorSubject();
    subject.torrent = torrent;
    subject.folder = folder;
    const ErrorWords::Text words = ErrorWords::forRequest(error, subject);
    m_actionFailure = words.toMap();
    Q_EMIT actionFailureChanged();
    const QString sentence = words.sentence();
    return sentence.isEmpty() ? error.toString() : sentence;
}

QVariantList App::serverRows() const
{
    QVariantList current;
    QVariantList others;
    const auto torrentsText = [](int count) {
        return count == 1 ? tr("1 torrent") : tr("%1 torrents").arg(count);
    };
    for (int row = 0; row < m_serverList->count(); ++row) {
        const QModelIndex at = m_serverList->index(row, 0);
        const auto role = [&](ServerListModel::Role r) { return m_serverList->data(at, r); };
        const QString id = role(ServerListModel::IdRole).toString();
        const QString address = role(ServerListModel::UrlTextRole).toString();
        const QString state = role(ServerListModel::StateRole).toString();
        const bool isCurrent = role(ServerListModel::IsCurrentRole).toBool();
        QStringList detail{address};
        QString meta;
        if (isCurrent) {
            meta = tr("on it now");
            // docs/44 D11: waiting on a timeout, or nothing there.
            if (state == QLatin1String("waiting"))
                detail << tr("isn't answering");
            else if (state == QLatin1String("down"))
                detail << tr("unreachable");
            else if (state == QLatin1String("refused"))
                detail << tr("sign-in refused");
            else if (state == QLatin1String("certificate"))
                detail << tr("certificate refused");
            else if (state == QLatin1String("syncing"))
                detail << tr("first sync");
            else
                detail << torrentsText(m_engine->torrents()->count());
            const QString version = m_engine->globalState()->serverVersion();
            if (!version.isEmpty() && state != QLatin1String("down") &&
                state != QLatin1String("waiting") && state != QLatin1String("refused"))
                detail << tr("qBittorrent %1")
                              .arg(version.startsWith(QLatin1Char('v')) ? version.mid(1) : version);
        } else if (state == QLatin1String("failed")) {
            meta = role(ServerListModel::LastReasonRole).toString();
            detail << tr("last try %1").arg(role(ServerListModel::LastSeenRole).toString());
            const QString why = role(ServerListModel::LastDetailRole).toString();
            if (!why.isEmpty())
                detail << why;
        } else if (state == QLatin1String("seen")) {
            const int count = role(ServerListModel::LastCountRole).toInt();
            if (count >= 0)
                meta = torrentsText(count);
            detail << tr("last seen %1").arg(role(ServerListModel::LastSeenRole).toString());
        } else {
            detail << tr("not seen yet");
        }
        detail.removeAll(QString());
        const QVariantMap item{
            {QStringLiteral("key"), id},
            {QStringLiteral("title"), role(ServerListModel::NameRole)},
            {QStringLiteral("meta"), meta},
            {QStringLiteral("detail"), detail.join(QStringLiteral(" · "))},
            {QStringLiteral("current"), isCurrent},
            {QStringLiteral("state"), state},
            {QStringLiteral("address"), address},
            {QStringLiteral("countText"), role(ServerListModel::CountTextRole)},
            {QStringLiteral("lastReason"), role(ServerListModel::LastReasonRole)},
            {QStringLiteral("lastCount"), role(ServerListModel::LastCountRole)},
        };
        (isCurrent ? current : others).append(item);
    }
    return current + others;
}

QVariantMap App::busiestOtherServer() const
{
    const QString id = m_serverList->busiestOther();
    const int row = m_serverList->indexOfId(id);
    if (row < 0)
        return {};
    const QModelIndex at = m_serverList->index(row, 0);
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), m_serverList->data(at, ServerListModel::NameRole)},
        {QStringLiteral("count"), m_serverList->data(at, ServerListModel::LastCountRole)},
    };
}

void App::failAdd(const ApiError &error, const QString &torrent)
{
    ErrorWords::Subject subject = errorSubject();
    subject.torrent = torrent;
    const ErrorWords::Text words = ErrorWords::forRequest(error, subject);
    m_addFailure = words.toMap();
    Q_EMIT addFailureChanged();
    const QString sentence = words.sentence();
    Q_EMIT addFinished(false, sentence.isEmpty() ? error.message() : sentence, QString());
}

void App::loadEmptyDemo()
{
    startDemo(1, false);
    QJsonArray removed;
    for (const QString &hash : m_engine->torrents()->hashes())
        removed.append(hash);
    QJsonObject diff;
    diff[QStringLiteral("rid")] = 2;
    diff[QStringLiteral("torrents_removed")] = removed;
    applyDemoData(diff);
}

bool App::serversFolded() const
{
    const_cast<App *>(this)->loadSettings();
    return m_serversFolded;
}

void App::setServersFolded(bool folded)
{
    loadSettings();
    if (m_serversFolded == folded)
        return;
    m_serversFolded = folded;
    saveSettings();
    Q_EMIT settingsChanged();
}

} // namespace omaseed::app
