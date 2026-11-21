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
    // `text` is the line as the row shows it ("last: recheck · Arcade Set 01 · today 15:20"),
    // `what` as saved.
    return {{QStringLiteral("text"), AgentGate::lastShown(gate.lastText(), gate.lastAt())},
            {QStringLiteral("what"), gate.lastText()},
            {QStringLiteral("at"), gate.lastAt()}};
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

void App::noteAgentAct(const AgentAct &act,
                       const QString &subject,
                       const core::ActionOutcome &outcome)
{
    if (act.word.isEmpty())
        return;
    // C45 V18: the journal and the row's last line say what came of it.
    QString how = act.origin.asked ? QStringLiteral("allowed in the window")
                                   : QStringLiteral("allowed in Settings");
    QString result;
    const bool serverWide = outcome.hashes.isEmpty();
    const qsizetype failed = outcome.failedHashes.size();
    if (serverWide ? !outcome.cause.isEmpty() : failed > 0) {
        const bool all = serverWide || failed == outcome.hashes.size();
        result = all ? QStringLiteral("didn't") : QStringLiteral("%1 didn't").arg(failed);
        how += QStringLiteral(", %1").arg(result);
        if (!outcome.cause.isEmpty())
            how += QStringLiteral(": %1").arg(outcome.cause);
    }
    qInfo().noquote() << AgentGate::journalLine(act.word, subject, how, act.origin);
    setAgentLast(AgentGate::lastLine(act.word, subject, result), QDateTime::currentDateTimeUtc());
}

void App::noteAgentRefusal(const QString &word,
                           const QString &how,
                           const core::AgentOrigin &origin,
                           const QString &subject)
{
    qInfo().noquote() << AgentGate::journalLine(word, subject, how, origin);
    setAgentLast(AgentGate::lastLine(word, subject, how), QDateTime::currentDateTimeUtc());
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
