// #145: search through the server's own search plugins (board 48, DECISIONS C26). See the
// block at the end of App.hpp.

#include "App.hpp"

#include "core/Format.hpp"
#include "core/MagnetLink.hpp"
#include "core/SearchWords.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFileDialog>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QWindow>

namespace omaseed::app {

using core::SearchModel;
using core::SearchRow;
using core::SearchSession;

namespace {

/// The library's key for "likely on disk": the folded name and the exact size (#145 assumed 8).
QString nameSizeKey(const QString &name, qint64 size)
{
    return QStringLiteral("%1\n%2").arg(core::SearchWords::fold(name)).arg(size);
}

} // namespace

void App::setUpSearch()
{
    m_search = new SearchSession(this);
    m_search->setClient(m_client);
    m_searchModel = new SearchModel(this);
    m_searchPlugins = new core::SearchPlugins(this);
    m_searchPlugins->setClient(m_client);
    m_searchModel->setLibraryLookup(
        [this](const SearchRow &row) { return searchLibraryMatch(row); });

    connect(m_search, &SearchSession::hitsChanged, this, [this]() {
        // SR3: the library's private trackers, read once for this batch of rows.
        const auto library = std::make_shared<core::PrivateTrackers>(libraryPrivateTrackers());
        m_searchModel->setPrivateHostOf(
            [library](const QStringList &trackers) { return library->match(trackers); });
        m_searchModel->setHits(m_search->hits());
    });
    connect(m_searchPlugins, &core::SearchPlugins::countChanged, this, [this]() {
        m_searchModel->setPluginNames(m_searchPlugins->names());
    });

    // SR7's marks follow the library, at most every 2 s.
    m_searchMarksTimer = new QTimer(this);
    m_searchMarksTimer->setSingleShot(true);
    m_searchMarksTimer->setInterval(2000);
    connect(m_searchMarksTimer, &QTimer::timeout, this, [this]() {
        rebuildSearchIndex();
        m_searchModel->refreshMarks();
    });
    connect(m_engine->torrents(), &core::TorrentModel::updated, this, [this]() {
        if (m_searchModel->rowCount() > 0 && !m_searchMarksTimer->isActive())
            m_searchMarksTimer->start();
    });

    connect(this, &App::serverChanged, this, &App::switchSearchServer);
    if (QCoreApplication::instance() != nullptr)
        connect(QCoreApplication::instance(),
                &QCoreApplication::aboutToQuit,
                this,
                &App::shutdownSearch);
    m_searchServerId = m_hasServer ? m_server.id : QString();
}

bool App::searchOffered() const
{
    return m_demoMode || (m_hasServer && m_server.isQbittorrent());
}

bool App::searchServerLocal() const
{
    if (!m_hasServer)
        return false;
    // #145 assumed: the address is this computer (a loopback address, or its own name).
    const QString host = m_server.url.host();
    if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    if (host.compare(QHostInfo::localHostName(), Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}

QString App::pickSearchPluginFile()
{
    // The portal's chooser, as a dialog of our window (App.cpp's runPicker).
    QFileDialog dialog(nullptr,
                       tr("Choose a search plugin"),
                       QString(),
                       tr("Search plugins (*.py);;All files (*)"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    if (m_dialogParent) {
        dialog.winId();
        dialog.windowHandle()->setTransientParent(m_dialogParent);
    }
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
}

void App::setSearchTimings(int pollMs, int deadlineMs, int maxRunning)
{
    m_search->setTimings(pollMs, deadlineMs, maxRunning);
}

void App::switchSearchServer()
{
    const QString next = m_hasServer && !m_demoMode ? m_server.id : QString();
    if (next == m_searchServerId)
        return;
    // SR12: the server left keeps its last search for the session; its jobs are deleted the
    // next time omaseed is on it (#145 assumed 1).
    if (!m_searchServerId.isEmpty()) {
        if (m_search->state() != SearchSession::State::Idle)
            m_searchKept.insert(m_searchServerId, m_search->snapshot());
        m_searchLeftJobs[m_searchServerId] += m_search->jobIds();
    }
    m_search->forget();
    m_searchModel->startOver();
    m_searchPlugins->clear();
    m_searchServerId = next;
    const auto kept = m_searchKept.constFind(next);
    if (kept != m_searchKept.cend())
        m_search->restore(*kept);
}

void App::shutdownSearch()
{
    QList<int> left = m_searchLeftJobs.take(m_searchServerId);
    m_search->deleteJobs(left);
    if (m_search->shutdown() == 0 && left.isEmpty())
        return;
    // SR12: the deletes go out before the window closes; never more than a second.
    QEventLoop loop;
    connect(m_search, &SearchSession::jobsDeleted, &loop, &QEventLoop::quit);
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);
    loop.exec();
}

void App::refreshSearchPlugins()
{
    if (m_demoMode || !m_hasServer)
        return;
    m_searchPlugins->refresh();
}

void App::startSearch(const QString &pattern, const QString &category, const QStringList &plugins)
{
    const QString words = pattern.trimmed();
    if (words.isEmpty() || !searchOffered())
        return;
    // A demo asks no server: board 48's invented answers stand in.
    if (m_demoMode) {
        loadDemoSearch(QStringLiteral("done"));
        return;
    }
    // Jobs a search left here while the window was on another server (SR12).
    m_search->deleteJobs(m_searchLeftJobs.take(m_searchServerId));
    // The plugins as the server lists them now: one may have come or gone since the view
    // opened (another client, the Web UI).
    m_searchPlugins->refreshThen([this, words, category, plugins]() {
        QList<SearchSession::Plugin> asked;
        for (const QVariant &value : m_searchPlugins->enabledPlugins()) {
            const QVariantMap plugin = value.toMap();
            const QString name = plugin.value(QStringLiteral("name")).toString();
            if (!plugins.isEmpty() && !plugins.contains(name))
                continue;
            asked.append({name,
                          plugin.value(QStringLiteral("fullName")).toString(),
                          plugin.value(QStringLiteral("categories")).toStringList()});
        }
        rebuildSearchIndex();
        m_searchModel->startOver();
        m_search->start(words, category, asked);
    });
}

void App::rebuildSearchIndex()
{
    m_searchByHash.clear();
    m_searchByNameSize.clear();
    const core::TorrentModel *model = m_engine->torrents();
    for (int row = 0; row < model->count(); ++row) {
        const QString hash =
            model->data(model->index(row), core::TorrentModel::HashRole).toString();
        const core::Torrent &torrent = model->torrent(hash);
        SearchModel::LibraryMatch match;
        match.hash = torrent.hash;
        if (torrent.progress < 1.0)
            match.words =
                QStringLiteral("arriving at %1").arg(core::Format::percent(torrent.progress));
        else
            match.words = torrent.stateText() == QLatin1String("stopped")
                              ? QStringLiteral("resting")
                              : QStringLiteral("seeding");
        m_searchByHash.insert(torrent.hash.toLower(), match);
        if (!torrent.infohashV1.isEmpty())
            m_searchByHash.insert(torrent.infohashV1.toLower(), match);
        if (!torrent.infohashV2.isEmpty()) {
            m_searchByHash.insert(torrent.infohashV2.toLower(), match);
            m_searchByHash.insert(torrent.infohashV2.left(40).toLower(), match);
        }
        const qint64 size = torrent.totalSize > 0 ? torrent.totalSize : torrent.size;
        if (size > 0)
            m_searchByNameSize.insert(nameSizeKey(torrent.name, size), torrent.name);
    }
}

SearchModel::LibraryMatch App::searchLibraryMatch(const SearchRow &row) const
{
    for (const QString &hash : {row.hashV1, row.hashV2, row.hash()}) {
        if (hash.isEmpty())
            continue;
        const auto found = m_searchByHash.constFind(hash.toLower());
        if (found != m_searchByHash.cend())
            return *found;
    }
    SearchModel::LibraryMatch match;
    if (row.size > 0)
        match.name = m_searchByNameSize.value(nameSizeKey(row.name, row.size));
    return match;
}

QVariantMap App::searchAddable(const QString &key) const
{
    const SearchRow *row = m_searchModel->rowFor(key);
    if (row == nullptr)
        return {};
    const QHash<QString, QString> names = m_searchPlugins->names();
    const QString who = core::SearchResults::pluginWords(
        row->plugins, [&names](const QString &name) { return names.value(name, name); });
    QStringList parts = {QStringLiteral("from search"), who};
    if (row->isLink)
        parts.append(QStringLiteral("a link to a .torrent"));
    else if (row->trackers.size() == 1)
        parts.append(QStringLiteral("a magnet with 1 tracker"));
    else
        parts.append(QStringLiteral("a magnet with %1 trackers").arg(row->trackers.size()));
    const SearchModel::LibraryMatch match = searchLibraryMatch(*row);
    return {{QStringLiteral("key"), row->key},
            {QStringLiteral("text"), row->link},
            {QStringLiteral("downloader"), row->isLink ? row->plugin : QString()},
            {QStringLiteral("fromLine"), parts.join(QStringLiteral(" · "))},
            {QStringLiteral("name"), row->name},
            {QStringLiteral("libraryHash"), match.hash}};
}

void App::searchAdded(const QString &key)
{
    m_searchModel->markAdded(key);
}

void App::turnOffSearchPlugin(const QString &name)
{
    QString when;
    QString state;
    for (const QVariant &value : m_search->pluginRows()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("name")).toString() != name)
            continue;
        state = row.value(QStringLiteral("state")).toString();
        when = row.value(QStringLiteral("logTime")).toString();
    }
    const QString why =
        state == QLatin1String("failed")
            ? (when.isEmpty()
                   ? QStringLiteral("turned off · it failed in the last search")
                   : QStringLiteral("turned off · it failed in the last search, %1").arg(when))
            : QStringLiteral("turned off · it didn't answer in the last search");
    m_searchPlugins->setNote(name, why);
    m_searchPlugins->setEnabled(name, false);
}

} // namespace omaseed::app

namespace omaseed::app {

namespace {

QJsonObject demoPlugin(const QString &name,
                       const QString &fullName,
                       const QString &version,
                       bool enabled,
                       const QStringList &types)
{
    QJsonArray categories{QJsonObject{{QStringLiteral("id"), QStringLiteral("all")},
                                      {QStringLiteral("name"), QStringLiteral("All categories")}}};
    for (const QString &type : types)
        categories.append(
            QJsonObject{{QStringLiteral("id"), type},
                        {QStringLiteral("name"), type.at(0).toUpper() + type.mid(1)}});
    return {{QStringLiteral("name"), name},
            {QStringLiteral("fullName"), fullName},
            {QStringLiteral("version"), version},
            {QStringLiteral("url"), QStringLiteral("http://%1.invalid").arg(name)},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("supportedCategories"), categories}};
}

core::SearchHit demoHit(const QString &plugin,
                        const QString &name,
                        qint64 size,
                        int days,
                        int seeds,
                        int leechers,
                        const QString &hash,
                        const QStringList &trackers,
                        bool link = false)
{
    QString url;
    if (link) {
        url = QStringLiteral("http://%1.invalid/get/%2.torrent").arg(plugin, hash.left(6));
    } else {
        url = QStringLiteral("magnet:?xt=urn:btih:%1&dn=%2")
                  .arg(hash, QString::fromUtf8(QUrl::toPercentEncoding(name)));
        for (const QString &tracker : trackers)
            url += QStringLiteral("&tr=") + QString::fromUtf8(QUrl::toPercentEncoding(tracker));
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return core::SearchHit::fromJson(
        QJsonObject{{QStringLiteral("fileName"), name},
                    {QStringLiteral("fileSize"), size},
                    {QStringLiteral("fileUrl"), url},
                    {QStringLiteral("descrLink"),
                     QStringLiteral("http://%1.invalid/t/%2").arg(plugin, hash.left(6))},
                    {QStringLiteral("nbSeeders"), seeds},
                    {QStringLiteral("nbLeechers"), leechers},
                    {QStringLiteral("pubDate"), days < 0 ? -1 : now - qint64(days) * 86400},
                    {QStringLiteral("engineName"), plugin}},
        plugin);
}

QVariantMap demoPluginRow(const QString &name,
                          const QString &fullName,
                          const QString &state,
                          int count,
                          int seconds,
                          const QString &reason = {},
                          const QString &logTime = {})
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("fullName"), fullName},
            {QStringLiteral("state"), state},
            {QStringLiteral("count"), count},
            {QStringLiteral("seconds"), seconds},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("logTime"), logTime}};
}

} // namespace

