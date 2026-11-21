// App's #291 block (board 67, DECISIONS C50 GT3, GT8): the gate on what an agent may do,
// held in state.json's "agents" section. The declarations sit after #286's in App.hpp.
#include "ui/app/App.hpp"

namespace omaseed::app {

using core::AgentGate;

namespace {

const QString kAgentsSection = QStringLiteral("agents");

} // namespace

const AgentGate &App::agentGate() const
{
    if (!m_agentGateLoaded) {
        m_agentGateLoaded = true;
        m_agentGate = AgentGate::fromJson(stateSection(kAgentsSection));
    }
    return m_agentGate;
}

QString App::agentFixes() const
{
    return agentGate().level(AgentGate::Kind::Fixes);
}

QString App::agentMoves() const
{
    return agentGate().level(AgentGate::Kind::Moves);
}

QString App::agentRemove() const
{
    return agentGate().level(AgentGate::Kind::Remove);
}

QVariantMap App::agentLast() const
{
    const AgentGate &gate = agentGate();
    if (gate.lastText().isEmpty())
        return {};
    return {{QStringLiteral("text"), gate.lastText()}, {QStringLiteral("at"), gate.lastAt()}};
}

void App::setAgentReads(const QString &level)
{
    agentGate();
    // C50 GT8: only "off" turns reads off; anything else is the default, on.
    if (m_agentGate.setReads(level != QLatin1String("off")))
        saveAgentGate();
}

void App::setAgentFixes(const QString &level)
{
    setAgentLevel(AgentGate::Kind::Fixes, level);
}

void App::setAgentMoves(const QString &level)
{
    setAgentLevel(AgentGate::Kind::Moves, level);
}

void App::setAgentRemove(const QString &level)
{
    setAgentLevel(AgentGate::Kind::Remove, level);
}

void App::setAgentLevel(AgentGate::Kind kind, const QString &level)
{
    agentGate();
    if (m_agentGate.setLevel(kind, level))
        saveAgentGate();
}

void App::setAgentLast(const QString &text, const QDateTime &at)
{
    agentGate();
    m_agentGate.setLast(text, at);
    saveAgentGate();
}

void App::saveAgentGate()
{
    // The section is read again so a key a later omaseed adds survives this one's write.
    QJsonObject section = stateSection(kAgentsSection);
    const QJsonObject gate = m_agentGate.toJson();
    for (auto it = gate.begin(); it != gate.end(); ++it)
        section.insert(it.key(), it.value());
    if (!gate.contains(QStringLiteral("last")))
        section.remove(QStringLiteral("last"));
    writeStateSection(kAgentsSection, section);
    Q_EMIT agentsChanged();
}

} // namespace omaseed::app
