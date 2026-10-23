#include "integration/SingleInstance.hpp"

#include "integration/AppIdentity.hpp"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusReply>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QLoggingCategory>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <utility>

namespace omaseed::integration {

namespace {

const char *kApplicationInterface = "org.freedesktop.Application";

Q_LOGGING_CATEGORY(lcInstance, "omaseed.instance")

/// How long a secondary waits for the primary's answer to Open/Activate.
constexpr int kForwardTimeoutMs = 5000;

enum class NameClaim { Registered, Owned, Failed };

/// Asks the bus for the name without queueing, so "someone else has it" (Owned) and "the bus
/// said no" (Failed: a policy denial, a broken bus) are told apart.
NameClaim claimName(QDBusConnection &connection, const QString &name)
{
    QDBusConnectionInterface *bus = connection.interface();
    if (!bus)
        return NameClaim::Failed;
    const QDBusReply<QDBusConnectionInterface::RegisterServiceReply> reply =
        bus->registerService(name,
                             QDBusConnectionInterface::DontQueueService,
                             QDBusConnectionInterface::DontAllowReplacement);
    if (!reply.isValid()) {
        qCWarning(lcInstance).noquote()
            << QStringLiteral("could not request %1: %2").arg(name, reply.error().message());
        return NameClaim::Failed;
    }
    return reply.value() == QDBusConnectionInterface::ServiceRegistered ? NameClaim::Registered
                                                                        : NameClaim::Owned;
}

/// One line on stderr whatever the logging rules say: the user launched us and nothing else
/// will tell them their link went nowhere.
void tellUser(const QString &line)
{
    QTextStream(stderr) << QStringLiteral("omaseed: ") << line << Qt::endl;
}

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
        // The bar widget's two actions (docs/20 §3): "settings" (the popup's gear) and "retry"
        // (`r` while the server is unreachable); and `omaseed --quit`'s "quit" (#128). Any
        // other name is accepted and ignored, as the spec allows. The tray and the notices
        // talk to the running process directly.
        Q_UNUSED(parameter);
        m_owner->handleActivateAction(action_name, platform_data);
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

QDBusConnection SingleInstance::connection() const
{
    if (m_connectionName.isEmpty())
        return QDBusConnection(QStringLiteral("omaseed-single-instance-none"));
    return QDBusConnection(m_connectionName);
}

QStringList SingleInstance::absoluteUris(const QStringList &arguments,
                                         const QString &workingDirectory)
{
    static const QStringList uriPrefixes{
        QStringLiteral("magnet:"),
        QStringLiteral("http://"),
        QStringLiteral("https://"),
        QStringLiteral("file:"),
    };
    const QDir base(workingDirectory);
    QStringList result;
    result.reserve(arguments.size());
    for (const QString &argument : arguments) {
        const bool isUri =
            std::any_of(uriPrefixes.cbegin(), uriPrefixes.cend(), [&](const QString &prefix) {
                return argument.startsWith(prefix, Qt::CaseInsensitive);
            });
        if (isUri || argument.isEmpty()) {
            result.append(argument);
            continue;
        }
        const QFileInfo info(base, argument);
        result.append(info.exists() ? QUrl::fromLocalFile(info.absoluteFilePath()).toString()
                                    : argument);
    }
    return result;
}

bool SingleInstance::start(const QStringList &arguments)
{
    // A path is resolved here, against this process's working directory: the primary that
    // ends up opening it may have been started somewhere else entirely.
    const QStringList uris = absoluteUris(arguments, QDir::currentPath());
    const QString busAddress =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"));
    if (!busAddress.isEmpty())
        return startOnSessionBus(busAddress, uris);
    return startOnLocalSocket(uris);
}

void SingleInstance::deliverOwnUris(const QStringList &uris)
{
    if (uris.isEmpty())
        return;
    QTimer::singleShot(0, this, [this, uris]() { emit openRequested(uris); });
}

bool SingleInstance::becomePrimaryOnBus(const QString &connectionName, const QStringList &uris)
{
    m_connectionName = connectionName;
    m_isPrimary = true;
    deliverOwnUris(uris);
    return true;
}

void SingleInstance::exportApplication(QDBusConnection &connection)
{
    if (!m_adaptor)
        m_adaptor = new SingleInstanceDBusAdaptor(this);
    connection.registerObject(AppIdentity::dbusObjectPath(), this, QDBusConnection::ExportAdaptors);
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

    // The object before the name. A D-Bus activation (a link opened through GIO or the
    // portal while omaseed is closed) has its Open queued on the bus, which hands it over the
    // instant the name is taken, on Qt's D-Bus thread, before this thread is back from the
    // claim: with no object at the path yet it was answered "No such object path" and the
    // link was lost. On a secondary the object is never called (calls go to the name's owner)
    // and goes with the connection.
    exportApplication(connection);
    const QString serviceName = AppIdentity::dbusServiceName();
    switch (claimName(connection, serviceName)) {
    case NameClaim::Registered:
        return becomePrimaryOnBus(connectionName, uris);
    case NameClaim::Failed:
        // The bus refused outright (not "someone has it"): single-instance over the bus is
        // impossible, but the app itself can still run, guarded by the local socket instead.
        QDBusConnection::disconnectFromBus(connectionName);
        return startOnLocalSocket(uris);
    case NameClaim::Owned:
        break;
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

    QDBusPendingCall pending = connection.asyncCall(message, kForwardTimeoutMs);
    QElapsedTimer timer;
    timer.start();
    while (!pending.isFinished() && timer.elapsed() < kForwardTimeoutMs + 500)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    if (pending.isFinished() && !pending.isError()) {
        QDBusConnection::disconnectFromBus(connectionName);
        m_isPrimary = false;
        return false;
    }

    // The owner did not take the hand-off: it is quitting (the name may be free by now), hung,
    // or not omaseed at all. Try once to become the primary ourselves so the URIs still land.
    const QString why = pending.isFinished()
                            ? pending.error().message()
                            : QStringLiteral("no answer within %1 s").arg(kForwardTimeoutMs / 1000);
    qCWarning(lcInstance).noquote()
        << QStringLiteral("the running instance did not take %1: %2").arg(methodName, why);
    if (claimName(connection, serviceName) == NameClaim::Registered)
        return becomePrimaryOnBus(connectionName, uris);

    QDBusConnection::disconnectFromBus(connectionName);
    m_isPrimary = false;
    tellUser(QStringLiteral("another process owns %1 and did not take the hand-off (%2)%3")
                 .arg(serviceName,
                      why,
                      uris.isEmpty()
                          ? QString()
                          : QStringLiteral("; not opened: ") + uris.join(QLatin1Char(' '))));
    return false;
}

QString SingleInstance::localSocketName() const
{
    // One socket per app id per user; only used when the session bus is unreachable. It
    // lives in the user's runtime dir (0700), never the shared temp dir, where another user
    // could plant a socket of the same name first.
    QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        dir = QDir::tempPath();
    return dir + QLatin1Char('/') + AppIdentity::applicationId() + QStringLiteral(".instance");
}

bool SingleInstance::startOnLocalSocket(const QStringList &uris)
{
    const QString name = localSocketName();

    const auto forward = [&]() {
        QLocalSocket probe;
        probe.connectToServer(name);
        if (!probe.waitForConnected(200))
            return false;
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
        return true;
    };

    if (forward()) {
        m_isPrimary = false;
        return false;
    }

    // Two launches at the same moment would both miss each other's server and both listen,
    // the second unlinking the first's socket. The lock makes "check again, then listen" one
    // step; once listening, the socket itself is the guard and the lock goes. A lock left by
    // a process that is gone is taken over by QLockFile itself.
    QLockFile lock(name + QStringLiteral(".lock"));
    const bool locked = lock.tryLock(2000);
    if (forward()) {
        m_isPrimary = false;
        return false;
    }

    // Nobody answered: either we are first, or a crashed instance left a stale socket file.
    // Only the lock holder may remove it; without the lock we might unlink a live server.
    if (locked)
        QLocalServer::removeServer(name);
    auto *server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(name)) {
        delete server;
        // Someone may have got there between our probe and listen: hand over if so. Otherwise
        // run without the guard rather than quit as if we were the secondary.
        if (forward()) {
            m_isPrimary = false;
            return false;
        }
        qCWarning(lcInstance).noquote()
            << QStringLiteral("no single-instance guard: could not listen on %1").arg(name);
        m_isPrimary = true;
        deliverOwnUris(uris);
        return true;
    }

    m_localServer = server;
    connect(server, &QLocalServer::newConnection, this, &SingleInstance::acceptLocalConnection);
    m_isPrimary = true;
    deliverOwnUris(uris);
    return true;
}

SingleInstance::QuitResult SingleInstance::askToQuit(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    // Until `gone()` says so, or the time is up; events keep flowing meanwhile.
    const auto waitUntil = [&](const std::function<bool()> &gone) {
        while (timer.elapsed() < timeoutMs) {
            if (gone())
                return true;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(50);
        }
        return gone();
    };

    const QString busAddress =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"));
    if (!busAddress.isEmpty()) {
        const QString connectionName =
            QStringLiteral("omaseed-quit-%1").arg(QCoreApplication::applicationPid());
        QDBusConnection connection = QDBusConnection::connectToBus(busAddress, connectionName);
        const QString service = AppIdentity::dbusServiceName();
        const auto owned = [&connection, &service]() {
            const QDBusReply<bool> reply = connection.interface()->isServiceRegistered(service);
            return reply.isValid() && reply.value();
        };
        if (connection.isConnected() && connection.interface() && owned()) {
            QDBusMessage message =
                QDBusMessage::createMethodCall(service,
                                               AppIdentity::dbusObjectPath(),
                                               QString::fromLatin1(kApplicationInterface),
                                               QStringLiteral("ActivateAction"));
            message << QStringLiteral("quit") << QVariant::fromValue(QVariantList())
                    << QVariant(QVariantMap());
            QDBusPendingCall pending = connection.asyncCall(message, timeoutMs);
            waitUntil([&pending]() { return pending.isFinished(); });
            const bool asked = pending.isFinished() && !pending.isError();
            const bool gone = asked && waitUntil([&owned]() { return !owned(); });
            if (pending.isFinished() && pending.isError())
                qCWarning(lcInstance).noquote()
                    << QStringLiteral("quit: %1").arg(pending.error().message());
            QDBusConnection::disconnectFromBus(connectionName);
            return gone ? QuitResult::Quit : QuitResult::TimedOut;
        }
        QDBusConnection::disconnectFromBus(connectionName);
    }

    // The local socket: a primary that runs without the bus listens there.
    const QString name = localSocketName();
    const auto listening = [&name]() {
        QLocalSocket probe;
        probe.connectToServer(name);
        const bool up = probe.waitForConnected(200);
        probe.abort();
        return up;
    };
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(200))
        return QuitResult::NotRunning;
    socket.write(QByteArrayLiteral("{\"command\":\"quit\"}\n"));
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState)
        socket.waitForDisconnected(500);
    return waitUntil([&listening]() { return !listening(); }) ? QuitResult::Quit
                                                              : QuitResult::TimedOut;
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
            } else if (command == QLatin1String("quit")) {
                handleActivateAction(command, {});
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

void SingleInstance::handleActivateAction(const QString &name, const QVariantMap &platformData)
{
    if (name != QLatin1String("settings") && name != QLatin1String("retry") &&
        name != QLatin1String("quit")) {
        qCInfo(lcInstance).noquote() << QStringLiteral("unknown action %1 ignored").arg(name);
        return;
    }
    m_lastActivationToken = activationTokenFrom(platformData);
    emit actionRequested(name);
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
