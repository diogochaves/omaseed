#include "ui/app/App.hpp"

#include "core/Format.hpp"
#include "core/MagnetLink.hpp"
#include "core/PathMap.hpp"
#include "core/PeerInput.hpp"
#include "core/StateFile.hpp"
#include "core/TorrentFile.hpp"
#include "core/Version.hpp"
#include "integration/Notifier.hpp"
#include "integration/ThemeReader.hpp"
#include "integration/Xdg.hpp"
#include "ui/app/DemoData.hpp"

#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

#include <atomic>
#include <chrono>

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
    m_prefs = new ServerPreferences(m_client, this);
    m_filtered->setSourceModel(m_engine->torrents());
    // A tracker row lists what the rail counts for it: every torrent on that host.
    m_filtered->setTrackerModel(m_engine->trackers());

    // Here rather than in start(): demo and QML-test Apps never call start(), and their
    // `serverCount`/`firstRunNeeded` bindings must follow the store all the same.
    connect(m_servers, &ServerStore::changed, this, &App::serverChanged);
    // Settings › Servers warns while secrets go to a plain file (review Q10).
    connect(m_secrets, &SecretStore::backendResolved, this, &App::plainSecretsPathChanged);

    // The hero, the footer and the tray all read GlobalState and FilterCounts, so the tray
    // only has to be told when either of them moved.
    connect(m_engine->globalState(), &GlobalState::connectedChanged, this, &App::updateTray);
    // docs/16: the hero reads the listening port and anonymous mode, so the preferences are
    // read as soon as a server answers, and again whenever it comes back.
    connect(m_engine->globalState(), &GlobalState::connectedChanged, this, [this]() {
        if (!m_demoMode && m_hasServer && m_engine->globalState()->connected())
            m_prefs->reload();
    });
    connect(m_prefs, &ServerPreferences::posted, this, &App::followServerPreferences);
    connect(m_prefs, &ServerPreferences::apiKeyRotated, this, &App::followRotatedApiKey);
    connect(
        m_engine->globalState(), &GlobalState::useAltSpeedLimitsChanged, this, &App::updateTray);
    connect(m_engine->globalState(), &GlobalState::dlInfoSpeedChanged, this, &App::updateTray);
    connect(m_engine->globalState(), &GlobalState::upInfoSpeedChanged, this, &App::updateTray);
    connect(m_engine->globalState(), &GlobalState::freeSpaceOnDiskChanged, this, &App::updateTray);
    connect(m_engine->globalState(), &GlobalState::serverNameChanged, this, &App::updateTray);
    connect(m_engine->counts(), &FilterCounts::changed, this, &App::updateTray);
    // T5.5 Q14: `supportsShareLimitAction` is derived from the API version, nothing else.
    connect(m_engine->globalState(),
            &GlobalState::apiVersionChanged,
            this,
            &App::supportsShareLimitActionChanged);

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
    // serverBack only follows serverLost; after authRequired (a retry, a fixed password) the
    // first good sync is what says the server is back.
    connect(m_engine, &SyncEngine::synced, this, [this]() {
        if (!m_serverLost)
            return;
        m_serverLost = false;
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

QString App::plainSecretsPath() const
{
    if (m_demoMode || m_secrets->knownBackend() != SecretStore::Backend::File)
        return {};
    return SecretStore::fileBackendDir();
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

    if (!m_servers->load())
        qWarning().noquote() << QStringLiteral("servers: %1; it is kept as %2")
                                    .arg(m_servers->loadError(), m_servers->keptPath());

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

    qInfo().noquote() << QStringLiteral("server: %1 (%2)")
                             .arg(chosen->url.toString(QUrl::RemoveUserInfo), chosen->name);
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
    // A keychain read for the previous server may still be on its way; its answer must not
    // put that server's secret on a client now pointed at this one.
    const quint64 generation = ++m_serverGeneration;
    m_server = server;
    m_hasServer = true;
    // clearAuth() and a new base URL also cancel everything still queued, in flight or
    // backing off for the previous server (QbtClient::cancelAll).
    m_client->clearAuth();
    m_client->setBaseUrl(server.url);
    m_prefs->reset();
    m_engine->globalState()->setServerName(server.name);

    // The secret is a keychain round trip, so the engine starts in the callback: polling
    // before the key is set would spend one request on a guaranteed 403.
    switch (server.authMode) {
    case AuthMode::ApiKey: {
        auto *job = m_secrets->secret(server.id, SecretStore::Kind::ApiKey, this);
        connect(job,
                &SecretStoreJob::finished,
                this,
                [this, generation](bool ok, const QString &value, const QString &error) {
                    if (generation != m_serverGeneration)
                        return; // another server (or none) took over meanwhile
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
                [this, username, generation](bool ok, const QString &value, const QString &error) {
                    if (generation != m_serverGeneration)
                        return; // another server (or none) took over meanwhile
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

void App::startDemo(int rows, bool seedServer)
{
    applyDemoData(DemoData::maindata(rows));
    if (!seedServer)
        return;

    if (!m_servers->byId(m_server.id))
        m_servers->add(m_server);
    m_servers->setDefault(m_server.id);
    // As if the demo server had been tested: "v5.2.3 · 16 torrents" on its row.
    m_serverList->setProbeResult(m_server.id,
                                 true,
                                 QStringLiteral("5.2.3"),
                                 QStringLiteral("2.15.1"),
                                 m_engine->torrents()->count(),
                                 3,
                                 QString());
}

void App::applyDemoData(const QJsonObject &maindata)
{
    m_engine->stop();

    const bool wasDemo = m_demoMode;
    m_demoMode = true;
    // A demo never persists: servers added, edited or removed in its Settings stay in memory.
    m_servers->setPersistent(false);

    // The hero's server line in spec §1 Fig. 1; nothing behind it is real.
    m_server = Server();
    m_server.id = QStringLiteral("demo");
    m_server.name = QStringLiteral("home");
    m_server.url = QUrl(QStringLiteral("http://demo.example.org"));
    m_server.authMode = AuthMode::None;
    // True on purpose: there is a (fictional) library to draw, so the views show the table
    // and not the first-run page. `demoMode` is what tells them not to touch the network.
    m_hasServer = true;
    m_prefs->setOffline(DemoData::preferences());

    GlobalState *state = m_engine->globalState();
    state->setServerName(m_server.name);
    state->setServerVersions(QStringLiteral("5.2.3"), QStringLiteral("2.15.1"));
    state->setConnected(true);
    state->setLastSyncTime(QDateTime::currentDateTime());

    m_engine->applyDemoData(maindata);

    qInfo().noquote() << QStringLiteral("demo: %1 fictional torrents, no server")
                             .arg(m_engine->torrents()->count());

    if (!wasDemo) {
        Q_EMIT demoModeChanged();
        Q_EMIT plainSecretsPathChanged();
    }
    Q_EMIT serverChanged();
    updateTray();
}

// --- actions -----------------------------------------------------------------------------

void App::retryNow()
{
    if (m_demoMode || !m_hasServer)
        return;
    // A retry the user asked for is one deliberate attempt: after a refused login (the ban
    // expired, the password was fixed on the server) it may log in again (review REG-2).
    m_client->allowLoginAgain();
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

    // The Tray shows its icon when it is built; a saved "never" has to be applied here, not
    // only when the setting changes.
    loadSettings();
    tray->setIconVisible(m_trayIconMode != QLatin1String("never"));

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
        // The torrent row still carries what torrents/properties lacks (limits, magnet).
        m_details->setTorrentModel(m_engine->torrents());
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

void App::setGeneralTabActive(bool active)
{
    m_details->setGeneralActive(active);
}

void App::setFilePriority(const QString &hash, const QVariantList &fileIndexes, int priority)
{
    if (hash.isEmpty() || fileIndexes.isEmpty())
        return;
    // N1: the rows change at once, before the server answers (and instead of it in demo
    // mode, where there is none); the next poll confirms or corrects them.
    if (m_details->hash() == hash)
        m_details->files()->setLocalPriority(fileIndexes, priority);
    if (m_demoMode || !m_hasServer)
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
                Q_EMIT actionFailed(QStringLiteral("priority"), result.error().toString());
            }
            // The file list is polled every 2 s anyway, but the pane should not look stuck
            // for a whole tick after a key press, nor keep a local change the server refused.
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
    row.insert(QStringLiteral("inactiveSeedingTimeLimit"), torrent.inactiveSeedingTimeLimit);
    row.insert(QStringLiteral("shareLimitAction"), torrent.shareLimitAction);
    row.insert(QStringLiteral("sizeText"), Format::size(torrent.size));
    row.insert(QStringLiteral("progressText"), Format::percent(torrent.progress));
    row.insert(QStringLiteral("ratioText"), Format::ratio(torrent.ratio));
    row.insert(QStringLiteral("etaLongText"),
               torrent.hasEta() ? Format::eta(torrent.eta) : QStringLiteral("—"));
    row.insert(QStringLiteral("stateText"), torrent.stateText());
    row.insert(QStringLiteral("stateRole"), torrent.stateRole());
    row.insert(QStringLiteral("hasTrackerError"), torrent.hasTrackerError);
    // T4.3: exactly the check `stopStart()` makes, so the actions menu's "start"/"stop" row
    // and the delete sheet agree with what a bulk stop/start would actually do.
    row.insert(QStringLiteral("stopped"), isStoppedState(torrent.state));
    // docs/12 G4: General's Options group and the actions menu's four option rows.
    row.insert(QStringLiteral("seqDl"), torrent.seqDl);
    row.insert(QStringLiteral("firstLastPiecePrio"), torrent.firstLastPiecePrio);
    row.insert(QStringLiteral("superSeeding"), torrent.superSeeding);
    row.insert(QStringLiteral("autoTmm"), torrent.autoTmm);
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

QString App::limitShortText(qint64 bytesPerSecond)
{
    if (bytesPerSecond <= 0)
        return QStringLiteral("∞");
    return Format::speedCompact(bytesPerSecond);
}

QString App::speedShortText(qint64 bytesPerSecond)
{
    return Format::speedCompact(bytesPerSecond);
}

// --- end T3.5 -------------------------------------------------------------------------------

// --- T4.7: notifications ---------------------------------------------------------------------
//
// SyncEngine already decides *when* something is notice-worthy (one torrentCompleted per
// torrent, diskAlmostFull/serverLost/serverBack each exactly once per crossing); this block
// only turns those events into the words Notifier sends and answers what the shell reports
// back. There is no tracker-error notice (spec §6 Fig. 20): Q16 dropped it, so a failing
// tracker shows only as the Trackers tab badge and the rail filter.

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
    // A closed "completed" notice can no longer ask for its folder: drop its path (the
    // action, if any, arrives before the close), so the map does not grow for ever.
    connect(notifier,
            &omaseed::integration::Notifier::noticeClosed,
            this,
            [this](const QString &eventClass, const QString &subjectId) {
                if (eventClass == QLatin1String("completed"))
                    m_completedContentPaths.remove(subjectId);
            });
}

void App::attachXdg(omaseed::integration::Xdg *xdg)
{
    m_xdg = xdg;
}

QVariantMap App::notificationsEnabled() const
{
    // QML reads this before main.cpp attaches the notifier, so the file is read on demand
    // here too (the same lazy load the T4.5 getters do with loadSettings()).
    const_cast<App *>(this)->loadNotificationToggles();
    QVariantMap map;
    map.insert(QStringLiteral("completed"), m_notifyCompletedEnabled);
    map.insert(QStringLiteral("serverLost"), m_notifyServerLostEnabled);
    map.insert(QStringLiteral("diskFull"), m_notifyDiskFullEnabled);
    return map;
}

void App::setNotificationEnabled(const QString &key, bool enabled)
{
    loadNotificationToggles();
    bool *flag = nullptr;
    if (key == QStringLiteral("completed"))
        flag = &m_notifyCompletedEnabled;
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

int App::lowDiskThresholdGiB() const
{
    const_cast<App *>(this)->loadNotificationToggles();
    return m_lowDiskThresholdGiB;
}

void App::setLowDiskThresholdGiB(int gib)
{
    loadNotificationToggles();
    const int clamped = qMax(1, gib);
    if (clamped == m_lowDiskThresholdGiB)
        return;
    m_lowDiskThresholdGiB = clamped;
    saveNotificationToggles();
    applyLowDiskThreshold();
    Q_EMIT lowDiskThresholdGiBChanged();
}

void App::loadNotificationToggles()
{
    if (m_notificationTogglesLoaded)
        return;
    m_notificationTogglesLoaded = true;

    const QJsonObject section = stateSection(QStringLiteral("notifications"));
    m_notifyCompletedEnabled = section.value(QStringLiteral("completed")).toBool(true);
    m_notifyServerLostEnabled = section.value(QStringLiteral("serverLost")).toBool(true);
    m_notifyDiskFullEnabled = section.value(QStringLiteral("diskFull")).toBool(true);
    m_lowDiskThresholdGiB = qMax(1, section.value(QStringLiteral("lowDiskThresholdGiB")).toInt(2));
    applyLowDiskThreshold();
}

void App::saveNotificationToggles()
{
    QJsonObject section;
    section.insert(QStringLiteral("completed"), m_notifyCompletedEnabled);
    section.insert(QStringLiteral("serverLost"), m_notifyServerLostEnabled);
    section.insert(QStringLiteral("diskFull"), m_notifyDiskFullEnabled);
    section.insert(QStringLiteral("lowDiskThresholdGiB"), m_lowDiskThresholdGiB);
    writeStateSection(QStringLiteral("notifications"), section);
}

void App::applyLowDiskThreshold()
{
    m_engine->setLowDiskThreshold(qint64(m_lowDiskThresholdGiB) * 1024 * 1024 * 1024);
}

QString App::mapContentPathToLocal(const QString &remotePath) const
{
    if (remotePath.isEmpty())
        return QString();
    if (!pathMapEnabled())
        return remotePath;

    PathMap map;
    for (const PathMapping &mapping : m_server.pathMap)
        map.addMapping(mapping.remote, mapping.local);
    return map.toLocal(remotePath);
}

QString App::serverHostPortText() const
{
    return hostPort(m_server.url);
}

QString App::errorReasonText(const QString &text)
{
    // A client error reads "network: GET sync/maindata: Connection refused": the kind, the
    // request that failed, then the reason. Fig. 21's notice and Fig. 23's banner both print
    // only the reason ("connection refused"), so everything before the last ": " goes.
    QString reason = text;
    const qsizetype colon = reason.lastIndexOf(QStringLiteral(": "));
    if (colon >= 0)
        reason = reason.mid(colon + 2);
    if (!reason.isEmpty())
        reason.replace(0, 1, reason.at(0).toLower());
    return reason;
}

QString App::lostReasonText(const QString &lastError) const
{
    // GlobalState keeps only ApiError::toString(), which starts with the kind's name.
    const bool unauthorized = lastError.startsWith(QLatin1String("unauthorized"));
    const bool forbidden = lastError.startsWith(QLatin1String("forbidden"));
    if (unauthorized || forbidden) {
        const ApiError error(forbidden ? ApiError::Kind::Forbidden : ApiError::Kind::Unauthorized,
                             lastError);
        const QString words = credentialRefusedText(error, m_server.authMode);
        if (!words.isEmpty())
            return words;
    }
    return errorReasonText(lastError);
}

QString App::serverLostReasonText(const ApiError &error)
{
    const QString reason = errorReasonText(error.message());
    return reason.isEmpty() ? errorReasonText(error.toString()) : reason;
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

bool App::matchesHashPrefix(const QString &hash, const QString &query)
{
    // A hash is 40 (v1) or 64 (v2) hex characters, so a subsequence match on it is almost
    // free: "fed" finds an f, then an e, then a d somewhere in nearly every hash in the
    // library (spec §4 Fig. 12 promises "also matches hash", not "matches noise"). Only a
    // prefix counts, which is how a hash is ever typed or pasted-and-trimmed.
    if (hash.isEmpty() || query.isEmpty())
        return false;
    const QString word = query.trimmed().toLower();
    if (word.isEmpty() || word.contains(QLatin1Char(' ')))
        return false;
    for (const QChar ch : word) {
        const bool hex = (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
                         (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'));
        if (!hex)
            return false;
    }
    return hash.toLower().startsWith(word);
}

bool App::matchesTorrent(const core::Torrent &torrent, const QString &query)
{
    return matchesQuery(torrent.name, query) || matchesHashPrefix(torrent.infohashV1, query) ||
           matchesHashPrefix(torrent.infohashV2, query) ||
           matchesQuery(torrent.trackerHostKey(), query);
}

QVariantList App::jumpMatches(const QString &query, int limit) const
{
    QVariantList rows;
    const TorrentModel *model = m_engine->torrents();
    for (int i = 0; i < model->count() && rows.size() < limit; ++i) {
        const Torrent &torrent = model->torrentAt(i);
        if (!matchesTorrent(torrent, query))
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
        if (matchesTorrent(model->torrentAt(i), query))
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

void App::createAndApplyCategory(const QStringList &hashes, const QString &name)
{
    if (name.isEmpty())
        return;
    if (m_demoMode || !m_hasServer) {
        qInfo().noquote() << QStringLiteral("demo: create category %1 on %2 torrents")
                                 .arg(name)
                                 .arg(hashes.size());
        return;
    }
    m_client->torrentsCreateCategory(name)->then(
        this, [this, hashes, name](const QbtResult<QByteArray> &result) {
            // 409 is "it exists already", which is what the set needs anyway.
            if (result.isError() && result.error().kind() != ApiError::Kind::Conflict) {
                reportActionResult(QStringLiteral("category"), result);
                return;
            }
            if (hashes.isEmpty() || !m_hasServer)
                return;
            m_client->torrentsSetCategory(hashes, name)
                ->then(this, [this](const QbtResult<QByteArray> &set) {
                    reportActionResult(QStringLiteral("category"), set);
                });
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
    QJsonObject section = stateSection(QStringLiteral("tables"));
    section.insert(key, QJsonObject::fromVariantMap(state));
    writeStateSection(QStringLiteral("tables"), section);
}

QVariantMap App::tableState(const QString &key) const
{
    if (key.isEmpty())
        return {};
    const QJsonObject section = stateSection(QStringLiteral("tables"));
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
        map[QStringLiteral("kind")] = QStringLiteral("file");
        map[QStringLiteral("value")] = addable.value;
        map[QStringLiteral("token")] = token;
        map[QStringLiteral("pending")] = true;
        map[QStringLiteral("name")] = QFileInfo(addable.value).fileName();
        // Read and parsed off the GUI thread; a file that cannot be read comes back through
        // addableParsed with its error, like one that does not parse.
        parseTorrentInBackground(
            token, QFileInfo(addable.value).fileName(), addable.value, addable.value, QByteArray());
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
                                const TorrentFile &parsed,
                                const QString &error)
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
        {QStringLiteral("fileCount"), 0},
    };

    if (!parsed.isValid()) {
        map[QStringLiteral("error")] = error.isEmpty() ? tr("this file is not a .torrent") : error;
        return map;
    }
    map[QStringLiteral("valid")] = true;
    map[QStringLiteral("name")] = parsed.name;
    map[QStringLiteral("sizeText")] = Format::size(parsed.totalSize);
    map[QStringLiteral("infoHashV1")] = parsed.infoHashV1;
    map[QStringLiteral("infoHashV2")] = parsed.infoHashV2;
    map[QStringLiteral("fileCount")] = int(parsed.files.size());
    return map;
}

namespace {

/// Bigger than any real .torrent (qBittorrent itself refuses past 100 MiB); a guard against
/// reading a huge file someone named `.torrent` into memory.
constexpr qint64 kMaxTorrentFileBytes = 100 * 1024 * 1024;

/// How many parsed .torrent files are kept for a later add.
constexpr int kMaxPendingTorrentFiles = 64;

std::atomic<QThread *> s_lastTorrentParseThread{nullptr};

} // namespace

void App::rememberTorrentFile(const QString &token, const PendingTorrentFile &file)
{
    // Every parse (typed, dropped, picked, then abandoned) used to stay for the life of the
    // process. Keep the most recent ones only: more than a sheet's queue ever holds.
    m_pendingTorrentFiles.insert(token, file);
    m_pendingTorrentOrder.removeAll(token);
    m_pendingTorrentOrder.append(token);
    while (m_pendingTorrentOrder.size() > kMaxPendingTorrentFiles)
        m_pendingTorrentFiles.remove(m_pendingTorrentOrder.takeFirst());
}

void App::forgetTorrentFile(const QString &token)
{
    m_pendingTorrentFiles.remove(token);
    m_pendingTorrentOrder.removeAll(token);
}

int App::pendingTorrentFileCount() const
{
    return m_pendingTorrentFiles.size();
}

QThread *App::lastTorrentParseThread()
{
    return s_lastTorrentParseThread.load();
}

void App::parseTorrentInBackground(const QString &token,
                                   const QString &fileName,
                                   const QString &value,
                                   const QString &path,
                                   const QByteArray &bytesIn)
{
    // Reading, bencode and the SHA-1/SHA-256 of the info dict all run on the pool (plan §4
    // T4.2). QThreadPool rather than QtConcurrent so omaseed-app keeps its current link line.
    // The worker touches nothing of App's: it hops back through the application object,
    // which outlives every App, and only there is the QPointer to App read.
    QPointer<App> self(this);
    QThreadPool::globalInstance()->start([self, token, fileName, value, path, bytesIn]() {
        QByteArray bytes = bytesIn;
        QString error;
        TorrentFile parsed;
        if (!path.isEmpty()) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                error = tr("cannot read %1").arg(path);
            else if (file.size() > kMaxTorrentFileBytes)
                error = tr("%1 is too large to be a .torrent").arg(path);
            else
                bytes = file.readAll();
        }
        if (error.isEmpty())
            parsed = TorrentFile::parse(bytes, &error);
        s_lastTorrentParseThread.store(QThread::currentThread());

        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;
        QMetaObject::invokeMethod(
            application,
            [self, token, fileName, value, bytes, parsed, error]() {
                if (self.isNull())
                    return;
                const QVariantMap map = torrentFileMap(token, fileName, value, parsed, error);
                if (parsed.isValid())
                    self->rememberTorrentFile(
                        token,
                        PendingTorrentFile{fileName, bytes, parsed.files, parsed.torrentId()});
                Q_EMIT self->addableParsed(map);
            },
            Qt::QueuedConnection);
    });
}

void App::pickTorrentFile()
{
    // The portal's chooser, not a widget window of ours: main() selects Qt's
    // xdgdesktopportal platform theme, which serves QFileDialog's static pickers.
    QFileDialog::getOpenFileContent(
        tr("Torrent files (*.torrent);;All files (*)"),
        [this](const QString &fileName, const QByteArray &bytes) {
            if (fileName.isEmpty())
                return; // cancelled
            const QString token =
                QStringLiteral("file-%1").arg(++m_nextAddableToken, 4, 10, QLatin1Char('0'));
            parseTorrentInBackground(token, fileName, fileName, QString(), bytes);
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
    // A torrent with a v2 hash is keyed by that hash cut to 40 characters, not by its v1 hash,
    // so anything the key misses is looked up in the rows' own infohash fields.
    const QString id = TorrentFile::torrentIdFor(v1, v2);
    if (model->indexOfHash(id) >= 0)
        return torrentRow(id);
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
    const qint64 dlLimitKb = request.value(QStringLiteral("dlLimitKb")).toLongLong();
    if (dlLimitKb > 0)
        options.dlLimit = dlLimitKb * 1024;

    const QString rename = request.value(QStringLiteral("rename")).toString().trimmed();
    if (!rename.isEmpty() && rename != request.value(QStringLiteral("name")).toString())
        options.rename = rename;

    if (request.value(QStringLiteral("firstLastPiecePrio")).toBool())
        options.firstLastPiecePrio = true;

    return options;
}

void App::addTorrent(const QVariantMap &request)
{
    const QString kind = request.value(QStringLiteral("kind")).toString();
    const QString value = request.value(QStringLiteral("value")).toString();
    const QString token = request.value(QStringLiteral("token")).toString();
    // The key the new row will have in maindata, which is what the cursor and QML look for.
    const QString hash =
        TorrentFile::torrentIdFor(request.value(QStringLiteral("infoHashV1")).toString(),
                                  request.value(QStringLiteral("infoHashV2")).toString());

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

    QbtClient::AddOptions options = addOptionsFrom(request);

    // docs/11 D1: a file upload takes no file priorities ("Cannot specify filePriorities
    // when uploading torrent files", 5.2), so the choices go on afterwards, while the
    // torrent is still stopped and has not asked for a single piece.
    const QVariantList filePriorities =
        kind == QLatin1String("file") ? request.value(QStringLiteral("filePriorities")).toList()
                                      : QVariantList();
    const bool withPriorities = !AddFilesModel::priorityGroups(filePriorities).isEmpty();
    const bool keepStopped = request.value(QStringLiteral("stopped")).toBool();
    if (withPriorities)
        options.stopped = true;

    QbtReply<QByteArray> *reply = nullptr;
    QString torrentId = hash;
    if (kind == QLatin1String("file")) {
        const PendingTorrentFile file = m_pendingTorrentFiles.value(token);
        if (torrentId.isEmpty())
            torrentId = file.torrentId;
        if (file.bytes.isEmpty()) {
            Q_EMIT addFinished(false, tr("the .torrent was not read; pick it again"), QString());
            return;
        }
        reply = m_client->torrentsAddFile(file.fileName, file.bytes, options);
    } else {
        reply = m_client->torrentsAddUrls(QStringList{value}, options);
    }

    reply->then(this, [=, this](const QbtResult<QByteArray> &result) {
        if (result.isError()) {
            qWarning().noquote() << QStringLiteral("add failed: %1").arg(result.error().toString());
            Q_EMIT addFinished(false, result.error().message(), QString());
            return;
        }
        // qBittorrent 4.x answers "Fails." with 200 when it refuses the payload outright; 5.x
        // answers 409 Conflict instead, which arrives above as an error.
        const QString body = QString::fromUtf8(result.value()).trimmed();
        if (body.compare(QStringLiteral("Fails."), Qt::CaseInsensitive) == 0) {
            Q_EMIT addFinished(false, tr("the server refused this torrent"), QString());
            return;
        }
        if (!token.isEmpty())
            forgetTorrentFile(token);
        watchForAddedHash(torrentId);
        if (!withPriorities) {
            Q_EMIT addFinished(true, QString(), torrentId);
            return;
        }
        // 5.x answers with the ids it added; an older server says "Ok." and the metainfo's
        // own id stands in.
        const QJsonArray added = QJsonDocument::fromJson(result.value())
                                     .object()
                                     .value(QStringLiteral("added_torrent_ids"))
                                     .toArray();
        const QString id = added.isEmpty() ? torrentId : added.first().toString();
        applyAddFilePriorities(id, filePriorities, keepStopped);
    });
}

void App::applyAddFilePriorities(const QString &id,
                                 const QVariantList &priorities,
                                 bool keepStopped,
                                 int attempt)
{
    if (id.isEmpty()) {
        Q_EMIT addFinished(false, tr("added stopped, but the file choices were not applied"), id);
        return;
    }

    const QMap<int, QList<int>> groups = AddFilesModel::priorityGroups(priorities);
    if (groups.isEmpty()) {
        // Every choice is on the server: start it, unless the user asked for it stopped.
        if (keepStopped) {
            Q_EMIT addFinished(true, QString(), id);
            return;
        }
        m_client->torrentsStart(QStringList{id})
            ->then(this, [this, id](const QbtResult<QByteArray> &started) {
                if (started.isError()) {
                    Q_EMIT addFinished(false,
                                       tr("added with your file choices, but it did not start: %1")
                                           .arg(started.error().message()),
                                       id);
                    return;
                }
                Q_EMIT addFinished(true, QString(), id);
            });
        return;
    }

    // One `torrents/filePrio` per priority, one after the other: each success marks its ids
    // "normal" (done) and goes round again with what is left.
    const int priority = groups.firstKey();
    const QList<int> ids = groups.first();
    m_client->torrentsSetFilePrio(id, ids, priority)
        ->then(this, [=, this](const QbtResult<QByteArray> &result) {
            if (!result.isError()) {
                QVariantList rest = priorities;
                for (int at : ids)
                    rest[at] = int(FilePriority::Normal);
                applyAddFilePriorities(id, rest, keepStopped, attempt);
                return;
            }
            const ApiError::Kind kind = result.error().kind();
            // Not there yet (404) or its metadata not loaded yet (409): an older server adds
            // asynchronously. Try again after a short wait, for about five seconds.
            if ((kind == ApiError::Kind::NotFound || kind == ApiError::Kind::Conflict) &&
                attempt < 20) {
                QTimer::singleShot(250, this, [=, this]() {
                    applyAddFilePriorities(id, priorities, keepStopped, attempt + 1);
                });
                return;
            }
            qWarning().noquote()
                << QStringLiteral("add: file priorities failed: %1").arg(result.error().toString());
            Q_EMIT addFinished(false,
                               tr("added stopped, but the file choices were not applied: %1")
                                   .arg(result.error().message()),
                               id);
        });
}

void App::loadAddFiles(const QString &token)
{
    const auto it = m_pendingTorrentFiles.constFind(token);
    if (token.isEmpty() || it == m_pendingTorrentFiles.constEnd()) {
        m_addFiles->clear();
        return;
    }
    m_addFiles->load(it->files);
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
    if (!m_demoMode)
        return QString();
    const QString text = env("OMASEED_DEMO_ADD");
    // `torrent`: a real `.torrent` with a folder tree, so the file picker can be shown.
    if (text == QLatin1String("torrent"))
        return demoTorrentPath();
    return text;
}

QString App::demoTorrentPath() const
{
    if (!m_demoMode)
        return QString();
    // Written once, into a private directory of this process: a fixed name in the shared
    // /tmp could be a symlink another user planted, and the write would follow it.
    if (!m_demoTorrentPath.isEmpty() && QFile::exists(m_demoTorrentPath))
        return m_demoTorrentPath;
    if (!m_demoTempDir)
        m_demoTempDir = std::make_unique<QTemporaryDir>();
    if (!m_demoTempDir->isValid())
        return QString();
    const QString path = m_demoTempDir->filePath(QStringLiteral("Blender Open Movies.torrent"));
    QFile file(path);
    if (!file.open(QIODevice::NewOnly))
        return QString();
    file.write(DemoData::demoTorrentBytes());
    m_demoTorrentPath = path;
    return path;
}

void App::openUris(const QStringList &uris)
{
    if (uris.isEmpty())
        return;
    // A file manager hands over file:// URIs (%U), percent-encoded; the sheet's field shows
    // the path as a person reads it, and the parse takes either.
    QStringList readable;
    readable.reserve(uris.size());
    for (const QString &uri : uris) {
        const QUrl url(uri);
        readable.append(url.isLocalFile() ? url.toLocalFile() : uri);
    }
    Q_EMIT addUrisRequested(readable);
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
    writeStateSection(QStringLiteral("deleteFiles"), section);
    Q_EMIT deleteFilesDefaultChanged();
}

void App::loadDeleteFilesDefault()
{
    if (m_deleteFilesDefaultLoaded)
        return;
    m_deleteFilesDefaultLoaded = true;
    const QJsonObject section = stateSection(QStringLiteral("deleteFiles"));
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

void App::removeTracker(const QString &hash, const QString &url)
{
    if (hash.isEmpty() || url.isEmpty() || demoGuard(QStringLiteral("remove tracker"), 1) ||
        !m_hasServer)
        return;
    m_client->torrentsRemoveTrackers(hash, QStringList{url})
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("remove tracker"), result);
        });
}

void App::addTrackers(const QString &hash, const QStringList &urls)
{
    if (hash.isEmpty() || urls.isEmpty() || demoGuard(QStringLiteral("add trackers"), 1) ||
        !m_hasServer)
        return;
    m_client->torrentsAddTrackers(hash, urls)
        ->then(this, [this, hash](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("add tracker"), result);
            if (!result.isError() && m_details->hash() == hash)
                m_details->refreshNow();
        });
}

void App::addPeers(const QString &hash, const QStringList &peers)
{
    if (hash.isEmpty() || peers.isEmpty() || demoGuard(QStringLiteral("add peers"), 1) ||
        !m_hasServer)
        return;
    m_client->torrentsAddPeers(QStringList{hash}, peers)
        ->then(this, [this, hash](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("add peer"), result);
            if (!result.isError() && m_details->hash() == hash)
                m_details->refreshNow();
        });
}

void App::banPeer(const QString &address)
{
    if (address.isEmpty() || demoGuard(QStringLiteral("ban peer"), 1) || !m_hasServer)
        return;
    m_client->transferBanPeers(QStringList{address})
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("ban"), result);
            if (!result.isError())
                m_details->refreshNow();
        });
}

namespace {

QVariantMap peerInputMap(const core::PeerInputResult &result)
{
    return QVariantMap{{QStringLiteral("entries"), result.entries},
                       {QStringLiteral("invalid"), result.invalid}};
}

} // namespace

QVariantMap App::checkTrackerUrls(const QString &text) const
{
    return peerInputMap(core::PeerInput::trackerUrls(text));
}

QVariantMap App::checkPeers(const QString &text) const
{
    return peerInputMap(core::PeerInput::peers(text));
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

void App::requestGlobalLimits()
{
    if (m_demoMode) {
        // The demo server's own alt limits (DemoData::serverState), no normal ones.
        Q_EMIT globalLimitsLoaded(0, 0, 1024 * 1024, 200 * 1024);
        return;
    }
    if (!m_hasServer)
        return;
    m_client->appPreferences()->then(this, [this](const QbtResult<QJsonObject> &result) {
        if (result.isError()) {
            Q_EMIT actionFailed(QStringLiteral("limits"), result.error().toString());
            return;
        }
        // app/preferences carries these in bytes/s on 5.x (checked live on :8081).
        const QJsonObject &prefs = result.value();
        Q_EMIT globalLimitsLoaded(prefs.value(QStringLiteral("dl_limit")).toInteger(),
                                  prefs.value(QStringLiteral("up_limit")).toInteger(),
                                  prefs.value(QStringLiteral("alt_dl_limit")).toInteger(),
                                  prefs.value(QStringLiteral("alt_up_limit")).toInteger());
    });
}

void App::setGlobalLimits(qint64 dl, qint64 up, qint64 altDl, qint64 altUp)
{
    Q_EMIT globalLimitsRequested(dl, up, altDl, altUp);
    if (demoGuard(QStringLiteral("globalLimits"), 1) || !m_hasServer)
        return;

    QJsonObject changes;
    const auto put = [&changes](const char *key, qint64 value) {
        if (value >= 0)
            changes.insert(QLatin1String(key), value);
    };
    put("dl_limit", dl);
    put("up_limit", up);
    put("alt_dl_limit", altDl);
    put("alt_up_limit", altUp);
    if (changes.isEmpty())
        return;
    m_client->appSetPreferences(changes)->then(this, [this](const QbtResult<QByteArray> &result) {
        reportActionResult(QStringLiteral("limits"), result);
    });
}

QUrl App::urlAfterPreferences(const QUrl &url,
                              const QVariantMap &changes,
                              const QVariantMap &before)
{
    QUrl moved = url;
    const QString portKey = QStringLiteral("web_ui_port");
    const QString httpsKey = QStringLiteral("use_https");
    const bool wasHttps = url.scheme() == QLatin1String("https");
    if (changes.contains(portKey)) {
        const int oldPort = before.value(portKey).toInt();
        const int newPort = changes.value(portKey).toInt();
        if (oldPort > 0 && newPort > 0 && url.port(wasHttps ? 443 : 80) == oldPort)
            moved.setPort(newPort);
    }
    if (changes.contains(httpsKey)) {
        const int port = moved.port(wasHttps ? 443 : 80);
        const int webUiPort = changes.value(portKey, before.value(portKey)).toInt();
        // Only a URL that talks to qBittorrent itself changes scheme; a proxy in front
        // keeps its own.
        if (webUiPort <= 0 || port == webUiPort)
            moved.setScheme(changes.value(httpsKey).toBool() ? QStringLiteral("https")
                                                             : QStringLiteral("http"));
    }
    return moved;
}

void App::followServerPreferences(const QVariantMap &changes, const QVariantMap &before)
{
    if (m_demoMode || !m_hasServer || m_server.id == QLatin1String("environment"))
        return;
    QVariantMap fields = m_serverList->serverMap(m_server.id);
    if (fields.isEmpty())
        return;
    bool touched = false;
    QString secret;
    const QUrl url = urlAfterPreferences(m_server.url, changes, before);
    if (url != m_server.url) {
        fields.insert(QStringLiteral("url"), url.toString());
        touched = true;
    }
    if (m_server.authMode == AuthMode::Cookie) {
        const QString userKey = QStringLiteral("web_ui_username");
        const QString passKey = QStringLiteral("web_ui_password");
        if (changes.contains(userKey) && changes.value(userKey).toString() != m_server.username) {
            fields.insert(QStringLiteral("username"), changes.value(userKey).toString());
            touched = true;
        }
        if (changes.contains(passKey) && !changes.value(passKey).toString().isEmpty()) {
            secret = changes.value(passKey).toString();
            touched = true;
        }
    }
    if (!touched)
        return;
    qInfo().noquote() << QStringLiteral("server: following its own Web UI change");
    // qBittorrent moves its Web UI once it has answered; give it a moment before the next
    // request goes to the new place.
    QTimer::singleShot(750, this, [this, fields, secret]() { updateServer(fields, secret); });
}

void App::followRotatedApiKey(const QString &key)
{
    if (m_demoMode || !m_hasServer || m_server.authMode != AuthMode::ApiKey)
        return;
    const QVariantMap fields = m_serverList->serverMap(m_server.id);
    if (fields.isEmpty())
        return;
    m_client->setApiKey(key);
    updateServer(fields, key);
}

void App::setLimits(const QStringList &hashes,
                    qint64 dl,
                    qint64 ul,
                    double ratio,
                    qint64 seedingMinutes,
                    const QString &shareLimitAction,
                    qint64 inactiveSeedingMinutes)
{
    Q_EMIT limitsRequested(
        hashes, dl, ul, ratio, seedingMinutes, shareLimitAction, inactiveSeedingMinutes);
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
    // through, sentinels (-2 global, -1 none) included. The action (T5.5 Q14) travels only
    // when the server knows the field; a server still without a version answer gets the
    // field too, since 5.2.3 (the server this ships against) refuses the call without it.
    const QString apiVersion = m_engine->globalState()->apiVersion();
    std::optional<QbtClient::ShareLimitAction> action;
    if (apiVersion.isEmpty() || QbtClient::supportsShareLimitAction(apiVersion))
        action = QbtClient::shareLimitActionFromName(shareLimitAction);
    m_client->torrentsSetShareLimits(hashes, ratio, seedingMinutes, inactiveSeedingMinutes, action)
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

// --- docs/12 G4: per-torrent options ----------------------------------------------------------
//
// Each call sets every hash to one value, so a mixed selection comes out uniform. The two
// toggle endpoints flip whatever they are given, so only the hashes not already at `on` go
// to them. In demo mode the fictional rows flip locally, through the same merge a sync diff
// takes, so the pane and the menu show the change.

QStringList App::hashesNotAt(const QStringList &hashes,
                             bool on,
                             bool Torrent::*field,
                             PendingToggles &pending) const
{
    QStringList wrong;
    const TorrentModel *torrents = m_engine->torrents();
    for (const QString &hash : hashes) {
        if (torrents->indexOfHash(hash) < 0)
            continue;
        bool current = torrents->torrent(hash).*field;
        const auto it = pending.find(hash);
        if (it != pending.end()) {
            // The model caught up (or never will: the entry is old): trust it again.
            if (current == it->target || it->expiry.hasExpired())
                pending.erase(it);
            else
                current = it->target;
        }
        if (current != on)
            wrong.append(hash);
    }
    return wrong;
}

void App::sendToggle(const QStringList &flip,
                     bool on,
                     PendingToggles &pending,
                     QbtReply<QByteArray> *reply,
                     const QString &action)
{
    for (const QString &hash : flip)
        pending.insert(hash, PendingToggle{on, QDeadlineTimer(10000)});
    reply->then(this, [this, flip, &pending, action](const QbtResult<QByteArray> &result) {
        if (result.isError()) {
            for (const QString &hash : flip)
                pending.remove(hash);
        }
        reportActionResult(action, result);
    });
}

bool App::applyDemoOption(const QStringList &hashes, const QString &key, bool on)
{
    if (!m_demoMode)
        return false;
    QJsonObject torrents;
    for (const QString &hash : hashes)
        torrents.insert(hash, QJsonObject{{key, on}});
    m_engine->applyDemoData(QJsonObject{{QStringLiteral("torrents"), torrents}});
    qInfo().noquote() << QStringLiteral("demo: %1 %2 on %3").arg(key).arg(on).arg(hashes.size());
    return true;
}

void App::setSequentialDownload(const QStringList &hashes, bool on)
{
    const QStringList flip = hashesNotAt(hashes, on, &Torrent::seqDl, m_pendingSeqDl);
    if (flip.isEmpty() || applyDemoOption(flip, QStringLiteral("seq_dl"), on) || !m_hasServer)
        return;
    sendToggle(flip,
               on,
               m_pendingSeqDl,
               m_client->torrentsToggleSequentialDownload(flip),
               QStringLiteral("sequential"));
}

void App::setFirstLastPiecePrio(const QStringList &hashes, bool on)
{
    const QStringList flip =
        hashesNotAt(hashes, on, &Torrent::firstLastPiecePrio, m_pendingFirstLast);
    if (flip.isEmpty() || applyDemoOption(flip, QStringLiteral("f_l_piece_prio"), on) ||
        !m_hasServer)
        return;
    sendToggle(flip,
               on,
               m_pendingFirstLast,
               m_client->torrentsToggleFirstLastPiecePrio(flip),
               QStringLiteral("firstLast"));
}

void App::setSuperSeeding(const QStringList &hashes, bool on)
{
    if (hashes.isEmpty() || applyDemoOption(hashes, QStringLiteral("super_seeding"), on) ||
        !m_hasServer)
        return;
    m_client->torrentsSetSuperSeeding(hashes, on)
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("superSeeding"), result);
        });
}

void App::setAutoManagement(const QStringList &hashes, bool on)
{
    if (hashes.isEmpty() || applyDemoOption(hashes, QStringLiteral("auto_tmm"), on) || !m_hasServer)
        return;
    m_client->torrentsSetAutoManagement(hashes, on)
        ->then(this, [this](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("autoManagement"), result);
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
    // A torrent without metadata yet (a stopped magnet) has no content path; the save
    // folder is the next best thing to show.
    const Torrent torrent = m_engine->torrents()->torrent(hash);
    const QString remote = torrent.contentPath.isEmpty() ? torrent.savePath : torrent.contentPath;
    const QString local = mapContentPathToLocal(remote);
    if (local.isEmpty()) {
        qInfo().noquote() << QStringLiteral("open folder: no path known for %1").arg(hash);
        return;
    }
    qInfo().noquote() << QStringLiteral("open folder: %1").arg(local);
    m_xdg->showInFileManager(local);
}

QString App::pickDirectory(const QString &startDir) const
{
    return QFileDialog::getExistingDirectory(
        nullptr, tr("Choose a location"), startDir, QFileDialog::ShowDirsOnly);
}

// --- end T4.3 ------------------------------------------------------------------------------

// --- T4.5: settings ------------------------------------------------------------------------
// The settings modal's half of App (spec v2 §5 Fig. 16, §3 Fig. 9). See the block at the end
// of App.hpp for what it adds and why it sits here rather than woven through the file.

void App::attachTheme(omaseed::integration::ThemeReader *reader)
{
    m_themeReader = reader;
    loadSettings();
    applyAppearance();
}

QString App::addServer(const QVariantMap &fields, const QString &secret)
{
    // A demo keeps its servers in memory (applyDemoData); their secrets stay out of the
    // keychain for the same reason.
    const QString id = m_serverList->add(fields, m_demoMode ? QString() : secret);
    if (id.isEmpty())
        return {};
    qInfo().noquote() << QStringLiteral("server added: %1").arg(id);
    // The first server ever added is made default by the model, and there is no engine
    // running yet: move onto it so the table fills without a restart.
    if (!m_hasServer && m_servers->defaultId() == id)
        setDefaultServer(id);
    else
        Q_EMIT serverChanged();
    return id;
}

bool App::updateServer(const QVariantMap &fields, const QString &secret)
{
    if (!m_serverList->update(fields, m_demoMode ? QString() : secret))
        return false;
    const QString id = fields.value(QStringLiteral("id")).toString();
    // Editing the server the engine is on has to reach the live client, or the change only
    // shows after a restart.
    if (m_hasServer && !m_demoMode && m_server.id == id)
        setDefaultServer(id);
    else
        Q_EMIT serverChanged();
    return true;
}

bool App::removeServer(const QString &id)
{
    const bool wasLive = m_hasServer && m_server.id == id;
    if (!m_serverList->remove(id))
        return false;
    qInfo().noquote() << QStringLiteral("server removed: %1").arg(id);
    if (wasLive) {
        // Same teardown as a switch: nothing of the removed server may keep running, reach
        // the next one, or stay on screen when there is no next one.
        ++m_serverGeneration;
        m_engine->stop();
        m_engine->setClient(nullptr);
        m_engine->setClient(m_client);
        m_client->clearAuth();
        m_client->setBaseUrl(QUrl());
        m_prefs->reset();
        m_engine->globalState()->setServerName(QString());
        m_serverLost = false;
        m_hasServer = false;
        m_server = Server();
        updateTray();
        const std::optional<Server> next = m_servers->defaultServer();
        if (next)
            setDefaultServer(next->id);
    }
    Q_EMIT serverChanged();
    return true;
}

bool App::setDefaultServer(const QString &id)
{
    const std::optional<Server> server = m_servers->byId(id);
    if (!server || !m_servers->setDefault(id))
        return false;

    if (!m_demoMode) {
        // A different server is a different world: stop, throw away the rid, the models and
        // the probe (setClient(nullptr) does all three), then configure and start again.
        m_engine->stop();
        m_engine->setClient(nullptr);
        m_engine->setClient(m_client);
        m_serverLost = false;
        applyServer(*server);
    }
    qInfo().noquote() << QStringLiteral("server: %1 (%2) is now default")
                             .arg(server->url.toString(QUrl::RemoveUserInfo), server->name);
    Q_EMIT serverChanged();
    updateTray();
    return true;
}

void App::testServer(const QVariantMap &fields, const QString &secret)
{
    const Server server = ServerListModel::serverFromMap(fields);

    if (m_demoMode) {
        // There is no network behind a demo run, and the sheet still has to show something.
        QTimer::singleShot(0, this, [this, server]() {
            Q_EMIT serverTested(server.id,
                                true,
                                QStringLiteral("5.2.3"),
                                QStringLiteral("2.15.1"),
                                142,
                                3,
                                QString(),
                                QString());
        });
        return;
    }

    if (!server.url.isValid() || server.url.host().isEmpty()) {
        QTimer::singleShot(0, this, [this, server]() {
            Q_EMIT serverTested(
                server.id, false, QString(), QString(), -1, -1, tr("not a URL"), QString());
        });
        return;
    }

    if (!secret.isEmpty() || server.authMode == AuthMode::None || server.id.isEmpty()) {
        runServerTest(server, secret);
        return;
    }

    // An existing server tested without retyping its key: read the stored one first.
    const SecretStore::Kind kind = server.authMode == AuthMode::Cookie ? SecretStore::Kind::Password
                                                                       : SecretStore::Kind::ApiKey;
    auto *job = m_secrets->secret(server.id, kind, this);
    connect(job,
            &SecretStoreJob::finished,
            this,
            [this, server](bool ok, const QString &value, const QString &) {
                runServerTest(server, ok ? value : QString());
            });
}

void App::runServerTest(const Server &server, const QString &secret)
{
    auto *client = new QbtClient(this);
    client->setBaseUrl(server.url);
    client->setMaxRetries(0);
    client->setTimeout(std::chrono::seconds(8));
    switch (server.authMode) {
    case AuthMode::ApiKey:
        client->setApiKey(secret);
        break;
    case AuthMode::Cookie:
        client->setCredentials(server.username, secret);
        break;
    case AuthMode::None:
        break;
    }

    QElapsedTimer elapsed;
    elapsed.start();
    const QString id = server.id;

    const AuthMode mode = server.authMode;
    client->probe()->then(
        this, [this, client, elapsed, id, mode](const QbtResult<ServerInfo> &result) {
            const int latency = int(elapsed.elapsed());
            if (result.isError()) {
                const QString raw = result.error().toString();
                const QString refused = credentialRefusedText(result.error(), mode);
                const QString error = refused.isEmpty() ? raw : refused;
                const QString detail = refused.isEmpty() ? QString() : raw;
                client->deleteLater();
                m_serverList->setProbeResult(id, false, QString(), QString(), -1, latency, error);
                Q_EMIT serverTested(id, false, QString(), QString(), -1, latency, error, detail);
                return;
            }

            const ServerInfo info = result.value();
            // sync/maindata would do this too, but it also allocates a rid on the server; a
            // plain torrents/info is the cheaper way to count what is there.
            client->torrentsInfo()->then(
                this, [this, client, id, info, latency](const QbtResult<QJsonArray> &listResult) {
                    const int count = listResult.isError() ? -1 : int(listResult.value().size());
                    const QString version = info.version;
                    const QString apiVersion = info.apiVersion;
                    client->deleteLater();
                    m_serverList->setProbeResult(
                        id, true, version, apiVersion, count, latency, QString());
                    Q_EMIT serverTested(
                        id, true, version, apiVersion, count, latency, QString(), QString());
                });
        });
}

// --- the persisted flags ---------------------------------------------------------------

bool App::showServersGroup() const
{
    const_cast<App *>(this)->loadSettings();
    return m_showServersGroup;
}

void App::setShowServersGroup(bool show)
{
    loadSettings();
    if (m_showServersGroup == show)
        return;
    m_showServersGroup = show;
    saveSettings();
    Q_EMIT settingsChanged();
}

bool App::pathMapEnabled() const
{
    const_cast<App *>(this)->loadSettings();
    return m_pathMapEnabled;
}

void App::setPathMapEnabled(bool enabled)
{
    loadSettings();
    if (m_pathMapEnabled == enabled)
        return;
    m_pathMapEnabled = enabled;
    saveSettings();
    Q_EMIT settingsChanged();
}

bool App::followOmarchyTheme() const
{
    const_cast<App *>(this)->loadSettings();
    return m_followOmarchyTheme;
}

void App::setFollowOmarchyTheme(bool follow)
{
    loadSettings();
    if (m_followOmarchyTheme == follow)
        return;
    m_followOmarchyTheme = follow;
    saveSettings();
    applyAppearance();
    Q_EMIT settingsChanged();
}

int App::baseSizeOverride() const
{
    const_cast<App *>(this)->loadSettings();
    return m_baseSizeOverride;
}

void App::setBaseSizeOverride(int pixels)
{
    loadSettings();
    // Anything outside the range the shell itself offers is "follow the shell".
    const int clamped = (pixels >= 8 && pixels <= 32) ? pixels : 0;
    if (m_baseSizeOverride == clamped)
        return;
    m_baseSizeOverride = clamped;
    saveSettings();
    applyAppearance();
    Q_EMIT settingsChanged();
}

int App::windowAlphaOverride() const
{
    const_cast<App *>(this)->loadSettings();
    return m_windowAlphaOverride;
}

void App::setWindowAlphaOverride(int percent)
{
    loadSettings();
    const int clamped = (percent >= 50 && percent <= 100) ? percent : 0;
    if (m_windowAlphaOverride == clamped)
        return;
    m_windowAlphaOverride = clamped;
    saveSettings();
    Q_EMIT settingsChanged();
}

QString App::speedsAlignment() const
{
    const_cast<App *>(this)->loadSettings();
    return m_speedsAlignment;
}

void App::setSpeedsAlignment(const QString &side)
{
    loadSettings();
    const QString wanted =
        side == QLatin1String("right") ? QStringLiteral("right") : QStringLiteral("left");
    if (m_speedsAlignment == wanted)
        return;
    m_speedsAlignment = wanted;
    saveSettings();
    Q_EMIT settingsChanged();
}

namespace {

/// `value` when it is one of `allowed`, otherwise the first of them (the default).
QString oneOf(const QString &value, const QStringList &allowed)
{
    return allowed.contains(value) ? value : allowed.constFirst();
}

const QStringList kRailScrollHints{
    QStringLiteral("chevrons"), QStringLiteral("fade"), QStringLiteral("scrollbar")};
const QStringList kDetailsPlacements{
    QStringLiteral("auto"), QStringLiteral("right"), QStringLiteral("bottom")};
const QStringList kTrackerHostStyles{
    QStringLiteral("prefix"), QStringLiteral("left"), QStringLiteral("right")};
const QStringList kTrayIconModes{
    QStringLiteral("auto"), QStringLiteral("always"), QStringLiteral("never")};

int tintStep(int percent)
{
    return percent == 4 || percent == 8 ? percent : 0;
}

} // namespace

QString App::railScrollHint() const
{
    const_cast<App *>(this)->loadSettings();
    return m_railScrollHint;
}

void App::setRailScrollHint(const QString &hint)
{
    loadSettings();
    const QString wanted = oneOf(hint, kRailScrollHints);
    if (m_railScrollHint == wanted)
        return;
    m_railScrollHint = wanted;
    saveSettings();
    Q_EMIT settingsChanged();
}

QString App::detailsPlacement() const
{
    const_cast<App *>(this)->loadSettings();
    return m_detailsPlacement;
}

void App::setDetailsPlacement(const QString &placement)
{
    loadSettings();
    const QString wanted = oneOf(placement, kDetailsPlacements);
    if (m_detailsPlacement == wanted)
        return;
    m_detailsPlacement = wanted;
    saveSettings();
    Q_EMIT settingsChanged();
}

int App::surfaceTint() const
{
    const_cast<App *>(this)->loadSettings();
    return m_surfaceTint;
}

void App::setSurfaceTint(int percent)
{
    loadSettings();
    const int wanted = tintStep(percent);
    if (m_surfaceTint == wanted)
        return;
    m_surfaceTint = wanted;
    saveSettings();
    Q_EMIT settingsChanged();
}

QString App::trackerHostStyle() const
{
    const_cast<App *>(this)->loadSettings();
    return m_trackerHostStyle;
}

void App::setTrackerHostStyle(const QString &style)
{
    loadSettings();
    const QString wanted = oneOf(style, kTrackerHostStyles);
    if (m_trackerHostStyle == wanted)
        return;
    m_trackerHostStyle = wanted;
    saveSettings();
    Q_EMIT settingsChanged();
}

bool App::railHidden() const
{
    const_cast<App *>(this)->loadSettings();
    return m_railHidden;
}

void App::setRailHidden(bool hidden)
{
    loadSettings();
    if (m_railHidden == hidden)
        return;
    m_railHidden = hidden;
    saveSettings();
    Q_EMIT settingsChanged();
}

QString App::trayIconMode() const
{
    const_cast<App *>(this)->loadSettings();
    return m_trayIconMode;
}

void App::setTrayIconMode(const QString &mode)
{
    loadSettings();
    const QString next = oneOf(mode, kTrayIconModes);
    if (m_trayIconMode == next)
        return;
    m_trayIconMode = next;
    saveSettings();
    // `auto` behaves as `always` until the companion widget's heartbeat exists (plan T6.3).
    if (m_tray)
        m_tray->setIconVisible(next != QStringLiteral("never"));
    Q_EMIT settingsChanged();
}

QString App::versionText() const
{
    return QStringLiteral("omaseed %1").arg(omaseed::core::version());
}

QString App::qtVersionText() const
{
    return QString::fromLatin1(qVersion());
}

QString App::repoUrl() const
{
    return QStringLiteral("https://github.com/diogochaves/omaseed");
}

QString App::pluginAddCommand() const
{
    return QStringLiteral("omarchy plugin add https://github.com/diogochaves/omaseed-bar");
}

QJsonObject App::stateSection(const QString &key) const
{
    if (m_demoMode && m_demoState.contains(key))
        return m_demoState.value(key).toObject();
    return StateFile::readSection(key);
}

void App::writeStateSection(const QString &key, const QJsonObject &value)
{
    if (m_demoMode) {
        m_demoState.insert(key, value);
        return;
    }
    StateFile::writeSection(key, value);
}

void App::loadSettings()
{
    if (m_settingsLoaded)
        return;
    m_settingsLoaded = true;

    const QJsonObject general = stateSection(QStringLiteral("general"));
    m_showServersGroup = general.value(QStringLiteral("showServersGroup")).toBool(true);
    m_pathMapEnabled = general.value(QStringLiteral("pathMapEnabled")).toBool(true);

    const QJsonObject appearance = stateSection(QStringLiteral("appearance"));
    m_followOmarchyTheme = appearance.value(QStringLiteral("followOmarchyTheme")).toBool(true);
    m_baseSizeOverride = appearance.value(QStringLiteral("baseSizeOverride")).toInt(0);
    m_windowAlphaOverride = appearance.value(QStringLiteral("windowAlphaOverride")).toInt(0);
    m_trayIconMode =
        oneOf(appearance.value(QStringLiteral("trayIconMode")).toString(), kTrayIconModes);
    m_speedsAlignment =
        appearance.value(QStringLiteral("speedsAlignment")).toString(QStringLiteral("left")) ==
                QLatin1String("right")
            ? QStringLiteral("right")
            : QStringLiteral("left");
    m_railScrollHint =
        oneOf(appearance.value(QStringLiteral("railScrollHint")).toString(), kRailScrollHints);
    m_surfaceTint = tintStep(appearance.value(QStringLiteral("surfaceTint")).toInt(0));
    m_trackerHostStyle =
        oneOf(appearance.value(QStringLiteral("trackerHostStyle")).toString(), kTrackerHostStyles);
    m_railHidden = appearance.value(QStringLiteral("railHidden")).toBool(false);
    m_detailsPlacement =
        oneOf(appearance.value(QStringLiteral("detailsPlacement")).toString(), kDetailsPlacements);
}

void App::saveSettings()
{
    QJsonObject general;
    general[QStringLiteral("showServersGroup")] = m_showServersGroup;
    general[QStringLiteral("pathMapEnabled")] = m_pathMapEnabled;
    writeStateSection(QStringLiteral("general"), general);

    QJsonObject appearance;
    appearance[QStringLiteral("followOmarchyTheme")] = m_followOmarchyTheme;
    appearance[QStringLiteral("baseSizeOverride")] = m_baseSizeOverride;
    appearance[QStringLiteral("windowAlphaOverride")] = m_windowAlphaOverride;
    appearance[QStringLiteral("trayIconMode")] = m_trayIconMode;
    appearance[QStringLiteral("speedsAlignment")] = m_speedsAlignment;
    appearance[QStringLiteral("railScrollHint")] = m_railScrollHint;
    appearance[QStringLiteral("surfaceTint")] = m_surfaceTint;
    appearance[QStringLiteral("trackerHostStyle")] = m_trackerHostStyle;
    appearance[QStringLiteral("railHidden")] = m_railHidden;
    appearance[QStringLiteral("detailsPlacement")] = m_detailsPlacement;
    writeStateSection(QStringLiteral("appearance"), appearance);
}

void App::applyAppearance()
{
    if (!m_themeReader)
        return;
    m_themeReader->setFollowOmarchy(m_followOmarchyTheme);
    m_themeReader->setBaseSizeOverride(m_baseSizeOverride);
}

// --- end T4.5 --------------------------------------------------------------------------------

// --- T4.6: first run ---------------------------------------------------------------------

void App::detectServers()
{
    if (m_serverDetector) {
        m_serverDetector->disconnect(this);
        m_serverDetector->deleteLater();
    }

    m_serverDetector = new ServerDetector(this);
    connect(m_serverDetector,
            &ServerDetector::finished,
            this,
            [this](const QList<DetectedServer> &candidates) {
                QVariantList list;
                for (const DetectedServer &candidate : candidates) {
                    QVariantMap map;
                    map.insert(QStringLiteral("kind"), candidate.kind);
                    map.insert(QStringLiteral("url"), candidate.url);
                    map.insert(QStringLiteral("label"), candidate.label);
                    map.insert(QStringLiteral("detail"), candidate.detail);
                    list.append(map);
                }
                Q_EMIT serversDetected(list);
            });
    m_serverDetector->start();
}

QString App::installCommandsText() const
{
    // `qbittorrent-nox` ships two units (`pacman -Ql qbittorrent-nox | grep service`):
    // `qbittorrent-nox.service` (system-wide, one shared instance) and the per-user template
    // `qbittorrent-nox@.service`. The template is what first run wants: it runs as the
    // person who installed it, with their own config and download directories.
    return QStringLiteral("sudo pacman -S qbittorrent-nox\n"
                          "systemctl --user enable --now qbittorrent-nox@$USER");
}

void App::runInstallWithPkexec()
{
    // Never `sudo` from inside the app (CLAUDE.md): `pkexec` is the polkit-mediated
    // equivalent, and it is the user who authorizes it, not this process.
    runInstall(QStringLiteral("pkexec"),
               {QStringLiteral("pacman"),
                QStringLiteral("-S"),
                QStringLiteral("--noconfirm"),
                QStringLiteral("qbittorrent-nox")});
}

void App::runInstall(const QString &program, const QStringList &arguments)
{
    if (m_installProcess) {
        Q_EMIT installFinished(false, tr("an install is already running"));
        return;
    }

    auto *process = new QProcess(this);
    m_installProcess = process;
    // A crash or a kill emits errorOccurred(Crashed) and *then* finished(CrashExit); a read,
    // write or timeout error is followed by finished too. Only FailedToStart comes alone. So
    // finished answers every run that started, errorOccurred only the one that did not, and
    // each handler works on the process it was connected to, not the member.
    connect(process,
            &QProcess::finished,
            this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
                const bool ok = status == QProcess::NormalExit && exitCode == 0;
                QString message;
                if (!ok) {
                    message = QString::fromUtf8(process->readAllStandardError()).trimmed();
                    if (message.isEmpty())
                        message = status == QProcess::CrashExit
                                      ? process->errorString()
                                      : tr("the installer exited with code %1").arg(exitCode);
                }
                process->deleteLater();
                if (m_installProcess == process)
                    m_installProcess = nullptr;
                Q_EMIT installFinished(ok, message);
            });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        const QString message = process->errorString();
        process->deleteLater();
        if (m_installProcess == process)
            m_installProcess = nullptr;
        Q_EMIT installFinished(false, message);
    });

    process->start(program, arguments);
}

// --- end T4.6 ------------------------------------------------------------------------------

// --- T5.5 Q14: share-limit action ------------------------------------------------------------

bool App::supportsShareLimitAction() const
{
    return QbtClient::supportsShareLimitAction(m_engine->globalState()->apiVersion());
}

// --- end T5.5 Q14 ----------------------------------------------------------------------------

// --- N1: the Files tab's actions -------------------------------------------------------------

QString App::torrentFileLocalPath(const QString &hash, const QString &relativePath) const
{
    if (hash.isEmpty() || relativePath.isEmpty() || m_engine->torrents()->indexOfHash(hash) < 0)
        return {};
    const Torrent &torrent = m_engine->torrents()->torrent(hash);
    return mapContentPathToLocal(
        TorrentFilesModel::serverPathOf(torrent.contentPath, torrent.savePath, relativePath));
}

void App::openTorrentFile(const QString &hash, const QString &relativePath)
{
    const QString local = torrentFileLocalPath(hash, relativePath);
    if (local.isEmpty() || demoGuard(QStringLiteral("open file"), 1))
        return;
    // A remote server's path with no mapping for it, or a file not written yet: say so
    // rather than let the desktop fail silently.
    if (!QFileInfo::exists(local)) {
        Q_EMIT actionFailed(QStringLiteral("open"), tr("%1 is not on this computer").arg(local));
        return;
    }
    qInfo().noquote() << QStringLiteral("open file: %1").arg(local);
    QDesktopServices::openUrl(QUrl::fromLocalFile(local));
}

void App::revealTorrentFile(const QString &hash, const QString &relativePath)
{
    const QString local = torrentFileLocalPath(hash, relativePath);
    if (local.isEmpty() || demoGuard(QStringLiteral("open folder"), 1) || m_xdg.isNull())
        return;
    if (!QFileInfo::exists(local)) {
        Q_EMIT actionFailed(QStringLiteral("open folder"),
                            tr("%1 is not on this computer").arg(local));
        return;
    }
    qInfo().noquote() << QStringLiteral("open folder: %1").arg(local);
    m_xdg->showInFileManager(local);
}

void App::renameTorrentFile(const QString &hash,
                            const QString &oldPath,
                            const QString &newName,
                            bool folder)
{
    const QString name = newName.trimmed();
    if (hash.isEmpty() || oldPath.isEmpty() || name.isEmpty())
        return;
    if (name.contains(QLatin1Char('/')) || name == QLatin1String(".") ||
        name == QLatin1String("..")) {
        Q_EMIT actionFailed(QStringLiteral("rename"), tr("a name cannot hold a /"));
        return;
    }
    const qsizetype slash = oldPath.lastIndexOf(QLatin1Char('/'));
    const QString newPath = slash < 0 ? name : oldPath.left(slash + 1) + name;
    if (newPath == oldPath)
        return;

    Q_EMIT torrentFileRenameRequested(hash, oldPath, newPath, folder);
    if (m_demoMode) {
        // No server: the demo's own copy of the list moves, so the rename can be seen.
        if (m_details->hash() == hash || m_details->hash().isEmpty())
            m_details->files()->renameLocal(oldPath, newPath);
        return;
    }
    if (!m_hasServer)
        return;

    auto *reply = folder ? m_client->torrentsRenameFolder(hash, oldPath, newPath)
                         : m_client->torrentsRenameFile(hash, oldPath, newPath);
    reply->then(this, [this](const QbtResult<QByteArray> &result) {
        if (result.isError()) {
            Q_EMIT actionFailed(QStringLiteral("rename"), result.error().toString());
            return;
        }
        m_details->refreshNow();
    });
}

void App::setFilePriorityGroups(const QString &hash, const QVariantList &groups)
{
    for (const QVariant &group : groups) {
        const QVariantMap map = group.toMap();
        setFilePriority(hash,
                        map.value(QStringLiteral("fileIndexes")).toList(),
                        map.value(QStringLiteral("priority")).toInt());
    }
}

// --- end N1 ----------------------------------------------------------------------------------

} // namespace omaseed::app
