#include "ui/app/App.hpp"

#include "core/Format.hpp"
#include "core/PathMap.hpp"
#include "core/StateFile.hpp"
#include "core/Version.hpp"
#include "integration/Notifier.hpp"
#include "integration/ThemeReader.hpp"
#include "integration/Xdg.hpp"
#include "ui/app/DemoData.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

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
    const QString id = m_serverList->add(fields, secret);
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
    if (!m_serverList->update(fields, secret))
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
        m_engine->stop();
        m_hasServer = false;
        m_server = Server();
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
                                QString());
        });
        return;
    }

    if (!server.url.isValid() || server.url.host().isEmpty()) {
        QTimer::singleShot(0, this, [this, server]() {
            Q_EMIT serverTested(server.id, false, QString(), QString(), -1, -1, tr("not a URL"));
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

    client->probe()->then(this, [this, client, elapsed, id](const QbtResult<ServerInfo> &result) {
        const int latency = int(elapsed.elapsed());
        if (result.isError()) {
            const QString error = result.error().toString();
            client->deleteLater();
            m_serverList->setProbeResult(id, false, -1, latency, error);
            Q_EMIT serverTested(id, false, QString(), QString(), -1, latency, error);
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
                m_serverList->setProbeResult(id, true, count, latency, QString());
                Q_EMIT serverTested(id, true, version, apiVersion, count, latency, QString());
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

QString App::trayIconMode() const
{
    const_cast<App *>(this)->loadSettings();
    return m_trayIconMode;
}

void App::setTrayIconMode(const QString &mode)
{
    loadSettings();
    static const QStringList known{
        QStringLiteral("auto"), QStringLiteral("always"), QStringLiteral("never")};
    const QString next = known.contains(mode) ? mode : QStringLiteral("auto");
    if (m_trayIconMode == next)
        return;
    m_trayIconMode = next;
    saveSettings();
    // `auto` behaves as `always` until the companion widget's heartbeat exists (plan T6.3).
    if (m_tray && m_tray->systemTrayIcon() != nullptr)
        m_tray->systemTrayIcon()->setVisible(next != QStringLiteral("never"));
    Q_EMIT settingsChanged();
}

QString App::versionText() const
{
    return omaseed::core::versionLine();
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

void App::loadSettings()
{
    if (m_settingsLoaded)
        return;
    m_settingsLoaded = true;

    const QJsonObject general = StateFile::readSection(QStringLiteral("general"));
    m_showServersGroup = general.value(QStringLiteral("showServersGroup")).toBool(true);
    m_pathMapEnabled = general.value(QStringLiteral("pathMapEnabled")).toBool(true);

    const QJsonObject appearance = StateFile::readSection(QStringLiteral("appearance"));
    m_followOmarchyTheme = appearance.value(QStringLiteral("followOmarchyTheme")).toBool(true);
    m_baseSizeOverride = appearance.value(QStringLiteral("baseSizeOverride")).toInt(0);
    m_windowAlphaOverride = appearance.value(QStringLiteral("windowAlphaOverride")).toInt(0);
    m_trayIconMode =
        appearance.value(QStringLiteral("trayIconMode")).toString(QStringLiteral("auto"));
}

void App::saveSettings()
{
    QJsonObject general;
    general[QStringLiteral("showServersGroup")] = m_showServersGroup;
    general[QStringLiteral("pathMapEnabled")] = m_pathMapEnabled;
    StateFile::writeSection(QStringLiteral("general"), general);

    QJsonObject appearance;
    appearance[QStringLiteral("followOmarchyTheme")] = m_followOmarchyTheme;
    appearance[QStringLiteral("baseSizeOverride")] = m_baseSizeOverride;
    appearance[QStringLiteral("windowAlphaOverride")] = m_windowAlphaOverride;
    appearance[QStringLiteral("trayIconMode")] = m_trayIconMode;
    StateFile::writeSection(QStringLiteral("appearance"), appearance);
}

void App::applyAppearance()
{
    if (!m_themeReader)
        return;
    m_themeReader->setFollowOmarchy(m_followOmarchyTheme);
    m_themeReader->setBaseSizeOverride(m_baseSizeOverride);
}

// --- end T4.5 --------------------------------------------------------------------------------

} // namespace omaseed::app
