#include "integration/MotionPreference.hpp"

#include "integration/HyprlandEvents.hpp"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace omaseed::integration {

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kInterface = QStringLiteral("org.freedesktop.portal.Settings");
const QString kNamespace = QStringLiteral("org.freedesktop.appearance");
const QString kKey = QStringLiteral("reduced-motion");

} // namespace

QString MotionPreference::defaultEventSocket()
{
    return HyprlandEvents::defaultSocketPath();
}

MotionPreference::MotionPreference(QDBusConnection connection, QObject *parent)
    : MotionPreference(std::move(connection), Hyprland{}, parent)
{}

MotionPreference::MotionPreference(QDBusConnection connection,
                                   const Hyprland &hyprland,
                                   QObject *parent)
    : QObject(parent)
    , m_connection(std::move(connection))
    , m_hyprland(hyprland)
{
    readPortal();
    if (m_hyprland.hyprctl.isEmpty())
        return;
    readHyprland();
    // #111: the toggle (and a theme or profile switch) reloads Hyprland's config; ask again
    // then, the way Theme asks for the rounding (#37).
    if (!m_hyprland.eventSocket.isEmpty()) {
        m_hyprlandEvents = new HyprlandEvents(m_hyprland.eventSocket, this);
        connect(m_hyprlandEvents,
                &HyprlandEvents::configReloaded,
                this,
                &MotionPreference::readHyprland);
    }
}

MotionPreference::~MotionPreference()
{
    // Leaving hyprctl running past our own destructor makes QProcess complain on exit.
    for (QProcess *hyprctl : findChildren<QProcess *>(Qt::FindDirectChildrenOnly)) {
        if (hyprctl->state() != QProcess::NotRunning) {
            hyprctl->kill();
            hyprctl->waitForFinished(100);
        }
    }
}

void MotionPreference::readPortal()
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
            update(parse(QVariant::fromValue(reply.value())), m_animationsOff);
        w->deleteLater();
    });
}

void MotionPreference::readHyprland()
{
    // Two reloads in a row: only the last answer counts.
    const int read = ++m_hyprlandRead;
    auto *hyprctl = new QProcess(this);
    connect(hyprctl,
            &QProcess::finished,
            this,
            [this, hyprctl, read](int exitCode, QProcess::ExitStatus status) {
                hyprctl->deleteLater();
                if (read != m_hyprlandRead)
                    return;
                const bool off = exitCode == 0 && status == QProcess::NormalExit &&
                                 animationsOff(hyprctl->readAllStandardOutput());
                update(m_portalReduced, off);
            });
    // No hyprctl at all: no Hyprland, so nothing it says. (A crash also reports here, then
    // finishes.)
    connect(hyprctl, &QProcess::errorOccurred, this, [this, hyprctl](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        hyprctl->deleteLater();
        update(m_portalReduced, false);
    });
    hyprctl->start(
        m_hyprland.hyprctl,
        {QStringLiteral("-j"), QStringLiteral("getoption"), QStringLiteral("animations:enabled")});
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

bool MotionPreference::animationsOff(const QByteArray &getoption)
{
    // `getoption` answers {"option": "animations:enabled", "bool": false, "set": true}; an
    // unknown option is the plain text "no such option", which is not an object.
    const QJsonObject answer = QJsonDocument::fromJson(getoption).object();
    const QJsonValue flag = answer.value(QStringLiteral("bool"));
    if (flag.isBool())
        return !flag.toBool();
    const QJsonValue number = answer.value(QStringLiteral("int"));
    return number.isDouble() && number.toInt() == 0;
}

void MotionPreference::onSettingChanged(const QString &ns,
                                        const QString &key,
                                        const QDBusVariant &value)
{
    if (ns == kNamespace && key == kKey)
        update(parse(value.variant()), m_animationsOff);
}

void MotionPreference::update(bool portalReduced, bool animationsOff)
{
    const bool before = reduced();
    m_portalReduced = portalReduced;
    m_animationsOff = animationsOff;
    if (reduced() != before)
        Q_EMIT reducedChanged();
}

} // namespace omaseed::integration
