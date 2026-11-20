// DECISIONS C37 U5 and #226's line: `omaseed check <skin>`, the real binary. Its homes are
// its own (a fixture current theme, a fixture light theme installed, the skins invented:
// warm, warm.v2), with no display and no bus. The picture is the only file a check writes,
// and it shows the skin as it was saved last (herdr's lessons: a check with side effects, a
// shot of a stale frame).

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

class TestCheck : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void aGoodSkinIs0();
    void aDroppedTokenIsNamedAnd0();
    void aBrokenFileIs1WithWhy();
    void anUnknownSkinIs2();
    void anUnknownThemeIs2();
    void aLightTheme();
    void writesNothingButThePicture();
    void reachesNoBus();
    void twoSkinsTwoPictures();
    void savedTwiceShowsTheSecond();
    void aCloneOfOmarchyDrawsAsOmarchy();
    void aCloneOfConsoleDrawsAsConsole();
    void aCloneOfOmarchyLiftsNothing();
    void omaboxLineOnlyWithoutOmabox();
    void oddNameWithDots();

private:
    QString home(const QString &path = QString()) const;
    QProcessEnvironment environment() const;
    /// Runs omaseed with `arguments`; false when it didn't finish in time.
    bool run(const QStringList &arguments);
    QJsonObject json() const;
    void writeSkin(const QString &name, const QByteArray &bytes);
    /// `skin clone <from> <name>`, as a person starts one.
    void clone(const QString &from, const QString &name);
    QString picture(const QString &name) const;
    /// Every file under the homes: its size and modification time.
    QHash<QString, QString> files() const;

    std::unique_ptr<QTemporaryDir> m_home;
    QString m_path; ///< PATH for the next run; "" keeps the system's
    int m_exitCode = -1;
    QString m_out;
    QString m_err;
};

QString TestCheck::home(const QString &path) const
{
    return path.isEmpty() ? m_home->path() : m_home->path() + QLatin1Char('/') + path;
}

void TestCheck::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    m_path.clear();
    const QDir root(home());
    for (const char *dir : {"config", "data", "state", "cache", "runtime"})
        QVERIFY(root.mkpath(QString::fromLatin1(dir)));
    // The person's current theme, dark, and one light theme installed beside it.
    const QString fixtures = QStringLiteral(OMASEED_THEME_FIXTURES);
    QVERIFY(root.mkpath(QStringLiteral("state/omarchy/current/theme")));
    QVERIFY(QFile::copy(fixtures + QStringLiteral("/tokyo-night/colors.toml"),
                        home(QStringLiteral("state/omarchy/current/theme/colors.toml"))));
    QFile name(home(QStringLiteral("state/omarchy/current/theme.name")));
    QVERIFY(name.open(QIODevice::WriteOnly));
    name.write("tokyo-night\n");
    name.close();
    QVERIFY(root.mkpath(QStringLiteral("config/omarchy/themes/fixture-light")));
    QVERIFY(QFile::copy(fixtures + QStringLiteral("/catppuccin-latte/colors.toml"),
                        home(QStringLiteral("config/omarchy/themes/fixture-light/colors.toml"))));
}

void TestCheck::cleanup()
{
    m_home.reset();
}

QProcessEnvironment TestCheck::environment() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("HOME"), home());
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), home(QStringLiteral("config")));
    env.insert(QStringLiteral("XDG_DATA_HOME"), home(QStringLiteral("data")));
    env.insert(QStringLiteral("XDG_STATE_HOME"), home(QStringLiteral("state")));
    env.insert(QStringLiteral("XDG_CACHE_HOME"), home(QStringLiteral("cache")));
    env.insert(QStringLiteral("XDG_RUNTIME_DIR"), home(QStringLiteral("runtime")));
    env.insert(QStringLiteral("OMARCHY_PATH"), home(QStringLiteral("no-omarchy")));
    for (const char *name : {"QT_QPA_PLATFORM",
                             "WAYLAND_DISPLAY",
                             "WAYLAND_SOCKET",
                             "DISPLAY",
                             "DBUS_SESSION_BUS_ADDRESS",
                             "HYPRLAND_INSTANCE_SIGNATURE",
                             "OMASEED_SKIN",
                             "OMASEED_DISPLAY",
                             "OMASEED_TEST_PLATFORM",
                             "QT_LOGGING_RULES"})
        env.remove(QString::fromLatin1(name));
    if (!m_path.isEmpty())
        env.insert(QStringLiteral("PATH"), m_path);
    return env;
}

