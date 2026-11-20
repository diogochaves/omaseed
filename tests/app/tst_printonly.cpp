// #143: the flags that only print (--help, --version, --theme-roles) work with no display:
// over SSH, in a container, in a package check. The real binary, run with no QT_QPA_PLATFORM,
// no WAYLAND_DISPLAY and no DISPLAY, and a runtime directory of its own, so no Wayland socket
// can be found by its default name either. Before #143 each of these ended in SIGABRT.

#include <QDir>
#include <QFile>
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
    void skillPrintsWithNoDisplay();
    void openRefusesWithNoDisplay_data();
    void openRefusesWithNoDisplay();
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
    // DECISIONS C45 V2: --help is the help page, the words above Qt's options; --help-all
    // stays Qt's own, whose usage line names the binary.
    QVERIFY(m_out.contains(QLatin1String(" [options] [magnet: links or .torrent files...]\n")));
    if (flag != QLatin1String("--help-all")) {
        QVERIFY2(m_out.startsWith(
                     QLatin1String("Usage: omaseed [options] [magnet: links or .torrent files...]\n"
                                   "       omaseed <word> [arguments] [--json]\n")),
                 qPrintable(m_out));
        QVERIFY(m_out.contains(QLatin1String("Your library, from the running omaseed:")));
        QVERIFY(m_out.indexOf(QLatin1String("\n  errors ")) <
                m_out.indexOf(QLatin1String("Options:")));
    }
    QVERIFY(m_out.contains(QLatin1String("--theme-roles")));
    QVERIFY(m_out.contains(QLatin1String("--quit")));
    QVERIFY(m_out.contains(QLatin1String("--skill")));
    QVERIFY(m_out.contains(QLatin1String("--open <page>")));
}

// DECISIONS C36 M5 (#215): a wrong `--open` is refused before anything starts, display or not.
void TestPrintOnly::openRefusesWithNoDisplay_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("says");
    QTest::newRow("unknown page") << QStringList{QStringLiteral("--open"), QStringLiteral("quit")}
                                  << QStringLiteral(
                                         "--open takes add, search, settings, not \"quit\"");
    QTest::newRow("with a link") << QStringList{QStringLiteral("--open"),
                                                QStringLiteral("add"),
                                                QStringLiteral("magnet:?xt=urn:btih:abc")}
                                 << QStringLiteral("--open takes no links or files");
    QTest::newRow("with a demo") << QStringList{QStringLiteral("--open=search"),
                                                QStringLiteral("--demo"),
                                                QStringLiteral("table")}
                                 << QStringLiteral("--open doesn't go with --demo");
}

void TestPrintOnly::openRefusesWithNoDisplay()
{
    QFETCH(QStringList, arguments);
    QFETCH(QString, says);
    QVERIFY(run(arguments));
    QVERIFY2(m_exitStatus == QProcess::NormalExit, qPrintable(m_err));
    QCOMPARE(m_exitCode, 2);
    QVERIFY2(m_err.contains(says), qPrintable(m_err));
}

void TestPrintOnly::version()
{
    QVERIFY(run({QStringLiteral("--version")}));
    QVERIFY2(m_exitStatus == QProcess::NormalExit, qPrintable(m_err));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY2(m_out.startsWith(QLatin1String("omaseed ")), qPrintable(m_out + m_err));
}

// DECISIONS C45 V19: --skill prints the guide with no display and no bus, writing nothing.
void TestPrintOnly::skillPrintsWithNoDisplay()
{
    QVERIFY(run({QStringLiteral("--skill")}));
    QVERIFY2(m_exitStatus == QProcess::NormalExit, qPrintable(m_err));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY2(m_out.startsWith(QLatin1String("## Your library\n")), qPrintable(m_out + m_err));
    QVERIFY(m_out.contains(QLatin1String("  omaseed errors --json ")));
    QVERIFY(m_out.contains(QLatin1String("fields are only added within it.\n\n## Your skins\n")));
    QVERIFY(m_out.contains(QLatin1String("  omaseed skin list --json ")));
    QVERIFY(m_out.endsWith(QLatin1String("never read that folder.\n")));
    // Printing is not a start: no AGENTS.md or schema appears.
    QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("XDG_CONFIG_HOME/omaseed/AGENTS.md"))));
    QVERIFY(
        !QFile::exists(m_dir.filePath(QStringLiteral("XDG_CONFIG_HOME/omaseed/skin.schema.json"))));
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
    // A name a user skin could have, with no folder: Omarchy's, missing (C37 U1).
    QTest::newRow("no such skin") << "neon" << "omarchy" << "missing";
    QTest::newRow("no skin's name") << "neon skin" << "omarchy" << "unknown skin";
    QTest::newRow("a user skin") << "warm" << "warm" << "";
}

/// #171: `--theme-roles --skin NAME` resolves the grounds in that skin, for the contrast check
/// over every skin; a name that is no skin is Omarchy's, and says why.
void TestPrintOnly::themeRolesInASkin()
{
    QFETCH(QString, skin);
    QFETCH(QString, drawn);
    QFETCH(QString, reason);
    // The person's skins folder, under the run's own XDG_CONFIG_HOME.
    const QString folder = m_dir.filePath(QStringLiteral("XDG_CONFIG_HOME/omaseed/skins/warm"));
    QVERIFY(QDir().mkpath(folder));
    QFile file(folder + QStringLiteral("/skin.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"version": 1, "grounds": {"panel": {"tint": "accent", "alpha": 0.08}}})");
    file.close();
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
