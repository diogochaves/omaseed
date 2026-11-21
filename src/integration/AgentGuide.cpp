#include "integration/AgentGuide.hpp"

#include "integration/SkinReader.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>

namespace {

Q_LOGGING_CATEGORY(lcAgentGuide, "omaseed.agentguide")

const QByteArray kClaudeMd = QByteArrayLiteral("@AGENTS.md\n");

QByteArray readAll(const QString &path, bool *exists)
{
    QFile file(path);
    *exists = file.exists();
    if (!*exists || !file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool save(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        qCWarning(lcAgentGuide).noquote()
            << QStringLiteral("could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

/// A file of the skill as compiled in (DECISIONS C48: the qrc over data/agents/skill/).
QString skillFile(const QString &relative)
{
    return QStringLiteral(":/omaseed/agents/skill/") + relative;
}

QString resource(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

/// Whether `bytes` is a schema omaseed wrote: its first key is `$comment`, saying header().
bool isOurSchema(const QByteArray &bytes, const QString &header)
{
    static const QRegularExpression first(
        QStringLiteral("^\\s*\\{\\s*\"\\$comment\"\\s*:\\s*\"([^\"]*)\""));
    const QRegularExpressionMatch match = first.match(QString::fromUtf8(bytes.left(512)));
    return match.hasMatch() && match.captured(1) == header;
}

} // namespace

namespace omaseed::integration::AgentGuide {

QString header()
{
    return QStringLiteral("Written by omaseed on every start. Edits here are overwritten.");
}

QMap<QString, QString> frontmatter()
{
    QMap<QString, QString> fields;
    const QStringList lines =
        resource(skillFile(QStringLiteral("SKILL.md"))).split(QLatin1Char('\n'));
    if (lines.isEmpty() || lines.first() != QLatin1String("---"))
        return fields;
    static const QRegularExpression field(QStringLiteral("^([a-z-]+):\\s*(.*)$"));
    for (qsizetype i = 1; i < lines.size() && lines.at(i) != QLatin1String("---"); ++i) {
        const QRegularExpressionMatch match = field.match(lines.at(i));
        if (!match.hasMatch())
            continue;
        QString value = match.captured(2).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) &&
            value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2)
                        .replace(QStringLiteral("\\\""), QStringLiteral("\""));
        fields.insert(match.captured(1), value);
    }
    return fields;
}

QString map()
{
    const QString skill = resource(skillFile(QStringLiteral("SKILL.md")));
    if (!skill.startsWith(QLatin1String("---\n")))
        return skill;
    const qsizetype close = skill.indexOf(QLatin1String("\n---\n"), 3);
    if (close < 0)
        return QString();
    QString body = skill.mid(close + 5);
    while (body.startsWith(QLatin1Char('\n')))
        body.remove(0, 1);
    return body;
}

QStringList pages()
{
    // DECISIONS C48: the pages are the files in ref/, so a page is a file and nothing else.
    // The three the map names come first, in its order; any other after them, by name.
    static const QStringList order{
        QStringLiteral("library"), QStringLiteral("skins"), QStringLiteral("files")};
    QStringList found;
    const QStringList files = QDir(skillFile(QStringLiteral("ref")))
                                  .entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    for (const QString &file : files)
        found.append(file.chopped(3));
    QStringList result;
    for (const QString &name : order)
        if (found.removeAll(name) > 0)
            result.append(name);
    result.append(found);
    return result;
}

QString page(const QString &name)
{
    if (!pages().contains(name))
        return QString();
    return resource(skillFile(QStringLiteral("ref/%1.md").arg(name)));
}

QString printedMap()
{
    // docs/65 §4: printed, there is no folder beside the text, so a page is a command.
    static const QString refLine =
        QStringLiteral("\"ref: FILE\" is a page in ref/ beside this file: read it when the task "
                       "needs it.");
    static const QString commandLine =
        QStringLiteral("A page named \"omaseed skill PAGE\" prints with that command: run it "
                       "when the task needs it.");
    static const QRegularExpression ref(QStringLiteral("\\bref: ([a-z][a-z0-9-]*)\\.md\\b"));
    QStringList lines = map().split(QLatin1Char('\n'));
    for (QString &line : lines) {
        if (line == refLine)
            line = commandLine;
        else
            line.replace(ref, QStringLiteral("omaseed skill \\1"));
    }
    return lines.join(QLatin1Char('\n'));
}

QString pagesLine()
{
    return QStringLiteral("Pages: omaseed skill %1.").arg(pages().join(QStringLiteral(", ")));
}

QByteArray schemaBytes()
{
    // QJsonObject sorts its keys, and "$comment" sorts first, so the header leads the file.
    QJsonObject schema = SkinReader::schema();
    schema.insert(QStringLiteral("$comment"), header());
    return QJsonDocument(schema).toJson(QJsonDocument::Indented);
}

WriteResult writeInto(const QString &dir)
{
    WriteResult result;
    if (!QDir().mkpath(dir)) {
        qCWarning(lcAgentGuide).noquote() << QStringLiteral("could not create %1").arg(dir);
        return result;
    }

    // DECISIONS C45 V19: AGENTS.md is omaseed's while its first line says so. C48: it holds
    // the printed map, so an older whole guide (header first) is rewritten once.
    const QString agentsPath = QDir(dir).filePath(QStringLiteral("AGENTS.md"));
    const QByteArray agents = (header() + QStringLiteral("\n\n") + printedMap()).toUtf8();
    bool exists = false;
    const QByteArray had = readAll(agentsPath, &exists);
    if (!exists)
        result.agents = save(agentsPath, agents) ? Wrote::Written : Wrote::Failed;
    else if (had == agents)
        result.agents = Wrote::Same;
    else if (!had.startsWith(header().toUtf8() + '\n')) {
        qCInfo(lcAgentGuide).noquote()
            << QStringLiteral("left %1 alone: it doesn't start with omaseed's line, so it is "
                              "someone else's")
                   .arg(agentsPath);
        result.agents = Wrote::LeftAlone;
    } else
        result.agents = save(agentsPath, agents) ? Wrote::Written : Wrote::Failed;

    const QString claudePath = QDir(dir).filePath(QStringLiteral("CLAUDE.md"));
    const QByteArray claudeHad = readAll(claudePath, &exists);
    if (!exists)
        result.claude = save(claudePath, kClaudeMd) ? Wrote::Written : Wrote::Failed;
    else if (claudeHad == kClaudeMd)
        result.claude = Wrote::Same;
    else {
        qCInfo(lcAgentGuide).noquote()
            << QStringLiteral("left %1 alone: it says more than @AGENTS.md").arg(claudePath);
        result.claude = Wrote::LeftAlone;
    }

    // C37 U4: the schema a clone's `$schema` names, beside this guide.
    const QString schemaPath = QDir(dir).filePath(QStringLiteral("skin.schema.json"));
    const QByteArray schema = schemaBytes();
    const QByteArray schemaHad = readAll(schemaPath, &exists);
    if (!exists)
        result.schema = save(schemaPath, schema) ? Wrote::Written : Wrote::Failed;
    else if (schemaHad == schema)
        result.schema = Wrote::Same;
    else if (!isOurSchema(schemaHad, header())) {
        qCInfo(lcAgentGuide).noquote()
            << QStringLiteral("left %1 alone: its $comment isn't omaseed's line, so it is "
                              "someone else's")
                   .arg(schemaPath);
        result.schema = Wrote::LeftAlone;
    } else
        result.schema = save(schemaPath, schema) ? Wrote::Written : Wrote::Failed;
    return result;
}

} // namespace omaseed::integration::AgentGuide
