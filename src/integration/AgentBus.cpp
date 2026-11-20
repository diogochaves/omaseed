#include "integration/AgentBus.hpp"

#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QLoggingCategory>
#include <QObject>

namespace omaseed::integration {

namespace {

Q_LOGGING_CATEGORY(lcAgentBus, "omaseed.agentbus")

} // namespace

/// The adaptor Qt exports. Each method takes the call's QDBusMessage last (Qt leaves it out
/// of the signature), marks the reply delayed and sends it when the handler is done.
class AgentDBusAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.diogochaves.omaseed.Agent")

public:
    AgentDBusAdaptor(QObject *exported, const QDBusConnection &connection, AgentHandler *handler)
        : QDBusAbstractAdaptor(exported)
        , m_connection(connection)
        , m_handler(handler)
    {}

public Q_SLOTS:
    QString Status(const QString &options, const QDBusMessage &message)
    {
        m_handler->status(options, delayedReply(message));
        return QString();
    }

    QString Errors(const QString &options, const QDBusMessage &message)
    {
        m_handler->errors(options, delayedReply(message));
        return QString();
    }

    QString Log(const QString &options, const QDBusMessage &message)
    {
        m_handler->log(options, delayedReply(message));
        return QString();
    }

    QString Moves(const QString &options, const QDBusMessage &message)
    {
        m_handler->moves(options, delayedReply(message));
        return QString();
    }

    QString UseSkin(const QString &options, const QDBusMessage &message)
    {
        m_handler->useSkin(options, delayedReply(message));
        return QString();
    }

private:
    AgentHandler::Reply delayedReply(const QDBusMessage &message)
    {
        message.setDelayedReply(true);
        QDBusConnection connection = m_connection;
        QDBusMessage call = message;
        return [connection, call](const QString &json) mutable {
            if (!connection.send(call.createReply(json)))
                qCWarning(lcAgentBus) << "could not answer" << call.member();
        };
    }

    QDBusConnection m_connection;
    AgentHandler *m_handler;
};

QString AgentBus::interfaceName()
{
    return QStringLiteral("io.github.diogochaves.omaseed.Agent");
}

bool AgentBus::exportOn(QObject *exported, const QDBusConnection &connection, AgentHandler *handler)
{
    if (!exported || !handler || !connection.isConnected()) {
        qCInfo(lcAgentBus) << "not on the session bus: the words cannot ask";
        return false;
    }
    new AgentDBusAdaptor(exported, connection, handler);
    return true;
}

} // namespace omaseed::integration

#include "AgentBus.moc"
