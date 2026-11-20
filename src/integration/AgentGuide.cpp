#include "integration/AgentGuide.hpp"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QSaveFile>

namespace {

Q_LOGGING_CATEGORY(lcAgentGuide, "omaseed.agentguide")

const QByteArray kClaudeMd = QByteArrayLiteral("@AGENTS.md\n");

QByteArray readAll(const QString &path, bool *exists)
{
    QFile file(path);
    *exists = file.exists();
    if (!*exists || !file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool save(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        qCWarning(lcAgentGuide).noquote()
            << QStringLiteral("could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace

namespace omaseed::integration::AgentGuide {

QString header()
{
    return QStringLiteral("Written by omaseed on every start. Edits here are overwritten.");
}

QString text()
{
    QFile file(QStringLiteral(":/omaseed/agents/library.md"));
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

WriteResult writeInto(const QString &dir)
{
    WriteResult result;
    if (!QDir().mkpath(dir)) {
        qCWarning(lcAgentGuide).noquote() << QStringLiteral("could not create %1").arg(dir);
        return result;
    }

    // DECISIONS C45 V19: AGENTS.md is omaseed's while its first line says so.
    const QString agentsPath = QDir(dir).filePath(QStringLiteral("AGENTS.md"));
    const QByteArray agents = (header() + QStringLiteral("\n\n") + text()).toUtf8();
    bool exists = false;
    const QByteArray had = readAll(agentsPath, &exists);
    if (!exists)
        result.agents = save(agentsPath, agents) ? Wrote::Written : Wrote::Failed;
    else if (had == agents)
        result.agents = Wrote::Same;
    else if (!had.startsWith(header().toUtf8() + '\n')) {
        qCInfo(lcAgentGuide).noquote()
            << QStringLiteral("left %1 alone: it doesn't start with omaseed's line, so it is "
                              "someone else's")
                   .arg(agentsPath);
        result.agents = Wrote::LeftAlone;
    } else
        result.agents = save(agentsPath, agents) ? Wrote::Written : Wrote::Failed;

    const QString claudePath = QDir(dir).filePath(QStringLiteral("CLAUDE.md"));
    const QByteArray claudeHad = readAll(claudePath, &exists);
    if (!exists)
        result.claude = save(claudePath, kClaudeMd) ? Wrote::Written : Wrote::Failed;
    else if (claudeHad == kClaudeMd)
        result.claude = Wrote::Same;
    else {
        qCInfo(lcAgentGuide).noquote()
            << QStringLiteral("left %1 alone: it says more than @AGENTS.md").arg(claudePath);
        result.claude = Wrote::LeftAlone;
    }
    return result;
}

} // namespace omaseed::integration::AgentGuide
