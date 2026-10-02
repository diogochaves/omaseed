#include "core/TorrentWebSeedsModel.hpp"

#include <QJsonObject>
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
    };
}

void TorrentWebSeedsModel::applyWebSeeds(const QJsonArray &webSeeds)
{
    QList<QString> urls;
    urls.reserve(webSeeds.size());
    for (const QJsonValue &value : webSeeds) {
        const QString url = value.toObject().value(QLatin1StringView("url")).toString();
        if (!url.isEmpty())
            urls.append(url);
    }
    if (urls != m_urls) {
        const bool countChanges = urls.size() != m_urls.size();
        beginResetModel();
        m_urls = urls;
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
        endResetModel();
        Q_EMIT countChanged();
    }
    setLoaded(false);
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
