#include "core/TorrentWebSeedsModel.hpp"

#include "core/Format.hpp"

#include <QUrl>

namespace omaseed::core {

TorrentWebSeedsModel::TorrentWebSeedsModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int TorrentWebSeedsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_urls.size());
}

QVariant TorrentWebSeedsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_urls.size())
        return {};
    const QString &url = m_urls.at(index.row());
    switch (role) {
    case UrlRole:
        return url;
    case Qt::DisplayRole:
    case TextRole:
        return displayText(url);
    case HostRole:
        return QUrl(url).host();
    case StatusErrorRole:
        return m_statuses.value(index.row()).error;
    case StatusMetaRole:
        return m_statuses.value(index.row()).meta;
    default:
        return {};
    }
}

QHash<int, QByteArray> TorrentWebSeedsModel::roleNames() const
{
    return {
        {UrlRole, "url"},
        {TextRole, "text"},
        {HostRole, "host"},
        {StatusErrorRole, "statusError"},
        {StatusMetaRole, "statusMeta"},
    };
}

void TorrentWebSeedsModel::applyWebSeeds(const QStringList &webSeeds)
{
    QList<QString> urls;
    urls.reserve(webSeeds.size());
    for (const QString &url : webSeeds) {
        if (!url.isEmpty())
            urls.append(url);
    }
    if (urls != m_urls) {
        const bool countChanges = urls.size() != m_urls.size();
        beginResetModel();
        m_urls = urls;
        m_statuses = statuses();
        endResetModel();
        if (countChanges)
            Q_EMIT countChanged();
    }
    setLoaded(true);
}

void TorrentWebSeedsModel::clear()
{
    if (!m_urls.isEmpty()) {
        beginResetModel();
        m_urls.clear();
        m_statuses.clear();
        endResetModel();
        Q_EMIT countChanged();
    }
    setLoaded(false);
}

void TorrentWebSeedsModel::setLogLines(const QList<WebSeedLogLine> &lines)
{
    if (lines == m_logLines)
        return;
    m_logLines = lines;
    const QList<Status> before = m_statuses;
    m_statuses = statuses();
    for (qsizetype row = 0; row < m_statuses.size(); ++row) {
        const Status &was = before.value(row);
        if (was.error == m_statuses.at(row).error && was.meta == m_statuses.at(row).meta)
            continue;
        Q_EMIT dataChanged(index(int(row)), index(int(row)), {StatusErrorRole, StatusMetaRole});
    }
}

QList<TorrentWebSeedsModel::Status> TorrentWebSeedsModel::statuses() const
{
    struct Seen
    {
        int count = 0;
        const WebSeedLogLine *newest = nullptr;
    };
    QList<Seen> seen(m_urls.size());
    for (const WebSeedLogLine &line : m_logLines) {
        // The seed it is about: the URL itself, or the deepest folder seed holding the file.
        qsizetype best = -1;
        for (qsizetype row = 0; row < m_urls.size(); ++row) {
            if (ProblemMatch::isWebSeedUrl(line.url, m_urls.at(row)) &&
                (best < 0 || m_urls.at(row).size() > m_urls.at(best).size()))
                best = row;
        }
        if (best < 0)
            continue;
        ++seen[best].count;
        if (seen[best].newest == nullptr || line.at >= seen[best].newest->at)
            seen[best].newest = &line;
    }

    QList<Status> result;
    result.reserve(m_urls.size());
    for (const Seen &one : std::as_const(seen)) {
        Status status;
        if (one.newest == nullptr) {
            status.meta = tr("no errors logged");
        } else {
            status.error = one.newest->status;
            const QString when = Format::relativeTime(one.newest->at);
            status.meta = one.count == 1 ? when : tr("%1 times, last %2").arg(one.count).arg(when);
        }
        result.append(status);
    }
    return result;
}

QString TorrentWebSeedsModel::urlAt(int row) const
{
    return row >= 0 && row < m_urls.size() ? m_urls.at(row) : QString();
}

QString TorrentWebSeedsModel::displayText(const QString &url)
{
    static const QLatin1StringView secure("https://");
    return url.startsWith(secure, Qt::CaseInsensitive) ? url.mid(secure.size()) : url;
}

void TorrentWebSeedsModel::setLoaded(bool loaded)
{
    if (loaded == m_loaded)
        return;
    m_loaded = loaded;
    Q_EMIT loadedChanged();
}

} // namespace omaseed::core
