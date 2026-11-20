// App's room for one add (#275, board 64; DECISIONS C47 I8–I12, on the facts #271 checked): the
// server's temp folder and Save to, each with its drive's room and what the downloads in
// progress there still take, and the add's fields that send a torrent past the temp folder or
// through it. Kept out of App.cpp so it reads as one piece; the declarations sit at the end of
// App.hpp.
#include "ui/app/App.hpp"

#include "core/PromisedRoom.hpp"
#include "core/TempFolder.hpp"
#include "core/engines/qbt/ServerPreferences.hpp"

#include <QDir>

namespace omaseed::app {

using namespace omaseed::core;

namespace {

QString sourceWord(FreeSpaceProbe::Source source)
{
    switch (source) {
    case FreeSpaceProbe::Source::Default:
        return QStringLiteral("default");
    case FreeSpaceProbe::Source::Local:
        return QStringLiteral("local");
    case FreeSpaceProbe::Source::Server:
        return QStringLiteral("server");
    case FreeSpaceProbe::Source::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QString withoutTrailingSlash(QString path)
{
    while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

} // namespace

void App::setStorageReaderForTests(StorageReader reader)
{
    m_freeSpace = FreeSpaceProbe(std::move(reader));
    updateFreeSpace();
}

EngineKind App::addEngine() const
{
    if (m_server.engine != EngineKind::Unknown)
        return m_server.engine;
    return m_demoMode || qbtEngine() ? EngineKind::Qbittorrent : EngineKind::Unknown;
}

void App::applyTempFolder(AddOptions &options, const QVariantMap &request) const
{
    if (!m_features.tempFolderPerAdd)
        return; // Transmission: session-wide, nothing to send (#271 (5))
    const bool straight = request.contains(QStringLiteral("useTemp")) &&
                          !request.value(QStringLiteral("useTemp")).toBool();
    const EngineKind engine = addEngine();
    if (engine == EngineKind::Qbittorrent) {
        // #271 (1): useDownloadPath=false is straight to Save to; on sends nothing, so the
        // server (or the category, under autoTMM) picks the temp folder as it does today.
        if (straight)
            options.useDownloadPath = false;
        return;
    }
    if (engine != EngineKind::Deluge)
        return;
    const QVariantMap prefs = m_prefs->values();
    const TempFolder temp = TempFolders::fromPreferences(prefs, engine);
    if (!temp.on)
        return; // download_location = Save to is where it ends
    // #271 (4), #277: with move_completed on, download_location is the temp folder and
    // move_completed_path where it ends, so Save to goes in the latter.
    if (straight) {
        options.useDownloadPath = false;
        if (!options.savePath)
            options.savePath = TempFolders::defaultSavePath(prefs, engine);
        return;
    }
    if (!options.savePath)
        return; // the session's three values already end in its move_completed_path
    options.useDownloadPath = true;
    options.downloadPath = temp.path;
}

QVariantMap
App::roomFor(const QString &savePath, const QString &category, bool autoTMM, bool useTemp) const
{
    const EngineKind engine = addEngine();
    const QVariantMap prefs = m_prefs->values();
    const TempFolder session = TempFolders::fromPreferences(prefs, engine);

    // Where it ends: Save to, or what the server picks for an empty one.
    QString end = withoutTrailingSlash(savePath.trimmed());
    if (end.isEmpty()) {
        end = engine == EngineKind::Qbittorrent && autoTMM && !category.isEmpty()
                  ? autoManagedFolder(category)
                  : withoutTrailingSlash(TempFolders::defaultSavePath(prefs, engine));
    }

    // Where it downloads first. I12, #271 (2): a qBittorrent category's own folder, none, or
    // the session's rule (temp_path/<category>) — only under autoTMM: a typed Save to keeps
    // the session's temp_path.
    bool hasTemp = session.on;
    QString tempPath = withoutTrailingSlash(session.path);
    QString rule = QStringLiteral("session");
    if (engine == EngineKind::Qbittorrent && autoTMM && !category.isEmpty()) {
        const QVariantMap own = m_engine->categories()->tempFolderOf(category);
        rule = own.value(QStringLiteral("rule")).toString();
        if (rule == QLatin1String("own")) {
            hasTemp = true;
            const QString path = own.value(QStringLiteral("path")).toString();
            tempPath = QDir::isAbsolutePath(path) || session.path.isEmpty()
                           ? withoutTrailingSlash(path)
                           : withoutTrailingSlash(session.path) + QLatin1Char('/') + path;
        } else if (rule == QLatin1String("none")) {
            hasTemp = false;
            tempPath.clear();
        } else if (hasTemp) {
            tempPath += QLatin1Char('/') + category;
        }
    }
    if (!hasTemp)
        tempPath.clear();
    const bool switchable = hasTemp && m_features.tempFolderPerAdd;

    // I10: what downloads in progress still take, per drive, where omaseed reads the drive;
    // nothing when the server makes each file at its full size on the add.
    const bool preallocates = TempFolders::preallocates(prefs, engine);
    QHash<quint64, PromisedRoom::Promise> promised;
    if (!preallocates) {
        QList<Torrent> torrents;
        const TorrentModel *model = m_engine->torrents();
        torrents.reserve(model->rowCount());
        for (int row = 0; row < model->rowCount(); ++row)
            torrents.append(model->torrentAt(row));
        promised = PromisedRoom::byDevice(
            torrents, [this](const QString &path) { return m_freeSpace.answerLocal(path).device; });
    }

    const qint64 defaultFree = m_engine->globalState()->freeSpaceOnDisk();
    const auto lineOf = [&](const QString &path,
                            const FreeSpaceProbe::Answer &answer,
                            quint64 device) -> QVariantMap {
        const PromisedRoom::Promise promise =
            device != 0 ? promised.value(device) : PromisedRoom::Promise();
        return {
            {QStringLiteral("path"), path},
            {QStringLiteral("source"), sourceWord(answer.source)},
            {QStringLiteral("bytesFree"), answer.bytesFree},
            {QStringLiteral("bytesTotal"), answer.bytesTotal},
            {QStringLiteral("where"), answer.where},
            {QStringLiteral("defaultFree"), defaultFree},
            {QStringLiteral("fileSystem"), answer.fileSystem},
            {QStringLiteral("allocatesInFull"), FileSystems::allocatesInFull(answer.fileSystem)},
            {QStringLiteral("fat32"), FileSystems::isFat32(answer.fileSystem)},
            {QStringLiteral("promised"), promise.bytes},
            {QStringLiteral("promisedCount"), promise.count},
        };
    };

    // The second line keeps today's answer; the first reads only this machine's look (#271 (3)).
    const FreeSpaceProbe::Answer saveLocal = m_freeSpace.answerLocal(end);
    const FreeSpaceProbe::Answer saveAnswer = m_freeSpace.answer(end);
    QVariantMap room{
        {QStringLiteral("hasTemp"), hasTemp},
        {QStringLiteral("switchable"), switchable},
        {QStringLiteral("useTemp"), hasTemp && (useTemp || !switchable)},
        {QStringLiteral("tempPath"), tempPath},
        {QStringLiteral("tempRule"), rule},
        {QStringLiteral("savePath"), end},
        {QStringLiteral("save"), lineOf(end, saveAnswer, saveLocal.device)},
        {QStringLiteral("preallocates"), preallocates},
        {QStringLiteral("engine"), engineKindToString(engine)},
    };
    if (hasTemp) {
        const FreeSpaceProbe::Answer tempLocal = m_freeSpace.answerLocal(tempPath);
        room.insert(QStringLiteral("temp"), lineOf(tempPath, tempLocal, tempLocal.device));
        room.insert(QStringLiteral("sameDrive"), FreeSpaceProbe::sameDrive(tempLocal, saveLocal));
    } else {
        room.insert(QStringLiteral("temp"), QVariantMap());
        room.insert(QStringLiteral("sameDrive"), false);
    }
    return room;
}

void App::followPromisedRoom()
{
    // One number for what the downloads in progress promise and where: the free lines are read
    // again only when it moves (C47 I10), not on every poll.
    quint64 signature = 0;
    const TorrentModel *model = m_engine->torrents();
    for (int row = 0; row < model->rowCount(); ++row) {
        const Torrent &torrent = model->torrentAt(row);
        if (!PromisedRoom::isPromising(torrent))
            continue;
        // In 64 MiB steps: a download's every second is no reason to read the drives again.
        signature = qHashMulti(
            signature, torrent.hash, torrent.amountLeft >> 26, PromisedRoom::writesTo(torrent));
    }
    if (signature == m_promisedSignature)
        return;
    m_promisedSignature = signature;
    ++m_freeSpaceRevision;
    Q_EMIT freeSpaceChanged();
}

} // namespace omaseed::app
