// DECISIONS C48 AS6–AS8, AS10 (#286, docs/65 §5): omaseed's skill linked into each agent's
// skill folder. The two always, the others only when their agent is installed; a folder, a file
// or a link of the person's left and named; a build's link moved to the installed one; unlink
// taking only ours; a dead link still ours; no skill, nothing written; where the skill is found.
//
// Every case runs in a temporary HOME (and XDG homes) set here, never inherited: the real
// ~/.claude, ~/.agents, ~/.codex, ~/.pi, ~/.gemini, ~/.hermes and ~/.config/omaseed are never
// touched, and each case checks that first.

#include "integration/SkillLinker.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include <pwd.h>
#include <unistd.h>

namespace SkillLinker = omaseed::integration::SkillLinker;
using SkillLinker::State;

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

/// A skill folder at `root`/`relative` (".../omaseed/agents/skill" or ".../data/agents/skill").
QString makeSkill(const QString &root, const QString &relative)
{
    const QString dir = QDir(root).filePath(relative);
    writeFile(dir + QStringLiteral("/SKILL.md"), "---\nname: \"omaseed\"\n---\n# omaseed\n");
    writeFile(dir + QStringLiteral("/ref/library.md"), "# library\n");
    return dir;
}

/// The login's own home, from the password database: what $HOME must never be here.
QString realHome()
{
    const passwd *entry = ::getpwuid(::getuid());
    return entry ? QDir(QString::fromLocal8Bit(entry->pw_dir)).absolutePath() : QString();
}

SkillLinker::FolderState stateFor(const SkillLinker::Report &report, const QString &agent)
{
    for (const auto &folder : report.folders) {
        if (folder.agent == agent)
            return folder;
    }
    return {};
}

} // namespace

class TestSkillLinker : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void linksTheTwoAlways();
    void othersOnlyWhenInstalled();
    void aFolderOfYoursIsLeft();
    void aLinkElsewhereIsLeft();
    void aBuildLinkMovesToTheInstalled();
    void linkTwiceIsNothingToDo();
    void unlinkTakesOnlyOurs();
    void aDeadLinkIsOursAndDead();
    void noSourceIsNotWritten();
    void noFolderCanTakeItIsNotWritten();
    void statusSaysYoursForACopy();
    void findSourceOrder();
    void thePrintedGuideSaysLinkOnlyWhenUnlinked();
    void anOddSkillDirIsStillOurs();

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString path(const QString &relative) const { return m_tmp->filePath(relative); }
    QString home(const QString &relative) const { return path(QStringLiteral("home/") + relative); }
    QString installed() const { return path(QStringLiteral("usr-share/omaseed/agents/skill")); }
};

void TestSkillLinker::init()
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
    makeSkill(path(QStringLiteral("usr-share")), QStringLiteral("omaseed/agents/skill"));
}

void TestSkillLinker::cleanup()
{
    m_tmp.reset();
}

void TestSkillLinker::linksTheTwoAlways()
{
    const auto report = SkillLinker::link(installed());
    QVERIFY2(report.code.isEmpty(), qPrintable(report.message));
    for (const char *where : {".agents/skills/omaseed", ".claude/skills/omaseed"}) {
        const QFileInfo link(home(QString::fromLatin1(where)));
        QVERIFY2(link.isSymLink(), where);
        QCOMPARE(link.readSymLink(), installed());
        QVERIFY(QFileInfo::exists(link.filePath() + QStringLiteral("/SKILL.md")));
    }
    QCOMPARE(report.changed.size(), 2);
    QCOMPARE(stateFor(report, QStringLiteral("Codex")).state, State::Skipped);
    QVERIFY(!QFileInfo::exists(home(QStringLiteral(".codex"))));
    // AS6: the guide files the skill names, written once.
    for (const char *file : {"AGENTS.md", "CLAUDE.md", "skin.schema.json"})
        QVERIFY2(QFileInfo::exists(home(QStringLiteral(".config/omaseed/")) + QLatin1String(file)),
                 file);
}

