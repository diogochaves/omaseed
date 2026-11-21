#include "core/AgentGate.hpp"

namespace omaseed::core {

namespace {

const QString kOff = QStringLiteral("off");
const QString kAsk = QStringLiteral("ask");
const QString kAllow = QStringLiteral("allow");

} // namespace

AgentGate::Kind AgentGate::kindOf(const QString &action)
{
    for (const Kind kind : {Kind::Fixes, Kind::Moves, Kind::Remove}) {
        if (actionsOf(kind).contains(action))
            return kind;
    }
    return Kind::None;
}

QStringList AgentGate::actionsOf(Kind kind)
{
    switch (kind) {
    case Kind::Fixes:
        // C50 GT3: C45 V14's fixes, and locate (GT2).
        return {QStringLiteral("retry"),
                QStringLiteral("start-here"),
                QStringLiteral("recheck"),
                QStringLiteral("reannounce"),
                QStringLiteral("pause"),
                QStringLiteral("resume"),
                QStringLiteral("dismiss"),
                QStringLiteral("locate")};
    case Kind::Moves:
        return {QStringLiteral("move"), QStringLiteral("move-back")};
    case Kind::Remove:
        return {QStringLiteral("remove")};
    case Kind::None:
        break;
    }
    return {};
}

QStringList AgentGate::neverActions()
{
    // C45 V14 as C50 leaves it: deleting files, rename, categories, tags, adds, settings,
    // servers and quit.
    return {QStringLiteral("delete"),
            QStringLiteral("rename"),
            QStringLiteral("categories and tags"),
            QStringLiteral("adds"),
            QStringLiteral("settings"),
            QStringLiteral("servers"),
            QStringLiteral("quit")};
}

QStringList AgentGate::levelsOf(Kind kind)
{
    switch (kind) {
    case Kind::Fixes:
        return {kOff, kAsk, kAllow};
    case Kind::Moves:
    case Kind::Remove:
        return {kOff, kAsk}; // C50 GT3: never without asking
    case Kind::None:
        break;
    }
    return {};
}

QString AgentGate::keyOf(Kind kind)
{
    switch (kind) {
    case Kind::Fixes:
        return QStringLiteral("fixes");
    case Kind::Moves:
        return QStringLiteral("moves");
    case Kind::Remove:
        return QStringLiteral("remove");
    case Kind::None:
        break;
    }
    return {};
}

QString AgentGate::readsLevel() const
{
    return m_reads ? QStringLiteral("on") : kOff;
}

QString AgentGate::level(Kind kind) const
{
    switch (kind) {
    case Kind::Fixes:
        return m_fixes;
    case Kind::Moves:
        return m_moves;
    case Kind::Remove:
        return m_remove;
    case Kind::None:
        break;
    }
    return kOff;
}

bool AgentGate::setReads(bool on)
{
    if (m_reads == on)
        return false;
    m_reads = on;
    return true;
}

bool AgentGate::setLevel(Kind kind, const QString &level)
{
    QString *line = nullptr;
    switch (kind) {
    case Kind::Fixes:
        line = &m_fixes;
        break;
    case Kind::Moves:
        line = &m_moves;
        break;
    case Kind::Remove:
        line = &m_remove;
        break;
    case Kind::None:
        return false;
    }
    QString wanted = kOff;
    if (levelsOf(kind).contains(level))
        wanted = level;
    else if (level == kAllow)
        wanted = kAsk; // C50 GT3: moves and remove never run unasked, whatever the file says
    if (*line == wanted)
        return false;
    *line = wanted;
    return true;
}

AgentGate::Decision AgentGate::decide(const QString &action) const
{
    const Kind kind = kindOf(action);
    if (kind == Kind::None)
        return Decision::Never; // C45 V14
    if (!m_reads)
        return Decision::Off; // C50 GT8: reads off, do answers not-allowed
    const QString line = level(kind);
    if (line == kAllow && kind == Kind::Fixes)
        return Decision::Allow;
    if (line == kAsk || line == kAllow)
        return Decision::Ask;
    return Decision::Off;
}

QString AgentGate::decisionName(Decision decision)
{
    switch (decision) {
    case Decision::Off:
        return kOff;
    case Decision::Ask:
        return kAsk;
    case Decision::Allow:
        return kAllow;
    case Decision::Never:
        return QStringLiteral("never");
    }
    return kOff;
}

void AgentGate::setLast(const QString &text, const QDateTime &at)
{
    m_lastText = text;
    m_lastAt = at;
}

QJsonObject AgentGate::toJson() const
{
    QJsonObject section{{QStringLiteral("reads"), readsLevel()},
                        {QStringLiteral("fixes"), m_fixes},
                        {QStringLiteral("moves"), m_moves},
                        {QStringLiteral("remove"), m_remove}};
    if (!m_lastText.isEmpty())
        section.insert(QStringLiteral("last"),
                       QJsonObject{{QStringLiteral("text"), m_lastText},
                                   {QStringLiteral("at"), m_lastAt.toUTC().toString(Qt::ISODate)}});
    return section;
}

AgentGate AgentGate::fromJson(const QJsonObject &section)
{
    AgentGate gate;
    gate.m_reads = section.value(QStringLiteral("reads")).toString() != kOff;
    for (const Kind kind : {Kind::Fixes, Kind::Moves, Kind::Remove})
        gate.setLevel(kind, section.value(keyOf(kind)).toString());
    const QJsonObject last = section.value(QStringLiteral("last")).toObject();
    const QString text = last.value(QStringLiteral("text")).toString();
    if (!text.isEmpty())
        gate.setLast(
            text, QDateTime::fromString(last.value(QStringLiteral("at")).toString(), Qt::ISODate));
    return gate;
}

QJsonArray AgentGate::listLines() const
{
    QJsonArray lines;
    lines.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("reads")},
                             {QStringLiteral("level"), readsLevel()},
                             {QStringLiteral("levels"), QJsonArray{QStringLiteral("on"), kOff}},
                             {QStringLiteral("actions"),
                              QJsonArray{QStringLiteral("status"),
                                         QStringLiteral("errors"),
                                         QStringLiteral("log"),
                                         QStringLiteral("moves"),
                                         QStringLiteral("do")}}});
    for (const Kind kind : {Kind::Fixes, Kind::Moves, Kind::Remove})
        lines.append(
            QJsonObject{{QStringLiteral("kind"), keyOf(kind)},
                        {QStringLiteral("level"), level(kind)},
                        {QStringLiteral("levels"), QJsonArray::fromStringList(levelsOf(kind))},
                        {QStringLiteral("actions"), QJsonArray::fromStringList(actionsOf(kind))}});
    return lines;
}

} // namespace omaseed::core
