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

#include <QAbstractListModel>
#include <QJsonArray>
#include <QList>
#include <QString>

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
    };
    Q_ENUM(Role)

    explicit TorrentWebSeedsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_urls.size()); }
    bool loaded() const { return m_loaded; }

    /// The answer of `torrents/webseeds`; entries without a `url` are skipped. Sets `loaded`.
    void applyWebSeeds(const QJsonArray &webSeeds);
    /// No torrent, or another one on its way: no rows, not loaded.
    void clear();

    /// The whole URL of a row, for `y` and `d`; empty out of range.
    Q_INVOKABLE QString urlAt(int row) const;

    /// "https://mirror.example/pub/" -> "mirror.example/pub/"; any other URL (plain http
    /// included) comes back unchanged.
    static QString displayText(const QString &url);

Q_SIGNALS:
    void countChanged();
    void loadedChanged();

private:
    void setLoaded(bool loaded);

    QList<QString> m_urls;
    bool m_loaded = false;
};

} // namespace omaseed::core
