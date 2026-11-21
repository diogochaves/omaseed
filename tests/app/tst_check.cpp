// DECISIONS C37 U5 and #226's line: `omaseed check <skin>`, the real binary. Its homes are
// its own (a fixture current theme, dark; Omarchy's stock light theme and a fixture light
// theme installed; the skins invented: warm, warm.v2), with no display and no bus. By default
// a light and a dark picture (C37 U5, amended 2026-11-20), --theme one. The pictures are the
// only files a check writes, and they show the skin as it was saved last (herdr's lessons: a check
// with side effects, a shot of a stale frame).

#include <QColor>
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
#include <QRect>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

/// "" when the two pictures are the same; else what differs (sizes, or how many pixels,
/// where, and the first few), so a failure on CI says what it saw (#298).
QString imageDiff(const QImage &actual, const QImage &expected)
{
    if (actual.isNull() || expected.isNull())
        return QStringLiteral("a null picture (actual %1, expected %2)")
            .arg(actual.isNull())
            .arg(expected.isNull());
    if (actual.size() != expected.size())
        return QStringLiteral("sizes %1x%2 and %3x%4")
            .arg(actual.width())
            .arg(actual.height())
            .arg(expected.width())
            .arg(expected.height());
    const QImage a = actual.convertToFormat(QImage::Format_ARGB32);
    const QImage b = expected.convertToFormat(QImage::Format_ARGB32);
    int count = 0;
    QRect box;
    QStringList first;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const QRgb pa = a.pixel(x, y);
            const QRgb pb = b.pixel(x, y);
            if (pa == pb)
                continue;
            ++count;
            box |= QRect(x, y, 1, 1);
            if (first.size() < 6)
                first << QStringLiteral("(%1,%2) %3≠%4")
                             .arg(x)
                             .arg(y)
                             .arg(pa, 8, 16, QLatin1Char('0'))
                             .arg(pb, 8, 16, QLatin1Char('0'));
        }
    }
    if (count == 0)
        return QString();
    return QStringLiteral("%1 pixels differ in %2,%3 %4x%5 of %6x%7: %8")
        .arg(count)
        .arg(box.x())
        .arg(box.y())
        .arg(box.width())
        .arg(box.height())
        .arg(a.width())
        .arg(a.height())
        .arg(first.join(QStringLiteral(", ")));
}

} // namespace

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
    void aLightPersonsThemeAndTokyoNight();
    void noLightThemeInstalled();

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
    /// The answer's pictures, by mode ("light", "dark") or theme for --theme.
    QJsonArray pictures() const;
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
    // The person's current theme, dark; Omarchy's stock light theme; and one light theme of
    // the person's installed beside them.
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
    QVERIFY(root.mkpath(QStringLiteral("omarchy/themes/catppuccin-latte")));
    QVERIFY(QFile::copy(fixtures + QStringLiteral("/catppuccin-latte/colors.toml"),
                        home(QStringLiteral("omarchy/themes/catppuccin-latte/colors.toml"))));
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
    env.insert(QStringLiteral("OMARCHY_PATH"), home(QStringLiteral("omarchy")));
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

