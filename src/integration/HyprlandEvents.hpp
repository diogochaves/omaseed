#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QLocalSocket;
QT_END_NAMESPACE

namespace omaseed::integration {

/// Hyprland's event socket (`.socket2.sock`), one line per event: `name>>data`. omaseed only
/// needs `configreloaded` (#37: a theme or profile switch reloads Hyprland's config, and the
/// rounding may change with it), but every event is passed on.
///
/// Inert without a socket path (not under Hyprland). When Hyprland isn't answering, or goes
/// away (a restart), it tries again quietly, waiting longer each time up to half a minute.
class HyprlandEvents : public QObject
{
    Q_OBJECT

public:
    /// `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock`, or "" when this
    /// process doesn't run under Hyprland.
    static QString defaultSocketPath();

    explicit HyprlandEvents(const QString &socketPath, QObject *parent = nullptr);

    QString socketPath() const { return m_path; }
    bool isConnected() const;

Q_SIGNALS:
    void event(const QString &name, const QString &data);
    void configReloaded();
    void connected();

private:
    void connectToHyprland();
    void readLines();
    void retryLater();

    QString m_path;
    QLocalSocket *m_socket = nullptr;
    QTimer m_retry;
    QByteArray m_pending;
    int m_retryMs = 0;
};

} // namespace omaseed::integration
