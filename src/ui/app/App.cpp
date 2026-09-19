#include "ui/app/App.hpp"

#include "core/Format.hpp"
#include "core/MagnetLink.hpp"
#include "core/PathMap.hpp"
#include "core/StateFile.hpp"
#include "core/TorrentFile.hpp"
#include "integration/Notifier.hpp"
#include "integration/Xdg.hpp"
#include "ui/app/DemoData.hpp"

#include <QClipboard>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QThreadPool>
#include <QUrl>
#include <QtGlobal>

using namespace omaseed::core;
using omaseed::integration::Tray;

namespace omaseed::app {

namespace {

App *s_defaultInstance = nullptr;

QString env(const char *name)
{
    return QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
}

/// "127.0.0.1:8081" — what the hero prints next to the server's name.
QString hostPort(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty())
        return QString();
    return url.port() > 0 ? QStringLiteral("%1:%2").arg(url.host()).arg(url.port()) : url.host();
}

} // namespace

App::App(QObject *parent)
    : QObject(parent)
    , m_client(new QbtClient(this))
    , m_engine(new SyncEngine(this))
    , m_filtered(new TorrentFilterModel(this))
    , m_servers(new ServerStore(this))
    , m_secrets(new SecretStore(SecretStore::defaultServiceName(), this))
{
    m_engine->setClient(m_client);
    m_filtered->setSourceModel(m_engine->torrents());

    // The hero, the footer and the tray all read GlobalState and FilterCounts, so the tray
    // only has to be told when either of them moved.
    connect(m_engine->globalState(), &GlobalState::connectedChanged, this, &App::updateTray);
    connect(
        m_engine->globalState(), &GlobalState::useAltSpeedLimitsChanged, this, &App::updateTray);
    connect(m_engine->globalState(), &GlobalState::dlInfoSpeedChanged, this, &App::updateTray);
    connect(m_engine->globalState(), &GlobalState::upInfoSpeedChanged, this, &App::updateTray);
    connect(m_engine->globalState(), &GlobalState::freeSpaceOnDiskChanged, this, &App::updateTray);
    connect(m_engine->globalState(), &GlobalState::serverNameChanged, this, &App::updateTray);
    connect(m_engine->counts(), &FilterCounts::changed, this, &App::updateTray);

    connect(m_engine, &SyncEngine::firstSyncFinished, this, [this]() {
        if (m_demoMode)
            return; // the demo has no server to name; applyDemoData() logs its own line
        const GlobalState *state = m_engine->globalState();
        qInfo().noquote() << QStringLiteral("first sync: %1 torrents · qBittorrent %2 · API %3")
                                 .arg(m_engine->torrents()->count())
                                 .arg(state->serverVersion(), state->apiVersion());
    });
    connect(m_engine, &SyncEngine::serverLost, this, [this](const ApiError &error) {
        m_serverLost = true;
        qWarning().noquote() << QStringLiteral("server lost: %1").arg(error.toString());
        updateTray();
    });
    connect(m_engine, &SyncEngine::serverBack, this, [this]() {
        m_serverLost = false;
        qInfo().noquote() << QStringLiteral("server back");
        updateTray();
    });
    connect(m_engine, &SyncEngine::authRequired, this, [this](const ApiError &error) {
        m_serverLost = true;
        qWarning().noquote() << QStringLiteral("authentication required: %1").arg(error.toString());
        updateTray();
    });
}

App::~App()
{
    if (s_defaultInstance == this)
        s_defaultInstance = nullptr;
    m_engine->stop();
}

void App::setDefaultInstance(App *instance)
{
    s_defaultInstance = instance;
}

App *App::createForEngine(QQmlEngine *engine)
{
    if (s_defaultInstance != nullptr) {
        QQmlEngine::setObjectOwnership(s_defaultInstance, QQmlEngine::CppOwnership);
        return s_defaultInstance;
    }
    // Nobody wired one up (a QML test, qmllint's runtime twin, a tool): give the engine an
    // idle one. It has every model, no server, and never polls.
    return new App(engine);
}

// --- models ------------------------------------------------------------------------------

TorrentModel *App::torrents() const
{
    return m_engine->torrents();
}

FilterCounts *App::counts() const
{
    return m_engine->counts();
}

CategoryModel *App::categories() const
{
    return m_engine->categories();
}

TagModel *App::tags() const
{
    return m_engine->tags();
}

TrackerModel *App::trackers() const
{
    return m_engine->trackers();
}

GlobalState *App::globalState() const
{
    return m_engine->globalState();
}

// --- the chosen server -------------------------------------------------------------------

QString App::serverName() const
{
    return m_server.name;
}

QString App::serverUrlText() const
{
    return hostPort(m_server.url);
}

int App::serverCount() const
{
    return m_servers->count();
}

// --- lifecycle ---------------------------------------------------------------------------

void App::start()
{
    if (m_demoMode)
        return;

    m_servers->load();
    connect(m_servers, &ServerStore::changed, this, &App::serverChanged);

    if (m_servers->count() > 0) {
        configureFromStore();
    } else if (!configureFromEnvironment()) {
        qInfo().noquote() << QStringLiteral(
            "server: none configured; add one in settings (OMASEED_SERVER_URL overrides)");
        Q_EMIT serverChanged();
        updateTray();
        return;
    }

    Q_EMIT serverChanged();
    updateTray();
}

void App::configureFromStore()
{
    const std::optional<Server> chosen =
        m_servers->defaultServer()
            ? m_servers->defaultServer()
            : (m_servers->count() == 1 ? std::optional<Server>(m_servers->servers().constFirst())
                                       : std::nullopt);
    if (!chosen) {
        qInfo().noquote() << QStringLiteral("server: %1 configured, none of them default")
                                 .arg(m_servers->count());
        return;
    }

    qInfo().noquote()
        << QStringLiteral("server: %1 (%2)").arg(chosen->url.toString(), chosen->name);
    applyServer(*chosen);
}

bool App::configureFromEnvironment()
{
    const QString urlText = env("OMASEED_SERVER_URL");
    if (urlText.isEmpty())
        return false;

    const QUrl url(urlText);
    if (!url.isValid() || url.host().isEmpty()) {
        qWarning().noquote()
            << QStringLiteral("OMASEED_SERVER_URL is not a usable URL: %1").arg(urlText);
        return false;
    }

    Server server;
    server.id = QStringLiteral("environment");
    server.url = url;
    const QString name = env("OMASEED_SERVER_NAME");
    server.name = name.isEmpty() ? url.host() : name;

    const QString apiKey = env("OMASEED_API_KEY");
    const QString username = env("OMASEED_USERNAME");
    const QString password = env("OMASEED_PASSWORD");
    if (!apiKey.isEmpty()) {
        server.authMode = AuthMode::ApiKey;
        m_client->setApiKey(apiKey);
    } else if (!username.isEmpty()) {
        server.authMode = AuthMode::Cookie;
        server.username = username;
        m_client->setCredentials(username, password);
    } else {
        server.authMode = AuthMode::None;
    }

    qInfo().noquote()
        << QStringLiteral("server: %1 (from environment)").arg(url.toString(QUrl::RemoveUserInfo));

    m_server = server;
    m_hasServer = true;
    m_client->setBaseUrl(url);
    m_engine->globalState()->setServerName(server.name);
    startEngine();
    return true;
}

