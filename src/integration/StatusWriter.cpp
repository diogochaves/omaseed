#include "integration/StatusWriter.hpp"

#include "core/CategoryModel.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/ServerPreferences.hpp"
#include "core/StateFile.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentFolderScan.hpp"
#include "core/TorrentModel.hpp"
#include "core/Version.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QThreadPool>

#include <cstdio>

using namespace omaseed::core;

namespace omaseed::integration {

namespace {

constexpr int kRecentLimit = 5;
constexpr int kDownloadsLimit = 5;
/// The week is saved to state.json at most this often while it changes (and on a clean quit).
constexpr qint64 kWeekSaveSeconds = 300;

const QString kStateSection = QStringLiteral("status");

} // namespace

StatusWriter::StatusWriter(QObject *parent)
    : QObject(parent)
    , m_path(defaultPath())
{
    m_writeTimer.setSingleShot(true);
    connect(&m_writeTimer, &QTimer::timeout, this, &StatusWriter::tick);
    // Restarted by every write: it fires only after `heartbeat` without one.
    m_heartbeatTimer.setSingleShot(true);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &StatusWriter::heartbeat);
    m_sampleTimer.setInterval(1000);
    connect(&m_sampleTimer, &QTimer::timeout, this, &StatusWriter::sampleUpload);
    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(1000);
    connect(&m_rescanTimer, &QTimer::timeout, this, &StatusWriter::rescanDownloads);
}

StatusWriter::~StatusWriter() = default;

QString StatusWriter::defaultPath()
{
    // $XDG_RUNTIME_DIR itself, which is where the widget looks: Qt's RuntimeLocation refuses a
    // runtime directory that is not 0700 (an omabox's is 0755) and gives nothing at all.
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty())
        runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    return runtime + QStringLiteral("/omaseed/status.json");
}

void StatusWriter::setPath(const QString &path)
{
    m_path = path;
}

void StatusWriter::setIntervals(int minIntervalMs, int heartbeatMs)
{
    m_minInterval = std::max(0, minIntervalMs);
    m_heartbeatInterval = std::max(1, heartbeatMs);
    if (m_heartbeatTimer.isActive())
        m_heartbeatTimer.start(m_heartbeatInterval);
}

void StatusWriter::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

QDateTime StatusWriter::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

// --- sources ---------------------------------------------------------------------------------

void StatusWriter::setEngine(SyncEngine *engine)
{
    if (m_engine)
        disconnect(m_engine, nullptr, this, nullptr);
    m_engine = engine;
    m_milestonesFed = false;
    m_partsValid = false;
    if (!engine)
        return;

    connect(engine, &SyncEngine::synced, this, &StatusWriter::onSynced);
    connect(engine->torrents(), &TorrentModel::updated, this, &StatusWriter::markDirty);
    connect(engine->torrents(), &TorrentModel::countChanged, this, &StatusWriter::markDirty);
    connect(engine->counts(), &FilterCounts::changed, this, &StatusWriter::markDirty);
    connect(engine->categories(), &CategoryModel::countChanged, this, &StatusWriter::markDirty);
    connect(engine->categories(), &CategoryModel::dataChanged, this, &StatusWriter::markDirty);
    const GlobalState *state = engine->globalState();
    for (const auto signal : {&GlobalState::dlInfoSpeedChanged,
                              &GlobalState::upInfoSpeedChanged,
                              &GlobalState::useAltSpeedLimitsChanged,
                              &GlobalState::dlRateLimitChanged,
                              &GlobalState::upRateLimitChanged,
                              &GlobalState::freeSpaceOnDiskChanged,
                              &GlobalState::connectionStatusChanged,
                              &GlobalState::connectedChanged,
                              &GlobalState::serverVersionChanged,
                              &GlobalState::retryInSecondsChanged,
                              &GlobalState::allTimeChanged}) {
        connect(state, signal, this, &StatusWriter::markDirty);
    }
}

