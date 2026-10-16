#pragma once

// TagModel — the tag list of sync/maindata, kept sorted case-insensitively for the rail.
// A full update replaces the list; a partial one adds the names in `tags` and drops the ones
// in `tags_removed`.

#include <QAbstractListModel>
#include <QStringList>

namespace omaseed::core {

class TagModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
    };
    Q_ENUM(Role)

    explicit TagModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_tags.size()); }
    Q_INVOKABLE QStringList tags() const { return m_tags; }
    Q_INVOKABLE bool contains(const QString &tag) const { return m_tags.contains(tag); }

    void applyFullUpdate(const QStringList &tags);
    void applyPartial(const QStringList &added, const QStringList &removed = {});
    void clear();

Q_SIGNALS:
    void countChanged();

private:
    void setTags(QStringList next);

    QStringList m_tags;
};

} // namespace omaseed::core