void App::applyServer(const Server &server)
{
    m_server = server;
    m_hasServer = true;
    m_client->clearAuth();
    m_client->setBaseUrl(server.url);
    m_engine->globalState()->setServerName(server.name);

    // The secret is a keychain round trip, so the engine starts in the callback: polling
    // before the key is set would spend one request on a guaranteed 403.
    switch (server.authMode) {
    case AuthMode::ApiKey: {
        auto *job = m_secrets->secret(server.id, SecretStore::Kind::ApiKey, this);
        connect(job,
                &SecretStoreJob::finished,
                this,
                [this](bool ok, const QString &value, const QString &error) {
                    if (ok && !value.isEmpty())
                        m_client->setApiKey(value);
                    else
                        qWarning().noquote()
                            << QStringLiteral("no API key for this server: %1").arg(error);
                    startEngine();
                });
        break;
    }
    case AuthMode::Cookie: {
        auto *job = m_secrets->secret(server.id, SecretStore::Kind::Password, this);
        const QString username = server.username;
        connect(job,
                &SecretStoreJob::finished,
                this,
                [this, username](bool ok, const QString &value, const QString &error) {
                    if (ok)
                        m_client->setCredentials(username, value);
                    else
                        qWarning().noquote()
                            << QStringLiteral("no password for this server: %1").arg(error);
                    startEngine();
                });
        break;
    }
    case AuthMode::None:
        startEngine();
        break;
    }
}

void App::startEngine()
{
    if (m_demoMode || !m_hasServer)
        return;
    m_engine->start();
}

void App::startDemo(int rows)
{
    applyDemoData(DemoData::maindata(rows));
}

void App::applyDemoData(const QJsonObject &maindata)
{
    m_engine->stop();

    const bool wasDemo = m_demoMode;
    m_demoMode = true;

    // The hero's server line in spec §1 Fig. 1; nothing behind it is real.
    m_server = Server();
    m_server.id = QStringLiteral("demo");
    m_server.name = QStringLiteral("home");
    m_server.url = QUrl(QStringLiteral("http://demo.example.org"));
    // True on purpose: there is a (fictional) library to draw, so the views show the table
    // and not the first-run page. `demoMode` is what tells them not to touch the network.
    m_hasServer = true;

    GlobalState *state = m_engine->globalState();
    state->setServerName(m_server.name);
    state->setServerVersions(QStringLiteral("5.2.3"), QStringLiteral("2.15.1"));
    state->setConnected(true);
    state->setLastSyncTime(QDateTime::currentDateTime());

    m_engine->applyDemoData(maindata);

    qInfo().noquote() << QStringLiteral("demo: %1 fictional torrents, no server")
                             .arg(m_engine->torrents()->count());

    if (!wasDemo)
        Q_EMIT demoModeChanged();
    Q_EMIT serverChanged();
    updateTray();
}

// --- actions -----------------------------------------------------------------------------

void App::retryNow()
{
    if (m_demoMode || !m_hasServer)
        return;
    if (m_engine->isRunning())
        m_engine->retryNow();
    else
        m_engine->start();
}

void App::setVisible(bool visible)
{
    m_engine->setVisible(visible);
}

void App::toggleAltSpeed()
{
    if (m_demoMode || !m_hasServer)
        return;
    m_client->transferToggleSpeedLimitsMode()->then(
        this, [](const QbtResult<QByteArray> &result) {
            if (result.isError())
                qWarning().noquote()
                    << QStringLiteral("alt-speed toggle failed: %1").arg(result.error().toString());
        });
}

void App::stopAll()
{
    if (m_demoMode || !m_hasServer)
        return;
    m_client->torrentsStop({QStringLiteral("all")})
        ->then(this, [](const QbtResult<QByteArray> &result) {
            if (result.isError())
                qWarning().noquote()
                    << QStringLiteral("stop all failed: %1").arg(result.error().toString());
        });
}

void App::startAll()
{
    if (m_demoMode || !m_hasServer)
        return;
    m_client->torrentsStart({QStringLiteral("all")})
        ->then(this, [](const QbtResult<QByteArray> &result) {
            if (result.isError())
                qWarning().noquote()
                    << QStringLiteral("start all failed: %1").arg(result.error().toString());
        });
}

// --- the tray ----------------------------------------------------------------------------

Tray::State
App::trayStateFor(bool connected, bool serverLost, int errored, int downloading, bool altSpeed)
{
    if (!connected || serverLost)
        return Tray::Disconnected;
    if (errored > 0)
        return Tray::Error;
    if (altSpeed)
        return Tray::AltSpeed;
    if (downloading > 0)
        return Tray::Downloading;
    return Tray::Idle;
}

void App::attachTray(Tray *tray)
{
    m_tray = tray;
    if (tray == nullptr)
        return;

    connect(tray, &Tray::addRequested, this, &App::addRequested);
    connect(tray, &Tray::settingsRequested, this, &App::openSettings);
    connect(tray, &Tray::altSpeedToggled, this, [this](bool) { toggleAltSpeed(); });
    connect(tray, &Tray::stopAllRequested, this, &App::stopAll);
    connect(tray, &Tray::startAllRequested, this, &App::startAll);

    updateTray();
}

void App::updateTray()
{
    if (m_tray.isNull())
        return;

    const GlobalState *state = m_engine->globalState();
    const FilterCounts *filterCounts = m_engine->counts();

    m_tray->setState(trayStateFor(state->connected(),
                                  m_serverLost,
                                  filterCounts->errored(),
                                  filterCounts->downloading(),
                                  state->useAltSpeedLimits()));
    m_tray->setAltSpeedChecked(state->useAltSpeedLimits());
    updateTrayNumbers();
}

