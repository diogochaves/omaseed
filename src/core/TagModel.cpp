#include "core/TagModel.hpp"

#include <QCollator>
#include <QJsonValue>

#include <algorithm>

namespace omaseed::core {

namespace {

/// The rail lists names the way a person reads them: case-insensitive, numbers in order.
bool nameLessThan(const QString &a, const QString &b)
{
    static QCollator collator = [] {
        QCollator c;
        c.setCaseSensitivity(Qt::CaseInsensitive);
        c.setNumericMode(true);
        return c;
    }();
    const int compared = collator.compare(a, b);
    return compared != 0 ? compared < 0 : a < b;
}

} // namespace

TagModel::TagModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int TagModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_tags.size());
}

QVariant TagModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tags.size())
        return {};
    if (role == NameRole || role == Qt::DisplayRole)
        return m_tags.at(index.row());
    return {};
}

QHash<int, QByteArray> TagModel::roleNames() const
{
    return {{NameRole, "name"}};
}

void TagModel::applyFullUpdate(const QJsonArray &tags)
{
    QStringList next;
    next.reserve(tags.size());
    for (const QJsonValue &value : tags) {
        const QString tag = value.toString();
        if (!tag.isEmpty() && !next.contains(tag))
            next.append(tag);
    }
    setTags(std::move(next));
}

void TagModel::applyPartial(const QJsonArray &added, const QJsonArray &removed)
{
    if (added.isEmpty() && removed.isEmpty())
        return;

    QStringList next = m_tags;
    for (const QJsonValue &value : removed)
        next.removeAll(value.toString());
    for (const QJsonValue &value : added) {
        const QString tag = value.toString();
        if (!tag.isEmpty() && !next.contains(tag))
            next.append(tag);
    }
    setTags(std::move(next));
}

void TagModel::clear()
{
    setTags({});
}

void TagModel::setTags(QStringList next)
{
    std::sort(next.begin(), next.end(), nameLessThan);
    if (next == m_tags)
        return;

    // Tag lists are short and change rarely; a reset keeps this honest and cheap.
    beginResetModel();
    m_tags = std::move(next);
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace omaseed::core
