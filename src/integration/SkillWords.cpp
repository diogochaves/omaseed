#include "integration/SkillWords.hpp"

#include "core/AgentWords.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace omaseed::integration::SkillWords {

namespace {

using SkillLinker::FolderState;
using SkillLinker::Report;
using SkillLinker::State;

/// A word and its column: the lines' first word is padded to nine (docs/65 §5).
QString line(const QString &word, const QString &rest)
{
    return word.leftJustified(8) + QLatin1Char(' ') + rest + QLatin1Char('\n');
}

QString tilde(const QString &path, const QString &home)
{
    if (!home.isEmpty() && (path == home || path.startsWith(home + QLatin1Char('/'))))
        return QLatin1Char('~') + path.mid(home.size());
    return path;
}

/// The agents in `state`, the shared ~/.agents last ("Claude Code, Codex, the shared
/// ~/.agents").
QString names(const Report &report, State state)
{
    QStringList named;
    QString shared;
    for (const FolderState &folder : report.folders) {
        if (folder.state != state)
            continue;
        if (folder.agent.startsWith(QLatin1String("the shared ")))
            shared = folder.agent;
        else
            named << folder.agent;
    }
    if (!shared.isEmpty())
        named << shared;
    return named.join(QStringLiteral(", "));
}

QString yoursLine(const FolderState &folder, const QString &home)
{
    if (folder.copy)
        return line(QStringLiteral("yours"),
                    folder.agent + QStringLiteral(": ") + tilde(folder.path, home) +
                        QStringLiteral(" is your own copy;")) +
               line(QString(), QStringLiteral("it won't follow omaseed's upgrades"));
    return line(QStringLiteral("yours"),
                folder.agent + QStringLiteral(": ") + tilde(folder.path, home) +
                    QStringLiteral(" is ") + folder.why);
}

QString linkLines(const Report &report, const QString &home)
{
    QString out;
    bool first = true;
    for (const FolderState &folder : report.folders) {
        if (folder.state != State::Linked)
            continue;
        QString rest = tilde(folder.path, home);
        if (first)
            rest += QStringLiteral("   → ") + tilde(report.source, home);
        first = false;
        out += line(QStringLiteral("linked"), rest);
    }
    if (const QString skipped = names(report, State::Skipped); !skipped.isEmpty())
        out += line(QStringLiteral("skipped"), skipped + QStringLiteral(": not installed"));
    for (const FolderState &folder : report.folders) {
        if (folder.state == State::Yours)
            out +=
                line(QStringLiteral("left"),
                     tilde(folder.path, home) + QStringLiteral(": ") +
                         (folder.copy ? QStringLiteral("your own copy, not a link") : folder.why));
        else if (folder.state == State::Failed)
            out += line(QStringLiteral("failed"),
                        tilde(folder.path, home) + QStringLiteral(": ") + folder.why);
    }
    out += QStringLiteral("Agents load it when you ask about torrents or omaseed's skin.\n"
                          "omaseed skill unlink takes it out.\n");
    return out;
}

QString unlinkLines(const Report &report, const QString &home)
{
    QString out;
    for (const QString &path : report.changed)
        out += line(QStringLiteral("removed"), tilde(path, home));
    for (const FolderState &folder : report.folders) {
        if (folder.state == State::Yours)
            out +=
                line(QStringLiteral("left"),
                     tilde(folder.path, home) + QStringLiteral(": ") +
                         (folder.copy ? QStringLiteral("your own copy, not a link") : folder.why));
        else if (folder.state == State::Failed)
            out += line(QStringLiteral("failed"),
                        tilde(folder.path, home) + QStringLiteral(": ") + folder.why);
    }
    out += report.changed.isEmpty()
               ? QStringLiteral("Nothing to take out: no agent's folder holds omaseed's link.\n")
               : QStringLiteral("omaseed skill link puts it back.\n");
    return out;
}

QString statusLines(const Report &report, const QString &home)
{
    QString out;
    if (const QString linked = names(report, State::Linked); !linked.isEmpty())
        out += line(QStringLiteral("linked"), linked);
    if (const QString dead = names(report, State::Dead); !dead.isEmpty())
        out += line(QStringLiteral("dead"),
                    dead + QStringLiteral(": the link points nowhere (omaseed's skill is gone); "
                                          "omaseed skill link or unlink"));
    for (const FolderState &folder : report.folders) {
        if (folder.state == State::Yours)
            out += yoursLine(folder, home);
    }
    if (const QString absent = names(report, State::Absent); !absent.isEmpty())
        out += line(QStringLiteral("absent"), absent + QStringLiteral(": not linked"));
    if (const QString skipped = names(report, State::Skipped); !skipped.isEmpty())
        out += line(QStringLiteral("skipped"), skipped + QStringLiteral(": not installed"));
    if (names(report, State::Linked).isEmpty())
        out += QStringLiteral("omaseed skill link links it.\n");
    return out;
}

} // namespace

QByteArray json(const QString &command, const QDateTime &at, const Report &report)
{
    QJsonArray folders;
    for (const FolderState &folder : report.folders) {
        QJsonObject object{
            {QStringLiteral("agent"), folder.agent},
            {QStringLiteral("path"), folder.path},
            {QStringLiteral("state"), SkillLinker::stateName(folder.state)},
            {QStringLiteral("target"),
             folder.target.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(folder.target)},
        };
        if (folder.copy)
            object.insert(QStringLiteral("copy"), true);
        if (!folder.why.isEmpty())
            object.insert(QStringLiteral("why"), folder.why);
        folders.append(object);
    }
    QList<std::pair<QString, QJsonValue>> fields{
        {QStringLiteral("v"), 1},
        {QStringLiteral("command"), command},
        {QStringLiteral("at"), core::AgentWords::isoUtc(at)},
        {QStringLiteral("source"),
         report.source.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(report.source)},
        {QStringLiteral("folders"), folders},
    };
    if (command != QLatin1String("skill status"))
        fields.append({QStringLiteral("changed"), QJsonArray::fromStringList(report.changed)});
    return core::AgentWords::orderedJson(fields);
}

QString lines(const QString &command, const Report &report, const QString &home)
{
    if (command == QLatin1String("skill link"))
        return linkLines(report, home);
    if (command == QLatin1String("skill unlink"))
        return unlinkLines(report, home);
    return statusLines(report, home);
}

} // namespace omaseed::integration::SkillWords