QJsonArray TestCheck::pictures() const
{
    return json().value(QLatin1String("pictures")).toArray();
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
    QCOMPARE(answer.value(QLatin1String("baseSize")).toInt(), 12);
    QVERIFY(answer.value(QLatin1String("missingFonts")).isArray());
    QVERIFY(answer.value(QLatin1String("omabox")).isBool());
    QVERIFY(!answer.contains(QLatin1String("unchecked")));
    for (const char *gone : {"picture", "theme", "mode", "lifted"})
        QVERIFY2(!answer.contains(QLatin1String(gone)), gone);

    // A light and a dark picture: Omarchy's stock light theme, the person's dark one.
    const QJsonArray drawn = answer.value(QLatin1String("pictures")).toArray();
    QCOMPARE(drawn.size(), 2);
    const QJsonObject light = drawn.at(0).toObject();
    const QJsonObject dark = drawn.at(1).toObject();
    QCOMPARE(light.value(QLatin1String("mode")).toString(), QStringLiteral("light"));
    QCOMPARE(light.value(QLatin1String("theme")).toString(), QStringLiteral("catppuccin-latte"));
    QCOMPARE(light.value(QLatin1String("picture")).toString(),
             picture(QStringLiteral("warm.light")));
    QCOMPARE(dark.value(QLatin1String("mode")).toString(), QStringLiteral("dark"));
    QCOMPARE(dark.value(QLatin1String("theme")).toString(), QStringLiteral("tokyo-night"));
    QCOMPARE(dark.value(QLatin1String("picture")).toString(), picture(QStringLiteral("warm.dark")));
    QVERIFY(light.value(QLatin1String("lifted")).isArray());
    QVERIFY(dark.value(QLatin1String("lifted")).isArray());
    const QImage lightImage(picture(QStringLiteral("warm.light")));
    const QImage darkImage(picture(QStringLiteral("warm.dark")));
    QVERIFY(!lightImage.isNull() && !darkImage.isNull());
    QVERIFY(darkImage.width() >= 1440 && darkImage.height() >= 960);
    QCOMPARE(lightImage.size(), darkImage.size());
    QVERIFY(lightImage != darkImage);
    // The light picture is lighter: its top-left corner is the window's ground.
    QVERIFY(QColor(lightImage.pixel(4, 4)).lightness() > QColor(darkImage.pixel(4, 4)).lightness());

    // The lines: the head, one line per picture.
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    QVERIFY2(m_out.startsWith(QStringLiteral("warm · drawn · base size 12\n")), qPrintable(m_out));
    QVERIFY2(m_out.contains(QStringLiteral(
                 "  light · catppuccin-latte · ~/cache/omaseed/check/warm.light.png\n")),
             qPrintable(m_out));
    QVERIFY2(m_out.contains(
                 QStringLiteral("  dark · tokyo-night · ~/cache/omaseed/check/warm.dark.png\n")),
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
    // Drawn whole as Omarchy's, and its pictures say so.
    QVERIFY(QFileInfo::exists(picture(QStringLiteral("warm.light"))));
    QVERIFY(QFileInfo::exists(picture(QStringLiteral("warm.dark"))));

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
    // --theme NAME: that theme's picture alone.
    const QJsonArray drawn = pictures();
    QCOMPARE(drawn.size(), 1);
    const QJsonObject only = drawn.first().toObject();
    QCOMPARE(only.value(QLatin1String("theme")).toString(), QStringLiteral("fixture-light"));
    QCOMPARE(only.value(QLatin1String("mode")).toString(), QStringLiteral("light"));
    QCOMPARE(only.value(QLatin1String("picture")).toString(),
             picture(QStringLiteral("console@fixture-light")));
    QVERIFY(QFileInfo::exists(picture(QStringLiteral("console@fixture-light"))));
    QVERIFY(!QFileInfo::exists(picture(QStringLiteral("console.light"))));
    QVERIFY(!QFileInfo::exists(picture(QStringLiteral("console.dark"))));

    QVERIFY(run({QStringLiteral("check"),
                 QStringLiteral("console"),
                 QStringLiteral("--theme"),
                 QStringLiteral("fixture-light")}));
    QCOMPARE(m_exitCode, 0);
    QVERIFY2(m_out.contains(QStringLiteral(
                 "  light · fixture-light · ~/cache/omaseed/check/console@fixture-light.png\n")),
             qPrintable(m_out));
    QVERIFY2(!m_out.contains(QStringLiteral("  dark ·")), qPrintable(m_out));
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
    QVERIFY2(after.remove(picture(QStringLiteral("warm.light"))) == 1, "the light one is written");
    QVERIFY2(after.remove(picture(QStringLiteral("warm.dark"))) == 1, "the dark one is written");
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
    const QImage omarchy(picture(QStringLiteral("omarchy.dark")));
    const QImage console(picture(QStringLiteral("console.dark")));
    QVERIFY(!omarchy.isNull() && !console.isNull());
    QVERIFY(omarchy != console);
}

void TestCheck::savedTwiceShowsTheSecond()
{
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("omarchy")}));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("console")}));
    const QImage omarchy(picture(QStringLiteral("omarchy.dark")));
    const QImage console(picture(QStringLiteral("console.dark")));

    // warm saved as a copy of Omarchy's, checked; then saved again as a copy of Console's.
    clone(QStringLiteral("omarchy"), QStringLiteral("warm"));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    QVERIFY2(imageDiff(QImage(picture(QStringLiteral("warm.dark"))), omarchy).isEmpty(),
             qPrintable(imageDiff(QImage(picture(QStringLiteral("warm.dark"))), omarchy)));

    clone(QStringLiteral("console"), QStringLiteral("console-copy"));
    QFile copy(home(QStringLiteral("config/omaseed/skins/console-copy/skin.json")));
    QVERIFY(copy.open(QIODevice::ReadOnly));
    writeSkin(QStringLiteral("warm"), copy.readAll());
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    QVERIFY2(imageDiff(QImage(picture(QStringLiteral("warm.dark"))), console).isEmpty(),
             qPrintable(imageDiff(QImage(picture(QStringLiteral("warm.dark"))), console)));
}

void TestCheck::aCloneOfOmarchyDrawsAsOmarchy()
{
    clone(QStringLiteral("omarchy"), QStringLiteral("warm"));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("omarchy")}));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    for (const QString &mode : {QStringLiteral(".light"), QStringLiteral(".dark")}) {
        const QString diff = imageDiff(QImage(picture(QStringLiteral("warm") + mode)),
                                       QImage(picture(QStringLiteral("omarchy") + mode)));
        QVERIFY2(diff.isEmpty(), qPrintable(mode + QStringLiteral(": ") + diff));
    }
}