void App::loadDemoSearch(const QString &stage)
{
    // Board 48's invented plugins; nothing here names a real site.
    const QString a = QStringLiteral("plugin_a");
    const QString b = QStringLiteral("plugin_b");
    const QString c = QStringLiteral("plugin_c");
    QJsonArray plugins;
    if (stage != QLatin1String("none")) {
        const bool on = stage != QLatin1String("off");
        plugins = {
            demoPlugin(
                a,
                QStringLiteral("plugin a"),
                QStringLiteral("1.4"),
                on,
                {QStringLiteral("movies"), QStringLiteral("music"), QStringLiteral("software")}),
            demoPlugin(b,
                       QStringLiteral("plugin b"),
                       QStringLiteral("2.0"),
                       on,
                       {QStringLiteral("movies"), QStringLiteral("tv")}),
            demoPlugin(
                c,
                QStringLiteral("plugin c"),
                QStringLiteral("0.9"),
                on,
                {QStringLiteral("music"), QStringLiteral("books"), QStringLiteral("movies")})};
    }
    m_searchPlugins->clear();
    m_searchPlugins->applyList(plugins);
    m_searchModel->startOver();
    rebuildSearchIndex();

    SearchSession::Snapshot snapshot;
    if (stage == QLatin1String("none") || stage == QLatin1String("off")) {
        m_search->restore(snapshot);
        return;
    }
    snapshot.pattern = QStringLiteral("open movie night");
    snapshot.category = QStringLiteral("all");
    if (stage == QLatin1String("nopython")) {
        snapshot.state = SearchSession::State::NoPython;
        m_search->restore(snapshot);
        return;
    }
    const QStringList t1 = {QStringLiteral("udp://tracker-one.example:1337"),
                            QStringLiteral("udp://tracker-two.example:6969"),
                            QStringLiteral("https://tracker-three.example/announce")};
    const QStringList t2 = {QStringLiteral("udp://tracker-one.example:1337"),
                            QStringLiteral("udp://tracker-four.example:80"),
                            QStringLiteral("udp://tracker-five.example:2710")};
    const auto hash = [](int n) {
        return QStringLiteral("%1").arg(n, 40, 16, QLatin1Char('a')).left(40);
    };
    QList<core::SearchHit> hits = {
        demoHit(a,
                QStringLiteral("Open Movie Night (2026) 1080p"),
                1'567'000'000,
                3,
                41,
                6,
                hash(1),
                t1),
        demoHit(a,
                QStringLiteral("Open Movie Night (2026) 720p"),
                503'000'000,
                3,
                17,
                2,
                hash(10),
                t1.mid(0, 2)),
        demoHit(a,
                QStringLiteral("Open Movie Night (2026) [extras]"),
                1'288'000'000,
                14,
                12,
                0,
                hash(3),
                {},
                true),
        demoHit(a,
                QStringLiteral("Open Movie Night (2025)"),
                723'000'000,
                380,
                8,
                0,
                hash(7),
                t1.mid(0, 2)),
        demoHit(a,
                QStringLiteral("open-movie-night-2026-soundtrack [FLAC]"),
                108'000'000,
                3,
                3,
                1,
                hash(9),
                t1.mid(0, 2)),
        demoHit(a,
                QStringLiteral("open movie night poster pack"),
                38'900'000,
                120,
                2,
                0,
                hash(8),
                t1.mid(0, 1)),
    };
    // SR7: one result is a torrent of the demo library, another has its name and size.
    const core::TorrentModel *library = m_engine->torrents();
    if (library->count() >= 2) {
        const core::Torrent &mine = library->torrent(
            library->data(library->index(0), core::TorrentModel::HashRole).toString());
        const core::Torrent &other = library->torrent(
            library->data(library->index(1), core::TorrentModel::HashRole).toString());
        hits.append(demoHit(a,
                            mine.name,
                            mine.totalSize > 0 ? mine.totalSize : mine.size,
                            5,
                            23,
                            3,
                            mine.infohashV1.isEmpty() ? mine.hash : mine.infohashV1,
                            t1));
        if (stage != QLatin1String("one"))
            hits.append(demoHit(c,
                                other.name,
                                other.totalSize > 0 ? other.totalSize : other.size,
                                7,
                                9,
                                1,
                                hash(4),
                                t2.mid(0, 2)));
    }
    QVariantList rows;
    if (stage == QLatin1String("asking")) {
        hits.clear();
        snapshot.state = SearchSession::State::Asking;
        rows = {demoPluginRow(a, QStringLiteral("plugin a"), QStringLiteral("asking"), 0, -1),
                demoPluginRow(b, QStringLiteral("plugin b"), QStringLiteral("asking"), 0, -1),
                demoPluginRow(c, QStringLiteral("plugin c"), QStringLiteral("asking"), 0, -1)};
    } else if (stage == QLatin1String("one")) {
        snapshot.state = SearchSession::State::Asking;
        rows = {demoPluginRow(
                    a, QStringLiteral("plugin a"), QStringLiteral("answered"), int(hits.size()), 2),
                demoPluginRow(b, QStringLiteral("plugin b"), QStringLiteral("asking"), 0, -1),
                demoPluginRow(c, QStringLiteral("plugin c"), QStringLiteral("asking"), 0, -1)};
    } else {
        // plugin c's copy of the top row (no seed counts, two more trackers), and two of its own.
        hits.append(demoHit(c,
                            QStringLiteral("Open.Movie.Night.2026.1080p"),
                            1'567'000'000,
                            -1,
                            -1,
                            -1,
                            hash(1),
                            t2));
        hits.append(demoHit(c,
                            QStringLiteral("Open Movie Night (2026) [subtitles]"),
                            1'400'000,
                            -1,
                            -1,
                            -1,
                            hash(5),
                            t2.mid(0, 1)));
        hits.append(demoHit(c,
                            QStringLiteral("Open Movie Night 2026 commentary"),
                            230'000'000,
                            760,
                            0,
                            0,
                            hash(6),
                            t2.mid(0, 1)));
        snapshot.state = SearchSession::State::Done;
        const bool slow = stage == QLatin1String("slow");
        rows = {demoPluginRow(a, QStringLiteral("plugin a"), QStringLiteral("answered"), 7, 2),
                slow ? demoPluginRow(b, QStringLiteral("plugin b"), QStringLiteral("slow"), 0, -1)
                     : demoPluginRow(b,
                                     QStringLiteral("plugin b"),
                                     QStringLiteral("failed"),
                                     0,
                                     3,
                                     QStringLiteral("URLError: timed out"),
                                     QStringLiteral("16:01")),
                demoPluginRow(c, QStringLiteral("plugin c"), QStringLiteral("answered"), 4, 6)};
    }
    snapshot.hits = hits;
    snapshot.plugins = rows;
    m_search->restore(snapshot);
}

} // namespace omaseed::app