bool TestCheck::run(const QStringList &arguments)
{
    QProcess process;
    process.setProcessEnvironment(environment());
    process.setWorkingDirectory(home());
    process.start(QStringLiteral(OMASEED_BINARY), arguments);
    if (!process.waitForStarted(5000))
        return false;
    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning && timer.elapsed() < 60000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        process.waitForFinished(20);
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(2000);
        return false;
    }
    m_exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    m_out = QString::fromUtf8(process.readAllStandardOutput());
    m_err = QString::fromUtf8(process.readAllStandardError());
    return true;
}

QJsonObject TestCheck::json() const
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(m_out.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        qWarning().noquote() << "not one JSON object:" << m_out << m_err;
    return document.object();
}

void TestCheck::writeSkin(const QString &name, const QByteArray &bytes)
{
    const QString dir = home(QStringLiteral("config/omaseed/skins/") + name);
    QVERIFY(QDir().mkpath(dir));
    QFile file(dir + QStringLiteral("/skin.json"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
}

void TestCheck::clone(const QString &from, const QString &name)
{
    QVERIFY(run({QStringLiteral("skin"), QStringLiteral("clone"), from, name}));
    QCOMPARE(m_exitCode, 0);
}

QString TestCheck::picture(const QString &name) const
{
    return home(QStringLiteral("cache/omaseed/check/") + name + QStringLiteral(".png"));
}

QHash<QString, QString> TestCheck::files() const
{
    QHash<QString, QString> out;
    QDirIterator it(
        home(), QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        out.insert(
            info.filePath(),
            QStringLiteral("%1 %2").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch()));
    }
    return out;
}

void TestCheck::aGoodSkinIs0()
{
    writeSkin(QStringLiteral("warm"), R"({"version": 1, "description": "Warmer."})");
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm"), QStringLiteral("--json")}));
    QCOMPARE(m_exitCode, 0);
    const QJsonObject answer = json();
    QCOMPARE(answer.value(QLatin1String("command")).toString(), QStringLiteral("check"));
    QVERIFY(answer.contains(QLatin1String("omaseed")));
    QCOMPARE(answer.value(QLatin1String("skin")).toString(), QStringLiteral("warm"));
    QCOMPARE(answer.value(QLatin1String("version")).toInt(), 1);
    QCOMPARE(answer.value(QLatin1String("drawn")).toBool(), true);
    QVERIFY(!answer.contains(QLatin1String("why")));
    QCOMPARE(answer.value(QLatin1String("dropped")).toArray().size(), 0);
    QCOMPARE(answer.value(QLatin1String("theme")).toString(), QStringLiteral("tokyo-night"));
    QCOMPARE(answer.value(QLatin1String("mode")).toString(), QStringLiteral("dark"));
    QCOMPARE(answer.value(QLatin1String("baseSize")).toInt(), 12);
    QCOMPARE(answer.value(QLatin1String("picture")).toString(), picture(QStringLiteral("warm")));
    QVERIFY(answer.value(QLatin1String("missingFonts")).isArray());
    QVERIFY(answer.value(QLatin1String("lifted")).isArray());
    QVERIFY(answer.value(QLatin1String("omabox")).isBool());
    const QImage image(picture(QStringLiteral("warm")));
    QVERIFY(!image.isNull());
    QVERIFY(image.width() >= 1440 && image.height() >= 960);

    // The lines: the head, the picture.
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    QVERIFY2(m_out.startsWith(QStringLiteral("warm · drawn · tokyo-night (dark) · base size 12\n")),
             qPrintable(m_out));
    QVERIFY2(m_out.contains(QStringLiteral("  picture ~/cache/omaseed/check/warm.png\n")),
             qPrintable(m_out));
}

void TestCheck::aDroppedTokenIsNamedAnd0()
{
    writeSkin(QStringLiteral("warm"),
              R"({"version": 1, "grounds": {"hover": {"tint": "foreground", "alpha": 1.4}}})");
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm"), QStringLiteral("--json")}));
    QCOMPARE(m_exitCode, 0);
    const QJsonObject answer = json();
    QCOMPARE(answer.value(QLatin1String("drawn")).toBool(), true);
    const QJsonArray dropped = answer.value(QLatin1String("dropped")).toArray();
    QCOMPARE(dropped.size(), 1);
    const QJsonObject drop = dropped.first().toObject();
    QCOMPARE(drop.value(QLatin1String("token")).toString(), QStringLiteral("grounds.hover"));
    QVERIFY(drop.value(QLatin1String("value")).isObject());
    QVERIFY2(drop.value(QLatin1String("why")).toString().contains(QLatin1String("alpha")),
             qPrintable(drop.value(QLatin1String("why")).toString()));

    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    QVERIFY2(m_out.contains(QStringLiteral("  dropped grounds.hover {")), qPrintable(m_out));
}

void TestCheck::aBrokenFileIs1WithWhy()
{
    writeSkin(QStringLiteral("warm"), "{\"version\": 1,");
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm"), QStringLiteral("--json")}));
    QCOMPARE(m_exitCode, 1);
    const QJsonObject answer = json();
    QCOMPARE(answer.value(QLatin1String("drawn")).toBool(), false);
    QCOMPARE(answer.value(QLatin1String("why")).toString(),
             QStringLiteral("its skin.json doesn't parse"));
    // Drawn whole as Omarchy's, and its picture says so.
    QVERIFY(QFileInfo::exists(picture(QStringLiteral("warm"))));

    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 1);
    QVERIFY2(m_out.startsWith(
                 QStringLiteral("warm · drawn as Omarchy's: its skin.json doesn't parse · ")),
             qPrintable(m_out));
}

