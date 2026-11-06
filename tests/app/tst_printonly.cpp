// #143: the flags that only print (--help, --version, --theme-roles) work with no display:
// over SSH, in a container, in a package check. The real binary, run with no QT_QPA_PLATFORM,
// no WAYLAND_DISPLAY and no DISPLAY, and a runtime directory of its own, so no Wayland socket
// can be found by its default name either. Before #143 each of these ended in SIGABRT.

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

class TestPrintOnly : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void help_data();
    void help();
    void version();
    void themeRoles();
    void themeRolesInASkin_data();
    void themeRolesInASkin();

private:
    /// Runs omaseed with `arguments` and no display; false when it didn't finish in time.
    bool run(const QStringList &arguments);

    QTemporaryDir m_dir;
    int m_exitCode = -1;
    QProcess::ExitStatus m_exitStatus = QProcess::CrashExit;
    QString m_out;
    QString m_err;
};

void TestPrintOnly::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

bool TestPrintOnly::run(const QStringList &arguments)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char *name : {"QT_QPA_PLATFORM",
                             "WAYLAND_DISPLAY",
                             "WAYLAND_SOCKET",
                             "DISPLAY",
                             "DBUS_SESSION_BUS_ADDRESS",
                             "OMASEED_TEST_PLATFORM"})
        env.remove(QString::fromLatin1(name));
    // Every home of its own: the theme read is the bundled one, and nothing is written outside.
    for (const char *name : {"XDG_RUNTIME_DIR",
                             "XDG_CONFIG_HOME",
                             "XDG_STATE_HOME",
                             "XDG_DATA_HOME",
                             "XDG_CACHE_HOME"})
        env.insert(QString::fromLatin1(name), m_dir.filePath(QString::fromLatin1(name)));
    env.insert(QStringLiteral("HOME"), m_dir.path());

    QProcess process;
    process.setProcessEnvironment(env);
    process.start(QStringLiteral(OMASEED_BINARY), arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(2000);
        return false;
    }
    m_exitCode = process.exitCode();
    m_exitStatus = process.exitStatus();
    m_out = QString::fromUtf8(process.readAllStandardOutput());
    m_err = QString::fromUtf8(process.readAllStandardError());
    return true;
}

void TestPrintOnly::help_data()
{
    QTest::addColumn<QString>("flag");
    QTest::newRow("--help") << QStringLiteral("--help");
    QTest::newRow("-h") << QStringLiteral("-h");
    QTest::newRow("--help-all") << QStringLiteral("--help-all");
}

void TestPrintOnly::help()
{
    QFETCH(QString, flag);
    QVERIFY(run({flag}));
    QVERIFY2(m_exitStatus == QProcess::NormalExit, qPrintable(m_err));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY2(m_out.contains(QLatin1String("Usage:")), qPrintable(m_out + m_err));
    QVERIFY(m_out.contains(QLatin1String("--theme-roles")));
    QVERIFY(m_out.contains(QLatin1String("--quit")));
}

void TestPrintOnly::version()
{
    QVERIFY(run({QStringLiteral("--version")}));
    QVERIFY2(m_exitStatus == QProcess::NormalExit, qPrintable(m_err));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY2(m_out.startsWith(QLatin1String("omaseed ")), qPrintable(m_out + m_err));
}

void TestPrintOnly::themeRoles()
{
    QVERIFY(run({QStringLiteral("--theme-roles")}));
    QVERIFY2(m_exitStatus == QProcess::NormalExit, qPrintable(m_err));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(m_out.toUtf8(), &error);
    QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(m_out + m_err));
    QVERIFY(
        json.object().value(QLatin1String("colors")).toObject().contains(QLatin1String("accent")));
}

void TestPrintOnly::themeRolesInASkin_data()
{
    QTest::addColumn<QString>("skin");
    QTest::addColumn<QString>("drawn");
    QTest::addColumn<QString>("reason");
    QTest::newRow("omarchy") << "omarchy" << "omarchy" << "";
    QTest::newRow("console") << "console" << "console" << "";
    QTest::newRow("no such skin") << "neon" << "omarchy" << "unknown skin";
}

/// #171: `--theme-roles --skin NAME` resolves the grounds in that skin, for the contrast check
/// over every skin; a name that is no skin is Omarchy's, and says why.
void TestPrintOnly::themeRolesInASkin()
{
    QFETCH(QString, skin);
    QFETCH(QString, drawn);
    QFETCH(QString, reason);
    QVERIFY(run({QStringLiteral("--theme-roles"), QStringLiteral("--skin"), skin}));
    QVERIFY2(m_exitStatus == QProcess::NormalExit, qPrintable(m_err));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    const QJsonObject roles = QJsonDocument::fromJson(m_out.toUtf8()).object();
    QCOMPARE(roles.value(QLatin1String("skin")).toString(), drawn);
    QCOMPARE(roles.value(QLatin1String("skinReason")).toString(), reason);
    QVERIFY(roles.value(QLatin1String("grounds")).toObject().contains(QLatin1String("panel")));
}

QTEST_GUILESS_MAIN(TestPrintOnly)
#include "tst_printonly.moc"
