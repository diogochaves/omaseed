// #146: create a torrent from a folder (board 49, DECISIONS C27). See the block at the end of
// App.hpp. core/CreateJob carries the task; this keeps its choices and its id per server in
// state.json's "create" (never an announce URL), words the band and the strip, ends a public
// one with its magnet on the clipboard and a private one saved in Downloads with its made-here
// record.

#include "App.hpp"

#include "core/AnnounceMask.hpp"
#include "core/CreatePlan.hpp"
#include "core/Format.hpp"
#include "core/GlobalState.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"
#include "integration/Notifier.hpp"
#include "integration/Xdg.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>
#include <QUrl>

namespace omaseed::app {

using core::CreateJob;
using core::CreatePlan::FolderCheck;

namespace {

const QString kSection = QStringLiteral("create");

QString stateName(FolderCheck::State state)
{
    switch (state) {
    case FolderCheck::State::Ok:
        return QStringLiteral("ok");
    case FolderCheck::State::Missing:
        return QStringLiteral("missing");
    case FolderCheck::State::Unreadable:
        return QStringLiteral("unreadable");
    case FolderCheck::State::Empty:
        return QStringLiteral("empty");
    case FolderCheck::State::OnlyHidden:
        return QStringLiteral("hidden");
    case FolderCheck::State::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QString filesText(int files)
{
    return files == 1 ? QCoreApplication::translate("App", "1 file")
                      : QCoreApplication::translate("App", "%1 files").arg(files);
}

/// "~/Downloads/x.torrent" for a path under the home folder.
QString homeShort(const QString &path)
{
    const QString home = QDir::homePath();
    if (!home.isEmpty() && path.startsWith(home + QLatin1Char('/')))
        return QStringLiteral("~") + path.mid(home.size());
    return path;
}

QStringList stringList(const QVariant &value)
{
    QStringList out;
    for (const QString &item : value.toStringList()) {
        const QString trimmed = item.trimmed();
        if (!trimmed.isEmpty() && !out.contains(trimmed))
            out << trimmed;
    }
    return out;
}

} // namespace

void App::setUpCreate()
{
    m_createJob = new CreateJob(m_client, this);
    m_createBandTimer = new QTimer(this);
    m_createBandTimer->setSingleShot(true);
    connect(m_createBandTimer, &QTimer::timeout, this, [this]() {
        if (m_createJob->isActive()) {
            m_createBandShown = true;
            updateCreateBand();
        }
    });
    connect(m_createJob, &CreateJob::changed, this, &App::createJobChanged);
    connect(m_createJob, &CreateJob::finished, this, &App::createJobFinished);
    connect(m_createJob, &CreateJob::taskStarted, this, [this](const QString &taskId) {
        // CR13: the task's id while it runs, so omaseed finds it again after a restart. The
        // choices that end it, never a tracker URL.
        QJsonObject server = createState();
        QJsonObject task = QJsonObject::fromVariantMap(m_createRequest);
        task.remove(QStringLiteral("announce"));
        task.remove(QStringLiteral("trackers"));
        task.insert(QStringLiteral("taskId"), taskId);
        task.insert(QStringLiteral("startedAt"), QDateTime::currentMSecsSinceEpoch());
        server.insert(QStringLiteral("task"), task);
        writeCreateState(server);
    });
    connect(this, &App::serverChanged, this, &App::switchCreateServer);
    connect(m_engine->globalState(), &core::GlobalState::apiVersionChanged, this, [this]() {
        Q_EMIT createChanged();
        resumeStoredCreate();
    });
    connect(m_engine, &core::SyncEngine::firstSyncFinished, this, &App::resumeStoredCreate);
    m_createServerId = m_hasServer ? m_server.id : QString();
}

bool App::createOffered() const
{
    if (m_demoMode)
        return true;
    return m_hasServer && m_server.isQbittorrent() &&
           core::QbtClient::supportsTorrentCreator(m_engine->globalState()->apiVersion());
}

QString App::createRunning() const
{
    if (m_createJob == nullptr || !m_createJob->isActive())
        return {};
    return m_createJob->request().name;
}

QJsonObject App::createState() const
{
    if (!m_hasServer)
        return {};
    return stateSection(kSection).value(m_server.id).toObject();
}

void App::writeCreateState(const QJsonObject &server)
{
    if (!m_hasServer)
        return;
    QJsonObject section = stateSection(kSection);
    section.insert(m_server.id, server);
    writeStateSection(kSection, section);
}

core::MadeHere App::madeHere() const
{
    return core::MadeHere::fromJson(createState().value(QStringLiteral("made")).toArray());
}

void App::writeMadeHere(const core::MadeHere &made)
{
    QJsonObject server = createState();
    server.insert(QStringLiteral("made"), made.toJson());
    writeCreateState(server);
}

void App::forgetMadeHere(const QByteArray &bytes)
{
    core::MadeHere made = madeHere();
    if (made.isEmpty() || bytes.isEmpty())
        return;
    made.forgetMatching(core::TorrentFile::parse(bytes), core::MadeHere::digestOf(bytes));
    writeMadeHere(made);
}

core::CreateTrackers App::createTrackers() const
{
    QList<core::CreateTrackers::Torrent> torrents;
    core::TorrentModel *model = m_engine->torrents();
    for (int row = 0; row < model->count(); ++row) {
        const QString hash =
            model->data(model->index(row), core::TorrentModel::HashRole).toString();
        const core::Torrent &torrent = model->torrent(hash);
        torrents.append({hash, torrent.name, torrent.isPrivate, torrent.addedOn});
    }
    return core::CreateTrackers(torrents, m_engine->trackers()->byUrl());
}

QVariantMap App::createSheet() const
{
    const core::CreateTrackers trackers = createTrackers();
    QVariantList publicTop;
    for (const auto &tracker : trackers.publicTop())
        publicTop.append(QVariantMap{{QStringLiteral("url"), tracker.url},
                                     {QStringLiteral("count"), tracker.count}});
    QVariantList hosts;
    const QLocale locale = QLocale::c();
    for (const auto &host : trackers.privateHosts()) {
        hosts.append(QVariantMap{
            {QStringLiteral("host"), host.host},
            {QStringLiteral("count"), host.count},
            {QStringLiteral("newestName"), host.newestName},
            {QStringLiteral("newestText"),
             host.newestAdded.isValid()
                 ? locale.toString(host.newestAdded.toLocalTime().date(), QStringLiteral("d MMM"))
                 : QString()},
            {QStringLiteral("masked"), core::AnnounceMask::maskUrl(host.announce)},
            {QStringLiteral("otherKeys"), host.otherKeys}});
    }
    const QJsonObject state = createState();
    const QString forWhom = state.value(QStringLiteral("for")).toString();
    return {
        {QStringLiteral("publicTrackers"), publicTop},
        {QStringLiteral("lastTrackers"),
         state.value(QStringLiteral("trackers")).toVariant().toStringList()},
        {QStringLiteral("privateHosts"), hosts},
        {QStringLiteral("forWhom"),
         forWhom == QLatin1String("private") ? forWhom : QStringLiteral("anyone")},
        {QStringLiteral("host"), state.value(QStringLiteral("host")).toString()},
        {QStringLiteral("seesDisks"), !m_demoMode && m_freeSpace.seesServerDisks()},
        {QStringLiteral("downloads"), homeShort(downloadsFolder())},
    };
}

void App::checkCreateFolder(const QString &path, bool ignoreDotfiles)
{
    const QString folder = path.trimmed();
    const QString serverName = m_hasServer ? m_server.name : QString();
    // The words under the field, and whether they stop the create (CR17, board 49 §7).
    const auto words = [folder, serverName](const FolderCheck &check) {
        QVariantMap map{{QStringLiteral("state"), stateName(check.state)},
                        {QStringLiteral("isFile"), check.isFile},
                        {QStringLiteral("sized"), check.sized},
                        {QStringLiteral("files"), check.files},
                        {QStringLiteral("bytes"), double(check.bytes)},
                        {QStringLiteral("hidden"), check.hidden},
                        {QStringLiteral("hiddenNames"), check.hiddenNames}};
        QString line;
        bool blocking = true;
        const QString name = core::CreatePlan::nameOf(folder);
        switch (check.state) {
        case FolderCheck::State::Ok: {
            blocking = false;
            if (!check.sized) {
                line = tr("%1 in it, listed by %2's qBittorrent · omaseed can't see %2's disks, "
                          "so the size shows once it is hashed")
                           .arg(check.files == 1 ? tr("1 name") : tr("%1 names").arg(check.files),
                                serverName);
                break;
            }
            QStringList parts;
            parts << (check.isFile ? tr("one file") : filesText(check.files))
                  << core::Format::size(check.bytes);
            if (check.hidden > 0)
                parts << (check.hidden == 1
                              ? tr("1 hidden file left out (%1)")
                                    .arg(check.hiddenNames.join(QStringLiteral(", ")))
                              : tr("%1 hidden files left out (%2)")
                                    .arg(check.hidden)
                                    .arg(check.hiddenNames.join(QStringLiteral(", "))));
            parts << tr("read here");
            line = parts.join(QStringLiteral(" · "));
            break;
        }
        case FolderCheck::State::Missing:
            line = tr("%1 isn't there on %2").arg(folder, serverName);
            break;
        case FolderCheck::State::Unreadable:
            line = check.unreadableFile.isEmpty()
                       ? (check.sized ? tr("qBittorrent can't read %1").arg(folder)
                                      : tr("qBittorrent lists nothing in %1: it is empty, or "
                                           "closed to %2's qBittorrent")
                                            .arg(folder, serverName))
                       : tr("qBittorrent can't read %1 in %2").arg(check.unreadableFile, name);
            break;
        case FolderCheck::State::Empty:
            line = check.sized ? tr("%1 has nothing to share").arg(folder)
                               : tr("qBittorrent lists nothing in %1: it is empty, or closed to "
                                    "%2's qBittorrent")
                                     .arg(folder, serverName);
            break;
        case FolderCheck::State::OnlyHidden:
            line = tr("%1 has nothing to share (%2 left out)")
                       .arg(folder,
                            check.hidden == 1 ? tr("1 hidden file")
                                              : tr("%1 hidden files").arg(check.hidden));
            break;
        case FolderCheck::State::Unknown:
            line = tr("%1 isn't answering: nothing can start; create when back").arg(serverName);
            break;
        }
        map.insert(QStringLiteral("line"), line);
        map.insert(QStringLiteral("blocking"), blocking);
        return map;
    };

    if (folder.isEmpty()) {
        Q_EMIT createFolderChecked(path, {});
        return;
    }
    if (m_demoMode) {
        FolderCheck check;
        check.state = FolderCheck::State::Ok;
        check.sized = true;
        check.files = 212;
        check.bytes = qint64(1800) * 1000 * 1000;
        check.hidden = 1;
        check.hiddenNames = {QStringLiteral(".DS_Store")};
        Q_EMIT createFolderChecked(path, words(check));
        return;
    }
    const QString local = m_freeSpace.localPathFor(folder);
    if (!local.isEmpty()) {
        // A big folder is a long walk: the pool reads it, and the answer hops back through
        // the application object, as the .torrent parse does (App.cpp).
        QPointer<App> self(this);
        const auto answer = words;
        QThreadPool::globalInstance()->start([self, path, local, ignoreDotfiles, answer]() {
            const FolderCheck check = core::CreatePlan::checkLocal(local, ignoreDotfiles);
            QCoreApplication *application = QCoreApplication::instance();
            if (application == nullptr)
                return;
            QMetaObject::invokeMethod(application, [self, path, check, answer]() {
                if (!self.isNull())
                    Q_EMIT self->createFolderChecked(path, answer(check));
            });
        });
        return;
    }
    // A server whose disks this machine can't see: its own list, names only, one level (C16
    // L13). qBittorrent answers 404 for a missing folder and for a file alike, and [] for an
    // empty folder and one closed to it alike; the parent's list tells a file.
    m_client->appDirectoryContent(folder)->then(
        this, [=, this](const core::QbtResult<QJsonArray> &result) {
            if (!result.isError()) {
                QStringList names;
                for (const QJsonValue &value : result.value())
                    names << value.toString();
                Q_EMIT createFolderChecked(
                    path, words(core::CreatePlan::fromListing(true, names, ignoreDotfiles)));
                return;
            }
            if (result.error().httpStatus() != 404) {
                Q_EMIT createFolderChecked(path, words(FolderCheck()));
                return;
            }
            m_client->appDirectoryContent(core::CreatePlan::parentOf(folder))
                ->then(this, [=, this](const core::QbtResult<QJsonArray> &parent) {
                    FolderCheck check;
                    check.state = FolderCheck::State::Missing;
                    if (!parent.isError() && parent.value().contains(QJsonValue(folder))) {
                        check.state = FolderCheck::State::Ok;
                        check.isFile = true;
                        check.files = 1;
                    }
                    Q_EMIT createFolderChecked(path, words(check));
                });
        });
}

QString App::pieceSizeText(double bytes) const
{
    const qint64 size = qint64(bytes);
    if (size >= 1024 * 1024)
        return tr("%1 MiB").arg(size / (1024 * 1024));
    return tr("%1 KiB").arg(size / 1024);
}

QVariantList App::createPieceRows(double bytes, int files) const
{
    QVariantList rows;
    const QLocale locale = QLocale::c();
    for (const auto &row : core::CreatePlan::pieceRows(qint64(bytes), files)) {
        rows.append(QVariantMap{
            {QStringLiteral("size"), double(row.size)},
            {QStringLiteral("text"), pieceSizeText(double(row.size))},
            {QStringLiteral("countText"),
             row.count == 1 ? tr("1 piece") : tr("%1 pieces").arg(locale.toString(row.count))},
            {QStringLiteral("torrentText"), core::Format::size(row.torrentBytes)},
            {QStringLiteral("automatic"), row.automatic}});
    }
    return rows;
}

void App::listServerFolder(const QString &path)
{
    if (m_demoMode || !m_hasServer) {
        Q_EMIT serverFolderListed(path, {}, false);
        return;
    }
    m_client->appDirectoryContent(path)->then(
        this, [=, this](const core::QbtResult<QJsonArray> &result) {
            QStringList entries;
            if (!result.isError())
                for (const QJsonValue &value : result.value())
                    entries << value.toString();
            entries.sort(Qt::CaseInsensitive);
            Q_EMIT serverFolderListed(path, entries, !result.isError());
        });
}

void App::loadCreateSource(const QString &host)
{
    // "the last typed for it" first (C27 CR6).
    const QJsonObject typed = createState().value(QStringLiteral("sources")).toObject();
    if (typed.contains(host)) {
        Q_EMIT createSourceLoaded(host, typed.value(host).toString());
        return;
    }
    const auto found = createTrackers().privateHost(host);
    if (!found || m_demoMode) {
        Q_EMIT createSourceLoaded(host, QString());
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hash"), found->newestHash);
    m_client->get(QStringLiteral("torrents/export"), query)
        ->then(this, [=, this](const core::QbtResult<QByteArray> &result) {
            const QString source =
                result.isError() ? QString() : core::TorrentFile::parse(result.value()).source;
            Q_EMIT createSourceLoaded(host, source);
        });
}

QString App::maskAnnounce(const QString &url) const
{
    return core::AnnounceMask::maskUrl(url);
}

QString App::droppedFolder(const QList<QUrl> &urls) const
{
    if (urls.size() != 1 || !urls.first().isLocalFile())
        return {};
    const QFileInfo info(urls.first().toLocalFile());
    // A .torrent goes to the add sheet, as before.
    if (!info.exists() || (info.isFile() && info.suffix().compare(QStringLiteral("torrent"),
                                                                  Qt::CaseInsensitive) == 0))
        return {};
    if (!info.isDir())
        return {};
    return serverPathForPick(info.absoluteFilePath()).value(QStringLiteral("path")).toString();
}

bool App::startCreate(const QVariantMap &request)
{
    if (!createOffered() || (m_createJob->isActive()))
        return false;
    const QString folder = request.value(QStringLiteral("folder")).toString().trimmed();
    if (folder.isEmpty())
        return false;
    const bool isPrivate =
        request.value(QStringLiteral("forWhom")).toString() == QLatin1String("private");
    const QString host = request.value(QStringLiteral("host")).toString();

    CreateJob::Request job;
    job.name = core::CreatePlan::nameOf(folder);
    job.task.sourcePath = folder;
    job.task.format = request.value(QStringLiteral("format"), QStringLiteral("v1")).toString();
    job.task.pieceSize = qint64(request.value(QStringLiteral("pieceSize")).toDouble());
    job.task.isPrivate = isPrivate;
    job.task.ignoreDotfiles = request.value(QStringLiteral("ignoreDotfiles"), true).toBool();
    job.task.urlSeeds = stringList(request.value(QStringLiteral("webSeeds")));
    job.task.comment = request.value(QStringLiteral("comment")).toString().trimmed();
    job.addAfter = !isPrivate;
    job.category = isPrivate ? QString() : request.value(QStringLiteral("category")).toString();
    job.apiVersion = m_engine->globalState()->apiVersion();
    job.totalBytes = request.value(QStringLiteral("totalBytes"), -1).toLongLong();
    if (isPrivate) {
        // CR5: a pasted URL for this creation only; else the newest torrent's from the host.
        QString announce = request.value(QStringLiteral("announce")).toString().trimmed();
        if (announce.isEmpty()) {
            const auto found = createTrackers().privateHost(host);
            if (found)
                announce = found->announce;
        }
        if (announce.isEmpty())
            return false;
        job.task.trackers = {announce};
        job.task.source = request.value(QStringLiteral("source")).toString().trimmed();
    } else {
        job.task.trackers = stringList(request.value(QStringLiteral("trackers")));
    }

    // What the sheet keeps for next time, per server (CR2, CR4, CR6): never an announce URL
    // of a private tracker.
    QJsonObject server = createState();
    server.insert(QStringLiteral("for"),
                  isPrivate ? QStringLiteral("private") : QStringLiteral("anyone"));
    if (isPrivate) {
        server.insert(QStringLiteral("host"), host);
        QJsonObject sources = server.value(QStringLiteral("sources")).toObject();
        if (request.value(QStringLiteral("sourceTyped")).toBool())
            sources.insert(host, job.task.source);
        server.insert(QStringLiteral("sources"), sources);
    } else {
        server.insert(QStringLiteral("trackers"), QJsonArray::fromStringList(job.task.trackers));
    }
    writeCreateState(server);

    m_createRequest = request;
    m_createRequest.remove(QStringLiteral("announce"));
    m_createServerId = m_hasServer ? m_server.id : QString();
    m_createBandShown = false;
    m_createSavedPath.clear();
    m_createMagnet.clear();
    m_createHash.clear();
    m_createSaveError.clear();
    m_createBand.clear();
    if (m_demoMode) {
        qInfo().noquote() << QStringLiteral("demo: would create %1").arg(job.name);
        return true;
    }
    qInfo().noquote() << QStringLiteral("create: %1 (%2, %3)")
                             .arg(job.name,
                                  isPrivate ? QStringLiteral("private") : QStringLiteral("public"),
                                  job.task.format);
    m_createBandTimer->start(m_createBandMs);
    m_createJob->start(job);
    return true;
}

void App::resumeStoredCreate()
{
    if (m_demoMode || !m_hasServer || m_createJob->isActive() || !createOffered())
        return;
    const QJsonObject task = createState().value(QStringLiteral("task")).toObject();
    const QString taskId = task.value(QStringLiteral("taskId")).toString();
    if (taskId.isEmpty())
        return;
    const QVariantMap request = task.toVariantMap();
    CreateJob::Request job;
    const QString folder = request.value(QStringLiteral("folder")).toString();
    const bool isPrivate =
        request.value(QStringLiteral("forWhom")).toString() == QLatin1String("private");
    job.name = core::CreatePlan::nameOf(folder);
    job.task.sourcePath = folder;
    job.task.format = request.value(QStringLiteral("format"), QStringLiteral("v1")).toString();
    job.task.isPrivate = isPrivate;
    job.addAfter = !isPrivate;
    job.category = isPrivate ? QString() : request.value(QStringLiteral("category")).toString();
    job.apiVersion = m_engine->globalState()->apiVersion();
    job.totalBytes = request.value(QStringLiteral("totalBytes"), -1).toLongLong();
    m_createRequest = request;
    m_createRequest.remove(QStringLiteral("taskId"));
    m_createRequest.remove(QStringLiteral("startedAt"));
    m_createServerId = m_server.id;
    m_createBandShown = true; // it ran while omaseed was away: the band says so at once
    m_createSavedPath.clear();
    m_createMagnet.clear();
    m_createHash.clear();
    m_createSaveError.clear();
    qInfo().noquote() << QStringLiteral("create: taking up %1 again").arg(job.name);
    const qint64 ranMs = QDateTime::currentMSecsSinceEpoch() -
                         qint64(task.value(QStringLiteral("startedAt")).toDouble());
    m_createJob->resume(job, taskId, qMax<qint64>(0, ranMs));
}

void App::switchCreateServer()
{
    // The job and its band belong to the server it runs on; its id waits in that server's
    // state for the way back (resumeStoredCreate).
    if (m_createJob != nullptr && m_createJob->isActive()) {
        m_createJob->deleteLater();
        m_createJob = new CreateJob(m_client, this);
        connect(m_createJob, &CreateJob::changed, this, &App::createJobChanged);
        connect(m_createJob, &CreateJob::finished, this, &App::createJobFinished);
    }
    m_createBandTimer->stop();
    m_createBand.clear();
    m_createServerId = m_hasServer ? m_server.id : QString();
    Q_EMIT createChanged();
}

void App::setCreateTimings(int pollMs, int bandMs)
{
    m_createJob->setPollInterval(std::chrono::milliseconds(pollMs));
    m_createBandMs = bandMs;
}

void App::createJobChanged()
{
    if (m_createJob->isActive() && m_createBandShown)
        updateCreateBand();
    else
        Q_EMIT createChanged();
}

void App::createJobFinished()
{
    m_createBandTimer->stop();
    // The task is over: nothing to take up again.
    QJsonObject server = createState();
    server.remove(QStringLiteral("task"));
    writeCreateState(server);

    const CreateJob::State state = m_createJob->state();
    const CreateJob::Request &request = m_createJob->request();
    const QString name = request.name;
    const bool isPrivate = request.task.isPrivate;

    if (state == CreateJob::State::Cancelled) {
        // Board 49 §7: a dim strip, no band.
        m_createBand.clear();
        m_outcomes->reportWords(
            {{QStringLiteral("kind"), QStringLiteral("dim")},
             {QStringLiteral("mark"), QStringLiteral("×")},
             {QStringLiteral("headline"),
              tr("%1 wasn't made: cancelled at %2 %").arg(name).arg(m_createJob->progress())},
             {QStringLiteral("line"), tr("nothing saved, nothing added, the folder is as it was")},
             {QStringLiteral("more"),
              QVariantMap{{QStringLiteral("id"), QStringLiteral("createAgain")},
                          {QStringLiteral("text"), tr("create again…")}}}});
        Q_EMIT createChanged();
        return;
    }
    if (state == CreateJob::State::Done) {
        const core::TorrentFile &torrent = m_createJob->torrent();
        m_createHash = torrent.torrentId();
        if (isPrivate) {
            finishPrivateCreate();
        } else {
            m_createMagnet = torrent.magnetLink();
            if (m_createJob->failure() == CreateJob::Failure::None) {
                copyToClipboard(m_createMagnet);
                // CR13, C16 L6–L7: under 3 s there is no band, only the strip.
                if (!m_createBandShown) {
                    m_outcomes->reportWords(
                        {{QStringLiteral("kind"), QStringLiteral("done")},
                         {QStringLiteral("mark"), QStringLiteral("✓")},
                         {QStringLiteral("headline"),
                          tr("%1 is made and seeds from %2")
                              .arg(name, core::CreatePlan::parentOf(request.task.sourcePath))},
                         {QStringLiteral("line"),
                          tr("%1, %2 · the magnet is copied")
                              .arg(filesText(int(torrent.files.size())),
                                   core::Format::size(torrent.totalSize))},
                         {QStringLiteral("doneHashes"), QStringList{m_createHash}},
                         {QStringLiteral("hashes"), QStringList{m_createHash}},
                         {QStringLiteral("doneRow"), tr("made here")}});
                    m_createBand.clear();
                    Q_EMIT createChanged();
                    return;
                }
            }
        }
    }
    m_createBandShown = true;
    updateCreateBand();
    // C16 L8 for a creation: one notice when its band showed a minute or the window was hidden.
    const bool hidden = !m_engine->isVisible();
    if (!m_notifier.isNull() && (hidden || m_createJob->elapsedMs() > 60000)) {
        m_notifier->notifyCreate(m_createServerId,
                                 m_createBand.value(QStringLiteral("headline")).toString(),
                                 m_createBand.value(QStringLiteral("line")).toString());
    }
}

QString App::saveCreatedTorrent(const QString &fileName, QString *error) const
{
    const QString dir = downloadsFolder();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        *error = tr("there is no Downloads folder to save it in");
        return {};
    }
    const QString path = QDir(dir).filePath(core::CreatePlan::freeName(dir, fileName));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(m_createJob->bytes()) < 0 ||
        !file.commit()) {
        *error = file.errorString();
        return {};
    }
    return path;
}

void App::finishPrivateCreate()
{
    const CreateJob::Request &request = m_createJob->request();
    const QString host = m_createRequest.value(QStringLiteral("host")).toString();
    // CR15: "NAME [HOST].torrent" in this machine's Downloads; CR7: not added, the folder
    // remembered.
    const QString hostName =
        host.isEmpty() ? core::trackerHost(m_createJob->torrent().trackers().value(0)) : host;
    QString error;
    m_createSavedPath =
        saveCreatedTorrent(core::CreatePlan::savedName(request.name, hostName), &error);
    m_createSaveError = error;
    if (m_createSavedPath.isEmpty())
        return;
    core::MadeHere made = madeHere();
    made.dropExpired(QDateTime::currentDateTimeUtc());
    made.add(core::MadeHere::Record::of(m_createJob->torrent(),
                                        m_createJob->bytes(),
                                        request.task.sourcePath,
                                        hostName,
                                        QDateTime::currentDateTimeUtc()));
    writeMadeHere(made);
}

void App::updateCreateBand()
{
    QVariantMap band;
    const CreateJob::Request &request = m_createJob->request();
    const QString name = request.name;
    const QString serverName = m_hasServer ? m_server.name : QString();
    const bool isPrivate = request.task.isPrivate;
    const QString host = m_createRequest.value(QStringLiteral("host")).toString();
    const auto action = [](const QString &id, const QString &text) {
        return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("text"), text}};
    };
    QVariantList actions;
    QString kind = QStringLiteral("accent");
    QString glyph = QStringLiteral("◇");
    QString headline;
    QStringList line;
    double progress = -1;
    const core::TorrentFile &torrent = m_createJob->torrent();
    const QString pieces =
        m_createJob->pieceSize() > 0
            ? tr("%1 pieces").arg(pieceSizeText(double(m_createJob->pieceSize())))
            : QString();