void App::updateTrayNumbers()
{
    const GlobalState *state = m_engine->globalState();
    const FilterCounts *filterCounts = m_engine->counts();

    m_tray->setDownloadingCount(filterCounts->downloading());
    m_tray->setTooltip(state->serverName(),
                       state->dlSpeedText(),
                       state->upSpeedText(),
                       filterCounts->downloading(),
                       filterCounts->seeding());
    m_tray->setStats(state->dlSpeedText(),
                     state->upSpeedText(),
                     filterCounts->downloading(),
                     filterCounts->seeding(),
                     state->freeSpaceText());
}

// --- T3.4: details -------------------------------------------------------------------------
//
// The details pane of spec §2. `App.details` is the controller; the table's cursor calls
// selectTorrent(), `i` calls setDetailsVisible(), and the pane tells App which tab is on
// show so peers are only polled while somebody is looking at them.

void App::selectTorrent(const QString &hash)
{
    if (hash.isEmpty()) {
        m_details->setHash(QString());
        return;
    }

    if (m_demoMode) {
        // No server: the pane is filled from the same fictional library the table draws.
        m_details->applyDemo(DemoData::details(m_engine->torrents()->torrent(hash)));
        return;
    }

    m_details->setTorrentModel(m_engine->torrents());
    m_details->setClient(m_hasServer ? m_client : nullptr);
    m_details->setHash(hash);
}

void App::setDetailsVisible(bool visible)
{
    m_details->setVisible(visible);
}

void App::setPeersTabActive(bool active)
{
    m_details->setPeersActive(active);
}

void App::setFilePriority(const QString &hash, const QVariantList &fileIndexes, int priority)
{
    if (m_demoMode || !m_hasServer || hash.isEmpty() || fileIndexes.isEmpty())
        return;

    QList<int> ids;
    ids.reserve(fileIndexes.size());
    for (const QVariant &value : fileIndexes) {
        bool ok = false;
        const int id = value.toInt(&ok);
        if (ok)
            ids.append(id);
    }
    if (ids.isEmpty())
        return;

    m_client->torrentsSetFilePrio(hash, ids, priority)
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            if (result.isError()) {
                qWarning().noquote() << QStringLiteral("set file priority failed: %1")
                                            .arg(result.error().toString());
                return;
            }
            // The file list is polled every 2 s anyway, but the pane should not look stuck
            // for a whole tick after a key press.
            m_details->refreshNow();
        });
}

QVariantMap App::torrentRow(const QString &hash) const
{
    QVariantMap row;
    if (hash.isEmpty() || m_engine->torrents()->indexOfHash(hash) < 0)
        return row;

    const Torrent &torrent = m_engine->torrents()->torrent(hash);
    row.insert(QStringLiteral("hash"), torrent.hash);
    row.insert(QStringLiteral("name"), torrent.name);
    row.insert(QStringLiteral("category"), torrent.category);
    row.insert(QStringLiteral("tagsText"), torrent.tags.join(QStringLiteral(" · ")));
    row.insert(QStringLiteral("addedOnText"), Format::added(torrent.addedOn));
    // T4.3: the raw byte count, for the delete/location sheets' "N on disk" line — every
    // other field above is already text, this is the one a caller sums across a selection.
    row.insert(QStringLiteral("size"), torrent.size);
    row.insert(QStringLiteral("savePath"), torrent.savePath);
    row.insert(QStringLiteral("magnetUri"), torrent.magnetUri);
    row.insert(QStringLiteral("infohashV1"), torrent.infohashV1);
    row.insert(QStringLiteral("dlLimit"), torrent.dlLimit);
    row.insert(QStringLiteral("upLimit"), torrent.upLimit);
    row.insert(QStringLiteral("ratioLimit"), torrent.ratioLimit);
    row.insert(QStringLiteral("seedingTimeLimit"), torrent.seedingTimeLimit);
    row.insert(QStringLiteral("sizeText"), Format::size(torrent.size));
    row.insert(QStringLiteral("progressText"), Format::percent(torrent.progress));
    row.insert(QStringLiteral("ratioText"), Format::ratio(torrent.ratio));
    row.insert(QStringLiteral("etaLongText"), Format::eta(torrent.eta));
    row.insert(QStringLiteral("stateText"), torrent.stateText());
    row.insert(QStringLiteral("stateRole"), torrent.stateRole());
    row.insert(QStringLiteral("hasTrackerError"), torrent.hasTrackerError);
    // T4.3: exactly the check `stopStart()` makes, so the actions menu's "start"/"stop" row
    // and the delete sheet agree with what a bulk stop/start would actually do.
    row.insert(QStringLiteral("stopped"), isStoppedState(torrent.state));
    return row;
}

QString App::demoTab() const
{
    return env("OMASEED_DEMO_TAB");
}

// --- end T3.4 ------------------------------------------------------------------------------

// --- T3.5: the keymap's actions -------------------------------------------------------------

void App::requestTorrentAction(const QString &action, const QStringList &hashes)
{
    Q_EMIT torrentActionRequested(action, hashes);
}

QString App::sizeText(qint64 bytes) const
{
    return Format::size(bytes);
}

// --- end T3.5 -------------------------------------------------------------------------------

// --- T4.7: notifications ---------------------------------------------------------------------
//
// SyncEngine already decides *when* something is notice-worthy (one torrentCompleted per
// torrent, one trackerError per host, diskAlmostFull/serverLost/serverBack each exactly once
// per crossing); this block only turns those events into the words Notifier sends and answers
// what the shell reports back.

void App::attachNotifier(omaseed::integration::Notifier *notifier)
{
    m_notifier = notifier;
    loadNotificationToggles();
    if (notifier == nullptr)
        return;

    connect(m_engine,
            &SyncEngine::torrentCompleted,
            this,
            [this](const QString &hash,
                   const QString &name,
                   qint64 sizeBytes,
                   const QString &category,
                   const QString &contentPath) {
                if (m_notifier.isNull() || !m_notifyCompletedEnabled)
                    return;
                const QString localPath = mapContentPathToLocal(contentPath);
                const bool hasFolder = !localPath.isEmpty() && QFileInfo::exists(localPath);
                m_completedContentPaths.insert(hash, localPath);
                m_notifier->notifyCompleted(
                    hash, name, Format::size(sizeBytes), category, hasFolder);
            });

    connect(m_engine,
            &SyncEngine::trackerError,
            this,
            [this](const QString &host, const QStringList &hashes) {
                if (m_notifier.isNull() || !m_notifyTrackerErrorEnabled)
                    return;
                m_notifier->notifyTrackerError(host, int(hashes.size()));
            });

    connect(m_engine, &SyncEngine::diskAlmostFull, this, [this](qint64 freeBytes) {
        if (m_notifier.isNull() || !m_notifyDiskFullEnabled)
            return;
        m_notifier->notifyDiskAlmostFull(m_server.id, Format::size(freeBytes));
    });

    connect(m_engine, &SyncEngine::serverLost, this, [this](const ApiError &error) {
        if (m_notifier.isNull() || !m_notifyServerLostEnabled)
            return;
        const QString reason = serverLostReasonText(error);
        const int retrySeconds = m_engine->globalState()->retryInSeconds();
        m_notifier->notifyServerLost(
            m_server.id, m_server.name, serverHostPortText(), reason, retrySeconds);
    });

    connect(m_engine, &SyncEngine::serverBack, this, [this]() {
        if (m_notifier.isNull() || !m_notifyServerLostEnabled)
            return;
        m_notifier->notifyServerBack(m_server.id, m_server.name);
    });

    connect(notifier,
            &omaseed::integration::Notifier::actionInvoked,
            this,
            &App::onNotifierActionInvoked);
}

