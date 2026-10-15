#include "ui/app/App.hpp"

#include "core/CertificatePin.hpp"
#include "core/Format.hpp"
#include "core/LibraryMatch.hpp"
#include "core/MagnetLink.hpp"
#include "core/MetadataLookup.hpp"
#include "core/PathMap.hpp"
#include "core/PeerInput.hpp"
#include "core/PrivateTrackers.hpp"
#include "core/ProblemLog.hpp"
#include "core/QbtSettingsImport.hpp"
#include "core/RenamePlan.hpp"
#include "core/ServerLog.hpp"
#include "core/StateFile.hpp"
#include "core/TorrentFile.hpp"
#include "core/TorrentFolderScan.hpp"
#include "core/TrackerMerge.hpp"
#include "core/Version.hpp"
#include "core/WeeklyRecap.hpp"
#include "integration/Autostart.hpp"
#include "integration/Notifier.hpp"
#include "integration/StatusWriter.hpp"
#include "integration/ThemeReader.hpp"
#include "integration/WidgetHeartbeat.hpp"
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
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcessEnvironment>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <utility>

using namespace omaseed::core;
using omaseed::integration::Tray;

namespace omaseed::app {

namespace {

App *s_defaultInstance = nullptr;

/// docs/30 D7: how many commands the command palette lists first with nothing typed.
constexpr qsizetype kRecentCommands = 5;

/// The portal places a chooser as a dialog of the window that asked only when it is told
/// which window that is; without it Hyprland tiles the chooser beside us (Strata's does).
/// Qt's static pickers take a QWidget parent, which a Quick window is not, so the dialog's
/// own window is made transient for ours instead, and Qt hands that to the portal.
void makeTransientFor(QFileDialog &dialog, QWindow *window)
{
    if (!window)
        return;
    dialog.winId();
    dialog.windowHandle()->setTransientParent(window);
}

QStringList runPicker(QFileDialog &dialog, QWindow *window)
{
    makeTransientFor(dialog, window);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles() : QStringList();
}

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
    setUpMetadataLookup();
    m_altSpeed = new AltSpeed(this);
    m_altSpeed->setGlobalState(m_engine->globalState());
    m_altSpeed->setPreferences(m_prefs);
    m_filtered->setSourceModel(m_engine->torrents());
    // A tracker row lists what the rail counts for it: every torrent on that host.
    m_filtered->setTrackerModel(m_engine->trackers());
    // The add sheet's "in library" follows the library, not the moment of the last scan: a
    // torrent added from the bar widget while the sheet is up is marked at the next sync.
    // `updated` comes after the model's own bookkeeping (rowsRemoved comes before it).
    connect(m_engine->torrents(), &TorrentModel::updated, this, &App::refreshDownloadsInLibrary);

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
    // docs/31 D13: the other set and the schedule live only in the preferences, so they are
    // read again each time the alternative limits flip (a few times a day): a set or a schedule
    // changed in qBittorrent's Web UI shows the next time it switches.
    connect(m_engine->globalState(), &GlobalState::useAltSpeedLimitsChanged, this, [this]() {
        if (!m_demoMode && m_hasServer && m_engine->globalState()->connected())
            m_prefs->reload();
    });
    connect(m_prefs, &ServerPreferences::posted, this, &App::followServerPreferences);
    connect(m_prefs, &ServerPreferences::apiKeyRotated, this, &App::followRotatedApiKey);
    connect(
        m_engine->globalState(), &GlobalState::useAltSpeedLimitsChanged, this, &App::updateTray);
    connect(m_altSpeed, &AltSpeed::changed, this, &App::updateTray);
    connect(m_engine->globalState(), &GlobalState::dlInfoSpeedChanged, this, &App::updateTray);
    connect(m_engine->globalState(), &GlobalState::upInfoSpeedChanged, this, &App::updateTray);
    connect(m_engine->globalState(), &GlobalState::freeSpaceOnDiskChanged, this, &App::updateTray);
    // docs/25 §4: the add sheet's free line follows the server's figure, its default save
    // path (a preference) and the path map.
    connect(
        m_engine->globalState(), &GlobalState::freeSpaceOnDiskChanged, this, &App::updateFreeSpace);
    connect(m_prefs, &ServerPreferences::valuesChanged, this, &App::updateFreeSpace);
    connect(this, &App::serverChanged, this, &App::updateFreeSpace);
    connect(this, &App::settingsChanged, this, &App::updateFreeSpace);
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
        if (error.kind() == ApiError::Kind::Certificate && !serverId().isEmpty())
            Q_EMIT certificateRefused(serverId());
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

    // docs/32 (#55): what qBittorrent's log says beyond errors. The footer hears about a
    // failure omaseed asked for; the HTTP sources tab gets the lines about the torrent it
    // shows; the export note follows the newest failed copy; dismissals go to state.json.
    ProblemLog *problems = m_engine->problems();
    connect(problems, &ProblemLog::watchedFailure, this, &App::loggedFailure);
    const auto pushWebSeedLines = [this]() {
        m_details->webSeeds()->setLogLines(m_engine->problems()->webSeedLines(m_details->hash()));
    };
    connect(m_details, &TorrentDetails::hashChanged, this, pushWebSeedLines);
    connect(problems, &ProblemLog::logLinesChanged, this, pushWebSeedLines);
    connect(problems, &ProblemLog::logLinesChanged, this, &App::exportFailureChanged);
    connect(problems, &ProblemLog::dismissedChanged, this, &App::saveDismissedFailures);

    // docs/32 §6 (#56): the log face lists the server's log through its own filter, whose
    // levels each server keeps.
    m_logLines = new LogFilterModel(this);
    m_logLines->setLog(m_engine->serverLog());
    connect(m_logLines, &LogFilterModel::levelsChanged, this, &App::saveLogLevels);

    // #24: every poll goes into the ledger; the console reads it and the library.
    m_console->setSources(m_engine->torrents(),
                          m_filtered,
                          m_engine->trackers(),
                          m_engine->globalState(),
                          m_ledger.get());
    connect(m_engine, &SyncEngine::synced, this, &App::feedLedger);

