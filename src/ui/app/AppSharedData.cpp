// App's #28 block (docs/26): which torrents use the same data on disk, for the delete
// confirm, the location sheet and a category change. Kept out of App.cpp so it reads as one
// piece; the declarations sit at the end of App.hpp.
#include "ui/app/App.hpp"

#include "core/Format.hpp"
#include "core/SharedData.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

namespace omaseed::app {

using namespace omaseed::core;

namespace {

/// "tracker.b.example" from an announce URL; the URL itself when it has no host.
QString trackerHost(const QString &url)
{
    if (url.isEmpty())
        return QString();
    const QString host = QUrl(url).host();
    return host.isEmpty() ? url : host;
}

} // namespace

QVariantMap App::sharedData(const QStringList &hashes, bool fresh)
{
    if (fresh)
        m_sharedFiles.clear();

    TorrentModel *model = m_engine->torrents();
    QList<SharedDataRow> library;
    library.reserve(model->count());
    for (int row = 0; row < model->count(); ++row) {
        const Torrent &torrent = model->torrentAt(row);
        library.append({torrent.hash, torrent.savePath, torrent.contentPath, torrent.size});
    }

    const SharedData::Result found = SharedData::find(hashes, library, m_sharedFiles);

    // The no-root case: fetch what is missing, once, and say when it is in.
    bool pending = false;
    for (const QString &hash : found.needFiles) {
        if (m_demoMode) {
            // Demo rows have no server to ask. A list a test gave (`patchDemoFiles`) comes in
            // on a later turn of the event loop, as a reply would; without one, what cannot be
            // settled shares nothing.
            if (!m_demoSharedFiles.contains(hash))
                continue;
            pending = true;
            if (m_sharedFilesInFlight.contains(hash))
                continue;
            m_sharedFilesInFlight.insert(hash);
            QTimer::singleShot(0, this, [this, hash] {
                m_sharedFilesInFlight.remove(hash);
                m_sharedFiles.insert(hash, m_demoSharedFiles.value(hash));
                Q_EMIT sharedFilesLoaded();
            });
            continue;
        }
        pending = true;
        if (!m_hasServer || m_sharedFilesInFlight.contains(hash))
            continue;
        m_sharedFilesInFlight.insert(hash);
        m_client->torrentsFiles(hash)->then(
            this, [this, hash](const QbtResult<QJsonArray> &result) {
                m_sharedFilesInFlight.remove(hash);
                QStringList names;
                if (!result.isError()) {
                    for (const QJsonValue &file : result.value())
                        names.append(file.toObject().value(QStringLiteral("name")).toString());
                }
                // A refusal (the torrent went away meanwhile) is cached as "no files": it
                // shares nothing, and asking again would only be refused again.
                m_sharedFiles.insert(hash, names);
                Q_EMIT sharedFilesLoaded();
            });
    }
    const auto sharerMap = [&](const QString &hash) {
        const Torrent &torrent = model->torrent(hash);
        return QVariantMap{
            {QStringLiteral("hash"), hash},
            {QStringLiteral("name"), torrent.name},
            {QStringLiteral("tracker"), trackerHost(torrent.tracker)},
            {QStringLiteral("stateText"), torrent.stateText()},
            {QStringLiteral("ratioText"), Format::ratio(torrent.ratio)},
            {QStringLiteral("missingFiles"), torrent.state == TorrentState::MissingFiles},
        };
    };

    QVariantList sharing;
    for (const QString &hash : found.sharing) {
        QVariantList sharers;
        for (const QString &other : found.outsideOf.value(hash))
            sharers.append(sharerMap(other));
        sharing.append(QVariantMap{
            {QStringLiteral("hash"), hash},
            {QStringLiteral("name"), model->torrent(hash).name},
            {QStringLiteral("sharers"), sharers},
        });
    }
    int insideCount = 0;
    for (const QStringList &group : found.inside)
        insideCount += int(group.size());

    return QVariantMap{
        {QStringLiteral("sharing"), sharing},
        {QStringLiteral("outside"), found.outside},
        {QStringLiteral("insideCount"), insideCount},
        {QStringLiteral("insideSize"), found.insideSize},
        {QStringLiteral("diskSize"), found.diskSize},
        {QStringLiteral("diskSizeWithOutside"), found.diskSizeWithOutside},
        {QStringLiteral("pending"), pending},
    };
}

QStringList App::movedByCategory(const QStringList &hashes, const QString &category) const
{
    QStringList moved;
    TorrentModel *model = m_engine->torrents();
    for (const QString &hash : hashes) {
        if (model->indexOfHash(hash) < 0)
            continue;
        const Torrent &torrent = model->torrent(hash);
        if (torrent.autoTmm && torrent.category != category)
            moved.append(hash);
    }
    return moved;
}

void App::patchDemoFiles(const QVariantMap &files)
{
    if (!m_demoMode)
        return;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const QStringList names = it.value().toStringList();
        if (names.isEmpty())
            m_demoSharedFiles.remove(it.key());
        else
            m_demoSharedFiles.insert(it.key(), names);
    }
}

void App::patchDemoTorrents(const QVariantMap &torrents)
{
    if (!m_demoMode)
        return;
    m_engine->applyDemoData(
        QJsonObject{{QStringLiteral("torrents"), QJsonObject::fromVariantMap(torrents)}});
}

} // namespace omaseed::app
