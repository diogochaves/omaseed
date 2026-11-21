// DECISIONS C48 AS8 a (#286, docs/65 §5): Settings › General's Skill line. AgentSkill says
// what `omaseed skill status` says, and its link and unlink run SkillLinker's on a worker
// thread: busy while they run, one `finished` each, `failed` with the message when no folder
// could take the link.
//
// Every case runs in a temporary HOME (and XDG homes) set here, never inherited: the real
// ~/.claude, ~/.agents, ~/.codex and ~/.config/omaseed are never touched, and each case checks
// that first.

#include "integration/AgentSkill.hpp"
#include "integration/SkillLinker.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include <pwd.h>
#include <unistd.h>

using omaseed::integration::AgentSkill;
namespace SkillLinker = omaseed::integration::SkillLinker;

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
}

/// The login's own home, from the password database: what $HOME must never be here.
QString realHome()
{
    const passwd *entry = ::getpwuid(::getuid());
    return entry ? QDir(QString::fromLocal8Bit(entry->pw_dir)).absolutePath() : QString();
}

} // namespace

class TestAgentSkill : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void constructingReadsNothing();
    void refreshSaysWhatStatusSays();
    void linkThenUnlink();
    void noSkillFailsWithItsMessage();

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString path(const QString &relative) const { return m_tmp->filePath(relative); }
    QString home(const QString &relative) const { return path(QStringLiteral("home/") + relative); }
    QString installed() const { return path(QStringLiteral("usr-share/omaseed/agents/skill")); }
};

void TestAgentSkill::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
    QDir().mkpath(path(QStringLiteral("home")));
    qputenv("HOME", path(QStringLiteral("home")).toUtf8());
    qputenv("XDG_CONFIG_HOME", path(QStringLiteral("home/.config")).toUtf8());
    qputenv("XDG_DATA_HOME", path(QStringLiteral("data-home")).toUtf8());
    qputenv("XDG_DATA_DIRS", path(QStringLiteral("usr-share")).toUtf8());
    qputenv("XDG_CACHE_HOME", path(QStringLiteral("home/.cache")).toUtf8());
    qputenv("XDG_STATE_HOME", path(QStringLiteral("home/.local/state")).toUtf8());
    qunsetenv("OMASEED_SKILL_DIR");
    // The guard: nothing below may run in the real home.
    QVERIFY(!realHome().isEmpty());
    QVERIFY(QDir::homePath() != realHome());
    QCOMPARE(QDir::homePath(), path(QStringLiteral("home")));
    writeFile(installed() + QStringLiteral("/SKILL.md"),
              "---\nname: \"omaseed\"\n---\n# omaseed\n");
}

void TestAgentSkill::cleanup()
{
    m_tmp.reset();
}

void TestAgentSkill::constructingReadsNothing()
{
    AgentSkill skill;
    QSignalSpy finished(&skill, &AgentSkill::finished);
    QVERIFY(!skill.linked());
    QVERIFY(!skill.busy());
    QVERIFY(skill.agents().isEmpty());
    QTest::qWait(50);
    QCOMPARE(finished.count(), 0);
    // Not even the folders it would make.
    QVERIFY(!QFileInfo::exists(home(QStringLiteral(".claude"))));
    QVERIFY(!QFileInfo::exists(home(QStringLiteral(".agents"))));
}

void TestAgentSkill::refreshSaysWhatStatusSays()
{
    QDir().mkpath(home(QStringLiteral(".codex")));
    QVERIFY(SkillLinker::link(installed()).code.isEmpty());

    AgentSkill skill;
    QSignalSpy finished(&skill, &AgentSkill::finished);
    skill.refresh();
    QVERIFY(skill.busy());
    QVERIFY(finished.wait(5000));
    QVERIFY(!skill.busy());
    QVERIFY(skill.linked());
    // The shared ~/.agents is a folder, not an agent: named only when it is the one link.
    QCOMPARE(skill.agents(), QStringList({QStringLiteral("Claude Code"), QStringLiteral("Codex")}));
    // A refresh says nothing failed.
    QVERIFY(skill.message().isEmpty());

    QVERIFY(QFile::remove(home(QStringLiteral(".claude/skills/omaseed"))));
    QVERIFY(QFile::remove(home(QStringLiteral(".codex/skills/omaseed"))));
    skill.refresh();
    QVERIFY(finished.wait(5000));
    QVERIFY(skill.linked());
    QCOMPARE(skill.agents(), QStringList({QStringLiteral("the shared ~/.agents")}));

    QVERIFY(QFile::remove(home(QStringLiteral(".agents/skills/omaseed"))));
    skill.refresh();
    QVERIFY(finished.wait(5000));
    QVERIFY(!skill.linked());
    QVERIFY(skill.agents().isEmpty());
    // What `omaseed skill status` says: nothing linked.
    QVERIFY(!SkillLinker::anyLinked());
}

void TestAgentSkill::linkThenUnlink()
{
    AgentSkill skill;
    QSignalSpy changed(&skill, &AgentSkill::changed);
    QSignalSpy finished(&skill, &AgentSkill::finished);
    QSignalSpy failed(&skill, &AgentSkill::failed);

    skill.link();
    QVERIFY(skill.busy());
    QCOMPARE(changed.count(), 1);
    // One at a time: a second ask while the first runs is dropped.
    skill.unlink();
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.count(), 1);
    QVERIFY(!skill.busy());
    QCOMPARE(changed.count(), 2);
    QCOMPARE(failed.count(), 0);
    QVERIFY(skill.linked());
    QCOMPARE(skill.agents(), QStringList({QStringLiteral("Claude Code")}));
    for (const char *where : {".agents/skills/omaseed", ".claude/skills/omaseed"}) {
        const QFileInfo link(home(QString::fromLatin1(where)));
        QVERIFY2(link.isSymLink(), where);
        QCOMPARE(link.readSymLink(), installed());
    }
    // As the word does (AS6): the guide files the skill names.
    QVERIFY(QFileInfo::exists(home(QStringLiteral(".config/omaseed/skin.schema.json"))));

    skill.unlink();
    QVERIFY(skill.busy());
    QVERIFY(finished.wait(5000));
    QVERIFY(!skill.busy());
    QVERIFY(!skill.linked());
    QVERIFY(skill.agents().isEmpty());
    QVERIFY(!QFileInfo(home(QStringLiteral(".claude/skills/omaseed"))).isSymLink());
    QVERIFY(!QFileInfo(home(QStringLiteral(".agents/skills/omaseed"))).isSymLink());
    // The folders it made stay.
    QVERIFY(QFileInfo(home(QStringLiteral(".claude/skills"))).isDir());
    QCOMPARE(failed.count(), 0);
}

void TestAgentSkill::noSkillFailsWithItsMessage()
{
    QVERIFY(QDir(path(QStringLiteral("usr-share"))).removeRecursively());
    AgentSkill skill;
    QSignalSpy finished(&skill, &AgentSkill::finished);
    QSignalSpy failed(&skill, &AgentSkill::failed);
    skill.link();
    QVERIFY(finished.wait(5000));
    QCOMPARE(failed.count(), 1);
    const QString message = failed.at(0).at(0).toString();
    QVERIFY2(message.contains(QStringLiteral("isn't installed")), qPrintable(message));
    QCOMPARE(skill.message(), message);
    QVERIFY(!skill.linked());
    QVERIFY(!QFileInfo(home(QStringLiteral(".claude/skills/omaseed"))).isSymLink());
}

QTEST_GUILESS_MAIN(TestAgentSkill)
#include "tst_agentskill.moc"
