#include "OmarchyMenuBlock.hpp"

#include "OmarchyMenuFile.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

namespace omaseed::core {

namespace {

// The same calls and file the template's rows use (data/omarchy-menu.jsonc).
QString busCall()
{
    return QStringLiteral("busctl --user call io.github.diogochaves.omaseed "
                          "/io/github/diogochaves/omaseed io.github.diogochaves.omaseed.Status");
}

QString running()
{
    return QStringLiteral("busctl --user status io.github.diogochaves.omaseed");
}

QString statusFile()
{
    return QStringLiteral("\"$XDG_RUNTIME_DIR/omaseed/status.json\"");
}

QString serverIcon()
{
    return QStringLiteral("\U000F048B"); // nf-md-server
}

QString entry(const QString &id, const QStringList &fields)
{
    return QStringLiteral("  \"%1\": {%2},").arg(id, fields.join(QLatin1Char(',')));
}

QString field(const QString &name, const QString &value)
{
    return QStringLiteral("\"%1\":%2").arg(name, OmarchyMenuBlock::jsonString(value));
}

QStringList serverRows(const QList<OmarchyMenuBlock::Server> &servers)
{
    // C36 M11: only with two servers or more. The `when` says it again, for a block a longer
    // list left behind.
    if (servers.size() < 2)
        return {};
    QStringList rows;
    rows << entry(QStringLiteral("trigger.torrents.servers"),
                  {field(QStringLiteral("icon"), serverIcon()),
                   field(QStringLiteral("label"), QStringLiteral("Servers")),
                   field(QStringLiteral("when"),
                         QStringLiteral("%1 && jq -e '.servers | length > 1' %2")
                             .arg(running(), statusFile()))});
    for (qsizetype i = 0; i < servers.size(); ++i) {
        const QString id = OmarchyMenuBlock::shellQuoted(servers.at(i).id);
        // `$id` is jq's, inside single quotes: the shell leaves it alone.
        const QString checked =
            QStringLiteral("jq -e --arg id %1 '.servers[] | select(.id == $id) | .current' %2")
                .arg(id, statusFile());
        rows << entry(QStringLiteral("trigger.torrents.servers.%1").arg(i + 1),
                      {field(QStringLiteral("icon"), serverIcon()),
                       field(QStringLiteral("label"), servers.at(i).name),
                       field(QStringLiteral("checked"), checked),
                       field(QStringLiteral("action"),
                             QStringLiteral("%1 SwitchServer s %2").arg(busCall(), id))});
    }
    return rows;
}

} // namespace

QString OmarchyMenuBlock::serversPlaceholder()
{
    return QStringLiteral("// omaseed: Servers ›");
}

QString OmarchyMenuBlock::templateText()
{
    QFile file(QStringLiteral(":/omaseed/omarchy-menu.jsonc"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QString all = QString::fromUtf8(file.readAll());
    const QString open = QString::fromUtf8(OmarchyMenuFile::openMarkerPrefix());
    const QString close = QString::fromUtf8(OmarchyMenuFile::closeMarker());
    QStringList inside;
    bool on = false;
    for (const QString &line : all.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!on && trimmed.startsWith(open)) {
            on = true;
        } else if (on && trimmed.startsWith(close)) {
            return inside.join(QLatin1Char('\n')) + QLatin1Char('\n');
        } else if (on) {
            inside << line;
        }
    }
    return {};
}

QString OmarchyMenuBlock::text(const QList<Server> &servers)
{
    const QString base = templateText();
    if (base.isEmpty())
        return {};
    QStringList out;
    for (const QString &line : base.split(QLatin1Char('\n'))) {
        if (line.trimmed().startsWith(serversPlaceholder()))
            out << serverRows(servers);
        else
            out << line;
    }
    return out.join(QLatin1Char('\n'));
}

QString OmarchyMenuBlock::shellQuoted(const QString &value)
{
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

QString OmarchyMenuBlock::jsonString(const QString &value)
{
    const QByteArray array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}

} // namespace omaseed::core