void TestCheck::aCloneOfConsoleDrawsAsConsole()
{
    clone(QStringLiteral("console"), QStringLiteral("warm"));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("console")}));
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    for (const QString &mode : {QStringLiteral(".light"), QStringLiteral(".dark")}) {
        const QString diff = imageDiff(QImage(picture(QStringLiteral("warm") + mode)),
                                       QImage(picture(QStringLiteral("console") + mode)));
        QVERIFY2(diff.isEmpty(), qPrintable(mode + QStringLiteral(": ") + diff));
    }
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
        const QJsonArray drawn = pictures();
        QCOMPARE(drawn.size(), theme.isEmpty() ? 2 : 1);
        for (const QJsonValue &one : drawn)
            QCOMPARE(one.toObject().value(QLatin1String("lifted")).toArray(), QJsonArray());
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
    QVERIFY(QFileInfo::exists(picture(QStringLiteral("warm.v2.dark"))));
}

void TestCheck::aLightPersonsThemeAndTokyoNight()
{
    // The person's theme is light: it stands for light, and dark is Tokyo Night, omaseed's
    // bundled one here, where Omarchy's themes have no tokyo-night.
    const QString fixtures = QStringLiteral(OMASEED_THEME_FIXTURES);
    const QString current = home(QStringLiteral("state/omarchy/current/theme/colors.toml"));
    QVERIFY(QFile::remove(current));
    QVERIFY(QFile::copy(fixtures + QStringLiteral("/flexoki-light/colors.toml"), current));
    QFile name(home(QStringLiteral("state/omarchy/current/theme.name")));
    QVERIFY(name.open(QIODevice::WriteOnly | QIODevice::Truncate));
    name.write("flexoki-light\n");
    name.close();

    QVERIFY(run({QStringLiteral("check"), QStringLiteral("console"), QStringLiteral("--json")}));
    QCOMPARE(m_exitCode, 0);
    const QJsonArray drawn = pictures();
    QCOMPARE(drawn.size(), 2);
    QCOMPARE(drawn.at(0).toObject().value(QLatin1String("mode")).toString(),
             QStringLiteral("light"));
    QCOMPARE(drawn.at(0).toObject().value(QLatin1String("theme")).toString(),
             QStringLiteral("flexoki-light"));
    QCOMPARE(drawn.at(1).toObject().value(QLatin1String("mode")).toString(),
             QStringLiteral("dark"));
    QCOMPARE(drawn.at(1).toObject().value(QLatin1String("theme")).toString(),
             QStringLiteral("tokyo-night"));
    QVERIFY(QFileInfo::exists(picture(QStringLiteral("console.light"))));
    QVERIFY(QFileInfo::exists(picture(QStringLiteral("console.dark"))));
}

void TestCheck::noLightThemeInstalled()
{
    // No stock light theme: the dark picture alone, and the answer says light wasn't drawn.
    QVERIFY(QDir(home(QStringLiteral("omarchy/themes/catppuccin-latte"))).removeRecursively());
    writeSkin(QStringLiteral("warm"),
              R"({"version": 1, "grounds": {"hover": {"tint": "foreground", "alpha": 1.4}}})");
    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm"), QStringLiteral("--json")}));
    QCOMPARE(m_exitCode, 0);
    const QJsonObject answer = json();
    QCOMPARE(answer.value(QLatin1String("dropped")).toArray().size(), 1);
    const QJsonArray drawn = answer.value(QLatin1String("pictures")).toArray();
    QCOMPARE(drawn.size(), 1);
    QCOMPARE(drawn.first().toObject().value(QLatin1String("mode")).toString(),
             QStringLiteral("dark"));
    const QJsonObject unchecked = answer.value(QLatin1String("unchecked")).toObject();
    QCOMPARE(unchecked.value(QLatin1String("mode")).toString(), QStringLiteral("light"));
    QCOMPARE(unchecked.value(QLatin1String("themes")).toArray(),
             QJsonArray({QStringLiteral("catppuccin-latte"), QStringLiteral("flexoki-light")}));
    QVERIFY(!QFileInfo::exists(picture(QStringLiteral("warm.light"))));

    QVERIFY(run({QStringLiteral("check"), QStringLiteral("warm")}));
    QCOMPARE(m_exitCode, 0);
    QVERIFY2(m_out.contains(QStringLiteral("  light · not drawn: no light theme installed "
                                           "(catppuccin-latte, flexoki-light)\n")),
             qPrintable(m_out));
}

QTEST_GUILESS_MAIN(TestCheck)

#include "tst_check.moc"
