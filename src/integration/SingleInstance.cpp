#include "integration/SingleInstance.hpp"

#include "integration/AppIdentity.hpp"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QTimer>

#include <utility>

namespace omaseed::integration {

namespace {

const char *kApplicationInterface = "org.freedesktop.Application";

} // namespace

/// Exposes `org.freedesktop.Application` on the owning `SingleInstance`'s object. Method
/// names and parameter types follow the freedesktop.org spec exactly (`a{sv}` platform_data
/// maps to QVariantMap, `as` to QStringList, `av` to QVariantList; all three are Qt D-Bus
/// built-in types, so no manual QDBusArgument marshalling is needed).
class SingleInstanceDBusAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

public:
    explicit SingleInstanceDBusAdaptor(SingleInstance *owner)
        : QDBusAbstractAdaptor(owner)
        , m_owner(owner)
    {}

public slots:
    void Activate(const QVariantMap &platform_data) { m_owner->handleActivate(platform_data); }

    void Open(const QStringList &uris, const QVariantMap &platform_data)
    {
        m_owner->handleOpen(uris, platform_data);
    }

    void ActivateAction(const QString &action_name,
                        const QVariantList &parameter,
                        const QVariantMap &platform_data)
    {
        // No actions are published yet; the tray (T1.5) and notifier (T1.6) menus will add
        // some later. Accepting and ignoring keeps us spec-compliant in the meantime.
        Q_UNUSED(action_name);
        Q_UNUSED(parameter);
        Q_UNUSED(platform_data);
    }

private:
    SingleInstance *m_owner;
};

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{}

SingleInstance::~SingleInstance()
{
    if (!m_connectionName.isEmpty())
        QDBusConnection::disconnectFromBus(m_connectionName);
    if (m_localServer)
        m_localServer->close();
}

bool SingleInstance::start(const QStringList &uris)
{
    const QString busAddress =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"));
    if (!busAddress.isEmpty())
        return startOnSessionBus(busAddress, uris);
    return startOnLocalSocket(uris);
}

