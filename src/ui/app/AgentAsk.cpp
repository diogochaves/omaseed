#include "ui/app/AgentAsk.hpp"

namespace omaseed::app {

namespace {

constexpr int kTwoMinutes = 120000; // C45 V17

int askTimeout()
{
    bool good = false;
    const int ms = qEnvironmentVariableIntValue("OMASEED_ASK_TIMEOUT_MS", &good);
    return good && ms > 0 ? ms : kTwoMinutes;
}

} // namespace

AgentAsk::AgentAsk(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(askTimeout());
    connect(&m_timer, &QTimer::timeout, this, [this] { finish(QStringLiteral("no-answer"), {}); });
}

bool AgentAsk::ask(const QVariantMap &request, Answer answer)
{
    if (m_answer || !answer)
        return false;
    m_request = request;
    m_answer = std::move(answer);
    m_timer.start();
    Q_EMIT openChanged();
    return true;
}

void AgentAsk::allow(const QVariantList &ticks)
{
    finish(QStringLiteral("allowed"), ticks);
}

void AgentAsk::refuse()
{
    finish(QStringLiteral("refused"), {});
}

void AgentAsk::finish(const QString &answer, const QVariantList &ticks)
{
    if (!m_answer)
        return;
    m_timer.stop();
    // Closed before the answer runs: what it does may open the next ask.
    const Answer call = std::move(m_answer);
    m_answer = nullptr;
    m_request.clear();
    Q_EMIT openChanged();
    call(answer, ticks);
}

} // namespace omaseed::app
