// #17 — `omaseed --install-bar-widget`: where the packaged widget is found, where it goes, and
// that the copy is a real copy (regular files, no symlinks), replaces an older one whole and
// leaves a git checkout from `omarchy plugin add` alone. Every path is under a temporary
// directory; HOME, XDG_CONFIG_HOME and the XDG data dirs point into it.

#include "integration/BarWidgetInstaller.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace Installer = omaseed::integration::BarWidgetInstaller;

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

/// A plugin folder like the packaged one: a manifest, a widget, a nested file.
QString makePlugin(const QString &root, const QByteArray &version)
{
    const QString dir = QDir(root).filePath(QStringLiteral("omaseed/plugin"));
    writeFile(dir + QStringLiteral("/manifest.json"), "{\"version\": \"" + version + "\"}");
    writeFile(dir + QStringLiteral("/Widget.qml"), "Item {}");
    writeFile(dir + QStringLiteral("/icons/mark.svg"), "<svg/>");
    // Package files are read-only; the copy must still be writable by its owner.
    QFile::setPermissions(dir + QStringLiteral("/Widget.qml"),
                          QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    return dir;
}

/// Everything in the folder that holds `target`, hidden entries included: after an install it
/// is the plugin alone, no staging or old copy left for the shell to find.
QStringList besideTarget(const QString &target)
{
    return QFileInfo(target).dir().entryList(QDir::AllEntries | QDir::Hidden |
                                             QDir::NoDotAndDotDot);
}

} // namespace

class TestBarWidgetInstaller : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void targetFollowsXdgConfigHome();
    void enableLineNamesThePlugin();
    void sourceFromEnvironmentFirst();
    void sourceFromDataDirs();
    void sourceFromDevTreeLast();
    void noSourceAnywhere();
    void installsARealCopy();
    void replacesAnOlderCopyWhole();
    void symlinksAreCopiedAsFiles();
    void leavesAGitCheckoutAlone();
    void missingSourceLeavesTargetAlone();

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString path(const QString &relative) const { return m_tmp->filePath(relative); }
};

void TestBarWidgetInstaller::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
    qputenv("HOME", path(QStringLiteral("home")).toUtf8());
    qputenv("XDG_CONFIG_HOME", path(QStringLiteral("config")).toUtf8());
    qputenv("XDG_DATA_HOME", path(QStringLiteral("data-home")).toUtf8());
    qputenv("XDG_DATA_DIRS", path(QStringLiteral("usr-share")).toUtf8());
    qunsetenv("OMASEED_PLUGIN_DIR");
}

void TestBarWidgetInstaller::cleanup()
{
    m_tmp.reset();
}

void TestBarWidgetInstaller::targetFollowsXdgConfigHome()
{
    QCOMPARE(Installer::targetDir(), path(QStringLiteral("config/omarchy/plugins/chaves.omaseed")));
    qunsetenv("XDG_CONFIG_HOME");
    QCOMPARE(Installer::targetDir(),
             QDir::home().filePath(QStringLiteral(".config/omarchy/plugins/chaves.omaseed")));
}

void TestBarWidgetInstaller::enableLineNamesThePlugin()
{
    QCOMPARE(Installer::pluginId(), QStringLiteral("chaves.omaseed"));
    QCOMPARE(Installer::enableCommand(), QStringLiteral("omarchy plugin enable chaves.omaseed"));
}

void TestBarWidgetInstaller::sourceFromEnvironmentFirst()
{
    makePlugin(path(QStringLiteral("usr-share")), "1");
    const QString custom = makePlugin(path(QStringLiteral("custom")), "2");
    qputenv("OMASEED_PLUGIN_DIR", custom.toUtf8());
    QCOMPARE(Installer::findSource(), custom);
    // An environment folder without a manifest is skipped, not trusted.
    qputenv("OMASEED_PLUGIN_DIR", path(QStringLiteral("nothing")).toUtf8());
    QCOMPARE(Installer::findSource(), path(QStringLiteral("usr-share/omaseed/plugin")));
}

void TestBarWidgetInstaller::sourceFromDataDirs()
{
    const QString system = makePlugin(path(QStringLiteral("usr-share")), "1");
    QCOMPARE(Installer::findSource(), system);
    // $XDG_DATA_HOME comes before $XDG_DATA_DIRS.
    const QString user = makePlugin(path(QStringLiteral("data-home")), "2");
    QCOMPARE(Installer::findSource(), user);
}

void TestBarWidgetInstaller::sourceFromDevTreeLast()
{
    const QString dev = makePlugin(path(QStringLiteral("src")), "dev");
    QCOMPARE(Installer::findSource(dev), dev);
    const QString system = makePlugin(path(QStringLiteral("usr-share")), "1");
    QCOMPARE(Installer::findSource(dev), system);
}