void App::attachXdg(omaseed::integration::Xdg *xdg)
{
    m_xdg = xdg;
}

QVariantMap App::notificationsEnabled() const
{
    QVariantMap map;
    map.insert(QStringLiteral("completed"), m_notifyCompletedEnabled);
    map.insert(QStringLiteral("trackerError"), m_notifyTrackerErrorEnabled);
    map.insert(QStringLiteral("serverLost"), m_notifyServerLostEnabled);
    map.insert(QStringLiteral("diskFull"), m_notifyDiskFullEnabled);
    return map;
}

void App::setNotificationEnabled(const QString &key, bool enabled)
{
    bool *flag = nullptr;
    if (key == QStringLiteral("completed"))
        flag = &m_notifyCompletedEnabled;
    else if (key == QStringLiteral("trackerError"))
        flag = &m_notifyTrackerErrorEnabled;
    else if (key == QStringLiteral("serverLost"))
        flag = &m_notifyServerLostEnabled;
    else if (key == QStringLiteral("diskFull"))
        flag = &m_notifyDiskFullEnabled;
    else
        return;

    if (*flag == enabled)
        return;
    *flag = enabled;
    saveNotificationToggles();
    Q_EMIT notificationsEnabledChanged();
}

void App::loadNotificationToggles()
{
    if (m_notificationTogglesLoaded)
        return;
    m_notificationTogglesLoaded = true;

    const QJsonObject section = StateFile::readSection(QStringLiteral("notifications"));
    m_notifyCompletedEnabled = section.value(QStringLiteral("completed")).toBool(true);
    m_notifyTrackerErrorEnabled = section.value(QStringLiteral("trackerError")).toBool(true);
    m_notifyServerLostEnabled = section.value(QStringLiteral("serverLost")).toBool(true);
    m_notifyDiskFullEnabled = section.value(QStringLiteral("diskFull")).toBool(true);
}

void App::saveNotificationToggles()
{
    QJsonObject section;
    section.insert(QStringLiteral("completed"), m_notifyCompletedEnabled);
    section.insert(QStringLiteral("trackerError"), m_notifyTrackerErrorEnabled);
    section.insert(QStringLiteral("serverLost"), m_notifyServerLostEnabled);
    section.insert(QStringLiteral("diskFull"), m_notifyDiskFullEnabled);
    StateFile::writeSection(QStringLiteral("notifications"), section);
}

QString App::mapContentPathToLocal(const QString &remotePath) const
{
    if (remotePath.isEmpty())
        return QString();

    PathMap map;
    for (const PathMapping &mapping : m_server.pathMap)
        map.addMapping(mapping.remote, mapping.local);
    return map.toLocal(remotePath);
}

QString App::serverHostPortText() const
{
    const QUrl &url = m_server.url;
    if (!url.isValid() || url.host().isEmpty())
        return QString();
    return url.port() > 0 ? QStringLiteral("%1:%2").arg(url.host()).arg(url.port()) : url.host();
}

QString App::serverLostReasonText(const ApiError &error)
{
    // ApiError::message() for a network failure reads "GET sync/maindata: Connection refused"
    // (QbtClient::mapError prefixes the request that failed); the notice wants just the
    // underlying reason, the way spec §6 Fig. 21 prints it ("connection refused").
    QString reason = error.message();
    const qsizetype colon = reason.indexOf(QStringLiteral(": "));
    if (colon >= 0)
        reason = reason.mid(colon + 2);
    if (reason.isEmpty())
        reason = error.toString();
    if (!reason.isEmpty())
        reason.replace(0, 1, reason.at(0).toLower());
    return reason;
}

void App::onNotifierActionInvoked(const QString &eventClass,
                                  const QString &subjectId,
                                  const QString &actionKey)
{
    if (eventClass == QStringLiteral("completed")) {
        if (actionKey == QStringLiteral("open-folder")) {
            const QString path = m_completedContentPaths.value(subjectId);
            if (!path.isEmpty() && !m_xdg.isNull())
                m_xdg->showInFileManager(path);
        } else if (actionKey == QStringLiteral("show")) {
            Q_EMIT showTorrentRequested(subjectId);
        }
        return;
    }

    if (eventClass == QStringLiteral("tracker-error")) {
        const QStringList hashes = m_engine->trackers()->hashesFor(subjectId);
        if (actionKey == QStringLiteral("show-trackers")) {
            if (!hashes.isEmpty()) {
                Q_EMIT showTorrentRequested(hashes.constFirst());
                Q_EMIT showTrackersRequested(hashes.constFirst());
            }
        } else if (actionKey == QStringLiteral("reannounce")) {
            if (!m_demoMode && m_hasServer && !hashes.isEmpty()) {
                m_client->torrentsReannounce(hashes)->then(
                    this, [](const QbtResult<QByteArray> &result) {
                        if (result.isError())
                            qWarning().noquote() << QStringLiteral("reannounce failed: %1")
                                                        .arg(result.error().toString());
                    });
            }
        }
        return;
    }

    if (eventClass == QStringLiteral("server") || eventClass == QStringLiteral("disk")) {
        if (actionKey == QStringLiteral("settings"))
            Q_EMIT openSettings();
    }
}

// --- end T4.7 ----------------------------------------------------------------------------------

// --- T4.4: palettes ------------------------------------------------------------------------

bool App::matchesQuery(const QString &text, const QString &query)
{
    // The same rule as `Kit/Fuzzy.qml`'s `match()`, reimplemented here because the jump
    // palette searches the whole library (App.torrents), not something a QML delegate can
    // hand a JS array of in one binding: every whitespace-separated word of `query` is
    // either a substring of `text` or hits it as a subsequence, case-insensitive.
    const QString haystack = text.toLower();
    const QStringList words =
        query.toLower().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString &word : words) {
        if (haystack.contains(word))
            continue;
        qsizetype at = 0;
        bool hit = true;
        for (const QChar ch : word) {
            const qsizetype found = haystack.indexOf(ch, at);
            if (found < 0) {
                hit = false;
                break;
            }
            at = found + 1;
        }
        if (!hit)
            return false;
    }
    return true;
}

