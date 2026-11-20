#pragma once

// CategoryModel — the server's categories (SyncDelta), sorted by name for the rail.
// Each row carries the name and the category's save path (spec §4 Fig. 13 shows both).

#include "core/engines/SyncDelta.hpp"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace omaseed::core {

struct Category
{
    QString name;
    QString savePath;
    /// CategoryPatch::downloadPath: a path, false, or null for the session's rule.
    QVariant downloadPath;

    bool operator==(const Category &other) const = default;
};

class CategoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        SavePathRole,
    };
    Q_ENUM(Role)

    explicit CategoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_categories.size()); }
    Q_INVOKABLE QStringList names() const;
    Q_INVOKABLE QString savePathOf(const QString &name) const;
    /// The category's temp folder (DECISIONS C47 I12, #271 (2)): {rule: "own" | "none" |
    /// "session", path} (path only for "own"). An unknown name, or a server that says nothing,
    /// is "session".
    Q_INVOKABLE QVariantMap tempFolderOf(const QString &name) const;

    void applyFullUpdate(const QList<CategoryPatch> &categories);
    void applyPartial(const QList<CategoryPatch> &changed, const QStringList &removed = {});
    void clear();

Q_SIGNALS:
    void countChanged();

private:
    void setCategories(QList<Category> next);

    QList<Category> m_categories;
};

} // namespace omaseed::core