bool SingleInstance::startOnSessionBus(const QString &busAddress, const QStringList &uris)
{
    // A private, per-instance connection name (rather than the QDBusConnection::sessionBus()
    // singleton) so two SingleInstance objects in the same process — as the unit test needs,
    // to exercise primary and secondary against a private test bus — do not share one
    // QDBusConnection.
    const QString connectionName = QStringLiteral("omaseed-single-instance-%1-%2")
                                       .arg(QCoreApplication::applicationPid())
                                       .arg(reinterpret_cast<quintptr>(this));
    QDBusConnection connection = QDBusConnection::connectToBus(busAddress, connectionName);
    if (!connection.isConnected()) {
        QDBusConnection::disconnectFromBus(connectionName);
        return startOnLocalSocket(uris);
    }

    const QString serviceName = AppIdentity::dbusServiceName();
    if (connection.registerService(serviceName)) {
        m_connectionName = connectionName;
        m_isPrimary = true;
        new SingleInstanceDBusAdaptor(this);
        connection.registerObject(
            AppIdentity::dbusObjectPath(), this, QDBusConnection::ExportAdaptors);

        if (!uris.isEmpty()) {
            const QStringList capturedUris = uris;
            QTimer::singleShot(
                0, this, [this, capturedUris]() { emit openRequested(capturedUris); });
        }
        return true;
    }

    // Another instance already owns the name: hand our URIs (or a plain activation) to it,
    // then step aside. We wait for the primary's reply (so it has definitely queued the work
    // before the caller exits) via an *asynchronous* call pumped with processEvents(), not
    // QDBusInterface::call()'s blocking mode: with no running QCoreApplication::exec() yet
    // (main.cpp checks start() before creating its QML engine), the blocking call's own
    // nested loop was observed to starve the sender's other connections for ~25s before
    // delivering the message at all — asyncCall() plus a bounded processEvents() loop below
    // delivers it immediately instead.
    const QString methodName = uris.isEmpty() ? QStringLiteral("Activate") : QStringLiteral("Open");
    QDBusMessage message =
        QDBusMessage::createMethodCall(serviceName,
                                       AppIdentity::dbusObjectPath(),
                                       QString::fromLatin1(kApplicationInterface),
                                       methodName);
    const QVariantMap platformData = outgoingPlatformData();
    if (!uris.isEmpty())
        message << QVariant(uris) << QVariant(platformData);
    else
        message << QVariant(platformData);

    QDBusPendingCall pending = connection.asyncCall(message);
    QElapsedTimer timer;
    timer.start();
    while (!pending.isFinished() && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    QDBusConnection::disconnectFromBus(connectionName);
    m_isPrimary = false;
    return false;
}

QString SingleInstance::localSocketName() const
{
    // One socket per app id per user; only used when the session bus is unreachable.
    return AppIdentity::applicationId() + QStringLiteral(".instance");
}

bool SingleInstance::startOnLocalSocket(const QStringList &uris)
{
    const QString name = localSocketName();

    // Try to connect to an existing primary first.
    {
        QLocalSocket probe;
        probe.connectToServer(name);
        if (probe.waitForConnected(200)) {
            QJsonObject payload;
            payload.insert(QStringLiteral("command"),
                           uris.isEmpty() ? QStringLiteral("activate") : QStringLiteral("open"));
            if (!uris.isEmpty())
                payload.insert(QStringLiteral("uris"), QJsonArray::fromStringList(uris));
            QByteArray line = QJsonDocument(payload).toJson(QJsonDocument::Compact);
            line.append('\n');
            probe.write(line);
            probe.waitForBytesWritten(500);
            probe.disconnectFromServer();
            if (probe.state() != QLocalSocket::UnconnectedState)
                probe.waitForDisconnected(500);
            m_isPrimary = false;
            return false;
        }
    }

    // Nobody answered: either we are first, or a crashed instance left a stale socket file.
    QLocalServer::removeServer(name);
    auto *server = new QLocalServer(this);
    if (!server->listen(name)) {
        delete server;
        m_isPrimary = false;
        return false;
    }

    m_localServer = server;
    connect(server, &QLocalServer::newConnection, this, &SingleInstance::acceptLocalConnection);
    m_isPrimary = true;

    if (!uris.isEmpty()) {
        const QStringList capturedUris = uris;
        QTimer::singleShot(0, this, [this, capturedUris]() { emit openRequested(capturedUris); });
    }
    return true;
}

void SingleInstance::acceptLocalConnection()
{
    while (m_localServer && m_localServer->hasPendingConnections()) {
        QLocalSocket *socket = m_localServer->nextPendingConnection();
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            if (!socket->canReadLine())
                return;
            const QByteArray line = socket->readLine();
            const QJsonObject payload = QJsonDocument::fromJson(line).object();
            const QString command = payload.value(QStringLiteral("command")).toString();
            if (command == QLatin1String("open")) {
                QStringList uris;
                for (const QJsonValue &value : payload.value(QStringLiteral("uris")).toArray())
                    uris.append(value.toString());
                handleOpen(uris, {});
            } else {
                handleActivate({});
            }
            socket->disconnectFromServer();
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
    }
}

void SingleInstance::handleActivate(const QVariantMap &platformData)
{
    m_lastActivationToken = activationTokenFrom(platformData);
    emit activateRequested();
}

void SingleInstance::handleOpen(const QStringList &uris, const QVariantMap &platformData)
{
    m_lastActivationToken = activationTokenFrom(platformData);
    emit openRequested(uris);
}

QString SingleInstance::takeLastActivationToken()
{
    return std::exchange(m_lastActivationToken, QString());
}

QVariantMap SingleInstance::outgoingPlatformData()
{
    // The freedesktop.Application convention (used by GTK/GNOME launchers) for handing an
    // XDG activation token from the calling process to the one it is activating.
    QVariantMap platformData;
    const QString token =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_ACTIVATION_TOKEN"));
    if (!token.isEmpty())
        platformData.insert(QStringLiteral("activation-token"), token);
    return platformData;
}

QString SingleInstance::activationTokenFrom(const QVariantMap &platformData)
{
    const auto it = platformData.constFind(QStringLiteral("activation-token"));
    if (it != platformData.constEnd())
        return it->toString();
    return {};
}

} // namespace omaseed::integration

#include "SingleInstance.moc"
