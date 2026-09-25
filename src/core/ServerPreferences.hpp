#pragma once

// ServerPreferences — qBittorrent's own settings for the server omaseed is on (docs/16).
//
// It keeps the last `app/preferences` answer as a QVariantMap for the Settings group to draw,
// and writes one change at a time through `app/setPreferences`, which leaves every key it is
// not given alone. The rules are docs/16 §3's:
//   * A change shows at once (the row does not wait), and its keys are "pending" until the
//     server answers.
//   * Refused (any error), the keys go back to what they were and `applyFailed` says why.
//   * Accepted, the preferences are read again. A key the server did not take (qBittorrent
//     often ignores a value it does not like rather than refusing it) then shows the
//     server's value, and `applied` names it in `kept`.
//   * Nothing is read on every sync: `reload()` runs when Settings asks, after each change,
//     and when App connects to a server.
// `reset()` forgets everything, for a server switch; answers still on their way for the
// previous server are dropped.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace omaseed::core {

class QbtClient;

class ServerPreferences : public QObject
{
    Q_OBJECT
    /// Every key the server sent, with the pending changes laid over them.
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    /// At least one answer has landed since the last `reset()`.
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY stateChanged)
    /// Why the last read failed, "" when it did not.
    Q_PROPERTY(QString loadError READ loadError NOTIFY stateChanged)
    /// Keys written and not answered yet.
    Q_PROPERTY(QStringList pendingKeys READ pendingKeys NOTIFY pendingChanged)
    /// `app/networkInterfaceList`: [{name, value}], "" first for "any".
    Q_PROPERTY(QVariantList networkInterfaces READ networkInterfaces NOTIFY interfacesChanged)
    /// `app/networkInterfaceAddressList` for the interface asked last: ["", "1.2.3.4", …].
    Q_PROPERTY(QStringList interfaceAddresses READ interfaceAddresses NOTIFY interfacesChanged)

public:
    explicit ServerPreferences(QbtClient *client, QObject *parent = nullptr);

    QVariantMap values() const { return m_values; }
    bool isLoaded() const { return m_loaded; }
    bool isLoading() const { return m_loading; }
    QString loadError() const { return m_loadError; }
    QStringList pendingKeys() const { return m_pending.keys(); }
    QVariantList networkInterfaces() const { return m_interfaces; }
    QStringList interfaceAddresses() const { return m_addresses; }

    /// The server's version has this key (a row for a key it lacks is hidden).
    Q_INVOKABLE bool has(const QString &key) const { return m_values.contains(key); }
    Q_INVOKABLE QVariant value(const QString &key) const { return m_values.value(key); }

    /// GET app/preferences.
    Q_INVOKABLE void reload();
    /// POST app/setPreferences with exactly these keys, then read them back.
    Q_INVOKABLE void apply(const QVariantMap &changes);
    /// GET app/networkInterfaceList, then the addresses of `iface` ("" for every interface).
    Q_INVOKABLE void loadInterfaces(const QString &iface = QString());
    /// POST app/rotateAPIKey (qBittorrent 5.2+): a new Web UI API key; `apiKeyRotated` has it.
    Q_INVOKABLE void rotateApiKey();

    /// Forget the server: no values, nothing pending, and late answers are dropped.
    void reset();
    /// The demo's server: these values, and changes that land at once with no client.
    void setOffline(const QVariantMap &values);

    /// `a == b` the way the server's JSON means it: 10240 and 10240.0 are the same number.
    static bool sameValue(const QVariant &a, const QVariant &b);

Q_SIGNALS:
    void valuesChanged();
    void stateChanged();
    void pendingChanged();
    void interfacesChanged();
    /// The server accepted the POST (before the read-back). App follows a Web UI port or
    /// login change here, since the read-back may already go to a server that moved.
    /// `before` holds what each key showed before the change.
    void posted(const QVariantMap &changes, const QVariantMap &before);
    /// The read-back after a change landed. `kept` lists the keys the server left as they
    /// were instead of taking the value sent.
    void applied(const QStringList &keys, const QStringList &kept);
    void applyFailed(const QStringList &keys, const QString &error);
    void apiKeyRotated(const QString &key);
    void apiKeyRotateFailed(const QString &error);

private:
    void setLoading(bool loading);
    /// Reads the preferences and hands the keys of `sent` to `applied` once they land.
    void readBack(const QVariantMap &sent);

    QbtClient *m_client = nullptr;
    QVariantMap m_values;
    /// key -> the value it had before the change on its way.
    QVariantMap m_pending;
    QVariantList m_interfaces;
    QStringList m_addresses;
    QString m_loadError;
    quint64 m_generation = 0;
    int m_reads = 0;
    bool m_loaded = false;
    bool m_loading = false;
    bool m_offline = false;
};

} // namespace omaseed::core