    switch (m_createJob->state()) {
    case CreateJob::State::Idle:
    case CreateJob::State::Cancelled:
        m_createBand.clear();
        Q_EMIT createChanged();
        return;
    case CreateJob::State::Starting:
    case CreateJob::State::Queued:
    case CreateJob::State::Hashing:
    case CreateJob::State::Fetching:
    case CreateJob::State::Adding: {
        headline = isPrivate && !host.isEmpty() ? tr("Creating %1 for %2").arg(name, host)
                                                : tr("Creating %1").arg(name);
        const int percent = m_createJob->progress();
        if (m_createJob->state() == CreateJob::State::Queued ||
            m_createJob->state() == CreateJob::State::Starting) {
            line << tr("waiting for %1's qBittorrent to start it").arg(serverName);
        } else if (m_createJob->state() == CreateJob::State::Fetching ||
                   m_createJob->state() == CreateJob::State::Adding) {
            line << (isPrivate ? tr("hashed · saving the .torrent")
                               : tr("hashed · adding it to %1").arg(serverName));
        } else if (request.totalBytes > 0) {
            line << tr("hashing %1 of %2 on %3 · %4 %")
                        .arg(core::Format::size(request.totalBytes * percent / 100),
                             core::Format::size(request.totalBytes),
                             serverName)
                        .arg(percent);
            const qint64 left = core::CreatePlan::secondsLeft(percent, m_createJob->elapsedMs());
            if (left >= 0)
                line << tr("about %1 left").arg(core::Format::duration(left));
        } else {
            line << tr("%1 % hashed on %2").arg(percent).arg(serverName);
        }
        progress = m_createJob->state() == CreateJob::State::Hashing ? percent / 100.0 : -1;
        line << request.task.format;
        const qint64 chosen =
            request.task.pieceSize > 0
                ? request.task.pieceSize
                : (request.totalBytes > 0 ? core::CreatePlan::autoPieceSize(request.totalBytes)
                                          : 0);
        line << (chosen > 0 ? tr("%1 pieces").arg(pieceSizeText(double(chosen)))
                            : tr("pieces chosen when it ends"));
        line << (isPrivate ? tr("then saved, not added") : tr("then it seeds from where it is"));
        actions << action(QStringLiteral("cancel"), tr("cancel"));
        break;
    }
    case CreateJob::State::Done:
        if (isPrivate) {
            if (m_createSavedPath.isEmpty()) {
                kind = QStringLiteral("red");
                glyph = QStringLiteral("✕");
                headline = tr("%1 was made, but couldn't be saved").arg(name);
                line << m_createSaveError;
                actions << action(QStringLiteral("again"), tr("try again"));
            } else {
                kind = QStringLiteral("green");
                glyph = QStringLiteral("✓");
                const QFileInfo saved(m_createSavedPath);
                headline = tr("%1 is ready to upload").arg(saved.fileName());
                line << tr("saved in %1").arg(homeShort(saved.path()))
                     << tr("not added: %1 doesn't know it yet").arg(host)
                     << tr("when the site's copy comes back, add it and it seeds from %1")
                            .arg(request.task.sourcePath);
                band.insert(
                    QStringLiteral("file"),
                    QVariantMap{{QStringLiteral("path"), m_createSavedPath},
                                {QStringLiteral("name"), saved.fileName()},
                                {QStringLiteral("sizeText"), core::Format::size(saved.size())}});
                actions << action(QStringLiteral("showFile"), tr("show the file"))
                        << action(QStringLiteral("copyPath"), tr("copy path"));
            }
        } else if (m_createJob->failure() == CreateJob::Failure::AddFailed) {
            // C27 §7: the add's own words; the .torrent is kept.
            kind = QStringLiteral("yellow");
            glyph = QStringLiteral("!");
            headline = tr("%1 is made, but %2 didn't add it").arg(name, serverName);
            line << m_createJob->errorText();
            line << (m_createSavedPath.isEmpty()
                         ? tr("the .torrent is kept: save it to add it yourself")
                         : tr("saved in %1").arg(homeShort(m_createSavedPath)));
            if (m_createSavedPath.isEmpty())
                actions << action(QStringLiteral("saveTorrent"), tr("save .torrent"));
            else
                actions << action(QStringLiteral("showFile"), tr("show the file"));
        } else {
            kind = QStringLiteral("green");
            glyph = QStringLiteral("✓");
            headline = tr("%1 is made and seeds from %2")
                           .arg(name, core::CreatePlan::parentOf(request.task.sourcePath));
            line << request.task.format;
            if (!pieces.isEmpty())
                line << pieces;
            line << tr("%1, %2").arg(filesText(int(torrent.files.size())),
                                     core::Format::size(torrent.totalSize));
            line << tr("hashed in %1")
                        .arg(core::Format::duration(
                            qMax<qint64>(1, (m_createJob->tookMs() + 500) / 1000)));
            line << tr("the magnet is copied");
            line << (m_createSavedPath.isEmpty()
                         ? tr("keep the files there: it seeds from them")
                         : tr("saved in %1").arg(homeShort(m_createSavedPath)));
            actions << action(QStringLiteral("copyMagnet"), tr("copy magnet"));
            if (m_createSavedPath.isEmpty())
                actions << action(QStringLiteral("saveTorrent"), tr("save .torrent"));
            else
                actions << action(QStringLiteral("showFile"), tr("show the file"));
            actions << action(QStringLiteral("show"), tr("show"));
        }
        actions << action(QStringLiteral("close"), tr("close"));
        break;
    case CreateJob::State::Failed: {
        kind = QStringLiteral("red");
        glyph = QStringLiteral("✕");
        headline = tr("%1 wasn't made").arg(name);
        const QString reason = m_createJob->errorText();
        switch (m_createJob->failure()) {
        case CreateJob::Failure::Restarted:
            line << tr("%1 restarted while hashing").arg(serverName);
            break;
        case CreateJob::Failure::Refused:
            line << tr("%1 refused it: %2").arg(serverName, reason);
            break;
        case CreateJob::Failure::Lost:
            line << tr("the .torrent couldn't be fetched: %1").arg(reason);
            break;
        default:
            line << (reason.startsWith(QStringLiteral("Permission denied"))
                         ? tr("qBittorrent couldn't read a file in it: permission denied")
                         : tr("qBittorrent said: %1").arg(reason));
            break;
        }
        line << tr("nothing was saved, the folder is as it was");
        actions << action(QStringLiteral("again"), tr("create again…"))
                << action(QStringLiteral("close"), tr("close"));
        break;
    }
    }
    band.insert(QStringLiteral("kind"), kind);
    band.insert(QStringLiteral("glyph"), glyph);
    band.insert(QStringLiteral("headline"), headline);
    band.insert(QStringLiteral("line"),
                core::AnnounceMask::maskText(line.join(QStringLiteral(" · "))));
    band.insert(QStringLiteral("progress"), progress);
    band.insert(QStringLiteral("actions"), actions);
    m_createBand = band;
    Q_EMIT createChanged();
}