void StatusWriter::setPreferences(ServerPreferences *preferences)
{
    if (m_preferences)
        disconnect(m_preferences, nullptr, this, nullptr);
    m_preferences = preferences;
    if (preferences)
        connect(preferences, &ServerPreferences::valuesChanged, this, &StatusWriter::markDirty);
}

void StatusWriter::setServer(const QString &id, const QString &name, const QUrl &url)
{
    QString hostPort;
    if (url.isValid() && !url.host().isEmpty()) {
        hostPort =
            url.port() > 0 ? QStringLiteral("%1:%2").arg(url.host()).arg(url.port()) : url.host();
    }
    const bool switched = id != m_serverId || hostPort != m_serverHostPort;
    if (switched && m_started)
        saveServerState();
    m_serverId = id;
    m_serverName = name;
    m_serverHostPort = hostPort;
    if (switched) {
        m_sessionUp.reset();
        m_sessionDown.reset();
        m_sampler.clear();
        m_milestones.clear();
        m_milestonesFed = false;
        m_week.resetBaseline();
        loadServerState();
    }
    markDirty();
}

void StatusWriter::setServerError(const QString &error)
{
    if (m_serverError == error)
        return;
    m_serverError = error;
    markDirty();
}

void StatusWriter::setWindowShown(bool shown)
{
    if (m_windowShown == shown)
        return;
    m_windowShown = shown;
    markDirty();
}

void StatusWriter::setPathMapper(std::function<QString(const QString &)> mapper)
{
    m_pathMapper = std::move(mapper);
}

void StatusWriter::setLibraryCheck(std::function<bool(const QString &, const QString &)> check)
{
    m_libraryCheck = std::move(check);
}

void StatusWriter::setDownloadsFolder(const QString &folder)
{
    if (m_downloadsFolder == folder)
        return;
    m_downloadsFolder = folder;
    if (m_watcher) {
        delete m_watcher;
        m_watcher = nullptr;
    }
    if (!folder.isEmpty()) {
        m_watcher = new QFileSystemWatcher(this);
        if (QFileInfo(folder).isDir())
            m_watcher->addPath(folder);
        connect(m_watcher,
                &QFileSystemWatcher::directoryChanged,
                &m_rescanTimer,
                qOverload<>(&QTimer::start));
    }
    if (m_started)
        rescanDownloads();
}

void StatusWriter::addCompletion(const QString &hash,
                                 const QString &name,
                                 qint64 size,
                                 const QString &category,
                                 const QString &contentPath)
{
    pushRecent(QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("completed")},
        {QStringLiteral("hash"), hash},
        {QStringLiteral("name"), name},
        {QStringLiteral("at"), now().toSecsSinceEpoch()},
        {QStringLiteral("size"), size},
        {QStringLiteral("category"), category},
        {QStringLiteral("path"), m_pathMapper ? m_pathMapper(contentPath) : contentPath},
    });
}

void StatusWriter::pushRecent(const QJsonObject &entry)
{
    m_recent = StatusDigest::pushRecent(m_recent, entry, kRecentLimit);
    saveServerState();
    markDirty();
}

// --- lifecycle -------------------------------------------------------------------------------

void StatusWriter::start()
{
    if (m_started)
        return;
    m_started = true;
    reloadAddCategory();
    m_heartbeatTimer.start(m_heartbeatInterval);
    m_sampleTimer.start();
    rescanDownloads();
    markDirty();
}

void StatusWriter::removeFile()
{
    m_writeTimer.stop();
    m_heartbeatTimer.stop();
    m_sampleTimer.stop();
    m_rescanTimer.stop();
    // A writer that never started owns no file: the one there may be another omaseed's.
    if (!m_started)
        return;
    saveServerState();
    m_started = false;
    QFile::remove(m_path);
    QFile::remove(m_path + QStringLiteral(".tmp"));
}

