// App's #28 block (docs/26): which torrents use the same data on disk, for the delete
// confirm, the location sheet, a category change and turning automatic management on (#65).
// Kept out of App.cpp so it reads as one piece; the declarations sit at the end of App.hpp.
#include "ui/app/App.hpp"

#include "core/Format.hpp"
#include "core/SharedData.hpp"
#include "core/engines/qbt/QbtTranslate.hpp"
#include "core/engines/qbt/ServerPreferences.hpp"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

#include <memory>

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

/// "/data/torrents" for "/data/torrents/" (and "/" stays "/").
QString withoutTrailingSlash(QString path)
{
    while (path.size() > 1 && (path.endsWith(QLatin1Char('/')) || path.endsWith(QLatin1Char('\\'))))
        path.chop(1);
    return path;
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
        m_client->files(hash)->then(this, [this, hash](const Result<QList<ContentFile>> &result) {
            m_sharedFilesInFlight.remove(hash);
            QStringList names;
            if (!result.isError()) {
                for (const ContentFile &file : result.value())
                    names.append(file.path);
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
            {QStringLiteral("missingFiles"), torrent.status.phase == TorrentPhase::Missing},
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

QStringList App::movedByAutoManagement(const QStringList &hashes) const
{
    // Where automatic management puts a torrent (autoManagedFolder()). When that is not
    // known, or the torrent is not complete (its files may sit in the incomplete folder),
    // it counts as moving.
    TorrentModel *model = m_engine->torrents();
    QStringList moved;
    for (const QString &hash : hashes) {
        if (model->indexOfHash(hash) < 0)
            continue;
        const Torrent &torrent = model->torrent(hash);
        if (torrent.autoTmm)
            continue;
        const QString folder = autoManagedFolder(torrent.category);
        if (!folder.isEmpty() && torrent.progress >= 1.0 &&
            folder == withoutTrailingSlash(torrent.savePath))
            continue;
        moved.append(hash);
    }
    return moved;
}

QString App::autoManagedFolder(const QString &category) const
{
    // The category's folder; a relative one, or none (then the category's name), under the
    // default save path; the default save path itself without a category.
    const QString defaultPath =
        withoutTrailingSlash(m_prefs->value(QStringLiteral("save_path")).toString());
    if (defaultPath.isEmpty() || category.isEmpty())
        return defaultPath;
    const QString own = m_engine->categories()->savePathOf(category);
    if (own.isEmpty())
        return defaultPath + QLatin1Char('/') + category;
    if (QDir::isAbsolutePath(own))
        return withoutTrailingSlash(own);
    return defaultPath + QLatin1Char('/') + withoutTrailingSlash(own);
}

void App::setAutoManagementWithCategories(const QStringList &hashes, const QVariantMap &categories)
{
    QHash<QString, QStringList> byCategory;
    for (auto it = categories.cbegin(); it != categories.cend(); ++it)
        byCategory[it.value().toString()].append(it.key());
    if (byCategory.isEmpty() || m_demoMode || !m_hasServer) {
        for (auto it = byCategory.cbegin(); it != byCategory.cend(); ++it)
            applyCategory(it.value(), it.key());
        setAutoManagement(hashes, true);
        return;
    }
    // The requests may run side by side: management goes on only once every category is
    // set, or a sharer would move to its old category's folder first.
    auto remaining = std::make_shared<qsizetype>(byCategory.size());
    auto refused = std::make_shared<bool>(false);
    for (auto it = byCategory.cbegin(); it != byCategory.cend(); ++it) {
        const QString name = it.key();
        m_client->setCategory(it.value(), name)
            ->then(this, [this, hashes, name, remaining, refused](const Result<Done> &result) {
                if (result.isError()) {
                    *refused = true;
                    qWarning().noquote()
                        << QStringLiteral("set category %1 failed, automatic management "
                                          "left off: %2")
                               .arg(name, result.error().toString());
                }
                if (--*remaining == 0 && !*refused)
                    setAutoManagement(hashes, true);
            });
    }
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
    m_engine->applyDemoData(QbtTranslate::syncDelta(
        QJsonObject{{QStringLiteral("torrents"), QJsonObject::fromVariantMap(torrents)}}));
}

void App::patchDemoPrefs(const QVariantMap &values, const QStringList &removed)
{
    if (!m_demoMode)
        return;
    QVariantMap next = m_prefs->values();
    for (const QString &key : removed)
        next.remove(key);
    next.insert(values);
    m_prefs->setOffline(next);
}

} // namespace omaseed::app