void App::createAction(const QString &id)
{
    if (id == QLatin1String("cancel")) {
        m_createJob->cancel();
    } else if (id == QLatin1String("copyMagnet")) {
        copyToClipboard(m_createMagnet);
    } else if (id == QLatin1String("saveTorrent")) {
        const QString name = m_createJob->request().name + QStringLiteral(".torrent");
        QString error;
        m_createSavedPath = saveCreatedTorrent(name, &error);
        m_createSaveError = error;
        updateCreateBand();
    } else if (id == QLatin1String("show")) {
        if (!m_createHash.isEmpty())
            Q_EMIT showTorrentRequested(m_createHash);
    } else if (id == QLatin1String("showFile")) {
        if (!m_xdg.isNull() && !m_createSavedPath.isEmpty())
            m_xdg->showInFileManager(m_createSavedPath);
    } else if (id == QLatin1String("copyPath")) {
        copyToClipboard(m_createSavedPath);
    } else if (id == QLatin1String("again") || id == QLatin1String("createAgain")) {
        m_createBand.clear();
        m_outcomes->dismiss();
        Q_EMIT createChanged();
        Q_EMIT createAgainRequested(m_createRequest);
    } else if (id == QLatin1String("close")) {
        m_createBand.clear();
        Q_EMIT createChanged();
    }
}

