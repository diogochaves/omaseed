#include "integration/MotionPreference.hpp"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace omaseed::integration {

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kInterface = QStringLiteral("org.freedesktop.portal.Settings");
const QString kNamespace = QStringLiteral("org.freedesktop.appearance");
const QString kKey = QStringLiteral("reduced-motion");

} // namespace

MotionPreference::MotionPreference(QDBusConnection connection, QObject *parent)
    : QObject(parent)
    , m_connection(std::move(connection))
{
    if (!m_connection.isConnected())
        return;
    m_connection.connect(kService,
                         kPath,
                         kInterface,
                         QStringLiteral("SettingChanged"),
                         this,
                         SLOT(onSettingChanged(QString, QString, QDBusVariant)));

    QDBusMessage call =
        QDBusMessage::createMethodCall(kService, kPath, kInterface, QStringLiteral("ReadOne"));
    call << kNamespace << kKey;
    // Never wait on the GUI thread: a portal that starts slowly answers when it can.
    auto *watcher = new QDBusPendingCallWatcher(m_connection.asyncCall(call, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<QDBusVariant> reply = *w;
        if (!reply.isError())
            setReduced(parse(QVariant::fromValue(reply.value())));
        w->deleteLater();
    });
}

bool MotionPreference::parse(const QVariant &value)
{
    QVariant v = value;
    while (v.canConvert<QDBusVariant>() && v.userType() == qMetaTypeId<QDBusVariant>())
        v = v.value<QDBusVariant>().variant();
    bool ok = false;
    const uint n = v.toUInt(&ok);
    return ok && n == 1;
}

void MotionPreference::onSettingChanged(const QString &ns,
                                        const QString &key,
                                        const QDBusVariant &value)
{
    if (ns == kNamespace && key == kKey)
        setReduced(parse(value.variant()));
}

void MotionPreference::setReduced(bool reduced)
{
    if (m_reduced == reduced)
        return;
    m_reduced = reduced;
    Q_EMIT reducedChanged();
}

} // namespace omaseed::integration