void TestSkillLinker::othersOnlyWhenInstalled()
{
    for (const char *agent :
         {".codex", ".pi/agent", ".gemini", ".hermes/profiles/a", ".hermes/profiles/b"})
        QVERIFY(QDir().mkpath(home(QString::fromLatin1(agent))));
    const auto report = SkillLinker::link(installed());
    QVERIFY2(report.code.isEmpty(), qPrintable(report.message));
    for (const char *where : {".codex/skills/omaseed",
                              ".pi/agent/skills/omaseed",
                              ".gemini/config/skills/omaseed",
                              ".hermes/skills/omaseed",
                              ".hermes/profiles/a/skills/omaseed",
                              ".hermes/profiles/b/skills/omaseed"})
        QVERIFY2(SkillLinker::isOurs(home(QString::fromLatin1(where))), where);
    QCOMPARE(report.changed.size(), 8);
    QCOMPARE(stateFor(report, QStringLiteral("Hermes profile b")).state, State::Linked);

    // Only ~/.pi, not ~/.pi/agent: pi isn't installed.
    m_tmp = std::make_unique<QTemporaryDir>();
    init();
    QVERIFY(QDir().mkpath(home(QStringLiteral(".pi"))));
    const auto second = SkillLinker::link(installed());
    QCOMPARE(stateFor(second, QStringLiteral("pi")).state, State::Skipped);
    QVERIFY(!QFileInfo::exists(home(QStringLiteral(".pi/agent"))));
}

void TestSkillLinker::aFolderOfYoursIsLeft()
{
    writeFile(home(QStringLiteral(".claude/skills/omaseed/notes.md")), "mine\n");
    writeFile(home(QStringLiteral(".agents/skills/omaseed")), "a file\n");
    QDir().mkpath(home(QStringLiteral(".codex")));
    const auto report = SkillLinker::link(installed());
    QVERIFY2(report.code.isEmpty(), qPrintable(report.message));
    QCOMPARE(readFile(home(QStringLiteral(".claude/skills/omaseed/notes.md"))),
             QByteArray("mine\n"));
    QCOMPARE(readFile(home(QStringLiteral(".agents/skills/omaseed"))), QByteArray("a file\n"));
    const auto claude = stateFor(report, QStringLiteral("Claude Code"));
    QCOMPARE(claude.state, State::Yours);
    QCOMPARE(claude.why, QStringLiteral("a folder of yours, not a link"));
    QVERIFY(!claude.copy);
    QCOMPARE(stateFor(report, QStringLiteral("the shared ~/.agents")).why,
             QStringLiteral("a file of yours, not a link"));
    QCOMPARE(stateFor(report, QStringLiteral("Codex")).state, State::Linked);
    QCOMPARE(report.changed, QStringList{home(QStringLiteral(".codex/skills/omaseed"))});
}

void TestSkillLinker::aLinkElsewhereIsLeft()
{
    const QString other = path(QStringLiteral("elsewhere/skill"));
    QDir().mkpath(other);
    QDir().mkpath(home(QStringLiteral(".claude/skills")));
    QVERIFY(QFile::link(other, home(QStringLiteral(".claude/skills/omaseed"))));
    const auto report = SkillLinker::link(installed());
    QVERIFY2(report.code.isEmpty(), qPrintable(report.message));
    QCOMPARE(QFileInfo(home(QStringLiteral(".claude/skills/omaseed"))).readSymLink(), other);
    const auto claude = stateFor(report, QStringLiteral("Claude Code"));
    QCOMPARE(claude.state, State::Yours);
    QCOMPARE(claude.target, other);
    QVERIFY(!SkillLinker::isOurs(home(QStringLiteral(".claude/skills/omaseed"))));
}

