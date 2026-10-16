#include "integration/BarMenu.hpp"

#include <QJsonObject>

namespace omaseed::integration {

namespace {

const QString kServerPrefix = QStringLiteral("server:");

BarMenu::Row separator(const QString &id)
{
    BarMenu::Row row;
    row.id = id;
    row.kind = BarMenu::Kind::Separator;
    row.enabled = false;
    return row;
}

BarMenu::Row action(const QString &id, const QString &label, const QString &title, bool enabled)
{
    BarMenu::Row row;
    row.id = id;
    row.label = label;
    row.title = title;
    row.enabled = enabled;
    return row;
}

} // namespace

QStringList BarMenu::actionIds()
{
    return {QStringLiteral("add"),
            QStringLiteral("alt-speed"),
            QStringLiteral("stop-all"),
            QStringLiteral("start-all"),
            QStringLiteral("settings"),
            QStringLiteral("quit")};
}

QString BarMenu::kindName(Kind kind)
{
    switch (kind) {
    case Kind::Action:
        return QStringLiteral("action");
    case Kind::Check:
        return QStringLiteral("check");
    case Kind::Radio:
        return QStringLiteral("radio");
    case Kind::Caption:
        return QStringLiteral("caption");
    case Kind::Separator:
        return QStringLiteral("separator");
    }
    return QStringLiteral("action");
}

QString BarMenu::serverDetail(const Server &server)
{
    if (server.state == QLatin1String("refused") || server.state == QLatin1String("down")) {
        if (!server.reason.isEmpty())
            return server.reason;
        return server.state == QLatin1String("refused") ? tr("sign-in refused") : tr("down");
    }
    if (server.state == QLatin1String("first-sync"))
        return tr("first sync");
    if (server.count < 0)
        return QString();
    return server.count == 1 ? tr("1 torrent") : tr("%1 torrents").arg(server.count);
}

BarMenu::BarMenu(QObject *parent)
    : QObject(parent)
    , m_altLabel(tr("alternative speed limits"))
    , m_altTitle(tr("Alternative Speed Limits"))
{}

void BarMenu::setServers(const QList<Server> &servers)
{
    if (m_servers == servers)
        return;
    m_servers = servers;
    Q_EMIT changed();
}

void BarMenu::setAltSpeed(bool on, const QString &label, const QString &title)
{
    if (m_altOn == on && m_altLabel == label && m_altTitle == title)
        return;
    m_altOn = on;
    m_altLabel = label;
    m_altTitle = title;
    Q_EMIT changed();
}

void BarMenu::setServerAnswering(bool answering)
{
    if (m_answering == answering)
        return;
    m_answering = answering;
    Q_EMIT changed();
}

QList<BarMenu::Row> BarMenu::rows() const
{
    QList<Row> out;
    // D19: one server never offers to switch.
    if (m_servers.size() >= 2) {
        Row caption;
        caption.id = QStringLiteral("servers");
        caption.label = tr("servers");
        caption.title = tr("servers");
        caption.kind = Kind::Caption;
        caption.enabled = false;
        out.append(caption);
        for (const Server &server : m_servers) {
            Row row;
            row.id = kServerPrefix + server.id;
            row.label = server.name;
            row.title = server.name;
            row.kind = Kind::Radio;
            row.checked = server.current;
            row.detail = serverDetail(server);
            row.urgent =
                server.state == QLatin1String("down") || server.state == QLatin1String("refused");
            out.append(row);
        }
        out.append(separator(QStringLiteral("separator:servers")));
    }

    Row alt = action(QStringLiteral("alt-speed"), m_altLabel, m_altTitle, m_answering);
    alt.kind = Kind::Check;
    alt.checked = m_altOn;
    out.append(alt);
    // board 33 §11 change 1: rows of the menu, never buttons; no per-torrent start or stop.
    out.append(action(QStringLiteral("stop-all"), tr("stop all"), tr("Stop All"), m_answering));
    out.append(action(QStringLiteral("start-all"), tr("start all"), tr("Start All"), m_answering));
    out.append(separator(QStringLiteral("separator:actions")));
    out.append(action(QStringLiteral("add"), tr("add torrent…"), tr("Add Torrent…"), true));
    out.append(action(QStringLiteral("settings"), tr("settings…"), tr("omaseed Settings…"), true));
    // board 33 §11 change 2: quit is the last row, after a separator.
    out.append(separator(QStringLiteral("separator:quit")));
    out.append(action(QStringLiteral("quit"), tr("quit"), tr("Quit omaseed"), true));
    return out;
}

QJsonArray BarMenu::menuJson() const
{
    QJsonArray out;
    for (const Row &row : rows()) {
        out.append(QJsonObject{
            {QStringLiteral("id"), row.id},
            {QStringLiteral("label"), row.title},
            {QStringLiteral("kind"), kindName(row.kind)},
            {QStringLiteral("checked"), row.checked},
            {QStringLiteral("enabled"), row.enabled},
            {QStringLiteral("detail"), row.detail},
            {QStringLiteral("urgent"), row.urgent},
        });
    }
    return out;
}

QJsonArray BarMenu::serversJson() const
{
    QJsonArray out;
    for (const Server &server : m_servers) {
        out.append(QJsonObject{
            {QStringLiteral("id"), server.id},
            {QStringLiteral("name"), server.name},
            {QStringLiteral("current"), server.current},
            {QStringLiteral("state"), server.state},
            {QStringLiteral("count"), server.count},
            {QStringLiteral("seen"), server.seen},
            {QStringLiteral("reason"), server.reason},
        });
    }
    return out;
}

bool BarMenu::invoke(const QString &id)
{
    if (id.startsWith(kServerPrefix)) {
        const QString serverId = id.mid(kServerPrefix.size());
        for (const Server &server : m_servers) {
            if (server.id == serverId) {
                Q_EMIT serverChosen(serverId);
                return true;
            }
        }
        return false;
    }
    for (const Row &row : rows()) {
        if (row.id != id)
            continue;
        if (!row.enabled || row.kind == Kind::Caption || row.kind == Kind::Separator)
            return false;
        Q_EMIT triggered(id);
        return true;
    }
    return false;
}

} // namespace omaseed::integration