void TestCheck::anUnknownSkinIs2()
{
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm"), QStringLiteral("--json")}));
    QCOMPARE(m_exitCode, 2);
    QCOMPARE(
        json().value(QLatin1String("error")).toObject().value(QLatin1String("code")).toString(),
        QStringLiteral("unknown-skin"));
    QVERIFY(!QFileInfo::exists(home(QStringLiteral("cache/omaseed"))));

    QVERIFY(run({QStringLiteral("check"), QStringLiteral("../warm")}));
    QCOMPARE(m_exitCode, 2);
    QVERIFY(m_out.isEmpty());
    QVERIFY(m_err.contains(QStringLiteral("can't be a skin's name")));
}

void TestCheck::anUnknownThemeIs2()
{
    QVERIFY(run({QStringLiteral("check"),
                 QStringLiteral("console"),
                 QStringLiteral("--theme"),
                 QStringLiteral("no-such-theme"),
                 QStringLiteral("--json")}));
    QCOMPARE(m_exitCode, 2);
    const QJsonObject failure = json().value(QLatin1String("error")).toObject();
    QCOMPARE(failure.value(QLatin1String("code")).toString(), QStringLiteral("no-theme"));
    QVERIFY(failure.value(QLatin1String("message"))
                .toString()
                .startsWith(QStringLiteral("No theme called no-such-theme.")));
    QVERIFY(!QFileInfo::exists(home(QStringLiteral("cache/omaseed"))));
}

void TestCheck::aLightTheme()
{
    QVERIFY(run({QStringLiteral("check"),
                 QStringLiteral("console"),
                 QStringLiteral("--theme"),
                 QStringLiteral("fixture-light"),
                 QStringLiteral("--json")}));
    QCOMPARE(m_exitCode, 0);
    const QJsonObject answer = json();
    QCOMPARE(answer.value(QLatin1String("theme")).toString(), QStringLiteral("fixture-light"));
    QCOMPARE(answer.value(QLatin1String("mode")).toString(), QStringLiteral("light"));
    QCOMPARE(answer.value(QLatin1String("picture")).toString(),
             picture(QStringLiteral("console@fixture-light")));
    QVERIFY(QFileInfo::exists(picture(QStringLiteral("console@fixture-light"))));
    QVERIFY(!QFileInfo::exists(picture(QStringLiteral("console"))));
}

void TestCheck::writesNothingButThePicture()
{
    // A state.json with a base size, read and never written; a skin and the guide's place.
    QVERIFY(QDir().mkpath(home(QStringLiteral("config/omaseed"))));
    QFile state(home(QStringLiteral("config/omaseed/state.json")));
    QVERIFY(state.open(QIODevice::WriteOnly));
    state.write(R"({"version": 1, "appearance": {"baseSizeOverride": 14}})");
    state.close();
    writeSkin(QStringLiteral("warm"), R"({"version": 1})");
    const QHash<QString, QString> before = files();
    QTest::qWait(20); // a file written again would show a later mtime

    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm"), QStringLiteral("--json")}));
    QCOMPARE(m_exitCode, 0);
    // The person's base size is read and applied.
    QCOMPARE(json().value(QLatin1String("baseSize")).toInt(), 14);
    QHash<QString, QString> after = files();
    QVERIFY2(after.remove(picture(QStringLiteral("warm"))) == 1, "the picture is written");
    QCOMPARE(after, before);
}

void TestCheck::reachesNoBus()
{
    // A session bus where libdbus looks without an address, and one named by the address:
    // check reaches neither (C37 U5: no bus, by construction).
    QLocalServer fallback;
    QVERIFY(fallback.listen(home(QStringLiteral("runtime/bus"))));
    QLocalServer named;
    QVERIFY(named.listen(home(QStringLiteral("named-bus"))));
    QProcessEnvironment env = environment();
    env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"),
               QStringLiteral("unix:path=") + home(QStringLiteral("named-bus")));
    QProcess process;
    process.setProcessEnvironment(env);
    process.start(QStringLiteral(OMASEED_BINARY),
                  {QStringLiteral("check"), QStringLiteral("console")});
    QVERIFY(process.waitForFinished(60000));
    QCOMPARE(process.exitCode(), 0);
    QCoreApplication::processEvents();
    QVERIFY(!fallback.hasPendingConnections());
    QVERIFY(!named.hasPendingConnections());
}