void StatusWriter::markDirty()
{
    if (!m_started || m_writeTimer.isActive())
        return;
    const qint64 since = m_sinceWrite.isValid() ? m_sinceWrite.elapsed() : m_minInterval;
    m_writeTimer.start(int(std::max<qint64>(0, m_minInterval - since)));
}

void StatusWriter::reloadAddCategory()
{
    const QString category = StateFile::readSection(QStringLiteral("widgetAdd"))
                                 .value(QStringLiteral("category"))
                                 .toString();
    if (category == m_addCategory)
        return;
    m_addCategory = category;
    markDirty();
}

void StatusWriter::tick()
{
    write(false);
}

void StatusWriter::write(bool force)
{
    if (!m_started)
        return;
    const QJsonObject next = content();
    if (next == m_lastContent && !force && m_sinceWrite.isValid())
        return;

    QJsonObject document = next;
    document.insert(QStringLiteral("t"), now().toSecsSinceEpoch());
    if (!writeFile(QJsonDocument(document).toJson(QJsonDocument::Compact)))
        return;
    m_lastContent = next;
    m_sinceWrite.start();
    m_heartbeatTimer.start(m_heartbeatInterval);
    ++m_writeCount;
    Q_EMIT written();
}

void StatusWriter::heartbeat()
{
    if (!m_started)
        return;
    reloadAddCategory();
    // A new day: today's downloads are another list, and the week moves on.
    if (m_scanDate != now().date())
        rescanDownloads();
    if (m_weekDirty &&
        (!m_weekSavedAt.isValid() || m_weekSavedAt.secsTo(now()) >= kWeekSaveSeconds))
        saveServerState();
    m_writeTimer.stop();
    write(true);
}

void StatusWriter::sampleUpload()
{
    const GlobalState *state = m_engine ? m_engine->globalState() : nullptr;
    const qint64 speed = state && state->connected() ? state->upInfoSpeed() : 0;
    m_sampler.sample(speed, now().toSecsSinceEpoch());
    markDirty();
}

void StatusWriter::onSynced()
{
    if (!m_engine)
        return;
    const GlobalState *state = m_engine->globalState();
    m_sessionUp.observe(state->upInfoData());
    m_sessionDown.observe(state->dlInfoData());
    m_week.observe(state->upInfoData(), now().date());
    m_weekDirty = true;

    // #36: only the torrents this poll touched, once the whole list has been seen.
    const TorrentModel *model = m_engine->torrents();
    QList<StatusDigest::RatioMilestones::Crossing> crossings;
    if (m_milestonesFed && model->lastChangeFollows(m_milestonesRevision)) {
        const TorrentModel::Change &change = model->lastChange();
        QList<Torrent> changed;
        changed.reserve(change.changed.size());
        for (const QString &hash : change.changed)
            changed.append(model->torrent(hash));
        crossings = m_milestones.observeChanges(changed, change.removed);
    } else {
        crossings = m_milestones.observe(StatusDigest::torrentsOf(*model));
    }
    m_milestonesFed = true;
    m_milestonesRevision = model->revision();
    for (const auto &crossing : crossings) {
        const Torrent &torrent = m_engine->torrents()->torrent(crossing.hash);
        Q_EMIT milestone(crossing.hash, torrent.name, crossing.ratio);
        pushRecent(QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("milestone")},
            {QStringLiteral("hash"), crossing.hash},
            {QStringLiteral("name"), torrent.name},
            {QStringLiteral("at"), now().toSecsSinceEpoch()},
            {QStringLiteral("ratio"), crossing.ratio},
            {QStringLiteral("path"),
             m_pathMapper ? m_pathMapper(torrent.contentPath) : torrent.contentPath},
        });
    }
    markDirty();
}

// --- downloads -------------------------------------------------------------------------------

