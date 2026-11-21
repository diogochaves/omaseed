#pragma once

// AgentSkill — Settings › General's Agents row, its Skill line (DECISIONS C48 AS8 a, docs/65
// §5): whether omaseed's skill is linked into the agents' skill folders, and the row's link
// the skill and unlink. It says what `omaseed skill status` says and runs the same code as
// `omaseed skill link|unlink` (SkillLinker), on a worker thread, so the row never blocks on
// a slow home folder.
//
// Nothing here runs unasked (AS8 a): constructing it reads nothing; the Settings modal calls
// refresh() when it opens, and link() and unlink() only on the person's click or enter.

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "integration/SkillLinker.hpp"

QT_BEGIN_NAMESPACE
class QThread;
QT_END_NAMESPACE

namespace omaseed::integration {

class AgentSkill : public QObject
{
    Q_OBJECT

    /// At least one agent's folder holds a live link of omaseed's.
    Q_PROPERTY(bool linked READ linked NOTIFY changed)
    /// The agents whose folders hold it, in SkillLinker's order ("Claude Code", "Codex"…),
    /// without the shared ~/.agents unless it is the only one.
    Q_PROPERTY(QStringList agents READ agents NOTIFY changed)
    /// A refresh, a link or an unlink is running.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// What the last link or unlink that failed said; empty after one that went through.
    Q_PROPERTY(QString message READ message NOTIFY changed)

public:
    /// `devSourceDir` is the source tree's data/agents/skill of a development build (empty
    /// in a release), found last, as `omaseed skill link` finds it.
    explicit AgentSkill(const QString &devSourceDir = QString(), QObject *parent = nullptr);
    ~AgentSkill() override;

    bool linked() const { return m_linked; }
    QStringList agents() const { return m_agents; }
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }

    /// main.cpp sets it in a development build.
    void setDevSourceDir(const QString &dir) { m_devSourceDir = dir; }

    /// Reads the folders again (SkillLinker::status). Answers with `finished`.
    Q_INVOKABLE void refresh();
    /// Links the skill into every installed agent's folder. Answers with `finished`, and
    /// `failed` first when no folder could take it.
    Q_INVOKABLE void link();
    /// Takes omaseed's links out again. Answers with `finished`.
    Q_INVOKABLE void unlink();

Q_SIGNALS:
    void changed();
    /// A link or unlink did not go through; `message` is ready to show.
    void failed(const QString &message);
    /// The refresh, link or unlink asked for is done and the properties say its outcome.
    void finished();

private:
    using Job = std::function<SkillLinker::Report(const QString &source)>;
    void run(Job job, bool reportsFailure);
    void apply(const SkillLinker::Report &report, bool reportsFailure);

    QString m_devSourceDir;
    QThread *m_thread = nullptr;
    bool m_linked = false;
    QStringList m_agents;
    bool m_busy = false;
    QString m_message;
};

} // namespace omaseed::integration
