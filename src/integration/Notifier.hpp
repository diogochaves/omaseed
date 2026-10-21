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
/// If the notification service is absent, the call logs and the notice is dropped; notices
/// are dropped until the service's owner changes (the shell starts or restarts), when it is
/// asked again. An owner change also forgets every live id: a new daemon numbers from 1
/// again. Calls are synchronous, with a 2 s timeout.
class Notifier : public QObject
{
    Q_OBJECT

public:
    explicit Notifier(QDBusConnection connection = QDBusConnection::sessionBus(),
                      QObject *parent = nullptr);

    /// "<name> · <sizeText> · <category>". Actions are "Open folder" (only when a folder is
    /// known) and "Show", which is the click. Event class "completed", subject the torrent hash.
    quint32 notifyCompleted(const QString &hash,
                            const QString &name,
                            const QString &sizeText,
                            const QString &category,
                            bool hasFolder);

    /// docs/28 N1: "<name> stopped with an error" / "<headline>: <detail>". Action "show",
    /// also the click. Event class "errored", subject the torrent hash.
    quint32 notifyErrored(const QString &hash,
                          const QString &name,
                          const QString &headline,
                          const QString &detail);

    /// docs/33 D22, D23: the window's headline and line, in ErrorWords' words ("private
    /// didn't answer" / "Nothing is listening at 127.0.0.1:8091. omaseed tries again in
    /// 10 s."), so the notice and the window say the same with the same retry figure.
    /// Action "settings", also the click. Event class "server", subject the server id.
    quint32 notifyServerLost(const QString &serverId, const QString &headline, const QString &body);

    /// The matching "back" toast: replaces the server-lost notice for the same server id.
    quint32 notifyServerBack(const QString &serverId, const QString &serverName);

    /// "<freeText> free". Action "settings", also the click. Event class "disk", subject the
    /// server id.
    quint32 notifyDiskAlmostFull(const QString &serverId, const QString &freeText);

    /// #24 T8: the weekly recap, `summary` / `body` as WeeklyRecap words them. Action
    /// "console" ("Open the console"), also the click. Event class "recap", subject the
    /// server id.
    quint32 notifyWeeklyRecap(const QString &serverId, const QString &summary, const QString &body);

    /// Board 42 E2 (DECISIONS C15 M16): a folder's move, once: at its end ("games moved" /
    /// "385 moved, 2 failed · 149 loose files carried"), or when it paused itself (T2–T4).
    /// Action "show", also the click. Event class "move", subject the server id.
    quint32 notifyMove(const QString &serverId, const QString &summary, const QString &body);

signals:
    /// Fired when the shell reports ActionInvoked for a notice this Notifier sent.
    /// eventClass/subjectId are the values the corresponding notify*() call was made with;
    /// actionKey is one of open-folder, show, settings, console.
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
        QString defaultAction; ///< what a click on the notice body ("default") stands for
    };

    bool ensureServiceAvailable();
    quint32 sendNotice(const QString &key,
                       const QString &eventClass,
                       const QString &subjectId,
                       const QString &summary,
                       const QString &body,
                       const QStringList &actions,
                       uchar urgency,
                       const QString &category,
                       const QString &defaultAction = QString());

    QDBusConnection m_connection;
    QDBusInterface *m_interface = nullptr;
    bool m_serviceChecked = false;
    bool m_serviceAvailable = false;
    QHash<QString, quint32> m_liveIdByKey;
    QHash<quint32, LiveNotice> m_liveNoticeById;
};

} // namespace omaseed::integration
