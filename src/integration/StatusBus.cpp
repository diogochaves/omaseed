#include "integration/StatusBus.hpp"

#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QLoggingCategory>
#include <QObject>

namespace omaseed::integration {

namespace {

Q_LOGGING_CATEGORY(lcStatusBus, "omaseed.statusbus")

} // namespace

/// The adaptor Qt exports. A method that answers later takes the call's QDBusMessage as its
/// last argument (Qt leaves it out of the signature), marks the reply delayed, and sends it
/// when the handler is done; the return value is then ignored.
class StatusDBusAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.diogochaves.omaseed.Status")

public:
    StatusDBusAdaptor(QObject *exported, const QDBusConnection &connection, StatusHandler *handler)
        : QDBusAbstractAdaptor(exported)
        , m_connection(connection)
        , m_handler(handler)
    {}

public Q_SLOTS:
    bool ToggleAltSpeed() { return m_handler->toggleAltSpeed(); }

    bool SetAltSpeed(bool on) { return m_handler->setAltSpeed(on); }

    QString DescribeSources(const QStringList &sources, const QDBusMessage &message)
    {
        m_handler->describeSources(sources, delayedReply(message));
        return QString();
    }

    QString AddSources(const QStringList &sources,
                       const QString &category,
                       bool stopped,
                       const QDBusMessage &message)
    {
        m_handler->addSources(sources, category, stopped, delayedReply(message));
        return QString();
    }

    void OpenAddSheet(const QStringList &sources) { m_handler->openAddSheet(sources); }

    void ShowTorrent(const QString &hash) { m_handler->showTorrent(hash); }

    void OpenFolder(const QString &hash) { m_handler->openFolder(hash); }

private:
    StatusHandler::Reply delayedReply(const QDBusMessage &message)
    {
        message.setDelayedReply(true);
        QDBusConnection connection = m_connection;
        QDBusMessage call = message;
        return [connection, call](const QString &json) mutable {
            if (!connection.send(call.createReply(json)))
                qCWarning(lcStatusBus) << "could not answer" << call.member();
        };
    }

    QDBusConnection m_connection;
    StatusHandler *m_handler;
};

QString StatusBus::interfaceName()
{
    return QStringLiteral("io.github.diogochaves.omaseed.Status");
}

bool StatusBus::exportOn(QObject *exported,
                         const QDBusConnection &connection,
                         StatusHandler *handler)
{
    if (!exported || !handler || !connection.isConnected()) {
        qCInfo(lcStatusBus) << "not on the session bus: the bar widget cannot act";
        return false;
    }
    new StatusDBusAdaptor(exported, connection, handler);
    return true;
}

} // namespace omaseed::integration

#include "StatusBus.moc"
