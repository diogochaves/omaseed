#include "integration/AgentClient.hpp"

#include "core/Version.hpp"
#include "integration/AppIdentity.hpp"
#include "integration/StatusWriter.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace omaseed::integration::AgentClient {

namespace {

/// The running omaseed's version, from the status file the widget reads (plugin/STATUS.md):
/// that one field, for the too-old sentence only (C45 V5).
QString runningVersion()
{
    QFile file(StatusWriter::defaultPath());
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QJsonObject status = QJsonDocument::fromJson(file.readAll()).object();
    return status.value(QStringLiteral("app"))
        .toObject()
        .value(QStringLiteral("version"))
        .toString();
}

Reply failed(const QString &code, const QString &message)
{
    Reply reply;
    reply.code = code;
    reply.message = message;
    return reply;
}

} // namespace

QString interfaceName()
{
    return AppIdentity::dbusServiceName() + QStringLiteral(".Agent");
}

QString busAddress()
{
    const QString address = qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS");
    if (!address.isEmpty())
        return address;
    // Under systemd the user's bus is a socket in the runtime directory, even when a shell
    // (over SSH, say) wasn't given the variable.
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty())
        return QString();
    const QString socket = runtime + QStringLiteral("/bus");
    if (!QFileInfo(socket).exists())
        return QString();
    return QStringLiteral("unix:path=") + socket;
}

QString notRunningWords()
{
    return QStringLiteral("omaseed isn't running, so it can't answer. Open it (or start it in "
                          "the tray with omaseed --hidden) and ask again.");
}

QString noBusWords()
{
    return QStringLiteral("omaseed can't be reached: there's no session bus here. Run this in "
                          "the person's desktop session.");
}

QString noReplyWords(int timeoutMs)
{
    return QStringLiteral("omaseed didn't answer in %1 s. Ask again in a moment.")
        .arg(timeoutMs / 1000);
}

QString tooOldWords(const QString &running, const QString &mine)
{
    const QString which =
        running.isEmpty() ? QStringLiteral("The omaseed that's running is older")
                          : QStringLiteral("The omaseed that's running is %1, older").arg(running);
    return QStringLiteral("%1 than this one (%2). Quit it and open it again to answer.")
        .arg(which, mine);
}

Reply call(const QString &method, const QJsonObject &options, int timeoutMs)
{
    const QString address = busAddress();
    if (address.isEmpty())
        return failed(QStringLiteral("no-bus"), noBusWords());

    const QString connectionName =
        QStringLiteral("omaseed-word-%1").arg(QCoreApplication::applicationPid());
    Reply reply;
    {
        QDBusConnection connection = QDBusConnection::connectToBus(address, connectionName);
        const QString service = AppIdentity::dbusServiceName();
        if (!connection.isConnected() || !connection.interface()) {
            reply = failed(QStringLiteral("no-bus"), noBusWords());
        } else if (const QDBusReply<bool> owned =
                       connection.interface()->isServiceRegistered(service);
                   !owned.isValid() || !owned.value()) {
            reply = failed(QStringLiteral("not-running"), notRunningWords());
        } else {
            QDBusMessage message = QDBusMessage::createMethodCall(
                service, AppIdentity::dbusObjectPath(), interfaceName(), method);
            message << QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact));
            QDBusPendingCall pending = connection.asyncCall(message, timeoutMs);
            // Pumped, as SingleInstance's hand-off is: no event loop runs yet.
            QElapsedTimer timer;
            timer.start();
            while (!pending.isFinished() && timer.elapsed() < timeoutMs + 500)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            const QDBusError::ErrorType error =
                pending.isFinished() ? pending.error().type() : QDBusError::Timeout;
            switch (error) {
            case QDBusError::NoError: {
                const QDBusPendingReply<QString> answer = pending;
                if (answer.isValid()) {
                    reply.answer = answer.value().toUtf8();
                    break;
                }
                // An owner whose method answers in another shape isn't this omaseed's.
                reply = failed(QStringLiteral("too-old"),
                               tooOldWords(runningVersion(), core::version()));
                break;
            }
            case QDBusError::UnknownInterface:
            case QDBusError::UnknownMethod:
            case QDBusError::UnknownObject:
                reply = failed(QStringLiteral("too-old"),
                               tooOldWords(runningVersion(), core::version()));
                break;
            case QDBusError::ServiceUnknown:
            case QDBusError::Disconnected:
                // It quit while we asked.
                reply = failed(QStringLiteral("not-running"), notRunningWords());
                break;
            default:
                reply = failed(QStringLiteral("no-reply"), noReplyWords(timeoutMs));
                break;
            }
        }
    }
    QDBusConnection::disconnectFromBus(connectionName);
    return reply;
}

} // namespace omaseed::integration::AgentClient