QVariantList App::jumpMatches(const QString &query, int limit) const
{
    QVariantList rows;
    const TorrentModel *model = m_engine->torrents();
    for (int i = 0; i < model->count() && rows.size() < limit; ++i) {
        const Torrent &torrent = model->torrentAt(i);
        if (!matchesQuery(torrent.name, query) && !matchesQuery(torrent.infohashV1, query) &&
            !matchesQuery(torrent.infohashV2, query) &&
            !matchesQuery(torrent.trackerHostKey(), query))
            continue;

        const QString category =
            torrent.category.isEmpty() ? QStringLiteral("none") : torrent.category;
        const QString third = torrent.progress >= 1.0
                                  ? QStringLiteral("ratio %1").arg(Format::ratio(torrent.ratio))
                                  : Format::percent(torrent.progress);
        QVariantMap row;
        row.insert(QStringLiteral("key"), torrent.hash);
        row.insert(QStringLiteral("title"), torrent.name);
        row.insert(QStringLiteral("meta"), torrent.stateText());
        row.insert(QStringLiteral("detail"),
                   QStringLiteral("%1 · %2 · %3").arg(category, Format::size(torrent.size), third));
        rows.append(row);
    }
    return rows;
}

int App::jumpMatchCount(const QString &query) const
{
    const TorrentModel *model = m_engine->torrents();
    int total = 0;
    for (int i = 0; i < model->count(); ++i) {
        const Torrent &torrent = model->torrentAt(i);
        if (matchesQuery(torrent.name, query) || matchesQuery(torrent.infohashV1, query) ||
            matchesQuery(torrent.infohashV2, query) ||
            matchesQuery(torrent.trackerHostKey(), query))
            ++total;
    }
    return total;
}

void App::createCategory(const QString &name)
{
    if (name.isEmpty())
        return;
    if (m_demoMode || !m_hasServer) {
        qInfo().noquote() << QStringLiteral("demo: create category %1").arg(name);
        return;
    }
    m_client->torrentsCreateCategory(name)->then(this, [name](const QbtResult<QByteArray> &result) {
        if (result.isError())
            qWarning().noquote() << QStringLiteral("create category %1 failed: %2")
                                        .arg(name, result.error().toString());
    });
}

void App::applyCategory(const QStringList &hashes, const QString &name)
{
    if (hashes.isEmpty())
        return;
    if (m_demoMode || !m_hasServer) {
        qInfo().noquote()
            << QStringLiteral("demo: set category %1 on %2 torrents").arg(name).arg(hashes.size());
        return;
    }
    m_client->torrentsSetCategory(hashes, name)
        ->then(this, [name](const QbtResult<QByteArray> &result) {
            if (result.isError())
                qWarning().noquote() << QStringLiteral("set category %1 failed: %2")
                                            .arg(name, result.error().toString());
        });
}

void App::createTag(const QString &name)
{
    if (name.isEmpty())
        return;
    if (m_demoMode || !m_hasServer) {
        qInfo().noquote() << QStringLiteral("demo: create tag %1").arg(name);
        return;
    }
    m_client->torrentsCreateTags({name})->then(this, [name](const QbtResult<QByteArray> &result) {
        if (result.isError())
            qWarning().noquote()
                << QStringLiteral("create tag %1 failed: %2").arg(name, result.error().toString());
    });
}

void App::applyTags(const QStringList &hashes, const QStringList &added, const QStringList &removed)
{
    if (hashes.isEmpty() || (added.isEmpty() && removed.isEmpty()))
        return;
    if (m_demoMode || !m_hasServer) {
        qInfo().noquote() << QStringLiteral("demo: +%1 tags -%2 tags on %3 torrents")
                                 .arg(added.size())
                                 .arg(removed.size())
                                 .arg(hashes.size());
        return;
    }
    if (!added.isEmpty())
        m_client->torrentsAddTags(hashes, added)
            ->then(this, [](const QbtResult<QByteArray> &result) {
                if (result.isError())
                    qWarning().noquote()
                        << QStringLiteral("add tags failed: %1").arg(result.error().toString());
            });
    if (!removed.isEmpty())
        m_client->torrentsRemoveTags(hashes, removed)
            ->then(this, [](const QbtResult<QByteArray> &result) {
                if (result.isError())
                    qWarning().noquote()
                        << QStringLiteral("remove tags failed: %1").arg(result.error().toString());
            });
}

QStringList App::tagsOf(const QString &hash) const
{
    const TorrentModel *model = m_engine->torrents();
    if (hash.isEmpty() || model->indexOfHash(hash) < 0)
        return {};
    const QStringList tags = model->torrent(hash).tags;
    QStringList trimmed;
    trimmed.reserve(tags.size());
    for (const QString &tag : tags) {
        const QString value = tag.trimmed();
        if (!value.isEmpty())
            trimmed.append(value);
    }
    return trimmed;
}

QString App::tableStateKey() const
{
    if (m_demoMode)
        return QStringLiteral("demo");
    if (!m_hasServer || m_server.id.isEmpty())
        return QString();
    return m_server.id;
}

void App::saveTableState(const QString &key, const QVariantMap &state)
{
    if (key.isEmpty())
        return;
    QJsonObject section = StateFile::readSection(QStringLiteral("tables"));
    section.insert(key, QJsonObject::fromVariantMap(state));
    StateFile::writeSection(QStringLiteral("tables"), section);
}

QVariantMap App::tableState(const QString &key) const
{
    if (key.isEmpty())
        return {};
    const QJsonObject section = StateFile::readSection(QStringLiteral("tables"));
    return section.value(key).toObject().toVariantMap();
}

// --- end T4.4 --------------------------------------------------------------------------------
// --- T4.2: add flow -----------------------------------------------------------------------------
// The add sheet's whole server-facing half (spec §3 Fig. 5 and Fig. 10). Parsing is local and
// never blocks the GUI thread for a .torrent; the POST is QbtClient's typed torrentsAddUrls /
// torrentsAddFile; the duplicate test reads the model that is already in memory.