void TestSkillLinker::aBuildLinkMovesToTheInstalled()
{
    const QString build =
        makeSkill(path(QStringLiteral("src")), QStringLiteral("data/agents/skill"));
    QVERIFY(SkillLinker::link(build).code.isEmpty());
    QCOMPARE(QFileInfo(home(QStringLiteral(".claude/skills/omaseed"))).readSymLink(), build);
    QVERIFY(SkillLinker::isOurs(home(QStringLiteral(".claude/skills/omaseed"))));

    const auto report = SkillLinker::link(installed());
    QVERIFY2(report.code.isEmpty(), qPrintable(report.message));
    QCOMPARE(QFileInfo(home(QStringLiteral(".claude/skills/omaseed"))).readSymLink(), installed());
    QCOMPARE(QFileInfo(home(QStringLiteral(".agents/skills/omaseed"))).readSymLink(), installed());
    QCOMPARE(report.changed.size(), 2);
    // No staging name left beside it, and the build's folder untouched.
    QCOMPARE(QDir(home(QStringLiteral(".claude/skills")))
                 .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot),
             QStringList{QStringLiteral("omaseed")});
    QVERIFY(QFileInfo::exists(build + QStringLiteral("/SKILL.md")));
    // And back.
    QVERIFY(SkillLinker::link(build).code.isEmpty());
    QCOMPARE(QFileInfo(home(QStringLiteral(".claude/skills/omaseed"))).readSymLink(), build);
}

void TestSkillLinker::linkTwiceIsNothingToDo()
{
    QVERIFY(SkillLinker::link(installed()).code.isEmpty());
    const auto again = SkillLinker::link(installed());
    QVERIFY2(again.code.isEmpty(), qPrintable(again.message));
    QVERIFY(again.changed.isEmpty());
    QCOMPARE(stateFor(again, QStringLiteral("Claude Code")).state, State::Linked);
}

void TestSkillLinker::unlinkTakesOnlyOurs()
{
    QDir().mkpath(home(QStringLiteral(".codex")));
    QDir().mkpath(home(QStringLiteral(".hermes")));
    writeFile(home(QStringLiteral(".codex/skills/omaseed/SKILL.md")), "name: omaseed\n");
    const QString other = path(QStringLiteral("elsewhere/skill"));
    QDir().mkpath(other);
    QDir().mkpath(home(QStringLiteral(".hermes/skills")));
    QVERIFY(QFile::link(other, home(QStringLiteral(".hermes/skills/omaseed"))));
    QVERIFY(SkillLinker::link(installed()).code.isEmpty());

    const auto report = SkillLinker::unlink(installed());
    QVERIFY2(report.code.isEmpty(), qPrintable(report.message));
    QCOMPARE(report.changed,
             (QStringList{home(QStringLiteral(".agents/skills/omaseed")),
                          home(QStringLiteral(".claude/skills/omaseed"))}));
    QVERIFY(!QFileInfo(home(QStringLiteral(".claude/skills/omaseed"))).isSymLink());
    QVERIFY(!QFileInfo::exists(home(QStringLiteral(".claude/skills/omaseed"))));
    // The folders it made stay; the person's folder and link stay.
    QVERIFY(QFileInfo(home(QStringLiteral(".claude/skills"))).isDir());
    QVERIFY(QFileInfo(home(QStringLiteral(".agents/skills"))).isDir());
    QCOMPARE(readFile(home(QStringLiteral(".codex/skills/omaseed/SKILL.md"))),
             QByteArray("name: omaseed\n"));
    QCOMPARE(QFileInfo(home(QStringLiteral(".hermes/skills/omaseed"))).readSymLink(), other);
    // Nothing left to take out.
    QVERIFY(SkillLinker::unlink(installed()).changed.isEmpty());
}

