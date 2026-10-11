// "Start at login": the XDG autostart entry is the setting. Every path is under a temporary
// directory with XDG_CONFIG_HOME pointing into it; the real ~/.config/autostart is never touched.

#include "integration/Autostart.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace Autostart = omaseed::integration::Autostart;

class TstAutostart : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        qputenv("XDG_CONFIG_HOME", m_dir->path().toUtf8());
    }

    void cleanup()
    {
        qunsetenv("XDG_CONFIG_HOME");
        m_dir.reset();
    }

    void entryLivesUnderXdgConfigHome()
    {
        QCOMPARE(Autostart::entryPath(),
                 m_dir->path() +
                     QStringLiteral("/autostart/io.github.diogochaves.omaseed.desktop"));
    }

    void offWithoutAnEntry() { QCOMPARE(Autostart::mode(), QStringLiteral("off")); }

    void trayWritesHidden()
    {
        QVERIFY(Autostart::setMode(QStringLiteral("tray"), QStringLiteral("omaseed")));
        QCOMPARE(Autostart::mode(), QStringLiteral("tray"));
        QFile file(Autostart::entryPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray contents = file.readAll();
        QVERIFY(contents.contains("Exec=omaseed --hidden\n"));
        QVERIFY(!contents.contains("DBusActivatable"));
    }

    void windowWritesNoFlagAndOffRemoves()
    {
        QVERIFY(Autostart::setMode(QStringLiteral("window"), QStringLiteral("omaseed")));
        QCOMPARE(Autostart::mode(), QStringLiteral("window"));
        QVERIFY(Autostart::setMode(QStringLiteral("off"), QStringLiteral("omaseed")));
        QVERIFY(!QFile::exists(Autostart::entryPath()));
        QCOMPARE(Autostart::mode(), QStringLiteral("off"));
        // Removing what is not there is not a failure.
        QVERIFY(Autostart::setMode(QStringLiteral("off"), QStringLiteral("omaseed")));
    }

    void hiddenTrueReadsAsOff()
    {
        QDir().mkpath(Autostart::directory());
        QFile file(Autostart::entryPath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\nExec=omaseed --hidden\nHidden=true\n");
        file.close();
        QCOMPARE(Autostart::mode(), QStringLiteral("off"));
    }

    void installedBinaryRunsByName()
    {
        QCOMPARE(Autostart::execProgram(QStringLiteral("/usr/bin/omaseed")),
                 QStringLiteral("omaseed"));
    }

    void devBuildRunsByPathQuotedWhenNeeded()
    {
        QCOMPARE(Autostart::execProgram(QStringLiteral("/home/u/code/omaseed/build/omaseed")),
                 QStringLiteral("/home/u/code/omaseed/build/omaseed"));
        QCOMPARE(Autostart::execProgram(QStringLiteral("/home/u/my code/omaseed")),
                 QStringLiteral("\"/home/u/my code/omaseed\""));
        // A path the entry would otherwise break on round-trips through mode() as "tray".
        const QString program = Autostart::execProgram(QStringLiteral("/home/u/a b/omaseed"));
        QVERIFY(Autostart::setMode(QStringLiteral("tray"), program));
        QCOMPARE(Autostart::mode(), QStringLiteral("tray"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_GUILESS_MAIN(TstAutostart)
#include "tst_autostart.moc"
