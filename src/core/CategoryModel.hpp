#pragma once

// CategoryModel — sync/maindata's `categories` map, sorted by name for the rail.
// Each row carries the name and the category's save path (spec §4 Fig. 13 shows both).

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace omaseed::core {

struct Category
{
    QString name;
    QString savePath;

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
    QStringList names() const;
    Q_INVOKABLE QString savePathOf(const QString &name) const;

    void applyFullUpdate(const QJsonObject &categories);
    void applyPartial(const QJsonObject &changed, const QJsonArray &removed);
    void clear();

Q_SIGNALS:
    void countChanged();

private:
    void setCategories(QList<Category> next);

    QList<Category> m_categories;
};

} // namespace omaseed::core
