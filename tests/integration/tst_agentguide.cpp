// DECISIONS C45 V19 (#163): the guide's library part, and the AGENTS.md and CLAUDE.md written
// into omaseed's config folder: both when missing, rewritten only when different, a file that
// isn't omaseed's left alone. Every path is under a temporary directory.

#include "core/AgentWords.hpp"
#include "integration/AgentGuide.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace Guide = omaseed::integration::AgentGuide;
namespace Words = omaseed::core::AgentWords;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
}

QDateTime mtime(const QString &path)
{
    return QFileInfo(path).fileTime(QFileDevice::FileModificationTime);
}

void ageFile(const QString &path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addDays(-3),
                             QFileDevice::FileModificationTime));
}

QByteArray agentsBytes()
{
    return (Guide::header() + QStringLiteral("\n\n") + Guide::text()).toUtf8();
}

} // namespace

class TestAgentGuide : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void writesBothWhenMissing();
    void rewritesOnlyWhenDifferent();
    void leavesAForeignAgentsMd();
    void leavesAForeignClaudeMd();
    void textNamesEveryWordAndExit();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_config;
};

void TestAgentGuide::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_config = m_dir->filePath(QStringLiteral("omaseed"));
}

void TestAgentGuide::writesBothWhenMissing()
{
    // The folder itself may not exist yet on a first start.
    const Guide::WriteResult result = Guide::writeInto(m_config);
    QCOMPARE(result.agents, Guide::Wrote::Written);
    QCOMPARE(result.claude, Guide::Wrote::Written);
    const QByteArray agents = readFile(QDir(m_config).filePath(QStringLiteral("AGENTS.md")));
    QVERIFY(agents.startsWith(
        "Written by omaseed on every start. Edits here are overwritten.\n\n## Your library\n"));
    QCOMPARE(agents, agentsBytes());
    QCOMPARE(readFile(QDir(m_config).filePath(QStringLiteral("CLAUDE.md"))),
             QByteArray("@AGENTS.md\n"));
}

void TestAgentGuide::rewritesOnlyWhenDifferent()
{
    QVERIFY(QDir().mkpath(m_config));
    const QString agents = QDir(m_config).filePath(QStringLiteral("AGENTS.md"));
    const QString claude = QDir(m_config).filePath(QStringLiteral("CLAUDE.md"));
    QCOMPARE(Guide::writeInto(m_config).agents, Guide::Wrote::Written);
    ageFile(agents);
    ageFile(claude);
    const QDateTime agentsAt = mtime(agents);
    const QDateTime claudeAt = mtime(claude);

    // The same text: neither file is touched, so neither mtime moves.
    Guide::WriteResult result = Guide::writeInto(m_config);
    QCOMPARE(result.agents, Guide::Wrote::Same);
    QCOMPARE(result.claude, Guide::Wrote::Same);
    QCOMPARE(mtime(agents), agentsAt);
    QCOMPARE(mtime(claude), claudeAt);

    // An older omaseed's guide (its header, other text) is replaced; an edit too.
    writeFile(agents, (Guide::header() + QStringLiteral("\n\nan older guide\n")).toUtf8());
    ageFile(agents);
    result = Guide::writeInto(m_config);
    QCOMPARE(result.agents, Guide::Wrote::Written);
    QCOMPARE(readFile(agents), agentsBytes());
    QVERIFY(mtime(agents) > agentsAt);
    QCOMPARE(mtime(claude), claudeAt);
}

void TestAgentGuide::leavesAForeignAgentsMd()
{
    QVERIFY(QDir().mkpath(m_config));
    const QString agents = QDir(m_config).filePath(QStringLiteral("AGENTS.md"));
    const QByteArray own = "# My notes\n\nWritten by omaseed on every start. Edits here are "
                           "overwritten.\n";
    writeFile(agents, own);
    const Guide::WriteResult result = Guide::writeInto(m_config);
    QCOMPARE(result.agents, Guide::Wrote::LeftAlone);
    QCOMPARE(readFile(agents), own);
    // CLAUDE.md is still made when it is missing.
    QCOMPARE(result.claude, Guide::Wrote::Written);
}

void TestAgentGuide::leavesAForeignClaudeMd()
{
    QVERIFY(QDir().mkpath(m_config));
    const QString claude = QDir(m_config).filePath(QStringLiteral("CLAUDE.md"));
    const QByteArray own = "@AGENTS.md\n\nAlso: be brief.\n";
    writeFile(claude, own);
    const Guide::WriteResult result = Guide::writeInto(m_config);
    QCOMPARE(result.claude, Guide::Wrote::LeftAlone);
    QCOMPARE(readFile(claude), own);
    QCOMPARE(result.agents, Guide::Wrote::Written);
}

void TestAgentGuide::textNamesEveryWordAndExit()
{
    const QString text = Guide::text();
    QVERIFY(text.startsWith(QStringLiteral("## Your library\n")));
    QVERIFY(text.endsWith(QStringLiteral(
        "Every answer is one JSON object with \"v\": 1; fields are only added within it.\n")));

    // Every read of the table, as its command line (C45 V19).
    for (const QString &word : {QStringLiteral("status"),
                                QStringLiteral("errors"),
                                QStringLiteral("log"),
                                QStringLiteral("moves")}) {
        QVERIFY2(!Words::method(word).isEmpty(), qPrintable(word));
        QVERIFY2(text.contains(QStringLiteral("  omaseed %1 --json ").arg(word)), qPrintable(word));
    }
    // Every failure code, under the exit the CLI gives it.
    for (const QString &code : {QStringLiteral("usage"),
                                QStringLiteral("unknown-server"),
                                QStringLiteral("ambiguous"),
                                QStringLiteral("not-running"),
                                QStringLiteral("too-old"),
                                QStringLiteral("no-bus"),
                                QStringLiteral("no-reply"),
                                QStringLiteral("not-current"),
                                QStringLiteral("unreachable"),
                                QStringLiteral("no-log"),
                                QStringLiteral("unknown-torrent"),
                                QStringLiteral("first-sync")}) {
        const QString exit = QStringLiteral("\n  %1  ").arg(Words::exitFor(code));
        const qsizetype line = text.indexOf(exit);
        QVERIFY2(line >= 0, qPrintable(code));
        const QString row = text.mid(line + 1, text.indexOf(QLatin1Char('\n'), line + 1) - line);
        QVERIFY2(row.contains(code), qPrintable(code + QStringLiteral(" not on: ") + row));
    }
    QVERIFY(text.contains(QStringLiteral("\n  0  answered")));

    // The library part only: no actions (they aren't built), no reserved word offered.
    QVERIFY(!text.contains(QStringLiteral("### Acting")));
    QVERIFY(!text.contains(QStringLiteral("omaseed do")));
    QVERIFY(!text.contains(QStringLiteral("omaseed check")));
    QVERIFY(!text.contains(QStringLiteral("omaseed skin")));
}

QTEST_GUILESS_MAIN(TestAgentGuide)
#include "tst_agentguide.moc"