namespace {

/// The last path segment of an http(s) URL, percent-decoded: what Fig. 5's subtitle shows for
/// a URL, since nothing is parsed for one until the server has fetched it.
QString urlDisplayName(const QString &text)
{
    const QUrl url(text);
    const QString path = url.path();
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const QString last = slash >= 0 ? path.mid(slash + 1) : path;
    return last.isEmpty() ? url.host() : QUrl::fromPercentEncoding(last.toUtf8());
}

QbtClient::ContentLayout contentLayoutFrom(const QString &name)
{
    if (name == QLatin1String("Subfolder"))
        return QbtClient::ContentLayout::Subfolder;
    if (name == QLatin1String("NoSubfolder"))
        return QbtClient::ContentLayout::NoSubfolder;
    return QbtClient::ContentLayout::Original;
}

} // namespace

QVariantMap App::parseAddable(const QString &textIn)
{
    const QString text = textIn.trimmed();

    QVariantMap map{
        {QStringLiteral("kind"), QStringLiteral("none")},
        {QStringLiteral("valid"), false},
        {QStringLiteral("pending"), false},
        {QStringLiteral("name"), QString()},
        {QStringLiteral("sizeText"), QString()},
        {QStringLiteral("infoHashV1"), QString()},
        {QStringLiteral("infoHashV2"), QString()},
        {QStringLiteral("value"), text},
        {QStringLiteral("token"), QString()},
        {QStringLiteral("error"), QString()},
    };
    if (text.isEmpty())
        return map;

    const integration::Addable addable = integration::Xdg::parseAddable(text);
    switch (addable.kind) {
    case integration::Addable::Kind::Magnet: {
        const MagnetLink link = MagnetLink::parse(addable.value);
        map[QStringLiteral("kind")] = QStringLiteral("magnet");
        map[QStringLiteral("value")] = addable.value;
        if (!link.isValid()) {
            map[QStringLiteral("error")] = tr("this magnet link carries no info hash");
            return map;
        }
        map[QStringLiteral("valid")] = true;
        map[QStringLiteral("infoHashV1")] = link.infoHashV1;
        map[QStringLiteral("infoHashV2")] = link.infoHashV2;
        map[QStringLiteral("name")] =
            link.displayName.isEmpty() ? link.infoHashV1 : link.displayName;
        if (link.exactLength > 0)
            map[QStringLiteral("sizeText")] = Format::size(link.exactLength);
        return map;
    }
    case integration::Addable::Kind::HttpUrl:
        map[QStringLiteral("kind")] = QStringLiteral("url");
        map[QStringLiteral("valid")] = true;
        map[QStringLiteral("value")] = addable.value;
        map[QStringLiteral("name")] = urlDisplayName(addable.value);
        return map;
    case integration::Addable::Kind::TorrentFile: {
        const QString token =
            QStringLiteral("file-%1").arg(++m_nextAddableToken, 4, 10, QLatin1Char('0'));
        QFile file(addable.value);
        if (!file.open(QIODevice::ReadOnly)) {
            map[QStringLiteral("kind")] = QStringLiteral("file");
            map[QStringLiteral("value")] = addable.value;
            map[QStringLiteral("error")] = tr("cannot read %1").arg(addable.value);
            return map;
        }
        const QByteArray bytes = file.readAll();
        file.close();
        map[QStringLiteral("kind")] = QStringLiteral("file");
        map[QStringLiteral("value")] = addable.value;
        map[QStringLiteral("token")] = token;
        map[QStringLiteral("pending")] = true;
        map[QStringLiteral("name")] = QFileInfo(addable.value).fileName();
        parseTorrentBytes(token, QFileInfo(addable.value).fileName(), addable.value, bytes);
        return map;
    }
    case integration::Addable::Kind::None:
        break;
    }

    map[QStringLiteral("error")] = tr("not a magnet link, a URL or a .torrent file");
    return map;
}

QVariantMap App::torrentFileMap(const QString &token,
                                const QString &fileName,
                                const QString &value,
                                const QByteArray &bytes) const
{
    QVariantMap map{
        {QStringLiteral("kind"), QStringLiteral("file")},
        {QStringLiteral("valid"), false},
        {QStringLiteral("pending"), false},
        {QStringLiteral("name"), fileName},
        {QStringLiteral("sizeText"), QString()},
        {QStringLiteral("infoHashV1"), QString()},
        {QStringLiteral("infoHashV2"), QString()},
        {QStringLiteral("value"), value},
        {QStringLiteral("token"), token},
        {QStringLiteral("error"), QString()},
    };

    QString error;
    const TorrentFile parsed = TorrentFile::parse(bytes, &error);
    if (!parsed.isValid()) {
        map[QStringLiteral("error")] = error.isEmpty() ? tr("this file is not a .torrent") : error;
        return map;
    }
    map[QStringLiteral("valid")] = true;
    map[QStringLiteral("name")] = parsed.name;
    map[QStringLiteral("sizeText")] = Format::size(parsed.totalSize);
    map[QStringLiteral("infoHashV1")] = parsed.infoHashV1;
    map[QStringLiteral("infoHashV2")] = parsed.infoHashV2;
    return map;
}

void App::parseTorrentBytes(const QString &token,
                            const QString &fileName,
                            const QString &value,
                            const QByteArray &bytes)
{
    // SHA-1 of a large .torrent is the one piece of parsing worth keeping off the GUI thread
    // (plan §4 T4.2). QThreadPool rather than QtConcurrent so omaseed-app keeps its current
    // link line; the hop back is an ordinary queued call onto this object's thread.
    QPointer<App> self(this);
    QThreadPool::globalInstance()->start([self, token, fileName, value, bytes]() {
        if (self.isNull())
            return;
        QMetaObject::invokeMethod(
            self,
            [self, token, fileName, value, bytes]() {
                if (self.isNull())
                    return;
                const QVariantMap map = self->torrentFileMap(token, fileName, value, bytes);
                if (map.value(QStringLiteral("valid")).toBool())
                    self->m_pendingTorrentFiles.insert(token, PendingTorrentFile{fileName, bytes});
                Q_EMIT self->addableParsed(map);
            },
            Qt::QueuedConnection);
    });
}

void App::pickTorrentFile()
{
    // getOpenFileContent never maps a widget window of ours: on Wayland Qt's
    // xdgdesktopportal platform theme serves the dialog, so the picker is the portal's.
    QFileDialog::getOpenFileContent(tr("Torrent files (*.torrent);;All files (*)"),
                                    [this](const QString &fileName, const QByteArray &bytes) {
                                        if (fileName.isEmpty())
                                            return; // cancelled
                                        const QString token = QStringLiteral("file-%1").arg(
                                            ++m_nextAddableToken, 4, 10, QLatin1Char('0'));
                                        parseTorrentBytes(token, fileName, fileName, bytes);
                                    });
}

