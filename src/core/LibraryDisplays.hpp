#pragma once

// LibraryDisplays — which display shows the library in each scope (board 46 H4, DECISIONS
// C24): rows (grouped under the five questions), table (one sorted list) or overview (board
// 46's shelves), remembered per scope, folders included.
//
// The library's own display is also what every scope without a display of its own shows
// (#168 assumed 2): picking one in the library changes every scope never set, picking one
// elsewhere sets that scope's only. The library's starts as rows on a new install and table on
// an existing one (H4): an install is existing when its state.json already holds anything
// besides `version` the first time this build reads it (#168 assumed 3); the caller writes the
// decision back at once, so it never flips.
//
// A scope's key (#168 assumed 4): the folder, else the category, tag or tracker, else the
// question, else the library; not per server. Pure Qt Core; `tst_librarydisplays`.

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace omaseed::core {

class LibraryDisplays
{
public:
    /// "rows", "table", "overview", in the display tool's order.
    static QStringList names();
    static bool isName(const QString &name);

    /// The library's key, the scope with no rail filter on.
    static QString libraryKey();
    /// The key a scope is remembered under. `category`, `tag` and `tracker` are the rail's
    /// three-valued filters (an invalid QVariant is unset, "" is the rail's none row);
    /// `question` a TorrentFilterModel::Question; `folder` "" for none.
    static QString scopeKey(int question,
                            const QVariant &category,
                            const QVariant &tag,
                            const QVariant &tracker,
                            const QString &folder);

    /// Whether `state` (the whole state.json, or `{}` when `fileExists` is false) is an
    /// install made before this build: anything in it besides `version`.
    static bool isExistingInstall(const QJsonObject &state, bool fileExists);
    /// The library's display for a first read: table for an existing install, rows else.
    static QString initialLibrary(bool existingInstall);

    explicit LibraryDisplays(const QString &library = QStringLiteral("rows"));

    QString library() const { return m_library; }
    /// The display `scopeKey` shows: its own, or the library's.
    QString displayFor(const QString &scopeKey) const;
    /// Whether `scopeKey` has a display of its own.
    bool hasOwn(const QString &scopeKey) const { return m_scopes.contains(scopeKey); }
    /// Picks `display` in `scopeKey`; false when that changed nothing (or `display` is not a
    /// display's name).
    bool set(const QString &scopeKey, const QString &display);

    /// `{"library": "rows", "scopes": {"folder:/data/films": "overview", …}}`.
    QJsonObject toJson() const;
    /// The reverse; a missing or unknown library display reads as `fallback`, unknown scope
    /// entries are dropped.
    static LibraryDisplays fromJson(const QJsonObject &json, const QString &fallback);

    bool operator==(const LibraryDisplays &other) const = default;

private:
    QString m_library;
    QHash<QString, QString> m_scopes;
};

} // namespace omaseed::core