void TestSkillLinker::aDeadLinkIsOursAndDead()
{
    const QString build =
        makeSkill(path(QStringLiteral("src")), QStringLiteral("data/agents/skill"));
    QVERIFY(SkillLinker::link(build).code.isEmpty());
    QVERIFY(QDir(path(QStringLiteral("src"))).removeRecursively());
    const auto status = SkillLinker::status(QString());
    QCOMPARE(stateFor(status, QStringLiteral("Claude Code")).state, State::Dead);
    QCOMPARE(stateFor(status, QStringLiteral("Claude Code")).target, build);
    QVERIFY(!SkillLinker::anyLinked());
    const auto report = SkillLinker::unlink(QString());
    QCOMPARE(report.changed.size(), 2);
    QVERIFY(!QFileInfo(home(QStringLiteral(".claude/skills/omaseed"))).isSymLink());
}

void TestSkillLinker::noSourceIsNotWritten()
{
    QVERIFY(QDir(path(QStringLiteral("usr-share"))).removeRecursively());
    QVERIFY(SkillLinker::findSource(path(QStringLiteral("src/data/agents/skill"))).isEmpty());
    const auto report = SkillLinker::link(QString());
    QCOMPARE(report.code, QStringLiteral("not-written"));
    QVERIFY2(report.message.contains(QLatin1String("omaseed's skill isn't installed: no SKILL.md")),
             qPrintable(report.message));
    QVERIFY(!QFileInfo::exists(home(QStringLiteral(".claude"))));
    QVERIFY(!QFileInfo::exists(home(QStringLiteral(".agents"))));
    QVERIFY(!QFileInfo::exists(home(QStringLiteral(".config/omaseed"))));
}

void TestSkillLinker::noFolderCanTakeItIsNotWritten()
{
    QDir().mkpath(home(QStringLiteral(".claude/skills/omaseed")));
    QDir().mkpath(home(QStringLiteral(".agents/skills/omaseed")));
    const auto report = SkillLinker::link(installed());
    QCOMPARE(report.code, QStringLiteral("not-written"));
    QVERIFY2(report.message.contains(QLatin1String("no folder could take the skill")),
             qPrintable(report.message));
    QVERIFY(!QFileInfo::exists(home(QStringLiteral(".config/omaseed"))));
}

void TestSkillLinker::statusSaysYoursForACopy()
{
    QDir().mkpath(home(QStringLiteral(".pi/agent")));
    writeFile(home(QStringLiteral(".pi/agent/skills/omaseed/SKILL.md")),
              "---\nname: \"omaseed\"\ndescription: \"x\"\n---\n");
    const auto status = SkillLinker::status(installed());
    QCOMPARE(status.source, installed());
    const auto pi = stateFor(status, QStringLiteral("pi"));
    QCOMPARE(pi.state, State::Yours);
    QVERIFY(pi.copy);
    QCOMPARE(stateFor(status, QStringLiteral("Claude Code")).state, State::Absent);
    QCOMPARE(stateFor(status, QStringLiteral("Gemini")).state, State::Skipped);
    // status writes nothing.
    QVERIFY(!QFileInfo::exists(home(QStringLiteral(".claude"))));
}