void TestBarWidgetInstaller::noSourceAnywhere()
{
    QVERIFY(Installer::findSource(path(QStringLiteral("src/plugin"))).isEmpty());
    const auto result = Installer::install(QString(), Installer::targetDir());
    QVERIFY(!result.ok);
    QVERIFY2(result.message.contains(QStringLiteral("not installed")), qPrintable(result.message));
    QVERIFY(!QFileInfo::exists(Installer::targetDir()));
}

void TestBarWidgetInstaller::installsARealCopy()
{
    const QString source = makePlugin(path(QStringLiteral("usr-share")), "1");
    const QString target = Installer::targetDir();
    const auto result = Installer::install(source, target);
    QVERIFY2(result.ok, qPrintable(result.message));
    QVERIFY(result.message.startsWith(QStringLiteral("Installed")));
    QVERIFY(result.message.endsWith(Installer::enableCommand()));

    for (const char *name : {"manifest.json", "Widget.qml", "icons/mark.svg"}) {
        const QFileInfo info(QDir(target).filePath(QLatin1String(name)));
        QVERIFY2(info.isFile() && !info.isSymLink(), name);
        QCOMPARE(info.permissions() & (QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                       QFileDevice::ReadOther | QFileDevice::WriteOther),
                 QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther);
    }
    QCOMPARE(readFile(target + QStringLiteral("/Widget.qml")), QByteArray("Item {}"));
    QCOMPARE(besideTarget(target), QStringList{QStringLiteral("chaves.omaseed")});
}

void TestBarWidgetInstaller::replacesAnOlderCopyWhole()
{
    const QString target = Installer::targetDir();
    writeFile(target + QStringLiteral("/manifest.json"), "{\"version\": \"0\"}");
    writeFile(target + QStringLiteral("/Gone.qml"), "Item {}");

    const QString source = makePlugin(path(QStringLiteral("usr-share")), "1");
    const auto result = Installer::install(source, target);
    QVERIFY2(result.ok, qPrintable(result.message));
    QVERIFY(result.message.startsWith(QStringLiteral("Updated")));
    QCOMPARE(readFile(target + QStringLiteral("/manifest.json")),
             QByteArray("{\"version\": \"1\"}"));
    QVERIFY(!QFileInfo::exists(target + QStringLiteral("/Gone.qml")));
    QCOMPARE(besideTarget(target), QStringList{QStringLiteral("chaves.omaseed")});
}

void TestBarWidgetInstaller::symlinksAreCopiedAsFiles()
{
    const QString source = makePlugin(path(QStringLiteral("usr-share")), "1");
    writeFile(path(QStringLiteral("elsewhere/README.md")), "read me");
    QVERIFY(QFile::link(path(QStringLiteral("elsewhere/README.md")),
                        source + QStringLiteral("/README.md")));

    const QString target = Installer::targetDir();
    QVERIFY(Installer::install(source, target).ok);
    const QFileInfo readme(target + QStringLiteral("/README.md"));
    QVERIFY(readme.isFile());
    QVERIFY(!readme.isSymLink());
    QCOMPARE(readFile(readme.filePath()), QByteArray("read me"));
}

void TestBarWidgetInstaller::leavesAGitCheckoutAlone()
{
    const QString target = Installer::targetDir();
    writeFile(target + QStringLiteral("/.git/HEAD"), "ref: refs/heads/main\n");
    writeFile(target + QStringLiteral("/manifest.json"), "{\"version\": \"git\"}");

    const QString source = makePlugin(path(QStringLiteral("usr-share")), "1");
    const auto result = Installer::install(source, target);
    QVERIFY(!result.ok);
    QVERIFY2(result.message.contains(QStringLiteral("omarchy plugin update chaves.omaseed")),
             qPrintable(result.message));
    QCOMPARE(readFile(target + QStringLiteral("/manifest.json")),
             QByteArray("{\"version\": \"git\"}"));
}

void TestBarWidgetInstaller::missingSourceLeavesTargetAlone()
{
    const QString target = Installer::targetDir();
    writeFile(target + QStringLiteral("/manifest.json"), "{\"version\": \"0\"}");
    const auto result = Installer::install(path(QStringLiteral("empty")), target);
    QVERIFY(!result.ok);
    QCOMPARE(readFile(target + QStringLiteral("/manifest.json")),
             QByteArray("{\"version\": \"0\"}"));
}

QTEST_GUILESS_MAIN(TestBarWidgetInstaller)
#include "tst_barwidgetinstaller.moc"
