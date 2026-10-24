#pragma once

// SearchModel — a search's rows, as the view lists them (#145, board 48 §4, DECISIONS C26
// SR3–SR8).
//
// It folds SearchSession's hits into one row per torrent (core/SearchResults), sorts them
// (most seeds then newest, newest, size, name: SR6's sort ▾), hides a reported 0 when "hide
// dead" is on (SR4: "?" stays), narrows them by `/`'s words (C22's SearchWords), and marks what
// is already yours (SR7): "in your library" by hash, "likely on disk" by name and size, "added"
// once the add sheet sent one. A row that arrives, or gains a copy, is "new" or "+1 copy" for a
// moment (SR5).
//
// Rows move with inserts, removes and moves rather than a reset, so the view keeps its
// cursor; `aboutToRefresh()` and `refreshed()` bracket each change, and `landedAbove` /
// `foldedAbove` count what came in above the row `anchorKey` names (SR5's line).
//
// Words are board 48's (§4): line 1 "size · age" and a mark, line 2 who found it and what to
// know, the fact on the right "41 · seeds".

#include "core/SearchResults.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QTimer>

#include <functional>

namespace omaseed::core {

class SearchModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    /// Rows before hide dead and `/`: one per torrent.
    Q_PROPERTY(int torrentCount READ torrentCount NOTIFY countChanged)
    /// Copies folded into other rows (results minus rows).
    Q_PROPERTY(int foldedCount READ foldedCount NOTIFY countChanged)
    /// Rows hide dead hides now.
    Q_PROPERTY(int hiddenDead READ hiddenDead NOTIFY countChanged)
    /// "seeds" (most seeds, then newest), "newest", "size", "name".
    Q_PROPERTY(QString order READ order WRITE setOrder NOTIFY orderChanged)
    Q_PROPERTY(bool hideDead READ hideDead WRITE setHideDead NOTIFY hideDeadChanged)
    /// `/`'s words: narrows the rows without asking again (SR14).
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    /// The cursor's row: what lands above it is counted (SR5).
    Q_PROPERTY(QString anchorKey READ anchorKey WRITE setAnchorKey NOTIFY anchorKeyChanged)
    Q_PROPERTY(int landedAbove READ landedAbove NOTIFY landedChanged)
    Q_PROPERTY(int foldedAbove READ foldedAbove NOTIFY landedChanged)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        MetaRole, ///< "1.46 GB · 3 days ago"
        SizeTextRole,
        AgeTextRole,
        SeedsRole,
        SeedsTextRole, ///< "41", "?", "0"
        SeedsCaptionRole,
        HealthRole,   ///< "good", "few", "unknown", "dead"
        SentenceRole, ///< line 2
        SentenceShortRole,
        PluginsRole, ///< "plugin a and plugin c"
        CopiesRole,
        TrackerCountRole,
        LeechersRole,
        IsLinkRole,
        MarkRole,     ///< "library", "disk", "added", ""
        MarkTextRole, ///< "in your library", "likely on disk", "added"
        FreshRole,    ///< "new", "copy", ""
        AddWordRole,  ///< "add…", or "trackers…" on one you have
        LinkRole,     ///< the folded magnet, or the link
        PageRole,
        HashRole,
        LibraryHashRole,
        PluginRole, ///< the naming copy's plugin (`downloader`)
        PrivateHostRole,
    };
    Q_ENUM(Role)

    /// What the library says about a row: `hash` and `words` ("arriving at 87 %") when its
    /// hash is a torrent you have; `name` when one of yours has its name and size.
    struct LibraryMatch
    {
        QString hash;
        QString words;
        QString name;
    };
    using LibraryLookup = std::function<LibraryMatch(const SearchRow &)>;

    explicit SearchModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int torrentCount() const { return int(m_all.size()); }
    int foldedCount() const;
    int hiddenDead() const { return m_hiddenDead; }
    QString order() const { return SearchResults::orderName(m_order); }
    void setOrder(const QString &order);
    bool hideDead() const { return m_hideDead; }
    void setHideDead(bool hide);
    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &text);
    QString anchorKey() const { return m_anchorKey; }
    void setAnchorKey(const QString &key);
    int landedAbove() const { return m_landedAbove; }
    int foldedAbove() const { return m_foldedAbove; }

    /// The session's hits, folded and shown. `privateHostOf` keeps private copies apart (SR3).
    void setHits(const QList<SearchHit> &hits);
    /// A new search: nothing it brings first is "new", nothing is "added" yet.
    void startOver();
    void setPrivateHostOf(std::function<QString(const QStringList &)> privateHostOf);
    void setLibraryLookup(LibraryLookup lookup);
    /// A plugin's own name (fullName), for line 2.
    void setPluginNames(const QHash<QString, QString> &names);
    /// The library or the plugins moved: the marks and words again.
    Q_INVOKABLE void refreshMarks();
    /// The add sheet sent `key`'s torrent (SR7's "added").
    Q_INVOKABLE void markAdded(const QString &key);
    /// SR5's line was read: the counts go back to 0.
    Q_INVOKABLE void clearLanded();
    /// Test seam: the clock ages read from (seconds), and how long new / +1 copy last.
    void setNow(std::function<qint64()> now) { m_now = std::move(now); }
    void setFreshMs(int ms) { m_freshTimer.setInterval(ms); }

    Q_INVOKABLE int indexOfKey(const QString &key) const;
    /// A row's roles by name, for the view's result pane and the add hand-off.
    Q_INVOKABLE QVariantMap get(int row) const;
    /// The row `key`, with its copies (`{plugin, name, seeds, trackers, naming}`) and trackers.
    Q_INVOKABLE QVariantMap details(const QString &key) const;
    const SearchRow *rowFor(const QString &key) const;

Q_SIGNALS:
    void countChanged();
    void orderChanged();
    void hideDeadChanged();
    void filterTextChanged();
    void anchorKeyChanged();
    void landedChanged();
    /// Around every change of the rows: the view keeps its cursor's row in place (SR5).
    void aboutToRefresh();
    void refreshed();

private:
    struct Shown
    {
        SearchRow row;
        LibraryMatch match;
        QString fresh;
    };

    void rebuild(bool countLanded);
    QString sentence(const Shown &shown, bool narrow) const;
    QString pluginText(const QStringList &plugins) const;
    QString markOf(const Shown &shown) const;

    QList<SearchHit> m_hits;
    QList<SearchRow> m_all; ///< folded, every row
    QList<Shown> m_rows;    ///< sorted, filtered: what the view lists
    std::function<QString(const QStringList &)> m_privateHostOf;
    LibraryLookup m_lookup;
    QHash<QString, QString> m_pluginNames;
    QHash<QString, QString> m_added;     ///< key -> "HH:mm" it was added
    QHash<QString, qint64> m_freshUntil; ///< key -> ms since epoch
    QHash<QString, QString> m_freshKind;
    QHash<QString, int> m_copiesSeen;
    std::function<qint64()> m_now;
    QTimer m_freshTimer;
    SearchResults::Order m_order = SearchResults::Order::MostSeeds;
    bool m_hideDead = false;
    QString m_filterText;
    QString m_anchorKey;
    int m_hiddenDead = 0;
    int m_landedAbove = 0;
    int m_foldedAbove = 0;
};

} // namespace omaseed::core
