#pragma once

// FolderModel — the rail's folders group as a list model (board 42 §2 F1–F3, DECISIONS C15
// M1–M3, #97).
//
// FolderTree works out the rows; this shows them. A row whose parent (or any row above it) is
// folded is not in the model, so the rail's cursor and its rows count the same entries. The
// folded paths are the window's to keep (state.json, F2); setting them only hides rows.
//
// During a move (F3), `marks` puts a word in a row's count column: the job's percentage on the
// folder being moved, "↻" on the save paths below it. A poll that leaves the rows as they were
// changes nothing, so the rail is not rebuilt every 1.5 s.

#include "core/FolderTree.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace omaseed::core {

class FolderModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Every row, folded away or not: 0 means the library has no save path to show.
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(QStringList folded READ folded WRITE setFolded NOTIFY foldedChanged)
    /// Moves on any change of rows, figures or marks, for bindings that call figures().
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        NameRole,
        DepthRole,
        HeadingRole,
        HasChildrenRole,
        FoldedRole,
        CountRole,
        CountHereRole,
        BytesRole,
        SavePathsRole,
        MarkRole,
    };
    Q_ENUM(Role)

    explicit FolderModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_shown.size()); }
    int total() const { return int(m_nodes.size()); }
    int revision() const { return m_revision; }

    /// The rows as FolderTree::build() gives them. The same rows again changes nothing.
    void setNodes(const QList<FolderNode> &nodes);
    const QList<FolderNode> &nodes() const { return m_nodes; }

    QStringList folded() const { return m_folded; }
    void setFolded(const QStringList &paths);
    /// Folds `path`'s row, or unfolds it.
    Q_INVOKABLE void toggleFold(const QString &path);

    /// What a row's count column says instead of its count, by path ("52 %", "↻").
    void setMarks(const QHash<QString, QString> &marks);

    /// The node at `path`, or nullptr.
    const FolderNode *node(const QString &path) const;
    /// The scope line's figures (F1) for `path`: path, name, count, countHere, bytes,
    /// savePaths, heading; empty when there is no such row.
    Q_INVOKABLE QVariantMap figures(const QString &path) const;
    /// The path of shown row `row`, or "".
    Q_INVOKABLE QString pathAt(int row) const;

Q_SIGNALS:
    void countChanged();
    void foldedChanged();
    void revisionChanged();

private:
    void rebuildShown();
    bool hiddenByFold(const FolderNode &node) const;

    QList<FolderNode> m_nodes;
    QHash<QString, int> m_byPath; ///< index into m_nodes
    QList<int> m_shown;           ///< indexes into m_nodes, the rows on show
    QStringList m_folded;
    QSet<QString> m_foldedSet;
    QHash<QString, QString> m_marks;
    int m_revision = 0;
};

} // namespace omaseed::core