void TestCheck::twoSkinsTwoPictures()
{
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("omarchy")}));
    QCOMPARE(m_exitCode, 0);
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("console")}));
    QCOMPARE(m_exitCode, 0);
    const QImage omarchy(picture(QStringLiteral("omarchy")));
    const QImage console(picture(QStringLiteral("console")));
    QVERIFY(!omarchy.isNull() && !console.isNull());
    QVERIFY(omarchy != console);
}

void TestCheck::savedTwiceShowsTheSecond()
{
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("omarchy")}));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("console")}));
    const QImage omarchy(picture(QStringLiteral("omarchy")));
    const QImage console(picture(QStringLiteral("console")));

    // warm saved as a copy of Omarchy's, checked; then saved again as a copy of Console's.
    clone(QStringLiteral("omarchy"), QStringLiteral("warm"));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    QCOMPARE(QImage(picture(QStringLiteral("warm"))), omarchy);

    clone(QStringLiteral("console"), QStringLiteral("console-copy"));
    QFile copy(home(QStringLiteral("config/omaseed/skins/console-copy/skin.json")));
    QVERIFY(copy.open(QIODevice::ReadOnly));
    writeSkin(QStringLiteral("warm"), copy.readAll());
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    QCOMPARE(QImage(picture(QStringLiteral("warm"))), console);
}

void TestCheck::aCloneOfOmarchyDrawsAsOmarchy()
{
    clone(QStringLiteral("omarchy"), QStringLiteral("warm"));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("omarchy")}));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    QCOMPARE(QImage(picture(QStringLiteral("warm"))), QImage(picture(QStringLiteral("omarchy"))));
}

void TestCheck::aCloneOfConsoleDrawsAsConsole()
{
    clone(QStringLiteral("console"), QStringLiteral("warm"));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("console")}));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    QCOMPARE(QImage(picture(QStringLiteral("warm"))), QImage(picture(QStringLiteral("console"))));
}

void TestCheck::aCloneOfOmarchyLiftsNothing()
{
    clone(QStringLiteral("omarchy"), QStringLiteral("warm"));
    for (const QString &theme : {QString(), QStringLiteral("fixture-light")}) {
        QStringList arguments{
            QStringLiteral("check"), QStringLiteral("warm"), QStringLiteral("--json")};
        if (!theme.isEmpty())
            arguments << QStringLiteral("--theme") << theme;
        QVERIFY(run(arguments));
        QCOMPARE(m_exitCode, 0);
        QCOMPARE(json().value(QLatin1String("lifted")).toArray(), QJsonArray());
    }
}

void TestCheck::omaboxLineOnlyWithoutOmabox()
{
    const QString line = QStringLiteral(
        "omabox would check this skin in every theme, in a desktop nobody sees: omabox.app\n");

    // No omabox on PATH.
    QVERIFY(QDir().mkpath(home(QStringLiteral("bin"))));
    m_path = home(QStringLiteral("bin"));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("console")}));
    QCOMPARE(m_exitCode, 0);
    QVERIFY2(m_out.endsWith(line), qPrintable(m_out));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("console"), QStringLiteral("--json")}));
    QCOMPARE(json().value(QLatin1String("omabox")).toBool(), false);

    // A stand-in omabox on it: nothing more said.
    QFile stub(home(QStringLiteral("bin/omabox")));
    QVERIFY(stub.open(QIODevice::WriteOnly));
    stub.write("#!/bin/sh\nexit 0\n");
    stub.close();
    QVERIFY(stub.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                QFileDevice::ExeOwner));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("console")}));
    QCOMPARE(m_exitCode, 0);
    QVERIFY2(!m_out.contains(QStringLiteral("omabox")), qPrintable(m_out));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("console"), QStringLiteral("--json")}));
    QCOMPARE(json().value(QLatin1String("omabox")).toBool(), true);
}

void TestCheck::oddNameWithDots()
{
    writeSkin(QStringLiteral("warm.v2"), R"({"version": 1})");
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm.v2"), QStringLiteral("--json")}));
    QCOMPARE(m_exitCode, 0);
    QCOMPARE(json().value(QLatin1String("skin")).toString(), QStringLiteral("warm.v2"));
    QVERIFY(QFileInfo::exists(picture(QStringLiteral("warm.v2"))));
}

QTEST_GUILESS_MAIN(TestCheck)

#include "tst_check.moc"
