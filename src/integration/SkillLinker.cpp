#include "integration/SkillLinker.hpp"

#include "core/Paths.hpp"
#include "integration/AgentGuide.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <unistd.h>

namespace omaseed::integration::SkillLinker {

namespace {

const QString kSkillPath = QStringLiteral("omaseed/agents/skill");

bool holdsSkill(const QString &dir)
{
    return !dir.isEmpty() && QFileInfo(QDir(dir).filePath(QStringLiteral("SKILL.md"))).isFile();
}

/// A folder holding a SKILL.md whose frontmatter says `name: omaseed`: a copy of omaseed's
/// skill someone put there (AS10: theirs, and it won't follow omaseed's upgrades).
bool isACopy(const QString &dir)
{
    QFile file(QDir(dir).filePath(QStringLiteral("SKILL.md")));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    static const QRegularExpression name(QStringLiteral("^name:\\s*\"?omaseed\"?\\s*$"));
    for (int i = 0; i < 20 && !file.atEnd(); ++i) {
        if (name.match(QString::fromUtf8(file.readLine()).trimmed()).hasMatch())
            return true;
    }
    return false;
}

QString errnoWords()
{
    return QString::fromLocal8Bit(std::strerror(errno));
}

/// `link` pointing at `source`, made where nothing is or moved over a link of ours: through a
/// hidden sibling renamed into place, so an agent never sees the name missing. Empty when done,
/// else why not.
QString makeLink(const QString &source, const QString &link)
{
    const QString folder = QFileInfo(link).absolutePath();
    if (!QDir().mkpath(folder))
        return QStringLiteral("can't make the folder");
    const QString staging = folder + QStringLiteral("/.omaseed.linking");
    QFile::remove(staging);
    const QByteArray from = QFile::encodeName(source);
    const QByteArray to = QFile::encodeName(staging);
    if (::symlink(from.constData(), to.constData()) != 0)
        return errnoWords();
    if (std::rename(to.constData(), QFile::encodeName(link).constData()) != 0) {
        const QString why = errnoWords();
        QFile::remove(staging);
        return why;
    }
    return QString();
}

/// What is at `folder`'s omaseed entry now, as status says it.
FolderState stateOf(const Folder &folder, const QString &source)
{
    FolderState state;
    state.agent = folder.agent;
    state.path = folder.link();
    const QFileInfo info(state.path);
    if (info.isSymLink()) {
        state.target = info.readSymLink();
        if (isOurs(state.path, source)) {
            state.state = info.exists() ? State::Linked : State::Dead;
            return state;
        }
        state.state = State::Yours;
        state.why = QStringLiteral("a link of yours, not omaseed's");
        return state;
    }
    if (info.exists()) {
        state.state = State::Yours;
        state.copy = info.isDir() && isACopy(state.path);
        state.why = info.isDir() ? QStringLiteral("a folder of yours, not a link")
                                 : QStringLiteral("a file of yours, not a link");
        return state;
    }
    state.state = folder.installed ? State::Absent : State::Skipped;
    return state;
}

QString notInstalledWords()
{
    QStringList places;
    for (const QString &dir :
         QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        places << QDir(dir).filePath(kSkillPath);
    return QStringLiteral("omaseed's skill isn't installed: no SKILL.md in %1.")
        .arg(places.join(QStringLiteral(", ")));
}

} // namespace

QString findSource(const QString &devSourceDir)
{
    const QString fromEnv = qEnvironmentVariable("OMASEED_SKILL_DIR");
    if (holdsSkill(fromEnv))
        return QDir(fromEnv).absolutePath();

    for (const QString &dir :
         QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        const QString candidate = QDir(dir).filePath(kSkillPath);
        if (holdsSkill(candidate))
            return QDir(candidate).absolutePath();
    }

    if (QCoreApplication::instance()) {
        const QString besideBinary = QDir(QCoreApplication::applicationDirPath())
                                         .filePath(QStringLiteral("../share/") + kSkillPath);
        if (holdsSkill(besideBinary))
            return QDir(besideBinary).absolutePath();
    }

    if (holdsSkill(devSourceDir))
        return QDir(devSourceDir).absolutePath();
    return QString();
}

QList<Folder> folders()
{
    // Omarchy's list (omarchy-provision-user), each with the name the lines say.
    const QDir home(QDir::homePath());
    const auto dirAt = [&home](const QString &relative) {
        return QFileInfo(home.filePath(relative)).isDir();
    };
    QList<Folder> list{
        {QStringLiteral("the shared ~/.agents"),
         home.filePath(QStringLiteral(".agents/skills")),
         true,
         true},
        {QStringLiteral("Claude Code"),
         home.filePath(QStringLiteral(".claude/skills")),
         true,
         true},
        {QStringLiteral("Codex"),
         home.filePath(QStringLiteral(".codex/skills")),
         false,
         dirAt(QStringLiteral(".codex"))},
        {QStringLiteral("pi"),
         home.filePath(QStringLiteral(".pi/agent/skills")),
         false,
         dirAt(QStringLiteral(".pi/agent"))},
        {QStringLiteral("Gemini"),
         home.filePath(QStringLiteral(".gemini/config/skills")),
         false,
         dirAt(QStringLiteral(".gemini"))},
        {QStringLiteral("Hermes"),
         home.filePath(QStringLiteral(".hermes/skills")),
         false,
         dirAt(QStringLiteral(".hermes"))},
    };
    const QDir profiles(home.filePath(QStringLiteral(".hermes/profiles")));
    for (const QString &name : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        list.append({QStringLiteral("Hermes profile ") + name,
                     profiles.filePath(name + QStringLiteral("/skills")),
                     false,
                     true});
    return list;
}

bool isOurs(const QString &linkPath, const QString &source)
{
    const QFileInfo info(linkPath);
    if (!info.isSymLink())
        return false;
    QString text = info.readSymLink();
    while (text.size() > 1 && text.endsWith(QLatin1Char('/')))
        text.chop(1);
    if (text.endsWith(QStringLiteral("/omaseed/agents/skill")) ||
        text.endsWith(QStringLiteral("/data/agents/skill")))
        return true;
    // The folder omaseed is using, whatever its path: link writes `source` as the link text.
    const QString target = QDir::cleanPath(text);
    const auto names = [&target](const QString &dir) {
        return !dir.isEmpty() && QDir::cleanPath(QDir(dir).absolutePath()) == target;
    };
    return names(source) || names(qEnvironmentVariable("OMASEED_SKILL_DIR"));
}

QString stateName(State state)
{
    switch (state) {
    case State::Linked:
        return QStringLiteral("linked");
    case State::Dead:
        return QStringLiteral("dead");
    case State::Yours:
        return QStringLiteral("yours");
    case State::Absent:
        return QStringLiteral("absent");
    case State::Skipped:
        return QStringLiteral("skipped");
    case State::Failed:
        return QStringLiteral("failed");
    }
    return QString();
}

Report status(const QString &source)
{
    Report report;
    report.source = source;
    for (const Folder &folder : folders())
        report.folders.append(stateOf(folder, source));
    return report;
}

bool anyLinked()
{
    const Report report = status(QString());
    return std::any_of(report.folders.begin(), report.folders.end(), [](const FolderState &f) {
        return f.state == State::Linked;
    });
}

Report link(const QString &source)
{
    Report report;
    report.source = source;
    if (source.isEmpty()) {
        report.folders = status(source).folders;
        report.code = QStringLiteral("not-written");
        report.message = notInstalledWords();
        return report;
    }
    bool linked = false;
    QStringList refused;
    for (const Folder &folder : folders()) {
        FolderState state = stateOf(folder, source);
        const bool ours = state.state == State::Linked || state.state == State::Dead;
        if (state.state == State::Absent || (ours && state.target != source)) {
            const QString why = makeLink(source, state.path);
            if (why.isEmpty()) {
                report.changed << state.path;
                state = stateOf(folder, source);
            } else {
                state.state = State::Failed;
                state.why = why;
            }
        }
        if (state.state == State::Linked)
            linked = true;
        else if (state.state == State::Yours || state.state == State::Failed)
            refused << state.path + QStringLiteral(": ") + state.why;
        report.folders.append(state);
    }
    if (!linked) {
        report.code = QStringLiteral("not-written");
        report.message = QStringLiteral("omaseed: no folder could take the skill: %1.")
                             .arg(refused.join(QStringLiteral("; ")));
        return report;
    }
    // AS6: the schema the skill names, and AGENTS.md, before omaseed's first start.
    AgentGuide::writeInto(core::paths::configDir());
    return report;
}

Report unlink(const QString &source)
{
    Report report;
    report.source = source;
    QStringList failed;
    for (const Folder &folder : folders()) {
        FolderState state = stateOf(folder, source);
        if (state.state == State::Linked || state.state == State::Dead) {
            if (QFile::remove(state.path)) {
                report.changed << state.path;
                state = stateOf(folder, source);
            } else {
                state.state = State::Failed;
                state.why = QStringLiteral("can't remove it");
                failed << state.path;
            }
        }
        report.folders.append(state);
    }
    if (!failed.isEmpty()) {
        report.code = QStringLiteral("not-written");
        report.message =
            QStringLiteral("omaseed: can't remove %1.").arg(failed.join(QStringLiteral(", ")));
    }
    return report;
}

QString notLinkedLine()
{
    return QStringLiteral("Coding agents load this by themselves once it is linked: omaseed skill "
                          "link.");
}

QString printedGuide()
{
    QString text =
        AgentGuide::printedMap() + QLatin1Char('\n') + AgentGuide::pagesLine() + QLatin1Char('\n');
    if (!anyLinked())
        text += notLinkedLine() + QLatin1Char('\n');
    return text;
}

} // namespace omaseed::integration::SkillLinker
