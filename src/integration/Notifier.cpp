#include "integration/Notifier.hpp"

#include "integration/AppIdentity.hpp"

#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QVariant>
#include <QVariantMap>
#include <QtDebug>

namespace omaseed::integration {

namespace {

const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");
const QString kInterface = QStringLiteral("org.freedesktop.Notifications");

constexpr uchar kUrgencyLow = 0;
constexpr uchar kUrgencyNormal = 1;

} // namespace

Notifier::Notifier(QDBusConnection connection, QObject *parent)
    : QObject(parent)
    , m_connection(std::move(connection))
    , m_interface(new QDBusInterface(kService, kPath, kInterface, m_connection, this))
{
    m_connection.connect(kService,
                         kPath,
                         kInterface,
                         QStringLiteral("ActionInvoked"),
                         this,
                         SLOT(onActionInvoked(uint, QString)));
    m_connection.connect(kService,
                         kPath,
                         kInterface,
                         QStringLiteral("NotificationClosed"),
                         this,
                         SLOT(onNotificationClosed(uint, uint)));
}

bool Notifier::ensureServiceAvailable()
{
    if (m_serviceChecked)
        return m_serviceAvailable;
    m_serviceChecked = true;

    const QDBusMessage reply = m_interface->call(QStringLiteral("GetServerInformation"));
    m_serviceAvailable = reply.type() != QDBusMessage::ErrorMessage;
    if (!m_serviceAvailable) {
        qWarning().noquote() << QStringLiteral("Notifier: no org.freedesktop.Notifications service "
                                               "available (%1); notices will be dropped")
                                    .arg(reply.errorMessage());
    }
    return m_serviceAvailable;
}

quint32 Notifier::sendNotice(const QString &key,
                             const QString &eventClass,
                             const QString &subjectId,
                             const QString &summary,
                             const QString &body,
                             const QStringList &actions,
                             uchar urgency,
                             const QString &category)
{
    if (!ensureServiceAvailable())
        return 0;

    const quint32 replacesId = m_liveIdByKey.value(key, 0);

    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), AppIdentity::applicationId());
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue(urgency));
    if (!category.isEmpty())
        hints.insert(QStringLiteral("category"), category);

    const QDBusReply<uint> reply =
        m_interface->call(QStringLiteral("Notify"),
                          omaseed::integration::AppIdentity::applicationName(),
                          replacesId,
                          AppIdentity::applicationId(),
                          summary,
                          body,
                          actions,
                          hints,
                          -1);
    if (!reply.isValid()) {
        qWarning().noquote() << QStringLiteral("Notifier: Notify call failed:")
                             << reply.error().message();
        return 0;
    }

    const quint32 id = reply.value();
    if (replacesId != 0 && replacesId != id)
        m_liveNoticeById.remove(replacesId);

    m_liveIdByKey.insert(key, id);
    m_liveNoticeById.insert(id, LiveNotice{key, eventClass, subjectId});
    return id;
}

quint32 Notifier::notifyCompleted(const QString &hash,
                                  const QString &name,
                                  const QString &sizeText,
                                  const QString &category,
                                  bool hasFolder)
{
    const QString key = QStringLiteral("completed|%1").arg(hash);
    const QString body = QStringLiteral("%1 · %2 · %3").arg(name, sizeText, category);

    QStringList actions;
    if (hasFolder)
        actions << QStringLiteral("open-folder") << QStringLiteral("open folder");
    actions << QStringLiteral("show") << QStringLiteral("show");

    return sendNotice(key,
                      QStringLiteral("completed"),
                      hash,
                      QStringLiteral("Download complete"),
                      body,
                      actions,
                      kUrgencyLow,
                      QStringLiteral("transfer.complete"));
}

quint32 Notifier::notifyServerLost(const QString &serverId,
                                   const QString &serverName,
                                   const QString &hostText,
                                   const QString &reasonText,
                                   int retrySeconds)
{
    const QString key = QStringLiteral("server|%1").arg(serverId);
    const QString summary = QStringLiteral("%1 is unreachable").arg(serverName);
    const QString body =
        QStringLiteral("%1 · %2 · retrying every %3 s").arg(hostText, reasonText).arg(retrySeconds);

    const QStringList actions{QStringLiteral("settings"), QStringLiteral("settings")};

    return sendNotice(key,
                      QStringLiteral("server"),
                      serverId,
                      summary,
                      body,
                      actions,
                      kUrgencyNormal,
                      QStringLiteral("network.disconnected"));
}

quint32 Notifier::notifyServerBack(const QString &serverId, const QString &serverName)
{
    const QString key = QStringLiteral("server|%1").arg(serverId);
    const QString summary = QStringLiteral("%1 is back").arg(serverName);
    const QString body = QStringLiteral("connected again");

    return sendNotice(key,
                      QStringLiteral("server"),
                      serverId,
                      summary,
                      body,
                      QStringList{},
                      kUrgencyLow,
                      QStringLiteral("network.connected"));
}

quint32 Notifier::notifyDiskAlmostFull(const QString &serverId, const QString &freeText)
{
    const QString key = QStringLiteral("disk|%1").arg(serverId);
    const QString body = QStringLiteral("%1 free").arg(freeText);

    const QStringList actions{QStringLiteral("settings"), QStringLiteral("settings")};

    return sendNotice(key,
                      QStringLiteral("disk"),
                      serverId,
                      QStringLiteral("Disk almost full"),
                      body,
                      actions,
                      kUrgencyNormal,
                      QString());
}

void Notifier::onActionInvoked(uint id, const QString &actionKey)
{
    const auto it = m_liveNoticeById.constFind(id);
    if (it == m_liveNoticeById.constEnd())
        return;
    emit actionInvoked(it->eventClass, it->subjectId, actionKey);
}

void Notifier::onNotificationClosed(uint id, uint /*reason*/)
{
    const auto it = m_liveNoticeById.constFind(id);
    if (it == m_liveNoticeById.constEnd())
        return;
    m_liveIdByKey.remove(it->key);
    const QString eventClass = it->eventClass;
    const QString subjectId = it->subjectId;
    m_liveNoticeById.erase(it);
    emit noticeClosed(eventClass, subjectId);
}

} // namespace omaseed::integration