QVariantMap App::findDuplicate(const QString &hashV1, const QString &hashV2) const
{
    const auto lower = [](const QString &hash) { return hash.trimmed().toLower(); };
    const QString v1 = lower(hashV1);
    const QString v2 = lower(hashV2);
    if (v1.isEmpty() && v2.isEmpty())
        return QVariantMap();

    TorrentModel *model = torrents();
    for (const QString &candidate : {v1, v2}) {
        if (!candidate.isEmpty() && model->indexOfHash(candidate) >= 0)
            return torrentRow(candidate);
    }
    // A hybrid torrent is keyed by its v1 hash, so a v2-only magnet has to be looked up in
    // the rows' own infohash fields.
    for (int row = 0; row < model->count(); ++row) {
        const QModelIndex index = model->index(row);
        const QString rowV1 = lower(model->data(index, TorrentModel::InfohashV1Role).toString());
        const QString rowV2 = lower(model->data(index, TorrentModel::InfohashV2Role).toString());
        if ((!v1.isEmpty() && (rowV1 == v1 || rowV2 == v1)) ||
            (!v2.isEmpty() && (rowV1 == v2 || rowV2 == v2))) {
            return torrentRow(model->data(index, TorrentModel::HashRole).toString());
        }
    }
    return QVariantMap();
}

QbtClient::AddOptions App::addOptionsFrom(const QVariantMap &request) const
{
    QbtClient::AddOptions options;

    const QString category = request.value(QStringLiteral("category")).toString();
    if (!category.isEmpty())
        options.category = category;

    const QStringList tags = request.value(QStringLiteral("tags")).toStringList();
    if (!tags.isEmpty())
        options.tags = tags;

    const QString savePath = request.value(QStringLiteral("savePath")).toString();
    if (!savePath.isEmpty())
        options.savepath = savePath;

    if (request.contains(QStringLiteral("autoTMM")))
        options.autoTMM = request.value(QStringLiteral("autoTMM")).toBool();

    if (request.value(QStringLiteral("stopped")).toBool())
        options.stopped = true;
    if (request.value(QStringLiteral("skipChecking")).toBool())
        options.skipChecking = true;
    if (request.value(QStringLiteral("sequentialDownload")).toBool())
        options.sequentialDownload = true;

    const QString layout = request.value(QStringLiteral("contentLayout")).toString();
    if (!layout.isEmpty())
        options.contentLayout = contentLayoutFrom(layout);

    const qint64 upLimitKb = request.value(QStringLiteral("upLimitKb")).toLongLong();
    if (upLimitKb > 0)
        options.upLimit = upLimitKb * 1024;

    return options;
}

void App::addTorrent(const QVariantMap &request)
{
    const QString kind = request.value(QStringLiteral("kind")).toString();
    const QString value = request.value(QStringLiteral("value")).toString();
    const QString token = request.value(QStringLiteral("token")).toString();
    const QString hash = request.value(QStringLiteral("infoHashV1")).toString().toLower();

    if (kind != QLatin1String("magnet") && kind != QLatin1String("url") &&
        kind != QLatin1String("file")) {
        Q_EMIT addFinished(false, tr("nothing to add"), QString());
        return;
    }

    if (m_demoMode) {
        qInfo().noquote() << QStringLiteral("demo: would add %1 (%2)").arg(value, kind);
        Q_EMIT addFinished(true, QString(), hash);
        return;
    }
    if (!m_hasServer) {
        Q_EMIT addFinished(false, tr("no server is configured"), QString());
        return;
    }

    const QbtClient::AddOptions options = addOptionsFrom(request);

    QbtReply<QByteArray> *reply = nullptr;
    if (kind == QLatin1String("file")) {
        const PendingTorrentFile file = m_pendingTorrentFiles.value(token);
        if (file.bytes.isEmpty()) {
            Q_EMIT addFinished(false, tr("the .torrent was not read; pick it again"), QString());
            return;
        }
        reply = m_client->torrentsAddFile(file.fileName, file.bytes, options);
    } else {
        reply = m_client->torrentsAddUrls(QStringList{value}, options);
    }

    reply->then(this, [this, hash, token](const QbtResult<QByteArray> &result) {
        if (result.isError()) {
            qWarning().noquote() << QStringLiteral("add failed: %1").arg(result.error().toString());
            Q_EMIT addFinished(false, result.error().message(), QString());
            return;
        }
        // qBittorrent answers "Fails." with 200 when it refuses the payload outright.
        const QString body = QString::fromUtf8(result.value()).trimmed();
        if (body.compare(QStringLiteral("Fails."), Qt::CaseInsensitive) == 0) {
            Q_EMIT addFinished(false, tr("the server refused this torrent"), QString());
            return;
        }
        if (!token.isEmpty())
            m_pendingTorrentFiles.remove(token);
        watchForAddedHash(hash);
        Q_EMIT addFinished(true, QString(), hash);
    });
}

void App::watchForAddedHash(const QString &hash)
{
    if (hash.isEmpty())
        return;

    QObject::disconnect(m_addWatch);
    m_awaitedAddHash = hash;

    if (!m_addWatchTimer) {
        m_addWatchTimer = new QTimer(this);
        m_addWatchTimer->setSingleShot(true);
        m_addWatchTimer->setInterval(10000);
        connect(m_addWatchTimer, &QTimer::timeout, this, [this]() {
            m_awaitedAddHash.clear();
            QObject::disconnect(m_addWatch);
        });
    }

    m_addWatch = connect(torrents(), &TorrentModel::updated, this, [this]() {
        if (m_awaitedAddHash.isEmpty() || torrents()->indexOfHash(m_awaitedAddHash) < 0)
            return;
        const QString found = m_awaitedAddHash;
        m_awaitedAddHash.clear();
        m_addWatchTimer->stop();
        QObject::disconnect(m_addWatch);
        Q_EMIT showTorrentRequested(found);
    });
    m_addWatchTimer->start();
}

QString App::demoAddText() const
{
    return m_demoMode ? env("OMASEED_DEMO_ADD") : QString();
}

void App::openUris(const QStringList &uris)
{
    if (uris.isEmpty())
        return;
    Q_EMIT addUrisRequested(uris);
}
// --- end T4.2 -----------------------------------------------------------------------------------

// --- T4.3: torrent actions ----------------------------------------------------------------

bool App::deleteFilesDefault() const
{
    const_cast<App *>(this)->loadDeleteFilesDefault();
    return m_deleteFilesDefault;
}

void App::setDeleteFilesDefault(bool value)
{
    loadDeleteFilesDefault();
    if (m_deleteFilesDefault == value)
        return;
    m_deleteFilesDefault = value;
    QJsonObject section;
    section.insert(QStringLiteral("value"), value);
    StateFile::writeSection(QStringLiteral("deleteFiles"), section);
    Q_EMIT deleteFilesDefaultChanged();
}