void StatusWriter::rescanDownloads()
{
    const int generation = ++m_scanGeneration;
    const QString folder = m_downloadsFolder;
    const QDate today = now().date();
    m_scanDate = today;
    if (folder.isEmpty()) {
        applyDownloads(QJsonArray());
        return;
    }
    // A folder made after the watch was set up: watch it now.
    if (m_watcher && m_watcher->directories().isEmpty() && QFileInfo(folder).isDir())
        m_watcher->addPath(folder);

    // TorrentFolderScan reads and hashes files: never on the GUI thread. Same hop back as
    // App::scanDownloads.
    QPointer<StatusWriter> self(this);
    QThreadPool::globalInstance()->start([self, generation, folder, today]() {
        const QList<FolderTorrent> found = TorrentFolderScan::scan(folder, kDownloadsLimit);
        QJsonArray rows;
        for (const FolderTorrent &torrent : found) {
            if (torrent.modified.date() != today)
                continue;
            rows.append(QJsonObject{
                {QStringLiteral("path"), torrent.path},
                {QStringLiteral("name"), torrent.name},
                {QStringLiteral("size"), torrent.size},
                {QStringLiteral("at"), torrent.modified.toSecsSinceEpoch()},
                {QStringLiteral("infoHashV1"), torrent.infoHashV1},
                {QStringLiteral("infoHashV2"), torrent.infoHashV2},
            });
        }
        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;
        QMetaObject::invokeMethod(
            application,
            [self, generation, rows]() {
                if (self.isNull() || generation != self->m_scanGeneration)
                    return;
                self->applyDownloads(rows);
            },
            Qt::QueuedConnection);
    });
}

void StatusWriter::applyDownloads(const QJsonArray &found)
{
    m_downloads = found;
    markDirty();
}

// --- state.json ------------------------------------------------------------------------------

QString StatusWriter::serverKey() const
{
    if (!m_serverId.isEmpty())
        return m_serverId;
    return m_serverHostPort.isEmpty() ? QStringLiteral("none") : m_serverHostPort;
}

void StatusWriter::loadServerState()
{
    const QJsonObject section = StateFile::readSection(kStateSection);
    const QString key = serverKey();
    m_week.fromJson(section.value(QStringLiteral("week")).toObject().value(key).toObject());
    m_recent = section.value(QStringLiteral("recent")).toObject().value(key).toArray();
    m_weekDirty = false;
}

void StatusWriter::saveServerState()
{
    QJsonObject section = StateFile::readSection(kStateSection);
    const QString key = serverKey();
    QJsonObject week = section.value(QStringLiteral("week")).toObject();
    week.insert(key, m_week.toJson());
    section.insert(QStringLiteral("week"), week);
    QJsonObject recent = section.value(QStringLiteral("recent")).toObject();
    recent.insert(key, m_recent);
    section.insert(QStringLiteral("recent"), recent);
    StateFile::writeSection(kStateSection, section);
    m_weekDirty = false;
    m_weekSavedAt = now();
}

// --- the document ----------------------------------------------------------------------------

bool StatusWriter::writeFile(const QByteArray &bytes)
{
    const QFileInfo info(m_path);
    const QString dir = info.absolutePath();
    if (!QDir().mkpath(dir))
        return false;
    QFile::setPermissions(dir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const QString temporary = m_path + QStringLiteral(".tmp");
    QFile file(temporary);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate,
                   QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return false;
    }
    // An older .tmp keeps its mode through open(): make sure it is ours alone.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const bool wrote = file.write(bytes) == bytes.size() && file.flush();
    file.close();
    if (!wrote) {
        QFile::remove(temporary);
        return false;
    }
    // rename(2) replaces the old file in one step; QFile::rename would refuse to overwrite.
    return std::rename(QFile::encodeName(temporary).constData(),
                       QFile::encodeName(m_path).constData()) == 0;
}

QJsonObject StatusWriter::snapshot() const
{
    QJsonObject document = content();
    document.insert(QStringLiteral("t"), now().toSecsSinceEpoch());
    return document;
}

