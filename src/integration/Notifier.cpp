#include "integration/Notifier.hpp"

#include "integration/AppIdentity.hpp"

#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
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
constexpr int kCallTimeoutMs = 2000;

} // namespace

Notifier::Notifier(QDBusConnection connection, QObject *parent)
    : QObject(parent)
    , m_connection(std::move(connection))
    , m_interface(new QDBusInterface(kService, kPath, kInterface, m_connection, this))
{
    // Every call is synchronous (callers use the id it returns); never let a wedged daemon
    // (the shell restarting) hold the GUI thread for Qt's default 25 s.
    m_interface->setTimeout(kCallTimeoutMs);

    // A notification daemon that restarts (or first appears after us) starts its ids again
    // at 1: the ids we keep would name *its* notices, perhaps another app's, so forget them,
    // and ask again whether the service exists.
    auto *watcher = new QDBusServiceWatcher(
        kService, m_connection, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this]() {
        m_liveIdByKey.clear();
        m_liveNoticeById.clear();
        m_serviceChecked = false;
    });

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
                             const QString &category,
                             const QString &defaultAction)
{
    if (!ensureServiceAvailable())
        return 0;

    const quint32 replacesId = m_liveIdByKey.value(key, 0);

    // Omarchy's own notifications draw no buttons: a click on the toast invokes the action
    // named "default" and nothing else, so the main button is offered under that name too.
    // Servers that draw buttons (mako, dunst, GNOME, KDE) do not draw "default" as one.
    QStringList allActions = actions;
    if (!defaultAction.isEmpty()) {
        const qsizetype at = actions.indexOf(defaultAction);
        const QString label =
            at >= 0 && at + 1 < actions.size() ? actions.at(at + 1) : defaultAction;
        allActions << QStringLiteral("default") << label;
    }

    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), AppIdentity::applicationId());
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue(urgency));
    if (!category.isEmpty())
        hints.insert(QStringLiteral("category"), category);

    const QDBusReply<uint> reply =
        m_interface->call(QStringLiteral("Notify"),
                          omaseed::integration::AppIdentity::displayName(),
                          replacesId,
                          AppIdentity::applicationId(),
                          summary,
                          body,
                          allActions,
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
    m_liveNoticeById.insert(id, LiveNotice{key, eventClass, subjectId, defaultAction});
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
                      QStringLiteral("transfer.complete"),
                      QStringLiteral("show"));
}

quint32 Notifier::notifyErrored(const QString &hash,
                                const QString &name,
                                const QString &headline,
                                const QString &detail)
{
    const QString key = QStringLiteral("errored|%1").arg(hash);
    const QString summary = QStringLiteral("%1 stopped with an error").arg(name);
    const QString body =
        detail.isEmpty() ? headline : QStringLiteral("%1: %2").arg(headline, detail);
    const QStringList actions{QStringLiteral("show"), QStringLiteral("show")};

    return sendNotice(key,
                      QStringLiteral("errored"),
                      hash,
                      summary,
                      body,
                      actions,
                      kUrgencyNormal,
                      QStringLiteral("transfer.error"),
                      QStringLiteral("show"));
}

quint32
Notifier::notifyServerLost(const QString &serverId, const QString &headline, const QString &body)
{
    const QString key = QStringLiteral("server|%1").arg(serverId);
    const QString summary = headline;

    const QStringList actions{QStringLiteral("settings"), QStringLiteral("settings")};

    return sendNotice(key,
                      QStringLiteral("server"),
                      serverId,
                      summary,
                      body,
                      actions,
                      kUrgencyNormal,
                      QStringLiteral("network.disconnected"),
                      QStringLiteral("settings"));
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
                      QString(),
                      QStringLiteral("settings"));
}

void Notifier::onActionInvoked(uint id, const QString &actionKey)
{
    const auto it = m_liveNoticeById.constFind(id);
    if (it == m_liveNoticeById.constEnd())
        return;
    const bool isDefault = actionKey == QStringLiteral("default");
    if (isDefault && it->defaultAction.isEmpty())
        return;
    emit actionInvoked(it->eventClass, it->subjectId, isDefault ? it->defaultAction : actionKey);
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

quint32
Notifier::notifyWeeklyRecap(const QString &serverId, const QString &summary, const QString &body)
{
    const QStringList actions{QStringLiteral("console"), QStringLiteral("open the console")};
    return sendNotice(QStringLiteral("recap|%1").arg(serverId),
                      QStringLiteral("recap"),
                      serverId,
                      summary,
                      body,
                      actions,
                      kUrgencyLow,
                      QString(),
                      QStringLiteral("console"));
}

quint32 Notifier::notifyMove(const QString &serverId, const QString &summary, const QString &body)
{
    const QStringList actions{QStringLiteral("show"), QStringLiteral("show")};
    return sendNotice(QStringLiteral("move|%1").arg(serverId),
                      QStringLiteral("move"),
                      serverId,
                      summary,
                      body,
                      actions,
                      kUrgencyNormal,
                      QStringLiteral("transfer"),
                      QStringLiteral("show"));
}

} // namespace omaseed::integration