void App::loadDeleteFilesDefault()
{
    if (m_deleteFilesDefaultLoaded)
        return;
    m_deleteFilesDefaultLoaded = true;
    const QJsonObject section = StateFile::readSection(QStringLiteral("deleteFiles"));
    m_deleteFilesDefault = section.value(QStringLiteral("value")).toBool(false);
}

bool App::isStoppedState(TorrentState state)
{
    return state == TorrentState::StoppedDownload || state == TorrentState::StoppedUpload ||
           state == TorrentState::PausedDownload || state == TorrentState::PausedUpload;
}

bool App::demoGuard(const QString &action, int count)
{
    if (!m_demoMode)
        return false;
    qInfo().noquote() << QStringLiteral("demo: %1 %2").arg(action).arg(count);
    return true;
}

void App::reportActionResult(const QString &action, const QbtResult<QByteArray> &result)
{
    if (result.isError())
        Q_EMIT actionFailed(action, result.error().toString());
}

void App::stopStart(const QStringList &hashes)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("stopStart"), hashes.size()) || !m_hasServer)
        return;

    const int index = m_engine->torrents()->indexOfHash(hashes.constFirst());
    const bool stopped =
        index >= 0 && isStoppedState(m_engine->torrents()->torrent(hashes.constFirst()).state);

    auto *reply = stopped ? m_client->torrentsStart(hashes) : m_client->torrentsStop(hashes);
    reply->then(this, [this](const QbtResult<QByteArray> &result) {
        reportActionResult(QStringLiteral("stopStart"), result);
    });
}

void App::forceStart(const QStringList &hashes)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("forceStart"), hashes.size()) || !m_hasServer)
        return;
    m_client->torrentsSetForceStart(hashes, true)
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("forceStart"), result);
        });
}

void App::recheck(const QStringList &hashes)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("recheck"), hashes.size()) || !m_hasServer)
        return;
    m_client->torrentsRecheck(hashes)->then(this, [this](const QbtResult<QByteArray> &result) {
        reportActionResult(QStringLiteral("recheck"), result);
    });
}

void App::reannounce(const QStringList &hashes)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("reannounce"), hashes.size()) || !m_hasServer)
        return;
    m_client->torrentsReannounce(hashes)->then(this, [this](const QbtResult<QByteArray> &result) {
        reportActionResult(QStringLiteral("reannounce"), result);
    });
}

void App::setCategory(const QStringList &hashes, const QString &name)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("category"), hashes.size()) || !m_hasServer)
        return;
    m_client->torrentsSetCategory(hashes, name)
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("category"), result);
        });
}

void App::addTags(const QStringList &hashes, const QStringList &tags)
{
    if (hashes.isEmpty() || tags.isEmpty() || demoGuard(QStringLiteral("addTags"), hashes.size()) ||
        !m_hasServer)
        return;
    m_client->torrentsAddTags(hashes, tags)
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("addTags"), result);
        });
}

void App::removeTags(const QStringList &hashes, const QStringList &tags)
{
    if (hashes.isEmpty() || tags.isEmpty() ||
        demoGuard(QStringLiteral("removeTags"), hashes.size()) || !m_hasServer)
        return;
    m_client->torrentsRemoveTags(hashes, tags)
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("removeTags"), result);
        });
}

void App::setLocation(const QStringList &hashes, const QString &path)
{
    if (hashes.isEmpty() || path.isEmpty() ||
        demoGuard(QStringLiteral("location"), hashes.size()) || !m_hasServer)
        return;
    m_client->torrentsSetLocation(hashes, path)
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("location"), result);
        });
}

void App::setLimits(
    const QStringList &hashes, qint64 dl, qint64 ul, double ratio, qint64 seedingMinutes)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("limits"), hashes.size()) || !m_hasServer)
        return;

    if (dl != -1) {
        m_client->torrentsSetDownloadLimit(hashes, dl)
            ->then(this, [this](const QbtResult<QByteArray> &result) {
                reportActionResult(QStringLiteral("limits"), result);
            });
    }
    if (ul != -1) {
        m_client->torrentsSetUploadLimit(hashes, ul)
            ->then(this, [this](const QbtResult<QByteArray> &result) {
                reportActionResult(QStringLiteral("limits"), result);
            });
    }

    // torrents/setShareLimits takes seedingTimeLimit in minutes (qBittorrent's own unit); the
    // caller (LimitsSheet) has already converted its days field, so this is passed straight
    // through, sentinels (-2 global, -1 none) included.
    m_client->torrentsSetShareLimits(hashes, ratio, seedingMinutes, -2)
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("limits"), result);
        });
}

void App::rename(const QString &hash, const QString &name)
{
    if (hash.isEmpty() || name.isEmpty() || demoGuard(QStringLiteral("rename"), 1) || !m_hasServer)
        return;
    m_client->torrentsRename(hash, name)->then(this, [this](const QbtResult<QByteArray> &result) {
        reportActionResult(QStringLiteral("rename"), result);
    });
}

void App::queueUp(const QStringList &hashes)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("queueUp"), hashes.size()) || !m_hasServer)
        return;
    m_client->torrentsIncreasePrio(hashes)->then(this, [this](const QbtResult<QByteArray> &result) {
        reportActionResult(QStringLiteral("queueUp"), result);
    });
}

void App::queueDown(const QStringList &hashes)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("queueDown"), hashes.size()) || !m_hasServer)
        return;
    m_client->torrentsDecreasePrio(hashes)->then(this, [this](const QbtResult<QByteArray> &result) {
        reportActionResult(QStringLiteral("queueDown"), result);
    });
}

void App::remove(const QStringList &hashes, bool deleteFiles)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("delete"), hashes.size()) || !m_hasServer)
        return;
    m_client->torrentsDelete(hashes, deleteFiles)
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("delete"), result);
        });
}

void App::copyToClipboard(const QString &text) const
{
    if (text.isEmpty())
        return;
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

void App::openFolder(const QString &hash)
{
    if (hash.isEmpty() || m_xdg.isNull())
        return;
    if (m_engine->torrents()->indexOfHash(hash) < 0)
        return;
    const QString local = mapContentPathToLocal(m_engine->torrents()->torrent(hash).contentPath);
    if (!local.isEmpty())
        m_xdg->showInFileManager(local);
}

QString App::pickDirectory(const QString &startDir) const
{
    return QFileDialog::getExistingDirectory(
        nullptr, tr("Choose a location"), startDir, QFileDialog::ShowDirsOnly);
}

// --- end T4.3 ------------------------------------------------------------------------------

} // namespace omaseed::app
