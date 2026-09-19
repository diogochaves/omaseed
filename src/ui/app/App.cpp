#include "ui/app/App.hpp"

#include "core/Format.hpp"
#include "ui/app/DemoData.hpp"

#include <QDebug>
#include <QProcessEnvironment>
#include <QQmlEngine>
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
    row.insert(QStringLiteral("sizeText"), Format::size(torrent.size));
    row.insert(QStringLiteral("progressText"), Format::percent(torrent.progress));
    row.insert(QStringLiteral("ratioText"), Format::ratio(torrent.ratio));
    row.insert(QStringLiteral("etaLongText"), Format::eta(torrent.eta));
    row.insert(QStringLiteral("stateText"), torrent.stateText());
    row.insert(QStringLiteral("stateRole"), torrent.stateRole());
    row.insert(QStringLiteral("hasTrackerError"), torrent.hasTrackerError);
    return row;
}

QString App::demoTab() const
{
    return env("OMASEED_DEMO_TAB");
}

// --- end T3.4 ------------------------------------------------------------------------------

} // namespace omaseed::app