const StatusWriter::TorrentParts &StatusWriter::torrentParts(const QDateTime &now) const
{
    const TorrentModel *model = m_engine ? m_engine->torrents() : nullptr;
    const qint64 at = now.toSecsSinceEpoch();
    const quint64 revision = model ? model->revision() : 0;
    const bool holds = m_partsValid && m_partsModel == model && m_partsRevision == revision &&
                       at >= m_partsAt && (m_partsQuietAt == 0 || at < m_partsQuietAt);
    if (holds)
        return m_parts;

    const QList<Torrent> torrents = model ? StatusDigest::torrentsOf(*model) : QList<Torrent>();
    m_parts.onOffer = StatusDigest::onOffer(torrents);
    m_parts.alive = StatusDigest::alive(torrents, now, &m_partsQuietAt);
    m_parts.topSeeders = StatusDigest::topSeeders(torrents);
    m_parts.active = StatusDigest::active(torrents);
    m_partsValid = true;
    m_partsModel = model;
    m_partsRevision = revision;
    m_partsAt = at;
    ++m_partsBuilds;
    return m_parts;
}

QJsonObject StatusWriter::content() const
{
    using StatusDigest::roundSignificant;

    const QDateTime current = now();
    const SyncEngine *engine = m_engine.data();
    const GlobalState *state = engine ? engine->globalState() : nullptr;
    const FilterCounts *counts = engine ? engine->counts() : nullptr;
    const TorrentParts &parts = torrentParts(current);
    const bool reachable = state && state->connected();

    QJsonObject document;
    document.insert(QStringLiteral("v"), schemaVersion);
    document.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
    document.insert(QStringLiteral("app"),
                    QJsonObject{
                        {QStringLiteral("version"), omaseed::core::version()},
                        {QStringLiteral("window"),
                         m_windowShown ? QStringLiteral("shown") : QStringLiteral("hidden")},
                    });

    // qBittorrent says "v5.2.3"; the schema (and every place the app prints it) says "5.2.3".
    QString serverVersion = state ? state->serverVersion() : QString();
    if (serverVersion.startsWith(QLatin1Char('v')))
        serverVersion.remove(0, 1);
    QString error = m_serverError;
    if (!reachable && error.isEmpty() && m_serverHostPort.isEmpty())
        error = QStringLiteral("no server set up");
    document.insert(
        QStringLiteral("server"),
        QJsonObject{
            {QStringLiteral("name"), m_serverName},
            {QStringLiteral("url"), m_serverHostPort},
            {QStringLiteral("version"), serverVersion},
            {QStringLiteral("reachable"), reachable},
            {QStringLiteral("error"), reachable ? QString() : error},
            {QStringLiteral("retryIn"), !reachable && state ? state->retryInSeconds() : 0},
        });
    QString connection = state ? state->connectionStatus() : QStringLiteral("disconnected");
    if (!reachable)
        connection = QStringLiteral("disconnected");
    document.insert(QStringLiteral("reachable"), connection);

    const qint64 dl = reachable ? roundSignificant(state->dlInfoSpeed()) : 0;
    const qint64 up = reachable ? roundSignificant(state->upInfoSpeed()) : 0;
    document.insert(QStringLiteral("speed"),
                    QJsonObject{{QStringLiteral("dl"), dl}, {QStringLiteral("up"), up}});
    const bool altOn = state && state->useAltSpeedLimits();
    document.insert(QStringLiteral("alt"),
                    QJsonObject{
                        {QStringLiteral("on"), altOn},
                        {QStringLiteral("dlLimit"), altOn ? state->dlRateLimit() : 0},
                        {QStringLiteral("upLimit"), altOn ? state->upRateLimit() : 0},
                    });
    document.insert(QStringLiteral("counts"),
                    QJsonObject{
                        {QStringLiteral("all"), counts ? counts->total() : 0},
                        {QStringLiteral("downloading"), counts ? counts->downloading() : 0},
                        {QStringLiteral("seeding"), counts ? counts->seeding() : 0},
                        {QStringLiteral("completed"), counts ? counts->completed() : 0},
                        {QStringLiteral("stopped"), counts ? counts->stopped() : 0},
                        {QStringLiteral("stalled"), counts ? counts->stalled() : 0},
                        {QStringLiteral("checking"), counts ? counts->checking() : 0},
                        {QStringLiteral("errored"), counts ? counts->errored() : 0},
                    });
    document.insert(QStringLiteral("disk"),
                    QJsonObject{{QStringLiteral("free"), state ? state->freeSpaceOnDisk() : 0}});

    const qint64 sessionUp = m_sessionUp.total();
    const qint64 sessionDown = m_sessionDown.total();
    document.insert(
        QStringLiteral("share"),
        QJsonObject{
            {QStringLiteral("sessionUp"), sessionUp},
            {QStringLiteral("sessionDown"), sessionDown},
            {QStringLiteral("sessionRatio"),
             sessionDown > 0 ? StatusDigest::roundRatio(double(sessionUp) / double(sessionDown))
                             : -1.0},
            {QStringLiteral("allTimeUp"), state ? state->allTimeUpload() : 0},
            {QStringLiteral("allTimeDown"), state ? state->allTimeDownload() : 0},
            {QStringLiteral("allTimeRatio"),
             state ? StatusDigest::roundRatio(state->globalRatio()) : -1.0},
            {QStringLiteral("onOffer"), parts.onOffer},
        });
    document.insert(QStringLiteral("uploadHour"), m_sampler.values(current.toSecsSinceEpoch()));
    document.insert(QStringLiteral("week"), m_week.week(current.date()));
    document.insert(QStringLiteral("alive"), parts.alive);
    document.insert(QStringLiteral("topSeeders"), parts.topSeeders);
    document.insert(QStringLiteral("active"), parts.active);
    document.insert(QStringLiteral("recent"), m_recent);

    // Add: the categories with the folder a torrent lands in, and the server's defaults.
    const QString defaultSavePath =
        m_preferences ? m_preferences->value(QStringLiteral("save_path")).toString() : QString();
    QJsonArray categories;
    if (engine) {
        const CategoryModel *model = engine->categories();
        for (const QString &name : model->names()) {
            QString savePath = model->savePathOf(name);
            // qBittorrent's own rule: a category without a path saves into a folder of its
            // name under the default save path.
            if (savePath.isEmpty() && !defaultSavePath.isEmpty())
                savePath = QDir(defaultSavePath).filePath(name);
            categories.append(QJsonObject{
                {QStringLiteral("name"), name},
                {QStringLiteral("savePath"),
                 m_pathMapper && !savePath.isEmpty() ? m_pathMapper(savePath) : savePath},
            });
        }
    }
    document.insert(QStringLiteral("categories"), categories);
    document.insert(
        QStringLiteral("add"),
        QJsonObject{
            {QStringLiteral("savePath"),
             m_pathMapper && !defaultSavePath.isEmpty() ? m_pathMapper(defaultSavePath)
                                                        : defaultSavePath},
            {QStringLiteral("autoTmm"),
             m_preferences ? m_preferences->value(QStringLiteral("auto_tmm_enabled")).toBool()
                           : false},
            {QStringLiteral("category"), m_addCategory},
        });

    QJsonArray downloads;
    for (const QJsonValue &value : m_downloads) {
        const QJsonObject file = value.toObject();
        const bool inLibrary =
            m_libraryCheck && m_libraryCheck(file.value(QStringLiteral("infoHashV1")).toString(),
                                             file.value(QStringLiteral("infoHashV2")).toString());
        downloads.append(QJsonObject{
            {QStringLiteral("path"), file.value(QStringLiteral("path"))},
            {QStringLiteral("name"), file.value(QStringLiteral("name"))},
            {QStringLiteral("size"), file.value(QStringLiteral("size"))},
            {QStringLiteral("at"), file.value(QStringLiteral("at"))},
            {QStringLiteral("inLibrary"), inLibrary},
        });
    }
    document.insert(QStringLiteral("downloads"), downloads);
    return document;
}

} // namespace omaseed::integration
