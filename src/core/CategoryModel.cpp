#include "core/CategoryModel.hpp"

#include <QCollator>

#include <algorithm>

namespace omaseed::core {

namespace {

bool nameLessThan(const Category &a, const Category &b)
{
    static QCollator collator = [] {
        QCollator c;
        c.setCaseSensitivity(Qt::CaseInsensitive);
        c.setNumericMode(true);
        return c;
    }();
    const int compared = collator.compare(a.name, b.name);
    return compared != 0 ? compared < 0 : a.name < b.name;
}

Category categoryFrom(const CategoryPatch &patch)
{
    return Category{patch.name, patch.savePath.value_or(QString())};
}

} // namespace

CategoryModel::CategoryModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int CategoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_categories.size());
}

QVariant CategoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_categories.size())
        return {};
    const Category &category = m_categories.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return category.name;
    case SavePathRole:
        return category.savePath;
    default:
        return {};
    }
}

QHash<int, QByteArray> CategoryModel::roleNames() const
{
    return {{NameRole, "name"}, {SavePathRole, "savePath"}};
}

QStringList CategoryModel::names() const
{
    QStringList list;
    list.reserve(int(m_categories.size()));
    for (const Category &category : m_categories)
        list.append(category.name);
    return list;
}

QString CategoryModel::savePathOf(const QString &name) const
{
    for (const Category &category : m_categories) {
        if (category.name == name)
            return category.savePath;
    }
    return {};
}

void CategoryModel::applyFullUpdate(const QList<CategoryPatch> &categories)
{
    QList<Category> next;
    next.reserve(categories.size());
    for (const CategoryPatch &patch : categories)
        next.append(categoryFrom(patch));
    setCategories(std::move(next));
}

void CategoryModel::applyPartial(const QList<CategoryPatch> &changed, const QStringList &removed)
{
    if (changed.isEmpty() && removed.isEmpty())
        return;

    QList<Category> next = m_categories;
    for (const QString &name : removed) {
        next.removeIf([&name](const Category &c) { return c.name == name; });
    }
    for (const CategoryPatch &patch : changed) {
        const Category category = categoryFrom(patch);
        const auto found = std::find_if(next.begin(), next.end(), [&category](const Category &c) {
            return c.name == category.name;
        });
        if (found == next.end()) {
            next.append(category);
            continue;
        }
        // A partial update carries only what changed: a field it leaves out keeps its old
        // value rather than falling back to empty.
        if (patch.savePath)
            found->savePath = *patch.savePath;
    }
    setCategories(std::move(next));
}

void CategoryModel::clear()
{
    setCategories({});
}

void CategoryModel::setCategories(QList<Category> next)
{
    std::sort(next.begin(), next.end(), nameLessThan);
    if (next == m_categories)
        return;

    const bool sameSize = next.size() == m_categories.size();
    if (sameSize) {
        // Only save paths moved: keep the rows and tell the views which ones changed.
        QList<int> rows;
        for (int row = 0; row < next.size(); ++row) {
            if (next.at(row).name != m_categories.at(row).name) {
                rows.clear();
                break;
            }
            if (next.at(row).savePath != m_categories.at(row).savePath)
                rows.append(row);
        }
        if (!rows.isEmpty()) {
            m_categories = std::move(next);
            for (const int touched : rows)
                Q_EMIT dataChanged(index(touched), index(touched), {SavePathRole});
            return;
        }
    }

    beginResetModel();
    m_categories = std::move(next);
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace omaseed::core
