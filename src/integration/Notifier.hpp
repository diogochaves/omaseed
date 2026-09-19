#pragma once

#include <QDBusConnection>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QDBusInterface;

namespace omaseed::integration {

/// Talks to org.freedesktop.Notifications directly (QDBusInterface/QDBusMessage, not
/// QSystemTrayIcon::showMessage, which cannot carry actions). One live notification is kept
/// per (event class, subject id): a second notice for the same key replaces the first in
/// place (replaces_id), and the key is forgotten once the shell reports the notice closed.
///
/// If the notification service is absent, the first call logs once and every notice after
/// that is silently dropped.
class Notifier : public QObject
{
    Q_OBJECT

public:
    explicit Notifier(QDBusConnection connection = QDBusConnection::sessionBus(),
                      QObject *parent = nullptr);

    /// "<name> · <sizeText> · <category>". Actions are "open folder" (only when a folder is
    /// known) and "show". Event class "completed", subject the torrent hash.
    quint32 notifyCompleted(const QString &hash,
                            const QString &name,
                            const QString &sizeText,
                            const QString &category,
                            bool hasFolder);

    /// "<host> · timed out for N torrent(s)". Actions "show trackers" and "reannounce".
    /// Event class "tracker-error", subject the tracker host: a second call for the same
    /// host replaces the live notice instead of stacking another one.
    quint32 notifyTrackerError(const QString &host, int count);

    /// "<serverName> is unreachable" / "<hostText> · <reasonText> · retrying every N s".
    /// Action "settings". Event class "server", subject the server id.
    quint32 notifyServerLost(const QString &serverId,
                             const QString &serverName,
                             const QString &hostText,
                             const QString &reasonText,
                             int retrySeconds);

    /// The matching "back" toast: replaces the server-lost notice for the same server id.
    quint32 notifyServerBack(const QString &serverId, const QString &serverName);

    /// "<freeText> free". Action "settings". Event class "disk", subject the server id.
    quint32 notifyDiskAlmostFull(const QString &serverId, const QString &freeText);

signals:
    /// Fired when the shell reports ActionInvoked for a notice this Notifier sent.
    /// eventClass/subjectId are the values the corresponding notify*() call was made with;
    /// actionKey is one of open-folder, show, show-trackers, reannounce, settings.
    void
    actionInvoked(const QString &eventClass, const QString &subjectId, const QString &actionKey);

    /// Fired when the shell reports NotificationClosed for a notice this Notifier sent (the
    /// point at which its key is forgotten, so the next notice for the same key starts fresh).
    void noticeClosed(const QString &eventClass, const QString &subjectId);

private slots:
    void onActionInvoked(uint id, const QString &actionKey);
    void onNotificationClosed(uint id, uint reason);

private:
    struct LiveNotice
    {
        QString key;
        QString eventClass;
        QString subjectId;
    };

    bool ensureServiceAvailable();
    quint32 sendNotice(const QString &key,
                       const QString &eventClass,
                       const QString &subjectId,
                       const QString &summary,
                       const QString &body,
                       const QStringList &actions,
                       uchar urgency,
                       const QString &category);

    QDBusConnection m_connection;
    QDBusInterface *m_interface = nullptr;
    bool m_serviceChecked = false;
    bool m_serviceAvailable = false;
    QHash<QString, quint32> m_liveIdByKey;
    QHash<quint32, LiveNotice> m_liveNoticeById;
};

} // namespace omaseed::integration
