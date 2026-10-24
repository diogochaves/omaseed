#pragma once

// SearchPlugins — the server's search plugins, for Settings › qBittorrent › Search plugins and
// the search view (#145, board 48 §5, DECISIONS C26 SR10, SR11).
//
// The plugins are the user's, on their server: omaseed shows each one's own name (fullName),
// version and types as qBittorrent lists them, and names, suggests or links to none (SR11).
//
// qBittorrent answers an install with 200 whatever happens, and a bad file leaves no trace, not
// even in its log; so after `install()` omaseed watches the list for 30 s and says "installed"
// once a plugin it hadn't listed (or a new version of one) shows, or "didn't install" (SR10).
// `checkUpdates()` asks qBittorrent to update the plugins on its own project's list; the outcome
// is only in its log, which omaseed reads for 30 s.

#include <QAbstractListModel>
#include <QHash>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

namespace omaseed::core {

class QbtClient;

class SearchPlugins : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int enabledCount READ enabledCount NOTIFY countChanged)
    /// The list has come in once from this server.
    Q_PROPERTY(bool loaded READ loaded NOTIFY countChanged)
    /// The install being watched: "" (none), "installing", "installed", "failed".
    Q_PROPERTY(QString installState READ installState NOTIFY installChanged)
    Q_PROPERTY(QString installSource READ installSource NOTIFY installChanged)
    /// What was installed: the plugin's own name.
    Q_PROPERTY(QString installName READ installName NOTIFY installChanged)
    Q_PROPERTY(int installSeconds READ installSeconds NOTIFY installChanged)
    /// The update check: "" (none), "checking", "done", "silent" (nothing in the log in 30 s).
    Q_PROPERTY(QString updateState READ updateState NOTIFY updateChanged)
    /// The log's lines about the check, joined, with the time it answered.
    Q_PROPERTY(QString updateText READ updateText NOTIFY updateChanged)
    /// The types the plugins that are on know, `[{id, name}]` in qBittorrent's order (SR6).
    Q_PROPERTY(QVariantList types READ types NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        FullNameRole,
        VersionRole,
        UrlRole,
        EnabledRole,
        TypesTextRole,  ///< "movies · music · software"
        CategoriesRole, ///< the ids, "all" left out
        NoteRole,       ///< "turned off · it failed in the last search, 16:01", "updated 16:20"
    };
    Q_ENUM(Role)

    explicit SearchPlugins(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setClient(QbtClient *client);
    /// Test seams: the install and update watches' poll and length.
    void setWatch(int pollMs, int lengthMs);

    int enabledCount() const;
    bool loaded() const { return m_loaded; }
    QString installState() const { return m_installState; }
    QString installSource() const { return m_installSource; }
    QString installName() const { return m_installName; }
    int installSeconds() const { return m_installSeconds; }
    QString updateState() const { return m_updateState; }
    QString updateText() const { return m_updateText; }
    QVariantList types() const;

    /// The list again (search/plugins).
    Q_INVOKABLE void refresh();
    /// A switch (enablePlugin), then the list again.
    Q_INVOKABLE void setEnabled(const QString &name, bool enabled);
    Q_INVOKABLE void uninstall(const QString &name);
    /// A URL, or a path on the server's own machine (SR10).
    Q_INVOKABLE void install(const QString &source);
    Q_INVOKABLE void dismissInstall();
    Q_INVOKABLE void checkUpdates();
    /// A line under a plugin's row (the search view turned it off, an update).
    Q_INVOKABLE void setNote(const QString &name, const QString &note);
    /// Another server: the list, the watches and the notes go.
    void clear();
    /// search/plugins' answer, as if it had come in (the demo's plugins).
    void applyList(const QJsonArray &list) { apply(list); }

    /// `{name, fullName, categories}` of each plugin that is on.
    QVariantList enabledPlugins() const;
    /// name -> fullName, every plugin.
    QHash<QString, QString> names() const;
    /// The update check's words from log/main's lines since `sinceSecs`: "" when there are
    /// none yet.
    static QString updateWords(const QJsonArray &lines, qint64 sinceSecs);

Q_SIGNALS:
    void countChanged();
    void installChanged();
    void updateChanged();

private:
    struct Plugin
    {
        QString name;
        QString fullName;
        QString version;
        QString url;
        bool enabled = true;
        QVariantList categories; // [{id, name}]
    };

    void apply(const QJsonArray &list);
    void watchInstall();
    void watchUpdate();

    QPointer<QbtClient> m_client;
    QList<Plugin> m_plugins;
    QHash<QString, QString> m_notes;
    bool m_loaded = false;
    quint64 m_generation = 0;

    int m_watchPollMs = 1000;
    int m_watchLengthMs = 30000;

    QTimer m_installTimer;
    QString m_installState;
    QString m_installSource;
    QString m_installName;
    int m_installSeconds = 0;
    qint64 m_installStartMs = 0;
    QHash<QString, QString> m_versionsBefore;

    QTimer m_updateTimer;
    QString m_updateState;
    QString m_updateText;
    qint64 m_updateStartMs = 0;
    qint64 m_updateSinceSecs = 0;
};

} // namespace omaseed::core
