#pragma once

// TorrentWebSeedsModel — the details pane's HTTP sources tab (#10, docs/12 L1).
//
// `torrents/webseeds` answers `[{"url": "https://mirror.example/pub/"}, …]`: the torrent's
// web seeds (BEP 19 `url-list`, BEP 17 `httpseeds`, and any added later). The rows keep the
// server's order. `text` is what the tab prints: the URL without `https://`, the usual case;
// a plain `http://` source keeps its scheme, so it stays visible without being coloured as a
// fault (the pieces' hashes check what any source sends). The whole URL travels in `url`,
// for the tooltip, `y` and `d`.
//
// `loaded` says whether the list for the current torrent has come back yet: the tab is only
// shown when there are web seeds, and while a newly selected torrent's list is on its way the
// pane keeps the tab rather than dropping it and losing the place (TorrentDetails clears the
// model, and so `loaded`, on every selection change).
//
// docs/32 W1 (#55): under each URL, what qBittorrent's log says about it. The API never
// reports a web seed's health, so a URL with no line says "no errors logged", never
// "working". `statusError` is the error's own words ("404 File not found", "host not found"),
// `statusMeta` when, and how often ("today 09:40", "10 times, last 3 Oct 22:59"). The lines
// come from ProblemLog through setLogLines(), for whichever torrent the pane shows; a line
// about a file is the seed's whose folder holds it.

#include "core/TorrentProblem.hpp"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>

namespace omaseed::core {

class TorrentWebSeedsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)

public:
    enum Role {
        UrlRole = Qt::UserRole + 1,
        TextRole,
        HostRole,
        StatusErrorRole, ///< docs/32 W1: the newest error's words, or empty
        StatusMetaRole,  ///< "today 09:40", "10 times, last …", or "no errors logged"
    };
    Q_ENUM(Role)

    explicit TorrentWebSeedsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_urls.size()); }
    bool loaded() const { return m_loaded; }

    /// The torrent's web seed URLs; empty ones are skipped. Sets `loaded`.
    void applyWebSeeds(const QStringList &webSeeds);
    /// No torrent, or another one on its way: no rows, not loaded. The log lines stay: the
    /// caller hands over the next torrent's with setLogLines().
    void clear();
    /// docs/32 W1: the URL seed lines about the torrent shown, oldest first.
    void setLogLines(const QList<WebSeedLogLine> &lines);

    /// The whole URL of a row, for `y` and `d`; empty out of range.
    Q_INVOKABLE QString urlAt(int row) const;

    /// "https://mirror.example/pub/" -> "mirror.example/pub/"; any other URL (plain http
    /// included) comes back unchanged.
    static QString displayText(const QString &url);

Q_SIGNALS:
    void countChanged();
    void loadedChanged();

private:
    struct Status
    {
        QString error;
        QString meta;
    };

    void setLoaded(bool loaded);
    /// Each row's status from m_logLines, in row order.
    QList<Status> statuses() const;

    QList<QString> m_urls;
    bool m_loaded = false;
    QList<WebSeedLogLine> m_logLines;
    QList<Status> m_statuses;
};

} // namespace omaseed::core