QVariantMap App::madeHereFor(const QString &token) const
{
    const PendingTorrentFile pending = m_pendingTorrentFiles.value(token);
    if (pending.bytes.isEmpty() || !m_hasServer)
        return {};
    const core::MadeHere made = madeHere();
    if (made.isEmpty())
        return {};
    const core::TorrentFile file = core::TorrentFile::parse(pending.bytes);
    if (!file.isValid())
        return {};
    const core::MadeHere::Match match = made.match(file, core::MadeHere::digestOf(pending.bytes));
    if (match.kind == core::MadeHere::Kind::None)
        return {};
    const core::MadeHere::Record &record = match.record;
    // Where the files are: the folder's parent holds a folder of the torrent's name; a site
    // that renamed the torrent gets the folder itself, without a subfolder.
    QString savePath = core::CreatePlan::parentOf(record.folder);
    QString layout = QStringLiteral("Original");
    if (record.multiFile && file.multiFile &&
        file.name != core::CreatePlan::nameOf(record.folder)) {
        savePath = record.folder;
        layout = QStringLiteral("NoSubfolder");
    }
    const QDateTime at = record.madeAt.toLocalTime();
    const QString madeText =
        at.date() == QDate::currentDate()
            ? tr("made here today %1")
                  .arg(QLocale::c().toString(at.time(), QStringLiteral("HH:mm")))
            : tr("made here %1").arg(QLocale::c().toString(at.date(), QStringLiteral("d MMM")));
    const QString madeId = core::TorrentFile::torrentIdFor(record.infoHashV1, record.infoHashV2);
    const bool inLibrary = m_engine->torrents()->indexOfHash(madeId) >= 0;
    const QString kind = match.kind == core::MadeHere::Kind::TheMadeFile ? QStringLiteral("made")
                         : match.kind == core::MadeHere::Kind::SameHash  ? QStringLiteral("same")
                                                                         : QStringLiteral("files");
    return {
        {QStringLiteral("kind"), kind},
        {QStringLiteral("name"), record.name},
        {QStringLiteral("folder"), record.folder},
        {QStringLiteral("host"), record.host},
        {QStringLiteral("madeText"), madeText},
        {QStringLiteral("fileCount"), int(record.files.size())},
        {QStringLiteral("savePath"), savePath},
        {QStringLiteral("layout"), layout},
        {QStringLiteral("check"), match.kind != core::MadeHere::Kind::SameHash},
        // What the site changed inside the torrent, when another hash says it did.
        {QStringLiteral("changedText"),
         match.kind != core::MadeHere::Kind::SameFiles || record.source == file.source ? QString()
         : record.source.isEmpty() ? tr("the site added the source tag %1").arg(file.source)
         : file.source.isEmpty()
             ? tr("the site took the source tag %1 out").arg(record.source)
             : tr("its source tag %1 became %2").arg(record.source, file.source)},
        {QStringLiteral("madeHash"), inLibrary ? madeId : QString()},
        {QStringLiteral("madeName"),
         inLibrary ? m_engine->torrents()->torrent(madeId).name : QString()},
    };
}

} // namespace omaseed::app