    // #31: the automatic copies. The hourly check starts with start(), so demos and tests
    // never write one.
    m_autoBackup->setSources(backupSources());
    m_autoBackup->setMode(
        stateSection(QStringLiteral("backup")).value(QStringLiteral("mode")).toString());
    m_autoBackup->setBeforeCopy([this] {
        if (m_ledger->isOpen())
            m_ledger->flush(QDateTime::currentSecsSinceEpoch());
    });
    connect(m_autoBackup, &AutoBackup::copied, this, &App::backupChanged);
    connect(m_autoBackup, &AutoBackup::failed, this, [this](const QString &message) {
        qWarning().noquote() << QStringLiteral("automatic backup: %1").arg(message);
        Q_EMIT actionFailed(tr("Automatic backup"), message);
    });
    m_autoBackupTimer->setInterval(std::chrono::hours(1));
    connect(m_autoBackupTimer, &QTimer::timeout, this, &App::runAutoBackupIfDue);
    // #32: the export summary counts the server's categories and tags.
    connect(m_engine->categories(), &CategoryModel::countChanged, this, &App::qbtBackupChanged);
    connect(m_engine->tags(), &TagModel::countChanged, this, &App::qbtBackupChanged);
    connect(this, &App::serverChanged, this, [this] {
        m_qbtRssFeeds = m_qbtRssRules = -1;
        Q_EMIT qbtBackupChanged();
    });
}

App::~App()
{
    if (s_defaultInstance == this)
        s_defaultInstance = nullptr;
    m_engine->stop();
    m_ledger->close();
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

    // #9: a real run keeps the Files tab's per-torrent filter and hide-skipped in state.json
    // (demos and QML tests never call start(), so they never write it).
    m_details->setRememberViewState(true);

    // #31: a copy when one is due, a minute in (the ledger has its first polls by then), then
    // a check every hour.
    m_autoBackupTimer->start();
    QTimer::singleShot(std::chrono::minutes(1), this, &App::runAutoBackupIfDue);

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
    m_client->setPinnedCertificate(server.certificateSha256);
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
    openLedger();
    loadDismissedFailures();
    loadLogLevels();
    m_engine->start();
}

// --- docs/32 (#55): failures only qBittorrent's log knows ---------------------------------------

void App::loadDismissedFailures()
{
    const QJsonArray keys = stateSection(QStringLiteral("failures"))
                                .value(m_server.id)
                                .toObject()
                                .value(QStringLiteral("dismissed"))
                                .toArray();
    QStringList list;
    for (const QJsonValue &key : keys)
        list << key.toString();
    m_engine->problems()->setDismissed(list);
}

void App::saveDismissedFailures()
{
    if (!m_hasServer || m_server.id.isEmpty())
        return;
    QJsonObject section = stateSection(QStringLiteral("failures"));
    const QStringList keys = m_engine->problems()->dismissed();
    if (keys.isEmpty())
        section.remove(m_server.id);
    else
        section.insert(
            m_server.id,
            QJsonObject{{QStringLiteral("dismissed"), QJsonArray::fromStringList(keys)}});
    writeStateSection(QStringLiteral("failures"), section);
}

// --- docs/32 §6 (#56): the log face ---------------------------------------------------------

ServerLog *App::serverLog() const
{
    return m_engine->serverLog();
}

void App::loadLogLevels()
{
    const QJsonValue levels = stateSection(QStringLiteral("log"))
                                  .value(m_server.id)
                                  .toObject()
                                  .value(QStringLiteral("levels"));
    m_loadingLogLevels = true;
    m_logLines->setLevels(levels.isDouble() ? levels.toInt() : 15);
    m_loadingLogLevels = false;
}

void App::saveLogLevels()
{
    if (m_loadingLogLevels || m_demoMode || !m_hasServer || m_server.id.isEmpty())
        return;
    QJsonObject section = stateSection(QStringLiteral("log"));
    if (m_logLines->levels() == 15)
        section.remove(m_server.id);
    else
        section.insert(m_server.id, QJsonObject{{QStringLiteral("levels"), m_logLines->levels()}});
    writeStateSection(QStringLiteral("log"), section);
}

void App::dismissFailure(const QString &hash)
{
    m_engine->problems()->dismiss(hash);
}

QVariantMap App::exportFailure() const
{
    QVariantMap failure = m_engine->problems()->exportFailure();
    if (failure.isEmpty())
        return failure;
    failure.insert(QStringLiteral("loggedAtText"),
                   Format::relativeTime(failure.value(QStringLiteral("at")).toDateTime()));
    failure.remove(QStringLiteral("at"));
    return failure;
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
    // The demo's log goes with its library, not with every fictional diff a test applies.
    if (maindata.value(QStringLiteral("full_update")).toBool()) {
        // A new demo is a new server's log: nothing dismissed yet (docs/32 D4).
        m_engine->problems()->setDismissed({});
        m_engine->problems()->applyEntries(DemoData::problemLog(maindata));
        // docs/32 §6 (#56): the log face's lines, as if read from the top.
        m_engine->serverLog()->applyEntries(DemoData::serverLog(maindata));
    }

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

void App::setDialogParent(QWindow *window)
{
    m_dialogParent = window;
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
    applyTrayIconVisibility();

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
    // docs/31 T1 (D14): the checkbox and the icon's mark follow the word.
    m_tray->setAltKind(m_altSpeed->kind());
    m_tray->setAltSpeedLabel(m_altSpeed->trayLabel());
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

void App::setWebSeedsTabActive(bool active)
{
    m_details->setWebSeedsActive(active);
}

void App::setFilePriority(const QString &hash, const QVariantList &fileIndexes, int priority)
{
    if (hash.isEmpty() || fileIndexes.isEmpty())
        return;
    // docs/27 D3: what a skipped file was, so ticking it again brings that back.
    if (priority == int(FilePriority::Skip) && m_details->hash() == hash) {
        const QHash<int, int> current = m_details->files()->priorityByFileIndex();
        QHash<int, int> &before = m_beforeSkip[hash];
        for (const QVariant &value : fileIndexes) {
            const int was = current.value(value.toInt(), int(FilePriority::Skip));
            if (was != int(FilePriority::Skip))
                before.insert(value.toInt(), was);
        }
    }
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
    row.insert(QStringLiteral("isPrivate"), torrent.isPrivate);
    // docs/25 §2: before its metadata arrives a torrent reads as public whatever it is, so
    // Fig. 10 offers no merge until it is known.
    row.insert(QStringLiteral("hasMetadata"),
               torrent.totalSize >= 0 && torrent.state != TorrentState::MetaDownload &&
                   torrent.state != TorrentState::ForcedMetaDownload);
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
    // docs/17 M10.1: the folded pane's strip reads the table's own words for these.
    row.insert(QStringLiteral("etaText"),
               torrent.hasEta() ? Format::etaShort(torrent.eta) : QStringLiteral("—"));
    row.insert(QStringLiteral("dlspeedText"), Format::speed(torrent.dlspeed));
    row.insert(QStringLiteral("upspeedText"), Format::speed(torrent.upspeed));
    row.insert(QStringLiteral("stateText"), torrent.stateText());
    row.insert(QStringLiteral("stateRole"), torrent.stateRole());
    row.insert(QStringLiteral("hasTrackerError"), torrent.hasTrackerError);
    // docs/28: why it is in error, from the server's log (ProblemLog); empty otherwise.
    row.insert(QStringLiteral("problem"), m_engine->torrents()->problem(hash));
    // docs/32: a move, rename or part file that failed while it kept working; empty otherwise.
    row.insert(QStringLiteral("failure"), m_engine->torrents()->failure(hash));
    // T4.3: exactly the check `stopStart()` makes, so the actions menu's "start"/"stop" row
    // and the delete sheet agree with what a bulk stop/start would actually do.
    row.insert(QStringLiteral("stopped"), startsOnToggle(torrent.state));
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

    // docs/28 N1: once per entry into error, after the log has been read, with its reason.
    connect(m_engine->problems(),
            &ProblemLog::torrentErrored,
            this,
            [this](const QString &hash, const QString &name, const TorrentProblem &problem) {
                if (m_notifier.isNull() || !m_notifyErroredEnabled)
                    return;
                m_notifier->notifyErrored(hash, name, problem.headline, problem.detail);
            });

    connect(m_engine, &SyncEngine::diskAlmostFull, this, [this](qint64 freeBytes) {
        if (m_notifier.isNull() || !m_notifyDiskFullEnabled)
            return;
        m_notifier->notifyDiskAlmostFull(m_server.id, Format::size(freeBytes));
    });

    connect(m_engine, &SyncEngine::serverLost, this, [this](const ApiError &error) {
        const int retrySeconds = m_engine->globalState()->retryInSeconds();
        // A hidden start (at login) gives its server a while to come up before saying so.
        if (m_quietStart.isActive()) {
            m_heldLoss = std::make_pair(error, retrySeconds);
            return;
        }
        if (m_notifier.isNull() || !m_notifyServerLostEnabled)
            return;
        m_notifier->notifyServerLost(m_server.id,
                                     m_server.name,
                                     serverHostPortText(),
                                     serverLostReasonText(error),
                                     retrySeconds);
    });

    connect(m_engine, &SyncEngine::serverBack, this, [this]() {
        // Back before the quiet start ran out: nobody was told it was gone.
        if (m_heldLoss) {
            m_heldLoss.reset();
            return;
        }
        if (m_notifier.isNull() || !m_notifyServerLostEnabled)
            return;
        m_notifier->notifyServerBack(m_server.id, m_server.name);
    });

    // The first good sync ends the quiet start: from then on a loss is news.
    connect(m_engine, &SyncEngine::synced, this, [this]() { m_quietStart.stop(); });
    // Still away when the quiet start runs out: say so now, and "back" when it is.
    m_quietStart.setSingleShot(true);
    connect(&m_quietStart, &QTimer::timeout, this, [this]() {
        if (!m_heldLoss)
            return;
        const auto [error, retrySeconds] = *m_heldLoss;
        m_heldLoss.reset();
        if (m_notifier.isNull() || !m_notifyServerLostEnabled)
            return;
        m_notifier->notifyServerLost(m_server.id,
                                     m_server.name,
                                     serverHostPortText(),
                                     serverLostReasonText(error),
                                     retrySeconds);
    });

    // #24 T8: every sync asks whether the weekly recap is due; most answers are one date
    // compare.
    connect(m_engine, &SyncEngine::synced, this, &App::checkWeeklyRecap);

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
    map.insert(QStringLiteral("errored"), m_notifyErroredEnabled);
    map.insert(QStringLiteral("weekly"), m_notifyWeeklyEnabled);
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
    else if (key == QStringLiteral("errored"))
        flag = &m_notifyErroredEnabled;
    else if (key == QStringLiteral("weekly"))
        flag = &m_notifyWeeklyEnabled;
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
    m_notifyErroredEnabled = section.value(QStringLiteral("errored")).toBool(true);
    m_notifyWeeklyEnabled = section.value(QStringLiteral("weekly")).toBool(false);
    m_weeklyRecapSent =
        QDate::fromString(section.value(QStringLiteral("weeklyRecapSent")).toString(), Qt::ISODate);
    m_lowDiskThresholdGiB = qMax(1, section.value(QStringLiteral("lowDiskThresholdGiB")).toInt(2));
    applyLowDiskThreshold();
}

void App::saveNotificationToggles()
{
    QJsonObject section;
    section.insert(QStringLiteral("completed"), m_notifyCompletedEnabled);
    section.insert(QStringLiteral("serverLost"), m_notifyServerLostEnabled);
    section.insert(QStringLiteral("diskFull"), m_notifyDiskFullEnabled);
    section.insert(QStringLiteral("errored"), m_notifyErroredEnabled);
    section.insert(QStringLiteral("weekly"), m_notifyWeeklyEnabled);
    if (m_weeklyRecapSent.isValid())
        section.insert(QStringLiteral("weeklyRecapSent"), m_weeklyRecapSent.toString(Qt::ISODate));
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
    return serverPathMap().toLocal(remotePath);
}

PathMap App::serverPathMap() const
{
    PathMap map;
    for (const PathMapping &mapping : m_server.pathMap)
        map.addMapping(mapping.remote, mapping.local);
    return map;
}

void App::updateFreeSpace()
{
    const QString savePath = m_prefs->value(QStringLiteral("save_path")).toString();
    m_freeSpace.setServer(
        savePath, m_engine->globalState()->freeSpaceOnDisk(), serverPathMap(), pathMapEnabled());
    ++m_freeSpaceRevision;
    Q_EMIT freeSpaceChanged();
}

QVariantMap App::freeSpaceFor(const QString &savePath) const
{
    const FreeSpaceProbe::Answer answer = m_freeSpace.answer(savePath);
    QString source = QStringLiteral("unknown");
    if (answer.source == FreeSpaceProbe::Source::Default)
        source = QStringLiteral("default");
    else if (answer.source == FreeSpaceProbe::Source::Local)
        source = QStringLiteral("local");
    return {
        {QStringLiteral("source"), source},
        {QStringLiteral("bytesFree"), answer.bytesFree},
        {QStringLiteral("bytesTotal"), answer.bytesTotal},
        {QStringLiteral("where"), answer.where},
        {QStringLiteral("defaultFree"), m_engine->globalState()->freeSpaceOnDisk()},
    };
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
    if (isCertificateError(lastError))
        return certificateRefusedText(m_server.certificateSha256);
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
    if (eventClass == QStringLiteral("errored")) {
        if (actionKey == QStringLiteral("show"))
            Q_EMIT showTorrentRequested(subjectId);
        return;
    }

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
        return;
    }

    // #24 T8: the weekly recap opens the console on the week it told.
    if (eventClass == QStringLiteral("recap") && actionKey == QStringLiteral("console"))
        Q_EMIT showConsoleRequested(QStringLiteral("7d"));
}

void App::checkWeeklyRecap()
{
    if (m_notifier.isNull() || m_demoMode)
        return;
    loadNotificationToggles();
    if (!m_notifyWeeklyEnabled)
        return;
    const QDateTime now = m_recapClock ? m_recapClock() : QDateTime::currentDateTime();
    // Dev only: one recap as soon as there is something to tell, whatever the day.
    const bool forced = !m_recapForced && !env("OMASEED_RECAP_DUE").isEmpty();
    if (!forced && !WeeklyRecap::due(now, m_weeklyRecapSent))
        return;
    if (m_ledger->start() == 0)
        return; // nothing polled yet in this run or any before it

    const TorrentModel *torrents = m_engine->torrents();
    QList<const Torrent *> library;
    library.reserve(torrents->count());
    for (int i = 0; i < torrents->count(); ++i)
        library.append(&torrents->torrentAt(i));
    const GlobalState *state = m_engine->globalState();
    // What is still buffered counts too: the ledger's queries add it.
    const WeeklyRecap recap = WeeklyRecap::compute(*m_ledger,
                                                   library,
                                                   now.date(),
                                                   state->allTimeUpload(),
                                                   state->allTimeDownload(),
                                                   m_console->rareSeeds());
    if (forced) {
        m_recapForced = true;
    } else {
        // Asked once a Sunday: a week that gave nothing is not asked again until the next.
        m_weeklyRecapSent = now.date();
        saveNotificationToggles();
    }
    if (recap.worthSending())
        m_notifier->notifyWeeklyRecap(m_server.id, recap.summary(), recap.body());
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

QStringList App::recentCommands() const
{
    const QJsonArray ids =
        stateSection(QStringLiteral("commands")).value(QStringLiteral("recent")).toArray();
    QStringList recent;
    for (const QJsonValue &id : ids) {
        if (id.isString() && !id.toString().isEmpty() && recent.size() < kRecentCommands)
            recent.append(id.toString());
    }
    return recent;
}

void App::noteCommandRun(const QString &id)
{
    if (id.isEmpty())
        return;
    QStringList recent = recentCommands();
    recent.removeAll(id);
    recent.prepend(id);
    while (recent.size() > kRecentCommands)
        recent.removeLast();
    QJsonObject section = stateSection(QStringLiteral("commands"));
    section.insert(QStringLiteral("recent"), QJsonArray::fromStringList(recent));
    writeStateSection(QStringLiteral("commands"), section);
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
        if (link.exactLength > 0) {
            map[QStringLiteral("sizeText")] = Format::size(link.exactLength);
            map[QStringLiteral("totalSize")] = link.exactLength;
        }
        map[QStringLiteral("trackers")] = link.trackers;
        map[QStringLiteral("isPrivate")] = false;
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
    map[QStringLiteral("totalSize")] = parsed.totalSize;
    map[QStringLiteral("trackers")] = parsed.trackers();
    map[QStringLiteral("isPrivate")] = parsed.isPrivate;
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
                    self->rememberTorrentFile(token,
                                              PendingTorrentFile{fileName,
                                                                 bytes,
                                                                 parsed.files,
                                                                 parsed.torrentId(),
                                                                 parsed.name,
                                                                 parsed.multiFile,
                                                                 parsed.pieceLength});
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

QStringList App::pickTorrentFiles()
{
    QFileDialog dialog(
        nullptr, tr("Choose torrents"), QString(), tr("Torrent files (*.torrent);;All files (*)"));
    dialog.setFileMode(QFileDialog::ExistingFiles);
    return runPicker(dialog, m_dialogParent);
}

QStringList App::splitAddables(const QString &text) const
{
    // A magnet glued to the one before it ("…&dn=a" + "magnet:?xt=…") starts a new one.
    static const QRegularExpression glued(QStringLiteral("(?<=\\S)(?=magnet:\\?)"),
                                          QRegularExpression::CaseInsensitiveOption);
    QString spaced = text;
    spaced.replace(glued, QStringLiteral(" "));
    QStringList sources;
    for (const integration::Addable &addable : integration::Xdg::parseAddableList(spaced))
        sources.append(addable.value);
    return sources;
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
    if (request.contains(QStringLiteral("items"))) {
        addBatch(request);
        return;
    }
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

    // A parsed .torrent, or a magnet or link whose metadata the lookup brought (docs/29 F2):
    // the sheet then has a file list, its choices and its renames.
    const bool isFile = kind == QLatin1String("file");
    const bool hasList = !token.isEmpty();

    // #67: the sheet refused such a name when it was typed, but Save to may have changed
    // since. qBittorrent would replace the file without a word (#64).
    if (hasList) {
        const QString taken = addRenameTaken(request);
        if (!taken.isEmpty()) {
            Q_EMIT addFinished(false, tr("%1 already exists on disk").arg(taken), QString());
            return;
        }
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

    const QVariantList filePriorities =
        hasList ? request.value(QStringLiteral("filePriorities")).toList() : QVariantList();
    const bool customised = !AddFilesModel::priorityGroups(filePriorities).isEmpty();
    // M18: the Web API's add takes no per-file names; they go on afterwards, stopped.
    const QVariantList renames =
        hasList ? request.value(QStringLiteral("fileRenames")).toList() : QVariantList();
    const QVariantMap renameAnchor = request.value(QStringLiteral("renameAnchor")).toMap();
    const bool withRenames = !renames.isEmpty();
    const bool keepStopped = request.value(QStringLiteral("stopped")).toBool();
    // docs/11 D1: a file upload takes no file priorities ("Cannot specify filePriorities
    // when uploading torrent files", 5.2), so the choices go on afterwards, while the
    // torrent is still stopped and has not asked for a single piece. A magnet or link takes
    // them with the add itself, in the session that fetched its metadata (docs/29 §2).
    const bool withPriorities = isFile && customised;
    if (!isFile && customised) {
        for (const QVariant &priority : filePriorities)
            options.filePriorities.append(priority.toInt());
    }
    if (withPriorities || withRenames)
        options.stopped = true;
    // D5 a: "add stopped" before its list is in stops it as soon as the list arrives, so the
    // files can be chosen in the Files tab before anything downloads. A plain stopped magnet
    // never connects, so it would never get there (T3).
    if (!isFile && !hasList && keepStopped &&
        request.value(QStringLiteral("untilMetadata")).toBool()) {
        options.stopped = false;
        options.stopCondition = QStringLiteral("MetadataReceived");
    }

    QbtReply<QByteArray> *reply = nullptr;
    QString torrentId = hash;
    if (isFile) {
        const PendingTorrentFile file = m_pendingTorrentFiles.value(token);
        if (torrentId.isEmpty())
            torrentId = file.torrentId;
        if (file.bytes.isEmpty()) {
            Q_EMIT addFinished(false, tr("the .torrent was not read; pick it again"), QString());
            return;
        }
        reply = m_client->torrentsAddFile(file.fileName, file.bytes, options);
    } else {
        if (torrentId.isEmpty() && hasList)
            torrentId = m_pendingTorrentFiles.value(token).torrentId;
        reply = m_client->torrentsAddUrls(QStringList{value}, options);
    }

    reply->then(this, [=, this](const QbtResult<QByteArray> &result) {
        if (result.isError()) {
            // #44: 400 to the priorities is a session that is not the one that fetched the
            // metadata any more.
            if (!isFile && customised && result.error().httpStatus() == 400) {
                qInfo().noquote() << QStringLiteral("add: the file choices were refused (%1); "
                                                    "asking for the metadata again")
                                         .arg(result.error().toString());
                retryUrlAdd(request, options, torrentId);
                return;
            }
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
        if (!isFile) {
            afterUrlAdd(result.value(), request, torrentId, {});
            return;
        }
        if (!token.isEmpty())
            forgetTorrentFile(token);
        watchForAddedHash(torrentId);
        if (!withPriorities && !withRenames) {
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
        if (withRenames)
            applyAddRenames(id, renames, renameAnchor, filePriorities, keepStopped);
        else
            applyAddFilePriorities(id, filePriorities, keepStopped);
    });
}

void App::addBatch(const QVariantMap &request)
{
    const QVariantList items = request.value(QStringLiteral("items")).toList();
    QList<QbtClient::AddFile> files;
    QStringList urls;
    QStringList tokens;
    QStringList ids;
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        const QString kind = item.value(QStringLiteral("kind")).toString();
        QString id = TorrentFile::torrentIdFor(item.value(QStringLiteral("infoHashV1")).toString(),
                                               item.value(QStringLiteral("infoHashV2")).toString());
        if (kind == QLatin1String("file")) {
            const QString token = item.value(QStringLiteral("token")).toString();
            const PendingTorrentFile file = m_pendingTorrentFiles.value(token);
            if (file.bytes.isEmpty()) {
                Q_EMIT addFinished(false,
                                   tr("%1 was not read; pick it again")
                                       .arg(item.value(QStringLiteral("name")).toString()),
                                   QString());
                return;
            }
            files.append(QbtClient::AddFile{file.fileName, file.bytes});
            tokens.append(token);
            if (id.isEmpty())
                id = file.torrentId;
        } else if (kind == QLatin1String("magnet") || kind == QLatin1String("url")) {
            urls.append(item.value(QStringLiteral("value")).toString());
        } else {
            continue;
        }
        if (!id.isEmpty())
            ids.append(id);
    }
    if (files.isEmpty() && urls.isEmpty()) {
        Q_EMIT addFinished(false, tr("nothing to add"), QString());
        return;
    }
    const QString first = ids.value(0);
    if (m_demoMode) {
        qInfo().noquote() << QStringLiteral("demo: would add %1 torrents at once")
                                 .arg(files.size() + urls.size());
        Q_EMIT addFinished(true, QString(), first);
        return;
    }
    if (!m_hasServer) {
        Q_EMIT addFinished(false, tr("no server is configured"), QString());
        return;
    }

    QbtClient::AddOptions options = addOptionsFrom(request);
    options.rename.reset(); // one name cannot fit several torrents
    m_client->torrentsAddMany(files, urls, options)
        ->then(this, [=, this](const QbtResult<QByteArray> &result) {
            if (result.isError()) {
                qWarning().noquote()
                    << QStringLiteral("batch add failed: %1").arg(result.error().toString());
                Q_EMIT addFinished(false, result.error().message(), QString());
                return;
            }
            const QString body = QString::fromUtf8(result.value()).trimmed();
            if (body.compare(QStringLiteral("Fails."), Qt::CaseInsensitive) == 0) {
                Q_EMIT addFinished(false, tr("the server refused these torrents"), QString());
                return;
            }
            for (const QString &token : tokens)
                forgetTorrentFile(token);
            if (!first.isEmpty())
                watchForAddedHash(first);
            Q_EMIT addFinished(true, QString(), first);
        });
}

void App::applyAddRenames(const QString &id,
                          const QVariantList &renames,
                          const QVariantMap &anchor,
                          const QVariantList &priorities,
                          bool keepStopped,
                          int attempt)
{
    if (id.isEmpty()) {
        Q_EMIT addFinished(false, tr("added stopped, but the new names were not applied"), id);
        return;
    }
    const auto retry = [=, this]() {
        QTimer::singleShot(250, this, [=, this]() {
            applyAddRenames(id, renames, anchor, priorities, keepStopped, attempt + 1);
        });
    };
    m_client->torrentsFiles(id)->then(this, [=, this](const QbtResult<QJsonArray> &result) {
        if (result.isError()) {
            const ApiError::Kind kind = result.error().kind();
            if ((kind == ApiError::Kind::NotFound || kind == ApiError::Kind::Conflict) &&
                attempt < 20) {
                retry();
                return;
            }
            Q_EMIT addFinished(false,
                               tr("added stopped, but the new names were not applied: %1")
                                   .arg(result.error().message()),
                               id);
            return;
        }
        // No files yet: the server has not loaded the metadata. Same wait as a 409.
        if (result.value().isEmpty() && attempt < 20) {
            retry();
            return;
        }
        // The server's paths are the metainfo's with the torrent's root folder in front (or
        // none, with "no subfolder"): the anchor file tells which.
        const int index = anchor.value(QStringLiteral("index")).toInt();
        const QString path = anchor.value(QStringLiteral("path")).toString();
        QString served;
        for (const QJsonValue &value : result.value()) {
            const QJsonObject file = value.toObject();
            if (file.value(QStringLiteral("index")).toInt(-1) == index) {
                served = file.value(QStringLiteral("name")).toString();
                break;
            }
        }
        const QString root = served.left(served.size() - path.size());
        if (path.isEmpty() || !served.endsWith(path) ||
            !(root.isEmpty() || root.endsWith(QLatin1Char('/')))) {
            qWarning().noquote()
                << QStringLiteral("add: renames: file %1 is %2 on the server").arg(path, served);
            Q_EMIT addFinished(
                false,
                tr("added stopped, but the new names were not applied: the server's file list "
                   "did not match the .torrent"),
                id);
            return;
        }
        sendAddRename(id, renames, 0, root, priorities, keepStopped);
    });
}

void App::sendAddRename(const QString &id,
                        const QVariantList &renames,
                        int at,
                        const QString &root,
                        const QVariantList &priorities,
                        bool keepStopped)
{
    if (at >= renames.size()) {
        applyAddFilePriorities(id, priorities, keepStopped);
        return;
    }
    const QVariantMap rename = renames.at(at).toMap();
    const QString from = root + rename.value(QStringLiteral("from")).toString();
    const QString to = root + rename.value(QStringLiteral("to")).toString();
    QbtReply<QByteArray> *reply =
        rename.value(QStringLiteral("kind")).toString() == QLatin1String("folder")
            ? m_client->torrentsRenameFolder(id, from, to)
            : m_client->torrentsRenameFile(id, from, to);
    reply->then(this, [=, this](const QbtResult<QByteArray> &result) {
        if (result.isError()) {
            qWarning().noquote() << QStringLiteral("add: rename %1 -> %2 failed: %3")
                                        .arg(from, to, result.error().toString());
            Q_EMIT addFinished(false,
                               tr("added stopped, but the new names were not applied: %1")
                                   .arg(result.error().message()),
                               id);
            return;
        }
        sendAddRename(id, renames, at + 1, root, priorities, keepStopped);
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

std::pair<RenameDisk, QString> App::addRenameDisk(const QVariantMap &where) const
{
    const auto it =
        m_pendingTorrentFiles.constFind(where.value(QStringLiteral("token")).toString());
    if (it == m_pendingTorrentFiles.constEnd() || it->files.isEmpty())
        return {};

    // The torrent's folder in the save path, as qBittorrent lays the files out: a multi-file
    // torrent's own folder, one named after a single file (less its extension) with
    // "Subfolder", none with "No subfolder".
    const QString layout = where.value(QStringLiteral("contentLayout")).toString();
    QString root;
    if (layout != QLatin1String("NoSubfolder")) {
        if (it->multiFile) {
            root = it->name;
        } else if (layout == QLatin1String("Subfolder")) {
            const qsizetype dot = it->name.lastIndexOf(QLatin1Char('.'));
            root = dot > 0 ? it->name.left(dot) : it->name;
        }
    }
    QStringList own;
    own.reserve(it->files.size());
    for (const TorrentFileEntry &file : it->files)
        own.append(root.isEmpty() ? file.path : root + QLatin1Char('/') + file.path);

    if (m_demoMode)
        return {m_demoDiskFolder.isEmpty() ? RenameDisk() : RenameDisk(m_demoDiskFolder, own),
                root};
    // Where the add saves to: the category's folder under automatic management, else Save
    // to, else the server's default.
    QString server = where.value(QStringLiteral("autoTMM")).toBool()
                         ? autoManagedFolder(where.value(QStringLiteral("category")).toString())
                         : where.value(QStringLiteral("savePath")).toString();
    if (server.isEmpty())
        server = autoManagedFolder(QString());
    const QString local = server.isEmpty() ? QString() : m_freeSpace.localPathFor(server);
    return {local.isEmpty() ? RenameDisk() : RenameDisk(local, own), root};
}

void App::setAddFilesWhere(const QVariantMap &where)
{
    const auto [disk, root] = addRenameDisk(where);
    m_addFiles->setRenameDisk(disk, root);
}

QString App::addRenameTaken(const QVariantMap &request) const
{
    const QVariantList renames = request.value(QStringLiteral("fileRenames")).toList();
    if (renames.isEmpty())
        return {};
    const auto [disk, root] = addRenameDisk(request);
    if (!disk.sees())
        return {};
    for (const QVariant &value : renames) {
        const QVariantMap rename = value.toMap();
        const QString to = rename.value(QStringLiteral("to")).toString();
        const QString onDisk = root.isEmpty() ? to : root + QLatin1Char('/') + to;
        const bool folder =
            rename.value(QStringLiteral("kind")).toString() == QLatin1String("folder");
        if (folder ? disk.folderTaken(onDisk) : disk.fileTaken(onDisk))
            return to.section(QLatin1Char('/'), -1);
    }
    return {};
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

// --- #44: a magnet's files before the add (docs/29) ---------------------------------------

namespace {

/// The demo .torrent's metadata as torrents/fetchMetadata answers it: paths under the
/// torrent's folder, as qBittorrent sends them.
QJsonObject demoMetadataBody(const TorrentFile &torrent)
{
    QJsonArray files;
    for (const TorrentFileEntry &file : torrent.files) {
        files.append(QJsonObject{
            {QStringLiteral("length"), file.size},
            {QStringLiteral("path"),
             torrent.multiFile ? torrent.name + QLatin1Char('/') + file.path : file.path},
        });
    }
    return QJsonObject{
        {QStringLiteral("hash"), torrent.torrentId()},
        {QStringLiteral("infohash_v1"), torrent.infoHashV1},
        {QStringLiteral("infohash_v2"), torrent.infoHashV2},
        {QStringLiteral("info"),
         QJsonObject{{QStringLiteral("name"), torrent.name},
                     {QStringLiteral("length"), torrent.totalSize},
                     {QStringLiteral("piece_length"), torrent.pieceLength},
                     {QStringLiteral("private"), torrent.isPrivate},
                     {QStringLiteral("files"), files}}},
        {QStringLiteral("trackers"), QJsonArray()},
    };
}

} // namespace

void App::setUpMetadataLookup()
{
    m_addLookup = new MetadataLookup(this);
    m_addLookup->setFetch([this](const QString &source) -> QbtReply<QbtClient::FetchedMetadata> * {
        if (!m_demoMode)
            return m_client->torrentsFetchMetadata(source);
        // Demo: the demo .torrent's magnet answers with its files, anything else looks for
        // ever, as a dead magnet does. Nothing leaves the process.
        auto *reply = new QbtReply<QbtClient::FetchedMetadata>(this);
        QbtClient::FetchedMetadata answer;
        const TorrentFile demo = TorrentFile::parse(DemoData::demoTorrentBytes());
        if (demo.isValid() && source.contains(demo.infoHashV1, Qt::CaseInsensitive)) {
            answer.ready = true;
            answer.body = demoMetadataBody(demo);
        }
        QTimer::singleShot(0, reply, [reply, answer]() { reply->complete(answer); });
        return reply;
    });
    // magnet-walk's test-only clock: F3 and F4 in seconds instead of a minute.
    const double scale = env("OMASEED_LOOKUP_CLOCK_SCALE").toDouble();
    if (scale > 0)
        m_addLookup->setClockScale(scale);
    connect(m_addLookup, &MetadataLookup::fetched, this, &App::takeFetchedMetadata);
    connect(
        m_engine->globalState(), &GlobalState::connectedChanged, this, &App::updateLookupServer);
    connect(
        m_engine->globalState(), &GlobalState::apiVersionChanged, this, &App::updateLookupServer);
    connect(m_engine, &SyncEngine::firstSyncFinished, this, &App::updateLookupServer);
}

void App::updateLookupServer()
{
    if (m_demoMode) {
        m_addLookup->setServer(true, QStringLiteral("2.15.1"));
        return;
    }
    // F7: the version (D11), a signed-in session and the library first: a magnet handed over
    // before the first sync waits for all three, and is checked against the library's private
    // trackers (D7) only once that is known.
    const GlobalState *state = m_engine->globalState();
    const bool ready =
        m_hasServer && state->connected() && !state->apiVersion().isEmpty() && m_engine->rid() > 0;
    if (ready && m_addLookup->phase() == MetadataLookup::Phase::Waiting &&
        m_addLookup->kind() == QLatin1String("magnet")) {
        const QString host =
            privateTrackerOf(m_lookupAddable.value(QStringLiteral("trackers")).toStringList());
        if (!host.isEmpty()) {
            m_addLookup->startPrivate(m_addLookup->key(), m_addLookup->source(), host);
            return;
        }
    }
    m_addLookup->setServer(ready, state->apiVersion());
}

QString App::privateTrackerOf(const QStringList &trackers) const
{
    if (trackers.isEmpty())
        return {};
    PrivateTrackers library;
    TorrentModel *model = m_engine->torrents();
    for (int row = 0; row < model->count(); ++row) {
        const QString hash = model->data(model->index(row), TorrentModel::HashRole).toString();
        const Torrent &torrent = model->torrent(hash);
        if (torrent.isPrivate)
            library.addTorrent(true, {torrent.tracker});
    }
    // Every announce URL of every private torrent, not only the one it uses right now.
    const TrackerModel *groups = m_engine->trackers();
    for (int row = 0; row < groups->rowCount(); ++row) {
        const QModelIndex index = groups->index(row);
        const QStringList hashes = groups->data(index, TrackerModel::HashesRole).toStringList();
        const bool anyPrivate =
            std::any_of(hashes.cbegin(), hashes.cend(), [model](const QString &hash) {
                return model->indexOfHash(hash) >= 0 && model->torrent(hash).isPrivate;
            });
        if (anyPrivate)
            library.addTorrent(true, groups->data(index, TrackerModel::UrlsRole).toStringList());
    }
    return library.match(trackers);
}

void App::lookUpMetadata(const QVariantMap &addable)
{
    const QString kind = addable.value(QStringLiteral("kind")).toString();
    const bool lookable = addable.value(QStringLiteral("valid")).toBool() &&
                          !addable.value(QStringLiteral("pending")).toBool() &&
                          (kind == QLatin1String("magnet") || kind == QLatin1String("url"));
    if (!lookable) {
        stopMetadataLookup();
        return;
    }
    // Its list is in already: nothing to ask.
    if (!addable.value(QStringLiteral("token")).toString().isEmpty())
        return;
    const QString value = addable.value(QStringLiteral("value")).toString();
    const QString key =
        kind == QLatin1String("magnet")
            ? TorrentFile::torrentIdFor(addable.value(QStringLiteral("infoHashV1")).toString(),
                                        addable.value(QStringLiteral("infoHashV2")).toString())
            : value;
    m_lookupAddable = addable;
    // The same magnet, its list already in (the field was read again, say, after a keystroke
    // past the hash): the same files under the same token, so the choices made stay.
    if (key == m_addLookup->key() && m_addLookup->phase() == MetadataLookup::Phase::Done &&
        !m_lookupToken.isEmpty()) {
        QMetaObject::invokeMethod(this, &App::takeFetchedMetadata, Qt::QueuedConnection);
        return;
    }
    if (key != m_addLookup->key())
        m_lookupToken.clear();
    if (kind == QLatin1String("magnet")) {
        const QString host =
            privateTrackerOf(addable.value(QStringLiteral("trackers")).toStringList());
        if (!host.isEmpty()) {
            m_addLookup->startPrivate(key, value, host);
            return;
        }
    }
    m_addLookup->start(key, value, kind);
    updateLookupServer();
}

void App::stopMetadataLookup()
{
    m_lookupToken.clear();
    m_lookupAddable.clear();
    m_addLookup->stop();
}

void App::setLookupTestClock(double ms)
{
    m_lookupTestNow = ms;
    if (ms < 0) {
        m_addLookup->setManualClock({});
        return;
    }
    m_addLookup->setManualClock([this]() { return qint64(m_lookupTestNow); });
    m_addLookup->wake();
}

QString App::demoMagnet() const
{
    if (!m_demoMode)
        return {};
    const TorrentFile demo = TorrentFile::parse(DemoData::demoTorrentBytes());
    if (!demo.isValid())
        return {};
    // No trackers: the demo library's private ones would turn it into F5.
    return QStringLiteral("magnet:?xt=urn:btih:%1&dn=%2")
        .arg(demo.infoHashV1, QString::fromLatin1(QUrl::toPercentEncoding(demo.name)));
}

void App::takeFetchedMetadata()
{
    if (m_addLookup->phase() != MetadataLookup::Phase::Done || m_lookupAddable.isEmpty())
        return;
    const MetadataLookup::Metadata &metadata = m_addLookup->metadata();
    if (m_lookupToken.isEmpty() || !m_pendingTorrentFiles.contains(m_lookupToken)) {
        m_lookupToken =
            QStringLiteral("meta-%1").arg(++m_nextAddableToken, 4, 10, QLatin1Char('0'));
        PendingTorrentFile file;
        file.fileName = metadata.name;
        file.files = metadata.files;
        file.torrentId = metadata.torrentId;
        file.name = metadata.name;
        file.multiFile = metadata.multiFile;
        file.pieceLength = metadata.pieceLength;
        rememberTorrentFile(m_lookupToken, file);
    }
    QVariantMap map = m_lookupAddable;
    map[QStringLiteral("token")] = m_lookupToken;
    map[QStringLiteral("valid")] = true;
    map[QStringLiteral("pending")] = false;
    map[QStringLiteral("name")] = metadata.name;
    map[QStringLiteral("fileCount")] = int(metadata.files.size());
    map[QStringLiteral("totalSize")] = metadata.totalSize;
    map[QStringLiteral("sizeText")] = Format::size(metadata.totalSize);
    map[QStringLiteral("isPrivate")] = metadata.isPrivate;
    if (!metadata.infoHashV1.isEmpty())
        map[QStringLiteral("infoHashV1")] = metadata.infoHashV1;
    if (!metadata.infoHashV2.isEmpty())
        map[QStringLiteral("infoHashV2")] = metadata.infoHashV2;
    // A link names no tracker until its .torrent is read; a magnet keeps its own `tr=`.
    if (map.value(QStringLiteral("trackers")).toStringList().isEmpty())
        map[QStringLiteral("trackers")] = metadata.trackers;
    Q_EMIT metadataFetched(map);
}

void App::afterUrlAdd(const QByteArray &body,
                      const QVariantMap &request,
                      const QString &torrentId,
                      const QVariantList &laterPriorities)
{
    const QString token = request.value(QStringLiteral("token")).toString();
    if (!token.isEmpty())
        forgetTorrentFile(token);
    if (token == m_lookupToken)
        m_lookupToken.clear();
    watchForAddedHash(torrentId);
    const QJsonArray added =
        QJsonDocument::fromJson(body).object().value(QStringLiteral("added_torrent_ids")).toArray();
    const QString id = added.isEmpty() ? torrentId : added.first().toString();
    const bool keepStopped = request.value(QStringLiteral("stopped")).toBool();
    const QVariantList renames =
        token.isEmpty() ? QVariantList() : request.value(QStringLiteral("fileRenames")).toList();
    if (!renames.isEmpty()) {
        applyAddRenames(id,
                        renames,
                        request.value(QStringLiteral("renameAnchor")).toMap(),
                        laterPriorities,
                        keepStopped);
        return;
    }
    if (!AddFilesModel::priorityGroups(laterPriorities).isEmpty()) {
        applyAddFilePriorities(id, laterPriorities, keepStopped);
        return;
    }
    Q_EMIT addFinished(true, QString(), id);
}

void App::retryUrlAdd(const QVariantMap &request,
                      const QbtClient::AddOptions &options,
                      const QString &torrentId,
                      int attempt)
{
    const QString value = request.value(QStringLiteral("value")).toString();
    // The last way: added to stop once its metadata is in, the priorities set then.
    const auto fallback = [=, this]() {
        QbtClient::AddOptions untilMetadata = options;
        untilMetadata.filePriorities.clear();
        untilMetadata.stopped = false;
        untilMetadata.stopCondition = QStringLiteral("MetadataReceived");
        m_client->torrentsAddUrls(QStringList{value}, untilMetadata)
            ->then(this, [=, this](const QbtResult<QByteArray> &result) {
                if (result.isError()) {
                    Q_EMIT addFinished(false, result.error().message(), QString());
                    return;
                }
                afterUrlAdd(result.value(),
                            request,
                            torrentId,
                            request.value(QStringLiteral("filePriorities")).toList());
            });
    };
    m_client->torrentsFetchMetadata(value)->then(
        this, [=, this](const QbtResult<QbtClient::FetchedMetadata> &fetched) {
            if (fetched.isError()) {
                fallback();
                return;
            }
            if (!fetched.value().ready) {
                // The swarm is known by now: a few seconds at most.
                if (attempt < 20) {
                    QTimer::singleShot(250, this, [=, this]() {
                        retryUrlAdd(request, options, torrentId, attempt + 1);
                    });
                } else {
                    fallback();
                }
                return;
            }
            m_client->torrentsAddUrls(QStringList{value}, options)
                ->then(this, [=, this](const QbtResult<QByteArray> &result) {
                    if (result.isError()) {
                        fallback();
                        return;
                    }
                    afterUrlAdd(result.value(), request, torrentId, {});
                });
        });
}

QString App::demoAddText() const
{
    if (!m_demoMode)
        return QString();
    const QString text = env("OMASEED_DEMO_ADD");
    // `torrent`: a real `.torrent` with a folder tree, so the file picker can be shown.
    if (text == QLatin1String("torrent"))
        return demoTorrentPath();
    // `magnet`: the same torrent as a magnet, which the demo lookup answers (docs/29 F2).
    if (text == QLatin1String("magnet"))
        return demoMagnet();
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

QString App::downloadsFolder() const
{
    if (!m_downloadsFolder.isEmpty() || m_demoMode)
        return m_downloadsFolder;
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

void App::setDownloadsFolder(const QString &folder)
{
    if (folder == m_downloadsFolder)
        return;
    m_downloadsFolder = folder;
    Q_EMIT downloadsFolderChanged();
}

void App::scanDownloads()
{
    const int scan = ++m_downloadsScan;
    const QString folder = downloadsFolder();
    // Same arrangement as parseTorrentInBackground: the worker touches nothing of App's and
    // hops back through the application object.
    QPointer<App> self(this);
    QThreadPool::globalInstance()->start([self, scan, folder]() {
        const QList<FolderTorrent> found = TorrentFolderScan::scan(folder);
        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;
        QMetaObject::invokeMethod(
            application,
            [self, scan, found]() {
                if (self.isNull() || scan != self->m_downloadsScan)
                    return;
                QVariantList list;
                list.reserve(found.size());
                for (const FolderTorrent &torrent : found) {
                    const bool inLibrary =
                        !self->findDuplicate(torrent.infoHashV1, torrent.infoHashV2).isEmpty();
                    list.append(QVariantMap{
                        {QStringLiteral("path"), torrent.path},
                        {QStringLiteral("name"), torrent.name},
                        {QStringLiteral("sizeText"), Format::size(torrent.size)},
                        {QStringLiteral("ageText"), Format::relativeTime(torrent.modified)},
                        {QStringLiteral("infoHashV1"), torrent.infoHashV1},
                        {QStringLiteral("infoHashV2"), torrent.infoHashV2},
                        {QStringLiteral("inLibrary"), inLibrary},
                    });
                }
                self->m_downloadsTorrents = list;
                Q_EMIT self->downloadsTorrentsChanged();
            },
            Qt::QueuedConnection);
    });
}

void App::refreshDownloadsInLibrary()
{
    if (m_downloadsTorrents.isEmpty())
        return;
    bool moved = false;
    for (QVariant &value : m_downloadsTorrents) {
        QVariantMap entry = value.toMap();
        const bool inLibrary = !findDuplicate(entry.value(QStringLiteral("infoHashV1")).toString(),
                                              entry.value(QStringLiteral("infoHashV2")).toString())
                                    .isEmpty();
        if (entry.value(QStringLiteral("inLibrary")).toBool() == inLibrary)
            continue;
        entry.insert(QStringLiteral("inLibrary"), inLibrary);
        value = entry;
        moved = true;
    }
    if (moved)
        Q_EMIT downloadsTorrentsChanged();
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

bool App::startsOnToggle(TorrentState state)
{
    // docs/28: starting an errored torrent clears the error in qBittorrent and makes it try
    // again (TorrentImpl::start), which is what `p` offers on the error block; stopping it
    // would only hide the error.
    return isStoppedState(state) || state == TorrentState::Error;
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
        index >= 0 && startsOnToggle(m_engine->torrents()->torrent(hashes.constFirst()).state);

    auto *reply = stopped ? m_client->torrentsStart(hashes) : m_client->torrentsStop(hashes);
    reply->then(this, [this](const QbtResult<QByteArray> &result) {
        reportActionResult(QStringLiteral("stopStart"), result);
    });
}

void App::startTorrents(const QStringList &hashes)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("start"), hashes.size()) || !m_hasServer)
        return;
    m_client->torrentsStart(hashes)->then(this, [this](const QbtResult<QByteArray> &result) {
        reportActionResult(QStringLiteral("start"), result);
    });
}

void App::stopTorrents(const QStringList &hashes)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("stop"), hashes.size()) || !m_hasServer)
        return;
    m_client->torrentsStop(hashes)->then(this, [this](const QbtResult<QByteArray> &result) {
        reportActionResult(QStringLiteral("stop"), result);
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

void App::removeTrackers(const QString &hash, const QStringList &urls)
{
    if (hash.isEmpty() || urls.isEmpty() ||
        demoGuard(QStringLiteral("remove tracker"), int(urls.size())) || !m_hasServer)
        return;
    m_client->torrentsRemoveTrackers(hash, urls)
        ->then(this, [this, hash](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("remove tracker"), result);
            if (!result.isError() && m_details->hash() == hash)
                m_details->refreshNow();
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

void App::loadDuplicateTrackers(const QString &hash, const QStringList &urls)
{
    if (hash.isEmpty())
        return;
    if (m_demoMode || !m_hasServer) {
        // The demo library has no trackers to ask for: every URL would be new.
        Q_EMIT duplicateTrackersLoaded(hash, TrackerMerge::missing(urls, {}), {});
        return;
    }
    m_client->torrentsTrackers(hash)->then(
        this, [this, hash, urls](const QbtResult<QJsonArray> &result) {
            if (result.isError()) {
                Q_EMIT actionFailed(QStringLiteral("trackers"), result.error().toString());
                return;
            }
            QStringList existing;
            for (const QJsonValue &entry : result.value()) {
                const QString url = entry.toObject().value(QStringLiteral("url")).toString();
                if (!TrackerMerge::key(url).isEmpty())
                    existing.append(url);
            }
            Q_EMIT duplicateTrackersLoaded(hash, TrackerMerge::missing(urls, existing), existing);
        });
}

void App::mergeTrackers(const QString &hash, const QStringList &urls)
{
    if (hash.isEmpty() || urls.isEmpty())
        return;
    if (demoGuard(QStringLiteral("merge trackers"), int(urls.size()))) {
        Q_EMIT trackersMerged(hash, int(urls.size()));
        return;
    }
    if (!m_hasServer)
        return;
    m_client->torrentsAddTrackers(hash, urls)
        ->then(this, [this, hash, urls](const QbtResult<QByteArray> &result) {
            if (result.isError()) {
                Q_EMIT actionFailed(QStringLiteral("merge trackers"), result.error().toString());
                return;
            }
            if (m_details->hash() == hash)
                m_details->refreshNow();
            Q_EMIT trackersMerged(hash, int(urls.size()));
        });
}

namespace {

/// Two library torrents already sharing one folder are one copy, not two: keep one match per
/// folder, the one that needs no layout change when there is a choice.
QVariantList oneCopyEach(const QVariantList &matches)
{
    QVariantList kept;
    QHash<QString, qsizetype> byFolder;
    for (const QVariant &value : matches) {
        const QVariantMap match = value.toMap();
        const QString folder = match.value(QStringLiteral("contentPath")).toString();
        const auto seen = byFolder.constFind(folder);
        if (seen == byFolder.constEnd()) {
            byFolder.insert(folder, kept.size());
            kept.append(match);
        } else if (match.value(QStringLiteral("layout")) == QStringLiteral("Original") &&
                   kept.at(*seen).toMap().value(QStringLiteral("layout")) !=
                       QStringLiteral("Original")) {
            kept[*seen] = match;
        }
    }
    return kept;
}

} // namespace

void App::findFilesMatch(const QString &token)
{
    const auto pending = m_pendingTorrentFiles.constFind(token);
    if (pending == m_pendingTorrentFiles.constEnd() || pending->files.isEmpty() || m_demoMode ||
        !m_hasServer) {
        Q_EMIT filesMatchFound(token, {});
        return;
    }
    qint64 total = 0;
    for (const TorrentFileEntry &file : pending->files)
        total += file.size;

    // Step 1 (docs/25 §3): complete torrents of the same total size, still where they were.
    // The server's `total_size` counts BEP-47 pad files, which a hybrid or v2 torrent puts
    // between its files (at most a piece each), and the file list leaves out: allow for them.
    const qint64 padding =
        qint64(pending->files.size()) * std::max<qint64>(pending->pieceLength, 0);
    TorrentModel *model = m_engine->torrents();
    QStringList candidates;
    for (int row = 0; row < model->count(); ++row) {
        const QString hash = model->data(model->index(row), TorrentModel::HashRole).toString();
        const Torrent &torrent = model->torrent(hash);
        if (hash == pending->torrentId || torrent.totalSize < total ||
            torrent.totalSize > total + padding || torrent.progress < 1.0 ||
            torrent.state == TorrentState::MissingFiles || torrent.state == TorrentState::Error ||
            torrent.state == TorrentState::Moving)
            continue;
        candidates.append(hash);
    }
    if (candidates.isEmpty()) {
        Q_EMIT filesMatchFound(token, {});
        return;
    }

    // Step 2: each candidate's files, as the server has them on disk.
    struct Search
    {
        int waiting = 0;
        QVariantList seeding;
        QVariantList others;
    };
    auto search = std::make_shared<Search>();
    search->waiting = int(candidates.size());
    const QString name = pending->name;
    const bool multiFile = pending->multiFile;
    const QList<TorrentFileEntry> files = pending->files;
    for (const QString &hash : candidates) {
        m_client->torrentsFiles(hash)->then(this, [=, this](const QbtResult<QJsonArray> &result) {
            if (!result.isError() && m_engine->torrents()->indexOfHash(hash) >= 0) {
                const Torrent &torrent = m_engine->torrents()->torrent(hash);
                LibraryMatch::Candidate candidate{torrent.savePath, torrent.contentPath, {}};
                for (const QJsonValue &value : result.value()) {
                    const QJsonObject file = value.toObject();
                    candidate.files.append({file.value(QStringLiteral("name")).toString(),
                                            file.value(QStringLiteral("size")).toInteger()});
                }
                const LibraryMatch::Result found =
                    LibraryMatch::match(name, multiFile, files, candidate);
                if (found.exact) {
                    const bool seeding = !isStoppedState(torrent.state);
                    const QVariantMap match{
                        {QStringLiteral("hash"), hash},
                        {QStringLiteral("name"), torrent.name},
                        {QStringLiteral("tracker"), QUrl(torrent.tracker).host()},
                        {QStringLiteral("stateText"), torrent.stateText()},
                        {QStringLiteral("ratioText"), Format::ratio(torrent.ratio)},
                        {QStringLiteral("seeding"), seeding},
                        {QStringLiteral("savePath"), found.savePath},
                        {QStringLiteral("layout"), found.layout},
                        {QStringLiteral("fileCount"), int(files.size())},
                        {QStringLiteral("contentPath"), torrent.contentPath},
                    };
                    (seeding ? search->seeding : search->others).append(match);
                }
            }
            if (--search->waiting == 0)
                Q_EMIT filesMatchFound(token, oneCopyEach(search->seeding + search->others));
        });
    }
}

void App::addWebSeeds(const QString &hash, const QStringList &urls)
{
    if (hash.isEmpty() || urls.isEmpty() || demoGuard(QStringLiteral("add HTTP sources"), 1) ||
        !m_hasServer)
        return;
    m_client->torrentsAddWebSeeds(hash, urls)
        ->then(this, [this, hash](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("add HTTP source"), result);
            if (!result.isError() && m_details->hash() == hash)
                m_details->refreshWebSeeds();
        });
}

void App::removeWebSeeds(const QString &hash, const QStringList &urls)
{
    if (hash.isEmpty() || urls.isEmpty() ||
        demoGuard(QStringLiteral("remove HTTP source"), int(urls.size())) || !m_hasServer)
        return;
    m_client->torrentsRemoveWebSeeds(hash, urls)
        ->then(this, [this, hash](const QbtResult<QByteArray> &result) {
            reportActionResult(QStringLiteral("remove HTTP source"), result);
            if (!result.isError() && m_details->hash() == hash)
                m_details->refreshWebSeeds();
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

void App::banPeers(const QStringList &addresses)
{
    if (addresses.isEmpty() || demoGuard(QStringLiteral("ban peer"), int(addresses.size())) ||
        !m_hasServer)
        return;
    m_client->transferBanPeers(addresses)->then(this, [this](const QbtResult<QByteArray> &result) {
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

QVariantMap App::checkWebSeedUrls(const QString &text) const
{
    return peerInputMap(core::PeerInput::webSeedUrls(text));
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
    // docs/32 D2: "OK" only means the move was queued; a failure is in the log. Watched from
    // before the request, so a read on its way meanwhile can't take the line first.
    m_engine->problems()->watchMove(hashes, path);
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
        // The word follows the sets (docs/31 §3): read them back.
        if (!result.isError())
            m_prefs->reload();
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
    bool loginTouched = false;
    QString secret;
    if (m_server.authMode == AuthMode::Cookie) {
        const QString userKey = QStringLiteral("web_ui_username");
        const QString passKey = QStringLiteral("web_ui_password");
        if (changes.contains(userKey) && changes.value(userKey).toString() != m_server.username) {
            fields.insert(QStringLiteral("username"), changes.value(userKey).toString());
            loginTouched = true;
        }
        if (changes.contains(passKey) && !changes.value(passKey).toString().isEmpty()) {
            secret = changes.value(passKey).toString();
            loginTouched = true;
        }
    }
    const QUrl url = urlAfterPreferences(m_server.url, changes, before);
    if (url == m_server.url && !loginTouched)
        return;

    const quint64 generation = m_serverGeneration;
    const auto follow = [this, fields, secret, loginTouched, generation](const QUrl &target) {
        if (generation != m_serverGeneration || (target == m_server.url && !loginTouched))
            return;
        QVariantMap moved = fields;
        moved.insert(QStringLiteral("url"), target.toString());
        qInfo().noquote() << QStringLiteral("server: following its own Web UI change");
        updateServer(moved, secret);
    };
    // qBittorrent moves its Web UI once it has answered; give it a moment before the next
    // request goes to the new place.
    if (url.scheme() == m_server.url.scheme()) {
        QTimer::singleShot(750, this, [follow, url]() { follow(url); });
        return;
    }
    // HTTPS turned on without a usable certificate and key: qBittorrent answers use_https
    // true and goes on in HTTP ("HTTPS setup failed, fallback to HTTP"). Only a scheme the
    // Web UI really speaks is followed, or the app would shut itself out.
    const QUrl stay = [&]() {
        QUrl kept = url;
        kept.setScheme(m_server.url.scheme());
        return kept;
    }();
    probeWebUi(
        url,
        [this, follow, url, stay, generation](bool answers) {
            if (generation != m_serverGeneration)
                return;
            if (answers) {
                follow(url);
                return;
            }
            qWarning().noquote() << QStringLiteral(
                                        "server: the Web UI did not answer on %1; staying on %2")
                                        .arg(url.toString(QUrl::RemoveUserInfo),
                                             stay.toString(QUrl::RemoveUserInfo));
            Q_EMIT webUiNotFollowed(
                url.scheme() == QLatin1String("https")
                    ? tr("qBittorrent could not start HTTPS (it needs a certificate and a key) "
                         "and stays on HTTP, so omaseed does too")
                    : tr("the Web UI did not answer on %1, so omaseed stays on %2")
                          .arg(url.toString(QUrl::RemoveUserInfo),
                               stay.toString(QUrl::RemoveUserInfo)));
            // qBittorrent restarted its Web UI all the same, and the session went with it:
            // sign in again where it still is.
            if (stay == m_server.url)
                setDefaultServer(m_server.id);
            else
                follow(stay);
        },
        8);
}

void App::probeWebUi(const QUrl &base, std::function<void(bool)> done, int attempts)
{
    if (m_probeNam == nullptr)
        m_probeNam = new QNetworkAccessManager(this);
    QUrl url = base;
    QString path = url.path();
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    url.setPath(path + QStringLiteral("api/v2/app/version"));
    QNetworkRequest request(url);
    // An HTTP server never answers a TLS hello, so a wrong scheme ends at this timeout.
    request.setTransferTimeout(1500);
    QNetworkReply *reply = m_probeNam->get(request);
    // A certificate shown is HTTPS up, trusted or not. A TLS failure without one is the
    // handshake meeting a plain HTTP server.
    auto certificateSeen = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::sslErrors, this, [certificateSeen](const QList<QSslError> &) {
        *certificateSeen = true;
    });
    connect(reply,
            &QNetworkReply::finished,
            this,
            [this, reply, base, done = std::move(done), attempts, certificateSeen]() mutable {
                reply->deleteLater();
                if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid() ||
                    *certificateSeen) {
                    done(true);
                    return;
                }
                if (attempts <= 1) {
                    done(false);
                    return;
                }
                QTimer::singleShot(
                    500, this, [this, base, done = std::move(done), attempts]() mutable {
                        probeWebUi(base, std::move(done), attempts - 1);
                    });
            });
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
// to any of them. In demo mode the fictional rows flip locally, through the same merge a sync
// diff takes, so the pane and the menu show the change. Against a server the change shows at
// once too (M12), as a pending value in the engine that the server's answer settles.

QStringList
App::hashesNotAt(const QStringList &hashes, bool on, bool Torrent::*field, const QString &key) const
{
    QStringList wrong;
    const TorrentModel *torrents = m_engine->torrents();
    for (const QString &hash : hashes) {
        if (torrents->indexOfHash(hash) < 0)
            continue;
        const std::optional<QJsonValue> pending = m_engine->pendingTorrentValue(hash, key);
        const bool current = pending ? pending->toBool() : torrents->torrent(hash).*field;
        if (current != on)
            wrong.append(hash);
    }
    return wrong;
}

void App::sendOption(const QStringList &hashes,
                     bool on,
                     bool Torrent::*field,
                     const QString &key,
                     QbtReply<QByteArray> *reply,
                     const QString &action)
{
    const TorrentModel *torrents = m_engine->torrents();
    for (const QString &hash : hashes)
        m_engine->setPendingTorrentValue(hash, key, on, torrents->torrent(hash).*field);
    const QPointer<SyncEngine> engine(m_engine);
    reply->then(this, [this, engine, hashes, key, action](const QbtResult<QByteArray> &result) {
        if (engine) {
            for (const QString &hash : hashes)
                engine->settlePendingTorrentValue(hash, key, result.isError());
        }
        reportActionResult(action, result);
    });
}

void App::setSequentialDownload(const QStringList &hashes, bool on)
{
    const QString key = QStringLiteral("seq_dl");
    const QStringList flip = hashesNotAt(hashes, on, &Torrent::seqDl, key);
    if (flip.isEmpty() || applyDemoOption(flip, key, on) || !m_hasServer)
        return;
    sendOption(flip,
               on,
               &Torrent::seqDl,
               key,
               m_client->torrentsToggleSequentialDownload(flip),
               QStringLiteral("sequential"));
}

void App::setFirstLastPiecePrio(const QStringList &hashes, bool on)
{
    const QString key = QStringLiteral("f_l_piece_prio");
    const QStringList flip = hashesNotAt(hashes, on, &Torrent::firstLastPiecePrio, key);
    if (flip.isEmpty() || applyDemoOption(flip, key, on) || !m_hasServer)
        return;
    sendOption(flip,
               on,
               &Torrent::firstLastPiecePrio,
               key,
               m_client->torrentsToggleFirstLastPiecePrio(flip),
               QStringLiteral("firstLast"));
}

void App::setSuperSeeding(const QStringList &hashes, bool on)
{
    const QString key = QStringLiteral("super_seeding");
    const QStringList change = hashesNotAt(hashes, on, &Torrent::superSeeding, key);
    if (change.isEmpty() || applyDemoOption(change, key, on) || !m_hasServer)
        return;
    sendOption(change,
               on,
               &Torrent::superSeeding,
               key,
               m_client->torrentsSetSuperSeeding(change, on),
               QStringLiteral("superSeeding"));
}

void App::setAutoManagement(const QStringList &hashes, bool on)
{
    const QString key = QStringLiteral("auto_tmm");
    const QStringList change = hashesNotAt(hashes, on, &Torrent::autoTmm, key);
    if (change.isEmpty() || applyDemoOption(change, key, on) || !m_hasServer)
        return;
    sendOption(change,
               on,
               &Torrent::autoTmm,
               key,
               m_client->torrentsSetAutoManagement(change, on),
               QStringLiteral("autoManagement"));
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

void App::remove(const QStringList &hashes, bool deleteFiles)
{
    if (hashes.isEmpty() || demoGuard(QStringLiteral("delete"), hashes.size()) || !m_hasServer)
        return;
    // docs/32 D2: the names now, while the torrents are still in the list; the files'
    // removal may fail after the answer, in the log only.
    if (deleteFiles)
        m_engine->problems()->watchDelete(hashes);
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
    QFileDialog dialog(nullptr, tr("Choose a location"), startDir);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly);
    return runPicker(dialog, m_dialogParent).value(0);
}

QString App::browseStartFor(const QString &serverPath) const
{
    if (!pathMapEnabled())
        return serverPath;
    const PathMap map = serverPathMap();
    return map.covers(serverPath) ? map.toLocal(serverPath) : serverPath;
}

QVariantMap App::serverPathForPick(const QString &localPath) const
{
    QVariantMap answer{{QStringLiteral("path"), localPath},
                       {QStringLiteral("local"), localPath},
                       {QStringLiteral("mapped"), false},
                       {QStringLiteral("outside"), false},
                       {QStringLiteral("mappings"), QString()}};
    const PathMap map = serverPathMap();
    if (localPath.isEmpty() || !pathMapEnabled() || map.count() == 0)
        return answer;
    QStringList pairs;
    for (const PathMap::Mapping &mapping : map.mappings())
        pairs.append(QStringLiteral("%1 → %2").arg(mapping.local, mapping.remote));
    answer.insert(QStringLiteral("mappings"), pairs.join(QStringLiteral(", ")));
    if (map.coversLocal(localPath)) {
        answer.insert(QStringLiteral("path"), map.toRemote(localPath));
        answer.insert(QStringLiteral("mapped"), true);
    } else {
        answer.insert(QStringLiteral("outside"), true);
    }
    return answer;
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
        m_client->setPinnedCertificate(QString());
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
    client->setPinnedCertificate(server.certificateSha256);
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
    const QString pinned = server.certificateSha256;
    client->probe()->then(
        this, [this, client, elapsed, id, mode, pinned](const QbtResult<ServerInfo> &result) {
            const int latency = int(elapsed.elapsed());
            if (result.isError()) {
                const QString raw = result.error().toString();
                QString refused = credentialRefusedText(result.error(), mode);
                if (result.error().kind() == ApiError::Kind::Certificate) {
                    refused = certificateRefusedText(pinned);
                    Q_EMIT serverCertificateRefused(id, result.error().certificateSha256(), pinned);
                }
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

bool App::uploadTrend() const
{
    const_cast<App *>(this)->loadSettings();
    return m_uploadTrend;
}

void App::setUploadTrend(bool on)
{
    loadSettings();
    if (m_uploadTrend == on)
        return;
    m_uploadTrend = on;
    saveSettings();
    Q_EMIT settingsChanged();
}

bool App::downloadTrend() const
{
    const_cast<App *>(this)->loadSettings();
    return m_downloadTrend;
}

void App::setDownloadTrend(bool on)
{
    loadSettings();
    if (m_downloadTrend == on)
        return;
    m_downloadTrend = on;
    saveSettings();
    Q_EMIT settingsChanged();
}

bool App::pulseRows() const
{
    const_cast<App *>(this)->loadSettings();
    return m_pulseRows;
}

void App::setPulseRows(bool on)
{
    loadSettings();
    if (m_pulseRows == on)
        return;
    m_pulseRows = on;
    saveSettings();
    Q_EMIT settingsChanged();
}

void App::setReducedMotion(bool reduced)
{
    if (m_reducedMotion == reduced)
        return;
    m_reducedMotion = reduced;
    Q_EMIT reducedMotionChanged();
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

void App::setLaunchHidden(bool hidden)
{
    m_launchHidden = hidden;
    if (hidden)
        m_quietStart.start(m_quietStartGrace);
    else
        m_quietStart.stop();
}

QString App::startAtLogin() const
{
    return omaseed::integration::Autostart::mode();
}

void App::setStartAtLogin(const QString &mode)
{
    namespace Autostart = omaseed::integration::Autostart;
    if (Autostart::mode() == mode)
        return;
    if (!Autostart::setMode(mode, Autostart::execProgram(QCoreApplication::applicationFilePath())))
        qWarning().noquote()
            << QStringLiteral("autostart: could not write %1").arg(Autostart::entryPath());
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
    applyTrayIconVisibility();
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

QString App::barWidgetCommand() const
{
    return QStringLiteral("omaseed --install-bar-widget");
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
    m_uploadTrend = appearance.value(QStringLiteral("uploadTrend")).toBool(false);
    m_downloadTrend = appearance.value(QStringLiteral("downloadTrend")).toBool(false);
    m_pulseRows = appearance.value(QStringLiteral("pulseRows")).toBool(false);
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
    appearance[QStringLiteral("uploadTrend")] = m_uploadTrend;
    appearance[QStringLiteral("downloadTrend")] = m_downloadTrend;
    appearance[QStringLiteral("pulseRows")] = m_pulseRows;
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
                Q_EMIT serversDetected(list, m_serverDetector->runningProcesses() > 1);
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
    // #64: qBittorrent refuses a name another of the torrent's files has, but lets the rename
    // replace any other file of that name.
    const RenameDisk disk = renameDisk(hash);
    if (folder ? disk.folderTaken(newPath) : disk.fileTaken(newPath)) {
        Q_EMIT actionFailed(QStringLiteral("rename"), tr("%1 already exists on disk").arg(name));
        return;
    }

    Q_EMIT torrentFileRenameRequested(hash, oldPath, newPath, folder);
    if (m_demoMode) {
        // No server: the demo's own copy of the list moves, so the rename can be seen.
        if (m_details->hash() == hash || m_details->hash().isEmpty())
            m_details->files()->renameLocal(oldPath, newPath);
        return;
    }
    if (!m_hasServer)
        return;

    // docs/32 D2: the rename itself happens after the answer; a failure is in the log.
    m_engine->problems()->watchRename(hash, 1);
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

void App::setFilesWanted(const QString &hash, const QVariantList &fileIndexes, bool wanted)
{
    if (hash.isEmpty() || fileIndexes.isEmpty())
        return;
    if (!wanted) {
        setFilePriority(hash, fileIndexes, int(FilePriority::Skip));
        return;
    }
    const QHash<int, int> current =
        m_details->hash() == hash ? m_details->files()->priorityByFileIndex() : QHash<int, int>();
    QHash<int, int> &before = m_beforeSkip[hash];
    QMap<int, QVariantList> groups;
    for (const QVariant &value : fileIndexes) {
        const int id = value.toInt();
        if (current.value(id, int(FilePriority::Skip)) != int(FilePriority::Skip))
            continue;
        const int was = before.contains(id) ? before.take(id) : int(FilePriority::Normal);
        groups[was].append(id);
    }
    for (auto group = groups.cbegin(); group != groups.cend(); ++group)
        setFilePriority(hash, group.value(), group.key());
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

// --- #11: renaming many files ------------------------------------------------------------------

namespace {

RenameOptions renameOptionsFrom(const QVariantMap &map)
{
    RenameOptions options;
    options.find = map.value(QStringLiteral("find")).toString();
    options.replace = map.value(QStringLiteral("replace")).toString();
    options.regex = map.value(QStringLiteral("regex")).toBool();
    options.everyMatch = map.value(QStringLiteral("everyMatch")).toBool();
    options.matchCase = map.value(QStringLiteral("matchCase")).toBool();
    options.includeExtension = map.value(QStringLiteral("includeExtension")).toBool();
    return options;
}

QString renameStatusText(RenameRow::Status status)
{
    switch (status) {
    case RenameRow::Status::Changed:
        return QStringLiteral("changed");
    case RenameRow::Status::Collides:
        return QStringLiteral("collides");
    case RenameRow::Status::Invalid:
        return QStringLiteral("invalid");
    case RenameRow::Status::Unchanged:
        break;
    }
    return QStringLiteral("unchanged");
}

RenamePlan planFor(const TorrentFilesModel *files,
                   const QStringList &paths,
                   const QVariantMap &options,
                   const RenameDisk &disk)
{
    const QSet<QString> renamed(paths.cbegin(), paths.cend());
    QStringList others;
    for (const QString &path : files->filePaths()) {
        if (!renamed.contains(path))
            others.append(path);
    }
    std::function<bool(const QString &)> taken;
    if (disk.sees())
        taken = [&disk](const QString &path) { return disk.fileTaken(path); };
    return RenamePlan::make(paths, others, renameOptionsFrom(options), taken);
}

} // namespace

RenameDisk App::renameDisk(const QString &hash) const
{
    if (hash.isEmpty() || hash != m_details->hash())
        return {};
    const QStringList own = m_details->files()->filePaths();
    if (own.isEmpty())
        return {};
    if (m_demoMode)
        return m_demoDiskFolder.isEmpty() ? RenameDisk() : RenameDisk(m_demoDiskFolder, own);
    if (!m_hasServer || m_engine->torrents()->indexOfHash(hash) < 0)
        return {};
    // The folder the file names are relative to, as the server writes it: one file's full
    // path (TorrentFilesModel::serverPathOf, which follows the content path to a download
    // path) less its name.
    const Torrent &torrent = m_engine->torrents()->torrent(hash);
    const QString first = own.first();
    const QString server =
        TorrentFilesModel::serverPathOf(torrent.contentPath, torrent.savePath, first);
    if (server.size() <= first.size())
        return {};
    const QString local = m_freeSpace.localPathFor(server.left(server.size() - first.size() - 1));
    return local.isEmpty() ? RenameDisk() : RenameDisk(local, own);
}

QVariantMap App::planFileRenames(const QStringList &paths, const QVariantMap &options) const
{
    const RenamePlan plan =
        planFor(m_details->files(), paths, options, renameDisk(m_details->hash()));
    QVariantList rows;
    for (const RenameRow &row : plan.rows()) {
        QVariantList marks;
        for (const auto &[start, length] : row.marks)
            marks.append(QVariant(QVariantList{int(start), int(length)}));
        rows.append(QVariantMap{
            {QStringLiteral("path"), row.path},
            {QStringLiteral("name"), row.path.mid(row.path.lastIndexOf(QLatin1Char('/')) + 1)},
            {QStringLiteral("newName"), row.newName},
            {QStringLiteral("status"), renameStatusText(row.status)},
            {QStringLiteral("marks"), marks},
        });
    }
    return {
        {QStringLiteral("rows"), rows},
        {QStringLiteral("error"), plan.error()},
        {QStringLiteral("changed"), plan.changed()},
        {QStringLiteral("collisions"), plan.collisions()},
        {QStringLiteral("invalid"), plan.invalid()},
        {QStringLiteral("canRun"), plan.canRun()},
    };
}

void App::renameTorrentFiles(const QString &hash,
                             const QStringList &paths,
                             const QVariantMap &options)
{
    if (hash.isEmpty() || hash != m_details->hash())
        return;
    // Planned again here, against the list and the disk as they are now, rather than trusting
    // the preview.
    const RenamePlan plan = planFor(m_details->files(), paths, options, renameDisk(hash));
    if (!plan.canRun()) {
        Q_EMIT actionFailed(QStringLiteral("rename"), tr("the new names collide or are not valid"));
        return;
    }
    QList<std::pair<QString, QString>> moves;
    const QList<RenameRow> rows = plan.rows();
    for (int index : plan.executionOrder())
        moves.append({rows.at(index).path, rows.at(index).newPath});
    for (const auto &[from, to] : std::as_const(moves))
        Q_EMIT torrentFileRenameRequested(hash, from, to, false);

    if (m_demoMode) {
        for (const auto &[from, to] : std::as_const(moves))
            m_details->files()->renameLocal(from, to);
        Q_EMIT torrentFilesRenamed(
            hash, int(moves.size()), int(moves.size()), QString(), QString());
        return;
    }
    if (!m_hasServer)
        return;
    // docs/32 D2: each file's rename happens after its answer; failures are in the log.
    m_engine->problems()->watchRename(hash, int(moves.size()));
    sendFileRename(hash, moves, 0);
}

void App::sendFileRename(const QString &hash,
                         const QList<std::pair<QString, QString>> &moves,
                         int at)
{
    if (at >= moves.size()) {
        m_details->refreshNow();
        Q_EMIT torrentFilesRenamed(hash, at, int(moves.size()), QString(), QString());
        return;
    }
    auto *reply = m_client->torrentsRenameFile(hash, moves.at(at).first, moves.at(at).second);
    reply->then(this, [this, hash, moves, at](const QbtResult<QByteArray> &result) {
        if (result.isError()) {
            m_details->refreshNow();
            Q_EMIT torrentFilesRenamed(
                hash, at, int(moves.size()), moves.at(at).first, result.error().toString());
            return;
        }
        sendFileRename(hash, moves, at + 1);
    });
}

// --- end N1 ----------------------------------------------------------------------------------

// --- #2: trusted certificates ------------------------------------------------------------------

QString App::serverId() const
{
    return m_hasServer && m_server.id != QLatin1String("environment") ? m_server.id : QString();
}

bool App::trustCertificate(const QString &id, const QString &sha256)
{
    const QString pin = CertificatePin::normalized(sha256);
    if (!sha256.isEmpty() && pin.isEmpty())
        return false;
    QVariantMap fields = m_serverList->serverMap(id);
    if (fields.isEmpty())
        return false;
    if (fields.value(QStringLiteral("certificateSha256")).toString() == pin)
        return true;
    fields.insert(QStringLiteral("certificateSha256"), pin);
    qInfo().noquote() << (pin.isEmpty()
                              ? QStringLiteral("server %1: trusted certificate forgotten").arg(id)
                              : QStringLiteral("server %1: trusting certificate %2")
                                    .arg(id, CertificatePin::displayText(pin)));
    // updateServer() reconnects the engine when this is its server.
    return updateServer(fields);
}

QString App::certificateText(const QString &sha256)
{
    return CertificatePin::displayText(sha256);
}

bool App::isCertificateError(const QString &lastError)
{
    return lastError.startsWith(ApiError::kindName(ApiError::Kind::Certificate));
}

// --- end #2 ----------------------------------------------------------------------------------

// --- #14: the bar widget's heartbeat ---------------------------------------------------------

void App::attachWidgetHeartbeat(omaseed::integration::WidgetHeartbeat *heartbeat)
{
    if (m_widgetHeartbeat)
        disconnect(m_widgetHeartbeat, nullptr, this, nullptr);
    m_widgetHeartbeat = heartbeat;
    if (heartbeat) {
        connect(
            heartbeat, &omaseed::integration::WidgetHeartbeat::widgetAliveChanged, this, [this]() {
                applyTrayIconVisibility();
                Q_EMIT barWidgetRunningChanged();
            });
    }
    applyTrayIconVisibility();
    Q_EMIT barWidgetRunningChanged();
}

bool App::barWidgetRunning() const
{
    return m_widgetHeartbeat && m_widgetHeartbeat->widgetAlive();
}

void App::applyTrayIconVisibility()
{
    if (m_tray.isNull())
        return;
    loadSettings();
    m_tray->setIconVisible(
        omaseed::integration::WidgetHeartbeat::trayIconVisible(m_trayIconMode, barWidgetRunning()));
}

// --- end #14 ---------------------------------------------------------------------------------

// --- #12: the bar widget's status file --------------------------------------------------------

void App::attachStatusWriter(omaseed::integration::StatusWriter *writer)
{
    m_statusWriter = writer;
    if (writer == nullptr)
        return;

    writer->setEngine(m_engine);
    writer->setPreferences(m_prefs);
    writer->setAltSpeed(m_altSpeed);
    writer->setLedger(m_ledger.get());
    writer->setPathMapper([this](const QString &path) { return mapContentPathToLocal(path); });
    writer->setLibraryCheck(
        [this](const QString &v1, const QString &v2) { return !findDuplicate(v1, v2).isEmpty(); });
    writer->setDownloadsFolder(downloadsFolder());
    connect(writer,
            &omaseed::integration::StatusWriter::milestone,
            this,
            [this](const QString &hash, const QString &name, int ratio) {
                const Torrent &t = m_engine->torrents()->torrent(hash);
                const qint64 days = t.seedingTime / 86400;
                Q_EMIT seedNotice(QStringLiteral("milestone"),
                                  hash,
                                  ratio == 1 ? tr("%1 paid back").arg(name)
                                             : tr("%1 · %2×").arg(name).arg(ratio),
                                  ratio == 1 ? (days == 1 ? tr("ratio 1.00 · 1 day")
                                                          : tr("ratio 1.00 · %1 days").arg(days))
                                             : tr("%1 given").arg(sizeText(t.uploaded)));
            });
    m_console->setRecentProvider([this]() {
        return m_statusWriter ? m_statusWriter->snapshot()
                                    .value(QStringLiteral("recent"))
                                    .toArray()
                                    .toVariantList()
                              : QVariantList();
    });
    connect(this, &App::downloadsFolderChanged, writer, [this]() {
        if (m_statusWriter)
            m_statusWriter->setDownloadsFolder(downloadsFolder());
    });

    updateStatusServer();
    connect(this, &App::serverChanged, writer, [this]() { updateStatusServer(); });
    connect(m_engine, &SyncEngine::serverLost, writer, [writer](const ApiError &error) {
        writer->setServerError(serverLostReasonText(error));
    });
    connect(m_engine, &SyncEngine::authRequired, writer, [writer](const ApiError &error) {
        writer->setServerError(serverLostReasonText(error));
    });
    connect(m_engine, &SyncEngine::serverBack, writer, [writer]() { writer->setServerError({}); });
    connect(m_engine, &SyncEngine::firstSyncFinished, writer, [writer]() {
        writer->setServerError({});
    });
    connect(m_engine,
            &SyncEngine::torrentCompleted,
            writer,
            [writer](const QString &hash,
                     const QString &name,
                     qint64 sizeBytes,
                     const QString &category,
                     const QString &contentPath) {
                writer->addCompletion(hash, name, sizeBytes, category, contentPath);
            });
    writer->start();
}

void App::updateStatusServer()
{
    if (m_statusWriter.isNull())
        return;
    m_statusWriter->setServer(m_server.id, serverName(), m_hasServer ? m_server.url : QUrl());
}

// --- end #12 ---------------------------------------------------------------------------------

// --- #24: the seed console ---------------------------------------------------------------------

void App::openLedger()
{
    const QString key = serverId().isEmpty() ? hostPort(m_server.url) : serverId();
    if (key == m_ledgerKey && m_ledger->isOpen())
        return;
    m_ledgerKey = key;
    m_ledgerDemoDone = false;
    m_ledger->open(SeedLedger::defaultPath(key));
}

void App::feedLedger()
{
    if (!m_hasServer && !m_demoMode)
        return;
    const TorrentModel *torrents = m_engine->torrents();
    const GlobalState *state = m_engine->globalState();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const auto totalsOf = [](const Torrent &t) {
        return SeedLedger::TorrentTotals{t.hash,
                                         t.uploaded,
                                         t.downloaded,
                                         t.addedOn.isValid() ? t.addedOn.toSecsSinceEpoch() : 0};
    };
    // #36: an idle poll changes nothing and costs nothing; a busy one, the rows it touched.
    const bool follows = m_fed && torrents->lastChangeFollows(m_fedRevision);
    if (follows && !m_ledger->wantsWholeList()) {
        const TorrentModel::Change &change = torrents->lastChange();
        QList<SeedLedger::TorrentTotals> changed;
        changed.reserve(change.changed.size());
        for (const QString &hash : change.changed)
            changed.append(totalsOf(torrents->torrent(hash)));
        m_ledger->observeChanges(
            now, state->allTimeUpload(), state->allTimeDownload(), changed, change.removed);
    } else {
        QList<SeedLedger::TorrentTotals> totals;
        totals.reserve(torrents->count());
        for (int i = 0; i < torrents->count(); ++i)
            totals.append(totalsOf(torrents->torrentAt(i)));
        m_ledger->observe(now, state->allTimeUpload(), state->allTimeDownload(), totals);
    }

    // T7: a complete torrent whose swarm just went down to you alone. Nothing is announced
    // from the poll that fills an empty map (the first one).
    const bool announce = !m_lastSwarm.isEmpty();
    if (follows) {
        const TorrentModel::Change &change = torrents->lastChange();
        for (const QString &hash : change.removed) {
            if (!change.changed.contains(hash))
                m_lastSwarm.remove(hash);
        }
        for (const QString &hash : change.changed)
            checkLastSeed(torrents->torrent(hash), m_lastSwarm.value(hash, 0), announce);
    } else {
        const QHash<QString, int> before = std::exchange(m_lastSwarm, {});
        for (int i = 0; i < torrents->count(); ++i) {
            const Torrent &t = torrents->torrentAt(i);
            checkLastSeed(t, before.value(t.hash, 0), announce);
        }
    }
    m_fed = true;
    m_fedRevision = torrents->revision();

    m_ledger->sampleSpeed(now, state->upInfoSpeed(), state->dlInfoSpeed());
    // Dev only: a made-up year before today, to judge the console as it will look a year in.
    if (!m_ledgerDemoDone && (m_demoMode || !env("OMASEED_LEDGER_DEMO").isEmpty()) &&
        torrents->count() > 0) {
        m_ledgerDemoDone = true;
        // A typical made-up day gives about a fifth of what the library holds.
        qint64 held = 0;
        for (int i = 0; i < torrents->count(); ++i)
            held += torrents->torrentAt(i).isComplete() ? torrents->torrentAt(i).size : 0;
        m_ledger->fillDemo(now, torrents->hashes(), std::max<qint64>(held / 5, 64LL * 1024 * 1024));
        m_console->refresh();
    }
}

void App::checkLastSeed(const Torrent &t, int before, bool announce)
{
    const int seeds = SeedConsole::knownSeeds(t);
    if (!t.isComplete() || seeds <= 0) {
        m_lastSwarm.remove(t.hash);
        return;
    }
    m_lastSwarm.insert(t.hash, seeds);
    if (announce && seeds == 1 && before > 1)
        Q_EMIT seedNotice(QStringLiteral("lastSeed"),
                          t.hash,
                          tr("You are now the only seed of %1").arg(t.name),
                          t.numIncomplete > 0 ? tr("%1 waiting").arg(t.numIncomplete) : QString());
}

// --- end #24 ---------------------------------------------------------------------------------

// --- #31: omaseed's own backup (docs/24 §3, §5) --------------------------------------------

namespace {

/// `path` with the home folder written as `~`.
QString tildePath(const QString &path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home + QLatin1Char('/')) ? QStringLiteral("~") + path.mid(home.size())
                                                    : path;
}

QString daysText(int days)
{
    return days == 1 ? QCoreApplication::translate("App", "1 day")
                     : QCoreApplication::translate("App", "%1 days").arg(days);
}

QString dayText(qint64 secs)
{
    return QLocale::c().toString(QDateTime::fromSecsSinceEpoch(secs).date(),
                                 QStringLiteral("d MMM yyyy"));
}

} // namespace

OmaseedBackup::Sources App::backupSources() const
{
    OmaseedBackup::Sources sources = OmaseedBackup::defaultSources();
    sources.appVersion = omaseed::core::version();
    // #32 (D3 a): each server's qBittorrent settings, as `qbittorrent/<server id>.json`.
    sources.extras = [this] { return qbtBackupExtras(); };
    return sources;
}

QString App::autoBackupMode() const
{
    return m_autoBackup->mode();
}

void App::setAutoBackupMode(const QString &mode)
{
    if (mode == m_autoBackup->mode())
        return;
    m_autoBackup->setMode(mode);
    writeStateSection(QStringLiteral("backup"),
                      QJsonObject{{QStringLiteral("mode"), m_autoBackup->mode()}});
    Q_EMIT backupChanged();
    // Turned on, or made more frequent: a copy now if one is due.
    if (!m_demoMode && m_autoBackupTimer->isActive())
        runAutoBackupIfDue();
}

QString App::lastBackupText() const
{
    const QDateTime last = m_autoBackup->lastCopyTime();
    if (!last.isValid())
        return QString();
    const QLocale locale = QLocale::c();
    const qint64 age = last.secsTo(QDateTime::currentDateTime());
    const QString time = locale.toString(last.time(), QStringLiteral("HH:mm"));
    if (age < 7 * 86400 && age >= 0)
        return locale.toString(last.date(), QStringLiteral("dddd")) + QLatin1Char(' ') + time;
    return locale.toString(last.date(), QStringLiteral("d MMM")) + QLatin1Char(' ') + time;
}

QString App::backupSummary() const
{
    const OmaseedBackup::Sources sources = backupSources();
    qint64 bytes = QFileInfo(sources.statePath).size() + QFileInfo(sources.serversPath).size();
    qint64 earliest = 0;
    const QList<Server> servers = m_servers->servers();
    for (const Server &server : servers) {
        const QString path = sources.ledgerPath(server.id);
        if (!QFileInfo::exists(path))
            continue;
        // The write-ahead log holds what the ledger wrote since its last checkpoint.
        bytes += QFileInfo(path).size() + QFileInfo(path + QStringLiteral("-wal")).size();
        // The open ledger writes every few minutes; its own start is the one to trust.
        const qint64 start = (m_ledger->isOpen() && m_ledger->path() == path)
                                 ? m_ledger->start()
                                 : OmaseedBackup::historyOf(path).start;
        if (start > 0 && (earliest == 0 || start < earliest))
            earliest = start;
    }
    const QString serverText =
        servers.size() == 1 ? tr("1 server") : tr("%1 servers").arg(servers.size());
    const QString size = Format::size(std::max<qint64>(bytes, 1));
    if (earliest > 0)
        return tr("Settings, %1 and the seed history since %2: about %3. Passwords and API keys "
                  "stay in the keyring.")
            .arg(serverText, dayText(earliest), size);
    return tr("Settings and %1: about %2. Passwords and API keys stay in the keyring.")
        .arg(serverText, size);
}

QString App::backupsDirText() const
{
    return tildePath(OmaseedBackup::defaultBackupsDir());
}

QString App::pickBackupSavePath() const
{
    const QString suggested = QDir::homePath() + QStringLiteral("/omaseed-") +
                              QDate::currentDate().toString(Qt::ISODate) + OmaseedBackup::kSuffix;
    QFileDialog dialog(
        nullptr, tr("Back up omaseed"), suggested, tr("omaseed backups (*.omaseed)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    QString path = runPicker(dialog, m_dialogParent).value(0);
    if (!path.isEmpty() && !path.endsWith(OmaseedBackup::kSuffix))
        path += OmaseedBackup::kSuffix;
    return path;
}

QString App::backUpTo(const QString &path)
{
    if (path.isEmpty())
        return tr("No file was chosen.");
    if (m_ledger->isOpen())
        m_ledger->flush(QDateTime::currentSecsSinceEpoch());
    QString error;
    if (!OmaseedBackup::createFile(
            backupSources(), path, QDateTime::currentSecsSinceEpoch(), &error))
        return error;
    qInfo().noquote() << QStringLiteral("backup: written to %1").arg(path);
    return QString();
}

QString App::pickBackupFile() const
{
    QFileDialog dialog(nullptr,
                       tr("Restore omaseed"),
                       OmaseedBackup::defaultBackupsDir(),
                       tr("omaseed backups (*.omaseed);;All files (*)"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    return runPicker(dialog, m_dialogParent).value(0);
}

QVariantMap App::readBackup(const QString &path)
{
    m_pendingRestore.reset();
    m_pendingRestorePath.clear();
    // The comparison with this machine's history reads the files: write what is buffered.
    if (m_ledger->isOpen())
        m_ledger->flush(QDateTime::currentSecsSinceEpoch());
    QString error;
    const std::optional<OmaseedBackup::Contents> contents = OmaseedBackup::parseFile(path, &error);
    if (!contents)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    m_pendingRestore = contents;
    m_pendingRestorePath = path;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QLocale locale = QLocale::c();
    QVariantMap out{
        {QStringLiteral("ok"), true},
        {QStringLiteral("title"), tr("Restore from %1").arg(QFileInfo(path).fileName())}};
    const QDateTime made = contents->created.toLocalTime();
    out.insert(
        QStringLiteral("sub"),
        tr("Made %1 by omaseed %2 on %3.")
            .arg(locale.toString(made, QStringLiteral("d MMM yyyy HH:mm")),
                 contents->appVersion.isEmpty() ? QStringLiteral("?") : contents->appVersion,
                 contents->machine.isEmpty() ? tr("an unnamed machine") : contents->machine));
    out.insert(
        QStringLiteral("safety"),
        tr("what is here now is saved first, to %1")
            .arg(tildePath(OmaseedBackup::datedName(
                OmaseedBackup::defaultBackupsDir(), QStringLiteral("before-restore"), now))));

    QVariantList parts;
    if (contents->hasSettings)
        parts.append(QVariantMap{
            {QStringLiteral("key"), QStringLiteral("settings")},
            {QStringLiteral("label"), tr("settings")},
            {QStringLiteral("side"), tr("replaces this machine's")},
            {QStringLiteral("detail"), tr("appearance, keys, columns, notifications, tray")},
            {QStringLiteral("checked"), true},
            {QStringLiteral("enabled"), true}});

    const QList<OmaseedBackup::Match> matches =
        OmaseedBackup::match(*contents, m_servers->servers(), backupSources());
    if (contents->hasServers && !matches.isEmpty()) {
        QStringList names;
        QStringList kept;
        QStringList added;
        for (const OmaseedBackup::Match &m : matches) {
            names.append(m.part.server.name);
            (m.localId.isEmpty() ? added : kept).append(m.part.server.name);
        }
        QStringList side;
        if (!kept.isEmpty())
            side.append(tr("%1 kept").arg(kept.size()));
        if (!added.isEmpty())
            side.append(tr("%1 added").arg(added.size()));
        QStringList detail;
        if (!kept.isEmpty())
            detail.append(tr("%1 already here (same address): kept, path maps updated.")
                              .arg(kept.join(QStringLiteral(", "))));
        if (!added.isEmpty())
            detail.append(
                tr("%1 added; you sign in once after.").arg(added.join(QStringLiteral(", "))));
        parts.append(QVariantMap{
            {QStringLiteral("key"), QStringLiteral("servers")},
            {QStringLiteral("label"), tr("servers · %1").arg(names.join(QStringLiteral(", ")))},
            {QStringLiteral("side"), side.join(QStringLiteral(" · "))},
            {QStringLiteral("detail"), detail.join(QLatin1Char(' '))},
            {QStringLiteral("checked"), true},
            {QStringLiteral("enabled"), true}});
    }

    for (const OmaseedBackup::Match &m : matches) {
        if (!m.part.hasLedger || !m.part.history.exists())
            continue;
        const QString name = m.part.server.name;
        const int theirs = m.part.history.days(now);
        QString detail;
        bool needsServers = false;
        if (m.localId.isEmpty()) {
            detail = tr("%1 is new here: its %2 come with it when servers are restored.")
                         .arg(name, daysText(theirs));
            needsServers = true;
        } else if (!m.local.exists()) {
            detail = tr("No history for %1 here yet: it starts with the backup's %2.")
                         .arg(name, daysText(theirs));
        } else if (m.backupLonger) {
            detail = tr("This machine has %1 for %2. The backup's %3 replace them.")
                         .arg(daysText(m.local.days(now)), name, daysText(theirs));
        } else if (m.local.start == m.part.history.start) {
            detail = tr("This machine already has this history for %1.").arg(name);
        } else {
            detail = tr("This machine's %1 for %2 are longer and stay.")
                         .arg(daysText(m.local.days(now)), name);
        }
        parts.append(
            QVariantMap{{QStringLiteral("key"), QStringLiteral("history:") + m.part.server.id},
                        {QStringLiteral("label"), tr("seed history · %1").arg(name)},
                        {QStringLiteral("side"), tr("since %1").arg(dayText(m.part.history.start))},
                        {QStringLiteral("detail"), detail},
                        {QStringLiteral("checked"), m.backupLonger},
                        {QStringLiteral("enabled"), m.backupLonger},
                        {QStringLiteral("needsServers"), needsServers}});
    }
    out.insert(QStringLiteral("parts"), parts);
    return out;
}

QVariantMap App::restoreBackup(const QVariantMap &choice)
{
    // A demo keeps its settings in memory and its servers fictional: it never writes the
    // real files a restore would replace.
    if (m_demoMode)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("text"), tr("Restore is off in the demo.")}};
    if (!m_pendingRestore)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("text"), tr("Choose a backup first.")}};
    OmaseedBackup::Plan plan;
    plan.settings =
        choice.value(QStringLiteral("settings")).toBool() && m_pendingRestore->hasSettings;
    plan.servers = choice.value(QStringLiteral("servers")).toBool();
    plan.history = choice.value(QStringLiteral("history")).toStringList();

    // The ledger's file may be replaced: write what it holds, let go of it, take it back after.
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_ledger->isOpen())
        m_ledger->flush(now);
    m_ledger->close();
    QString error;
    const std::optional<OmaseedBackup::Outcome> outcome =
        OmaseedBackup::restore(*m_pendingRestore, plan, *m_servers, backupSources(), now, &error);
    m_pendingRestore.reset();
    reloadAfterRestore();
    if (!outcome)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("text"), error}};

    QStringList done;
    if (plan.settings)
        done.append(tr("settings"));
    if (!outcome->addedServers.isEmpty())
        done.append(outcome->addedServers.size() == 1
                        ? tr("1 server added")
                        : tr("%1 servers added").arg(outcome->addedServers.size()));
    if (!outcome->historyRestored.isEmpty())
        done.append(tr("history for %1")
                        .arg(outcome->historyRestored.size() == 1
                                 ? tr("1 server")
                                 : tr("%1 servers").arg(outcome->historyRestored.size())));
    QString text = done.isEmpty() ? tr("Nothing to restore.")
                                  : tr("Restored: %1.").arg(done.join(QStringLiteral(", ")));
    text += QLatin1Char(' ') + tr("What was here is in %1.").arg(tildePath(outcome->safetyCopy));
    if (!outcome->addedServers.isEmpty())
        text += QLatin1Char(' ') + tr("Sign in to new servers once, in Servers.");
    qInfo().noquote() << QStringLiteral("restore: %1").arg(text);
    Q_EMIT backupChanged();
    if (plan.settings)
        Q_EMIT tableStatesRestored();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("text"), text}};
}

void App::reloadAfterRestore()
{
    m_settingsLoaded = false;
    loadSettings();
    m_notificationTogglesLoaded = false;
    loadNotificationToggles();
    m_autoBackup->setMode(
        stateSection(QStringLiteral("backup")).value(QStringLiteral("mode")).toString());
    applyAppearance();
    Q_EMIT settingsChanged();
    Q_EMIT notificationsEnabledChanged();
    // A fresh machine that had no server now has one.
    if (!m_hasServer && m_servers->count() > 0)
        configureFromStore();
    m_ledgerKey.clear();
    if (m_hasServer)
        openLedger();
    m_console->refresh();
    Q_EMIT serverChanged();
}

void App::openBackupsFolder() const
{
    const QString dir = OmaseedBackup::defaultBackupsDir();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

// --- end #31 ---------------------------------------------------------------------------------

// --- #32: a qBittorrent's settings, exported and imported (docs/24 §4–§6) --------------------

namespace {

const QString kQbtEntryPrefix = QStringLiteral("qbittorrent/");

QString countText(int n, const QString &one, const QString &many)
{
    return n == 1 ? one : many.arg(n);
}

/// "2 feeds, 5 rules", "1 feed", "none".
QString rssText(int feeds, int rules)
{
    QStringList parts;
    if (feeds > 0)
        parts.append(countText(feeds,
                               QCoreApplication::translate("App", "1 feed"),
                               QCoreApplication::translate("App", "%1 feeds")));
    if (rules > 0)
        parts.append(countText(rules,
                               QCoreApplication::translate("App", "1 rule"),
                               QCoreApplication::translate("App", "%1 rules")));
    return parts.isEmpty() ? QCoreApplication::translate("App", "none")
                           : parts.join(QStringLiteral(", "));
}

/// A server's name as part of a file name: "home", "seedbox-2".
QString fileStem(const QString &name)
{
    QString out = name.toLower();
    out.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    out.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return out.isEmpty() ? QStringLiteral("server") : out;
}

} // namespace

QString App::qbtExportSummary() const
{
    const int categories = m_engine->categories()->count();
    const int tags = m_engine->tags()->count();
    QStringList parts{tr("Preferences"),
                      countText(categories, tr("1 category"), tr("%1 categories")),
                      countText(tags, tr("1 tag"), tr("%1 tags"))};
    if (m_qbtRssFeeds >= 0 && m_qbtRssRules >= 0) {
        parts.append(countText(m_qbtRssFeeds, tr("1 RSS feed"), tr("%1 RSS feeds")));
        parts.append(countText(m_qbtRssRules, tr("1 rule"), tr("%1 rules")));
    } else {
        parts.append(tr("RSS feeds and rules"));
    }
    const QString last = parts.takeLast();
    return tr("%1 and %2. Passwords are left out.").arg(parts.join(QStringLiteral(", ")), last);
}

void App::refreshQbtBackupInfo()
{
    if (m_demoMode || !m_hasServer) {
        m_qbtRssFeeds = m_qbtRssRules = m_demoMode ? 0 : -1;
        Q_EMIT qbtBackupChanged();
        return;
    }
    const QString id = m_server.id;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("withData"), QStringLiteral("false"));
    m_client->get(QStringLiteral("rss/items"), query)
        ->then(this, [this, id](const QbtResult<QByteArray> &r) {
            if (r.isError() || m_server.id != id)
                return;
            QbtSettingsSnapshot s;
            s.rssItems = QJsonDocument::fromJson(r.value()).object();
            m_qbtRssFeeds = s.feedCount();
            Q_EMIT qbtBackupChanged();
        });
    m_client->get(QStringLiteral("rss/rules"))
        ->then(this, [this, id](const QbtResult<QByteArray> &r) {
            if (r.isError() || m_server.id != id)
                return;
            m_qbtRssRules = int(QJsonDocument::fromJson(r.value()).object().size());
            Q_EMIT qbtBackupChanged();
        });
}

QString App::pickQbtExportPath() const
{
    const QString suggested =
        QDir::homePath() + QStringLiteral("/qbittorrent-") + fileStem(m_server.name) +
        QLatin1Char('-') + QDate::currentDate().toString(Qt::ISODate) + QbtSettingsExport::kSuffix;
    QFileDialog dialog(nullptr,
                       tr("Export qBittorrent's settings"),
                       suggested,
                       tr("qBittorrent settings (*.json)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    QString path = runPicker(dialog, m_dialogParent).value(0);
    if (!path.isEmpty() && !path.endsWith(QbtSettingsExport::kSuffix))
        path += QbtSettingsExport::kSuffix;
    return path;
}

void App::collectQbtSettings(
    std::function<void(std::optional<QbtSettingsSnapshot>, const QString &)> done)
{
    if (m_demoMode) {
        // The demo's server: its offline preferences, categories and tags; no RSS.
        QbtSettingsSnapshot s;
        s.created = QDateTime::currentDateTimeUtc();
        s.appVersion = omaseed::core::version();
        s.serverName = m_server.name.isEmpty() ? QStringLiteral("demo") : m_server.name;
        s.version = m_engine->globalState()->serverVersion();
        s.apiVersion = m_engine->globalState()->apiVersion();
        s.preferences = QJsonObject::fromVariantMap(m_prefs->values());
        for (const QString &name : m_engine->categories()->names())
            s.categories.insert(name,
                                QJsonObject{{QStringLiteral("name"), name},
                                            {QStringLiteral("savePath"),
                                             m_engine->categories()->savePathOf(name)}});
        s.tags = m_engine->tags()->tags();
        QTimer::singleShot(0, this, [s, done] { done(s, QString()); });
        return;
    }
    if (!m_hasServer || !m_engine->globalState()->connected()) {
        QTimer::singleShot(0, this, [this, done] {
            done(std::nullopt, tr("%1 is not connected.").arg(m_server.name));
        });
        return;
    }
    auto *exporter = new QbtSettingsExport(m_client, this);
    const QString id = m_server.id;
    connect(exporter,
            &QbtSettingsExport::collected,
            this,
            [this, exporter, id, done](const QbtSettingsSnapshot &s) {
                exporter->deleteLater();
                // Kept for omaseed's backups (D3 a), under this machine's id for the server.
                if (!id.isEmpty())
                    m_qbtSnapshots.insert(id, QbtSettingsExport::toJson(s));
                done(s, QString());
            });
    connect(exporter, &QbtSettingsExport::failed, this, [exporter, done](const QString &why) {
        exporter->deleteLater();
        done(std::nullopt, why);
    });
    exporter->collect(m_server.name, m_server.url.toString(), omaseed::core::version());
}

void App::exportQbtSettings(const QString &path)
{
    if (path.isEmpty())
        return;
    ++m_qbtBusy;
    Q_EMIT qbtBackupChanged();
    collectQbtSettings([this, path](std::optional<QbtSettingsSnapshot> s, const QString &why) {
        --m_qbtBusy;
        Q_EMIT qbtBackupChanged();
        QString error = why;
        if (s && QbtSettingsExport::writeFile(*s, path, &error)) {
            m_qbtRssFeeds = s->feedCount();
            m_qbtRssRules = int(s->rssRules.size());
            qInfo().noquote() << QStringLiteral("qBittorrent settings: exported to %1").arg(path);
            Q_EMIT qbtBackupChanged();
            Q_EMIT qbtExportFinished(tr("Exported to %1.").arg(tildePath(path)), true);
            return;
        }
        Q_EMIT qbtExportFinished(tr("Not exported: %1").arg(error), false);
    });
}

QString App::pickQbtImportFile() const
{
    QFileDialog dialog(
        nullptr,
        tr("Import qBittorrent's settings"),
        QDir::homePath(),
        tr("qBittorrent settings (*.json);;omaseed backups (*.omaseed);;All files (*)"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    return runPicker(dialog, m_dialogParent).value(0);
}

QString App::readQbtSettings(const QString &path)
{
    m_importFile.reset();
    m_importHere.reset();
    if (path.isEmpty())
        return tr("No file was chosen.");
    QString error;
    std::optional<QbtSettingsSnapshot> file;
    if (path.endsWith(OmaseedBackup::kSuffix)) {
        // An omaseed backup (an automatic copy among them) holds each server's settings as
        // `qbittorrent/<id>.json`, under the ids of the machine that made it: this server's
        // is found by its address, then by its id, then as the only one (#34's lesson).
        const std::optional<OmaseedBackup::Contents> backup =
            OmaseedBackup::parseFile(path, &error);
        if (!backup)
            return error;
        QList<QbtSettingsSnapshot> found;
        QStringList ids;
        for (const TarArchive::Entry &entry : backup->extras) {
            if (!entry.name.startsWith(kQbtEntryPrefix))
                continue;
            if (std::optional<QbtSettingsSnapshot> s = QbtSettingsExport::parse(entry.data)) {
                found.append(*s);
                ids.append(QFileInfo(entry.name).completeBaseName());
            }
        }
        const QString here = OmaseedBackup::addressKey(m_server.url);
        for (qsizetype i = 0; i < found.size() && !file; ++i) {
            if (OmaseedBackup::addressKey(QUrl(found.at(i).serverUrl)) == here)
                file = found.at(i);
        }
        for (qsizetype i = 0; i < found.size() && !file; ++i) {
            if (ids.at(i) == m_server.id)
                file = found.at(i);
        }
        if (!file && found.size() == 1)
            file = found.constFirst();
        if (!file)
            return found.isEmpty()
                       ? tr("This backup holds no qBittorrent settings.")
                       : tr("This backup holds no qBittorrent settings for %1.").arg(m_server.name);
    } else {
        file = QbtSettingsExport::parseFile(path, &error);
        if (!file)
            return error;
    }
    m_importFile = file;
    ++m_qbtBusy;
    Q_EMIT qbtBackupChanged();
    collectQbtSettings([this, path](std::optional<QbtSettingsSnapshot> s, const QString &why) {
        --m_qbtBusy;
        Q_EMIT qbtBackupChanged();
        if (!s) {
            Q_EMIT qbtImportRead(
                {{QStringLiteral("ok"), false},
                 {QStringLiteral("error"),
                  tr("Could not read %1's settings: %2").arg(m_server.name, why)}});
            return;
        }
        m_importHere = s;
        Q_EMIT qbtImportRead(qbtImportInfo(path));
    });
    return QString();
}

QVariantMap App::qbtImportInfo(const QString &path)
{
    const QbtSettingsSnapshot &file = *m_importFile;
    const QbtSettingsSnapshot &here = *m_importHere;
    const QbtSettingsImport::Diff d = QbtSettingsImport::diff(file, here);
    const QString name = m_server.name.isEmpty() ? tr("this server") : m_server.name;

    QVariantList rows;
    const auto prefRow = [](const QbtSettingsImport::PrefChange &change) {
        return QVariantMap{{QStringLiteral("key"), change.key},
                           {QStringLiteral("kind"), QStringLiteral("pref")},
                           {QStringLiteral("group"), change.group},
                           {QStringLiteral("now"), change.here.toVariant()},
                           {QStringLiteral("from"), change.file.toVariant()},
                           {QStringLiteral("checked"), change.group.isEmpty()}};
    };
    for (const QbtSettingsImport::PrefChange &change : d.changes) {
        if (change.group.isEmpty())
            rows.append(prefRow(change));
    }
    if (!d.categories.isEmpty())
        rows.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("@categories")},
                                {QStringLiteral("kind"), QStringLiteral("categories")},
                                {QStringLiteral("label"), tr("categories")},
                                {QStringLiteral("group"), QString()},
                                {QStringLiteral("now"), QString::number(d.categoriesHere)},
                                {QStringLiteral("from"),
                                 QStringLiteral("+ ") + d.categories.join(QStringLiteral(", "))},
                                {QStringLiteral("checked"), true}});
    const bool newRss = !d.feeds.isEmpty() || !d.rules.isEmpty();
    if (!d.tags.isEmpty() || newRss) {
        QString label = tr("tags · RSS");
        if (d.tags.isEmpty())
            label = tr("RSS");
        else if (!newRss)
            label = tr("tags");
        QStringList now;
        QStringList from;
        if (!d.tags.isEmpty()) {
            now.append(QString::number(d.tagsHere));
            from.append(QStringLiteral("+ ") + d.tags.join(QStringLiteral(", ")));
        }
        if (newRss) {
            now.append(rssText(d.feedsHere, d.rulesHere));
            from.append(QStringLiteral("+ ") + rssText(int(d.feeds.size()), int(d.rules.size())));
        }
        rows.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("@tagsRss")},
                                {QStringLiteral("kind"), QStringLiteral("tagsRss")},
                                {QStringLiteral("label"), label},
                                {QStringLiteral("group"), QString()},
                                {QStringLiteral("now"), now.join(QStringLiteral(" · "))},
                                {QStringLiteral("from"), from.join(QStringLiteral(" · "))},
                                {QStringLiteral("checked"), true}});
    }
    for (const QbtSettingsImport::PrefChange &change : d.changes) {
        if (!change.group.isEmpty())
            rows.append(prefRow(change));
    }
    if (d.hasFolders)
        rows.append(QVariantMap{{QStringLiteral("key"), QbtSettingsImport::kFoldersKey},
                                {QStringLiteral("kind"), QStringLiteral("folders")},
                                {QStringLiteral("label"), tr("category and RSS rule folders")},
                                {QStringLiteral("group"), QStringLiteral("folders")},
                                {QStringLiteral("now"), tr("under %1's save path").arg(name)},
                                {QStringLiteral("from"), tr("as in the file")},
                                {QStringLiteral("checked"), false}});

    QStringList notes;
    if (!d.skipped.isEmpty())
        notes.append(countText(int(d.skipped.size()),
                               tr("1 setting this qBittorrent doesn't have is left out: %2."),
                               tr("%1 settings this qBittorrent doesn't have are left out: %2."))
                         .arg(d.skipped.join(QStringLiteral(", "))));
    if (d.categoryLimitsLeftOut > 0)
        notes.append(countText(d.categoryLimitsLeftOut,
                               tr("The share limits of 1 new category are left out: "
                                  "qBittorrent's Web API can't set them."),
                               tr("The share limits of %1 new categories are left out: "
                                  "qBittorrent's Web API can't set them.")));

    QString version = file.version.isEmpty() ? QStringLiteral("?") : file.version;
    if (version.startsWith(QLatin1Char('v')))
        version.remove(0, 1);
    const int differ = int(d.changes.size());
    QString sub = tr("From %1 (qBittorrent %2).").arg(QFileInfo(path).fileName(), version);
    sub += QLatin1Char(' ');
    if (differ == 0)
        sub +=
            tr("No setting differs from what %1 has now; %2 are the same.").arg(name).arg(d.same);
    else
        sub += countText(differ,
                         tr("1 setting differs from what %2 has now; %3 are the same and are "
                            "not listed."),
                         tr("%1 settings differ from what %2 has now; %3 are the same and are "
                            "not listed."))
                   .arg(name)
                   .arg(d.same);
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("title"), tr("Import settings into %1").arg(name)},
            {QStringLiteral("sub"), sub},
            {QStringLiteral("rows"), rows},
            {QStringLiteral("notes"), notes},
            {QStringLiteral("secrets"),
             tr("passwords were never exported: proxy, e-mail and DynDNS logins and the Web UI "
                "API key stay as they are")},
            {QStringLiteral("changeCount"), differ}};
}

void App::applyQbtImport(const QVariantMap &choice)
{
    if (!m_importFile || !m_importHere) {
        Q_EMIT qbtImportFinished(tr("Choose a file to import first."), false);
        return;
    }
    QbtSettingsImport::Plan plan;
    plan.keys = choice.value(QStringLiteral("keys")).toStringList();
    plan.categories = choice.value(QStringLiteral("categories")).toBool();
    plan.tags = plan.rss = choice.value(QStringLiteral("tagsRss")).toBool();
    plan.folders = choice.value(QStringLiteral("folders")).toBool();
    if (m_demoMode)
        plan.categories = plan.tags = plan.rss = false; // the demo's server is fiction

    auto *import = new QbtSettingsImport(m_demoMode ? nullptr : m_client, this);
    import->setPreferences(m_prefs);
    const QString name = m_server.name;
    ++m_qbtBusy;
    Q_EMIT qbtBackupChanged();
    connect(import,
            &QbtSettingsImport::finished,
            this,
            [this, import, name](const QbtSettingsImport::Outcome &outcome) {
                import->deleteLater();
                --m_qbtBusy;
                QStringList done;
                if (outcome.preferences > 0)
                    done.append(countText(outcome.preferences, tr("1 setting"), tr("%1 settings")));
                if (outcome.categories > 0)
                    done.append(
                        countText(outcome.categories, tr("1 category"), tr("%1 categories")));
                if (outcome.tags > 0)
                    done.append(countText(outcome.tags, tr("1 tag"), tr("%1 tags")));
                if (outcome.feeds > 0)
                    done.append(countText(outcome.feeds, tr("1 RSS feed"), tr("%1 RSS feeds")));
                if (outcome.rules > 0)
                    done.append(countText(outcome.rules, tr("1 rule"), tr("%1 rules")));
                QString text =
                    done.isEmpty()
                        ? tr("Nothing was imported.")
                        : tr("Imported into %1: %2.").arg(name, done.join(QStringLiteral(", ")));
                if (!outcome.kept.isEmpty())
                    text += QLatin1Char(' ') + tr("qBittorrent kept its own value for %1.")
                                                   .arg(outcome.kept.join(QStringLiteral(", ")));
                if (outcome.errors.size() == 1)
                    text += QLatin1Char(' ') + tr("Not done: %1.").arg(outcome.errors.constFirst());
                else if (outcome.errors.size() > 1)
                    text += QLatin1Char(' ') + tr("Not done: %1, and %2 more.")
                                                   .arg(outcome.errors.constFirst())
                                                   .arg(outcome.errors.size() - 1);
                qInfo().noquote() << QStringLiteral("qBittorrent settings: %1").arg(text);
                m_importFile.reset();
                m_importHere.reset();
                // The new counts; categories and tags also arrive with the next sync.
                refreshQbtBackupInfo();
                Q_EMIT qbtBackupChanged();
                Q_EMIT qbtImportFinished(text, outcome.ok());
            });
    import->apply(*m_importFile, *m_importHere, plan);
}

QList<TarArchive::Entry> App::qbtBackupExtras() const
{
    QHash<QString, QByteArray> latest;
    // What the newest automatic copy holds, so a server not connected this week keeps its
    // last known settings in the next copy too.
    const QStringList copies = m_autoBackup->copies();
    if (!copies.isEmpty()) {
        if (const std::optional<OmaseedBackup::Contents> last =
                OmaseedBackup::parseFile(copies.constFirst())) {
            for (const TarArchive::Entry &entry : last->extras) {
                if (entry.name.startsWith(kQbtEntryPrefix) &&
                    entry.name.endsWith(QStringLiteral(".json")))
                    latest.insert(QFileInfo(entry.name).completeBaseName(), entry.data);
            }
        }
    }
    for (auto it = m_qbtSnapshots.constBegin(); it != m_qbtSnapshots.constEnd(); ++it)
        latest.insert(it.key(), it.value());
    QList<TarArchive::Entry> out;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const Server &server : m_servers->servers()) {
        if (latest.contains(server.id))
            out.append({kQbtEntryPrefix + server.id + QStringLiteral(".json"),
                        latest.value(server.id),
                        now});
    }
    return out;
}

void App::runAutoBackupIfDue()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_autoBackupReading || !m_autoBackup->isDue(now))
        return;
    if (m_demoMode || !m_hasServer || !m_engine->globalState()->connected()) {
        m_autoBackup->runNow(now);
        return;
    }
    // D3 a: the copy carries the server's settings as they are now; a failed read still
    // makes the copy, with the settings the last one had.
    m_autoBackupReading = true;
    collectQbtSettings([this](std::optional<QbtSettingsSnapshot> s, const QString &why) {
        m_autoBackupReading = false;
        if (!s)
            qWarning().noquote()
                << QStringLiteral("automatic backup: qBittorrent's settings not read: %1").arg(why);
        m_autoBackup->runNow(QDateTime::currentSecsSinceEpoch());
    });
}

// --- end #32 ---------------------------------------------------------------------------------

} // namespace omaseed::app
