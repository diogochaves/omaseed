// DECISIONS C45 V17 (#293): the ask names the program that started `omaseed do`, read from
// /proc/PID/stat. A comm with spaces and parentheses, a missing pid, a stat that isn't one,
// and the word's parent through a tree standing in for /proc.

#include "core/ProcParent.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace omaseed::core;

class TestProcParent : public QObject
{
    Q_OBJECT

private slots:
    void parsesTheLine();
    void commWithSpacesAndParentheses();
    void notAStatLine_data();
    void notAStatLine();
    void readsTheParentUnderARoot();
    void missingPidSaysNothing();

private:
    static void writeStat(const QString &root, uint pid, const QByteArray &line);
};

void TestProcParent::writeStat(const QString &root, uint pid, const QByteArray &line)
{
    QVERIFY(QDir(root).mkpath(QString::number(pid)));
    QFile file(QStringLiteral("%1/%2/stat").arg(root).arg(pid));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(line + '\n');
}

void TestProcParent::parsesTheLine()
{
    const auto stat = ProcParent::parseStat("48213 (claude) S 4100 48213 4100 34817 0 4194560");
    QVERIFY(stat.has_value());
    QCOMPARE(stat->pid, 48213u);
    QCOMPARE(stat->ppid, 4100u);
    QCOMPARE(stat->comm, QStringLiteral("claude"));
}

void TestProcParent::commWithSpacesAndParentheses()
{
    // The comm ends at the last ')': a program may call itself anything.
    const auto stat = ProcParent::parseStat("51 (my (odd) prog) R 7 51 7 0 -1");
    QVERIFY(stat.has_value());
    QCOMPARE(stat->comm, QStringLiteral("my (odd) prog"));
    QCOMPARE(stat->ppid, 7u);
}

void TestProcParent::notAStatLine_data()
{
    QTest::addColumn<QByteArray>("line");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("no comm") << QByteArray("51 R 7");
    QTest::newRow("no ppid") << QByteArray("51 (sh) R");
    QTest::newRow("bad pid") << QByteArray("x (sh) R 7");
    QTest::newRow("zero pid") << QByteArray("0 (sh) R 7");
    QTest::newRow("bad ppid") << QByteArray("51 (sh) R seven");
}

void TestProcParent::notAStatLine()
{
    QFETCH(QByteArray, line);
    QVERIFY(!ProcParent::parseStat(line).has_value());
}

void TestProcParent::readsTheParentUnderARoot()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeStat(root.path(), 900, "900 (omaseed) S 48213 900 4100 0 -1");
    writeStat(root.path(), 48213, "48213 (claude) S 4100 48213 4100 0 -1");
    const ProcParent::Caller caller = ProcParent::callerOf(900, root.path());
    QCOMPARE(caller.wordPid, 900u);
    QCOMPARE(caller.word, QStringLiteral("omaseed"));
    QCOMPARE(caller.parentPid, 48213u);
    QCOMPARE(caller.parent, QStringLiteral("claude"));
}

void TestProcParent::missingPidSaysNothing()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(!ProcParent::read(0, root.path()).has_value());
    QVERIFY(!ProcParent::read(77, root.path()).has_value());
    ProcParent::Caller caller = ProcParent::callerOf(77, root.path());
    QCOMPARE(caller.wordPid, 77u);
    QVERIFY(caller.word.isEmpty());
    QCOMPARE(caller.parentPid, 0u);

    // The word is there, its parent is gone: the word alone.
    writeStat(root.path(), 900, "900 (omaseed) S 31337 900 4100 0 -1");
    caller = ProcParent::callerOf(900, root.path());
    QCOMPARE(caller.word, QStringLiteral("omaseed"));
    QCOMPARE(caller.parentPid, 0u);
    QVERIFY(caller.parent.isEmpty());

    // A stat file whose pid isn't the folder's is not trusted.
    writeStat(root.path(), 901, "902 (liar) S 1 900 4100 0 -1");
    QVERIFY(!ProcParent::read(901, root.path()).has_value());
}

QTEST_GUILESS_MAIN(TestProcParent)
#include "tst_procparent.moc"
