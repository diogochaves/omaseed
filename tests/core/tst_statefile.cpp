#include "core/StateFile.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::StateFile;

class TestStateFile : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void readWithoutFileGivesVersionOnly();
    void writeSectionCreatesTheFile();
    void writeSectionKeepsOtherSections();
    void readSectionOfUnknownKeyIsEmpty();
    void writeLeavesOnlyTheStateFile();

private:
    QTemporaryDir m_dir;
    QByteArray m_previousConfigHome;
};

void TestStateFile::init()
{
    QVERIFY(m_dir.isValid());
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_dir.path().toUtf8());
}

void TestStateFile::cleanup()
{
    if (m_previousConfigHome.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
}

void TestStateFile::readWithoutFileGivesVersionOnly()
{
    const QJsonObject state = StateFile::read();
    QCOMPARE(state.value(QStringLiteral("version")).toInt(), 1);
}

void TestStateFile::writeSectionCreatesTheFile()
{
    StateFile::writeSection(QStringLiteral("notifications"),
                            QJsonObject{{QStringLiteral("completed"), false}});
    QVERIFY(QFile::exists(StateFile::path()));

    const QJsonObject section = StateFile::readSection(QStringLiteral("notifications"));
    QCOMPARE(section.value(QStringLiteral("completed")).toBool(true), false);
}

void TestStateFile::writeSectionKeepsOtherSections()
{
    StateFile::writeSection(QStringLiteral("magnetHandlerAsked"), QJsonObject{});
    QJsonObject state = StateFile::read();
    state.insert(QStringLiteral("magnetHandlerAsked"), true);
    StateFile::write(state);

    StateFile::writeSection(QStringLiteral("notifications"),
                            QJsonObject{{QStringLiteral("diskFull"), false}});

    const QJsonObject after = StateFile::read();
    QCOMPARE(after.value(QStringLiteral("magnetHandlerAsked")).toBool(false), true);
    QCOMPARE(after.value(QStringLiteral("notifications"))
                 .toObject()
                 .value(QStringLiteral("diskFull"))
                 .toBool(true),
             false);
}

void TestStateFile::readSectionOfUnknownKeyIsEmpty()
{
    QVERIFY(StateFile::readSection(QStringLiteral("nope")).isEmpty());
}

// The write goes through a temporary file renamed into place (QSaveFile); nothing of it may
// be left next to state.json afterwards.
void TestStateFile::writeLeavesOnlyTheStateFile()
{
    StateFile::writeSection(QStringLiteral("a"), QJsonObject{{QStringLiteral("x"), 1}});
    StateFile::writeSection(QStringLiteral("b"), QJsonObject{{QStringLiteral("y"), 2}});
    const QDir dir = QFileInfo(StateFile::path()).absoluteDir();
    QCOMPARE(dir.entryList(QDir::Files | QDir::Hidden),
             QStringList({QStringLiteral("state.json")}));
    QCOMPARE(StateFile::readSection(QStringLiteral("a")).value(QStringLiteral("x")).toInt(), 1);
}

QTEST_GUILESS_MAIN(TestStateFile)

#include "tst_statefile.moc"