void TestSkillLinker::findSourceOrder()
{
    const QString dev = makeSkill(path(QStringLiteral("src")), QStringLiteral("data/agents/skill"));
    const QString custom = makeSkill(path(QStringLiteral("custom")), QStringLiteral("skill"));
    const QString user = path(QStringLiteral("data-home/omaseed/agents/skill"));

    // 1. $OMASEED_SKILL_DIR, when it holds a SKILL.md.
    qputenv("OMASEED_SKILL_DIR", custom.toUtf8());
    QCOMPARE(SkillLinker::findSource(dev), custom);
    qputenv("OMASEED_SKILL_DIR", path(QStringLiteral("nothing")).toUtf8());
    // 2. The XDG data dirs, $XDG_DATA_HOME first.
    QCOMPARE(SkillLinker::findSource(dev), installed());
    makeSkill(path(QStringLiteral("data-home")), QStringLiteral("omaseed/agents/skill"));
    QCOMPARE(SkillLinker::findSource(dev), user);
    QVERIFY(QDir(path(QStringLiteral("data-home"))).removeRecursively());
    QVERIFY(QDir(path(QStringLiteral("usr-share"))).removeRecursively());

    // 3. ../share/omaseed/agents/skill beside the binary (this test's, in the build tree).
    const QString beside = QDir(QCoreApplication::applicationDirPath())
                               .absoluteFilePath(QStringLiteral("../share/omaseed/agents/skill"));
    const QString shareRoot =
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../share"));
    QVERIFY2(!QFileInfo::exists(shareRoot), qPrintable(shareRoot));
    makeSkill(QFileInfo(beside).absolutePath(), QStringLiteral("skill"));
    const QString found = SkillLinker::findSource(dev);
    QDir(shareRoot).removeRecursively();
    QCOMPARE(found, QDir(beside).absolutePath());

    // 4. The source tree's, last.
    QCOMPARE(SkillLinker::findSource(dev), dev);
    QVERIFY(SkillLinker::findSource().isEmpty());
}

void TestSkillLinker::thePrintedGuideSaysLinkOnlyWhenUnlinked()
{
    QVERIFY(SkillLinker::printedGuide().endsWith(SkillLinker::notLinkedLine() + QLatin1Char('\n')));
    QVERIFY(SkillLinker::link(installed()).code.isEmpty());
    QVERIFY(SkillLinker::anyLinked());
    QVERIFY(SkillLinker::printedGuide().endsWith(
        QLatin1String("\nPages: omaseed skill library, skins, files.\n")));
    // AGENTS.md never carries the line.
    QVERIFY(!readFile(home(QStringLiteral(".config/omaseed/AGENTS.md")))
                 .contains(SkillLinker::notLinkedLine().toUtf8()));
}

// An OMASEED_SKILL_DIR whose path ends in neither /omaseed/agents/skill nor /data/agents/skill:
// the links link makes to it are omaseed's for status and unlink, not the person's.
void TestSkillLinker::anOddSkillDirIsStillOurs()
{
    const QString odd = makeSkill(path(QStringLiteral("odd")), QStringLiteral("my-skill"));
    qputenv("OMASEED_SKILL_DIR", odd.toUtf8());
    const QString source = SkillLinker::findSource();
    QCOMPARE(source, odd);
    QVERIFY(SkillLinker::link(source).code.isEmpty());
    const QString claude = home(QStringLiteral(".claude/skills/omaseed"));
    QCOMPARE(QFileInfo(claude).readSymLink(), odd);
    QVERIFY(SkillLinker::isOurs(claude, source));
    QVERIFY(SkillLinker::isOurs(claude)); // through $OMASEED_SKILL_DIR alone

    const auto status = SkillLinker::status(source);
    QCOMPARE(stateFor(status, QStringLiteral("Claude Code")).state, State::Linked);
    QCOMPARE(stateFor(status, QStringLiteral("the shared ~/.agents")).state, State::Linked);
    QVERIFY(SkillLinker::anyLinked());

    // The same link is the person's once omaseed uses another folder.
    qunsetenv("OMASEED_SKILL_DIR");
    QVERIFY(!SkillLinker::isOurs(claude, installed()));
    QCOMPARE(stateFor(SkillLinker::status(installed()), QStringLiteral("Claude Code")).state,
             State::Yours);

    // Back to it: unlink takes both out.
    qputenv("OMASEED_SKILL_DIR", odd.toUtf8());
    const auto report = SkillLinker::unlink(source);
    QVERIFY(report.code.isEmpty());
    QCOMPARE(report.changed.size(), 2);
    QVERIFY(!QFileInfo(claude).isSymLink());
    QVERIFY(QFileInfo::exists(odd + QStringLiteral("/SKILL.md")));
}

QTEST_GUILESS_MAIN(TestSkillLinker)
#include "tst_skilllinker.moc"
