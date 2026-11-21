#include "integration/AgentSkill.hpp"

#include <QDir>
#include <QThread>

#include <memory>

namespace omaseed::integration {

AgentSkill::AgentSkill(const QString &devSourceDir, QObject *parent)
    : QObject(parent)
    , m_devSourceDir(devSourceDir)
{}

AgentSkill::~AgentSkill()
{
    // A job still running finishes first: it is writing links, and half a rename is worse
    // than a short wait at quit.
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
}

void AgentSkill::refresh()
{
    run(&SkillLinker::status, false);
}

void AgentSkill::link()
{
    run(&SkillLinker::link, true);
}

void AgentSkill::unlink()
{
    run(&SkillLinker::unlink, true);
}

void AgentSkill::run(Job job, bool reportsFailure)
{
    // One at a time: what is running says the outcome the row shows next anyway.
    if (m_busy)
        return;
    m_busy = true;
    Q_EMIT changed();

    auto report = std::make_shared<SkillLinker::Report>();
    const QString devSourceDir = m_devSourceDir;
    m_thread = QThread::create([job = std::move(job), devSourceDir, report] {
        *report = job(SkillLinker::findSource(devSourceDir));
    });
    connect(m_thread, &QThread::finished, this, [this, report, reportsFailure] {
        m_thread->deleteLater();
        m_thread = nullptr;
        apply(*report, reportsFailure);
    });
    m_thread->start();
}

void AgentSkill::apply(const SkillLinker::Report &report, bool reportsFailure)
{
    QStringList agents;
    QString shared;
    for (const SkillLinker::FolderState &folder : report.folders) {
        if (folder.state != SkillLinker::State::Linked)
            continue;
        // The shared ~/.agents is a folder, not an agent: the line names it only when it is
        // the one link there is ("linked for Claude Code and Codex", docs/65 §5).
        if (folder.path == QDir::homePath() + QStringLiteral("/.agents/skills/omaseed"))
            shared = folder.agent;
        else
            agents.append(folder.agent);
    }
    if (agents.isEmpty() && !shared.isEmpty())
        agents.append(shared);

    m_linked = !agents.isEmpty();
    m_agents = agents;
    m_busy = false;
    if (reportsFailure)
        m_message = report.code.isEmpty() ? QString() : report.message;
    Q_EMIT changed();
    if (reportsFailure && !report.code.isEmpty())
        Q_EMIT failed(report.message);
    Q_EMIT finished();
}

} // namespace omaseed::integration
