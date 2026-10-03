// #17 — the bar widget is part of the install: where the packaged widget is found, that
// ~/.config/omarchy/plugins/chaves.omaseed becomes a link to it (replacing a link or an old copy,
// leaving a git checkout from `omarchy plugin add` alone), and that BarWidget rescans the shell and
// enables the widget until the shell knows it, then disables and unlinks it, against stand-in
// `omarchy` scripts. Every path is under a temporary directory; HOME, XDG_CONFIG_HOME and the XDG
// data dirs point into it.

#include "integration/BarWidget.hpp"
#include "integration/BarWidgetInstaller.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace Installer = omaseed::integration::BarWidgetInstaller;
using omaseed::integration::BarWidget;

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
    void linksToThePackagedWidget();
    void replacesALinkOrAnOldCopy();
    void leavesAGitCheckoutAlone();
    void unlinkRemovesOnlyTheLink();
    void inBarAnywhereButDisabled();
    void installLinksRescansAndEnables();
    void installStopsWhenTheShellNeverKnowsIt();
    void unavailableWithoutOmarchyOrAWidget();
    void followsChangesMadeElsewhere();

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
    qunsetenv("OMASEED_OMARCHY");
    qunsetenv("OMASEED_OMARCHY_SHELL");
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
    const auto result = Installer::link(QString(), Installer::targetDir());
    QVERIFY(!result.ok);
    QVERIFY2(result.message.contains(QStringLiteral("not installed")), qPrintable(result.message));
    QCOMPARE(Installer::presence(Installer::targetDir()), Installer::Presence::Absent);
}

void TestBarWidgetInstaller::linksToThePackagedWidget()
{
    const QString source = makePlugin(path(QStringLiteral("usr-share")), "1");
    const QString target = Installer::targetDir();
    const auto result = Installer::link(source, target);
    QVERIFY2(result.ok, qPrintable(result.message));
    QCOMPARE(Installer::presence(target), Installer::Presence::Linked);
    QCOMPARE(Installer::linkTarget(target), source);
    // Through the link the shell reads the package's files, so an upgrade is the widget's too.
    QCOMPARE(readFile(target + QStringLiteral("/Widget.qml")), QByteArray("Item {}"));
    QCOMPARE(besideTarget(target), QStringList{QStringLiteral("chaves.omaseed")});
    // Again: nothing to do.
    QVERIFY(Installer::link(source, target).ok);
    QCOMPARE(besideTarget(target), QStringList{QStringLiteral("chaves.omaseed")});
}

void TestBarWidgetInstaller::replacesALinkOrAnOldCopy()
{
    const QString target = Installer::targetDir();
    const QString old = makePlugin(path(QStringLiteral("old")), "0");
    QVERIFY(Installer::link(old, target).ok);
    const QString source = makePlugin(path(QStringLiteral("usr-share")), "1");
    QVERIFY(Installer::link(source, target).ok);
    QCOMPARE(Installer::linkTarget(target), source);
    QVERIFY2(QFileInfo::exists(old + QStringLiteral("/manifest.json")),
             "replacing a link must not touch what it pointed at");

    // A plain copy (the earlier installer's) goes whole.
    QVERIFY(QFile::remove(target));
    writeFile(target + QStringLiteral("/manifest.json"), "{\"version\": \"copy\"}");
    QCOMPARE(Installer::presence(target), Installer::Presence::Folder);
    QVERIFY(Installer::link(source, target).ok);
    QCOMPARE(Installer::linkTarget(target), source);
    QCOMPARE(besideTarget(target), QStringList{QStringLiteral("chaves.omaseed")});
}

void TestBarWidgetInstaller::leavesAGitCheckoutAlone()
{
    const QString target = Installer::targetDir();
    writeFile(target + QStringLiteral("/.git/HEAD"), "ref: refs/heads/main\n");
    writeFile(target + QStringLiteral("/manifest.json"), "{\"version\": \"git\"}");
    QCOMPARE(Installer::presence(target), Installer::Presence::Checkout);

    const QString source = makePlugin(path(QStringLiteral("usr-share")), "1");
    const auto result = Installer::link(source, target);
    QVERIFY(!result.ok);
    QVERIFY2(result.message.contains(QStringLiteral("omarchy plugin update chaves.omaseed")),
             qPrintable(result.message));
    QCOMPARE(readFile(target + QStringLiteral("/manifest.json")),
             QByteArray("{\"version\": \"git\"}"));
    QVERIFY(!Installer::unlink(target).ok);
    QVERIFY(QFileInfo::exists(target + QStringLiteral("/manifest.json")));
}

void TestBarWidgetInstaller::unlinkRemovesOnlyTheLink()
{
    const QString source = makePlugin(path(QStringLiteral("usr-share")), "1");
    const QString target = Installer::targetDir();
    QVERIFY(Installer::unlink(target).ok); // nothing there is fine
    QVERIFY(Installer::link(source, target).ok);
    QVERIFY(Installer::unlink(target).ok);
    QCOMPARE(Installer::presence(target), Installer::Presence::Absent);
    QVERIFY(QFileInfo::exists(source + QStringLiteral("/manifest.json")));
}

void TestBarWidgetInstaller::inBarAnywhereButDisabled()
{
    const QString json = Installer::shellConfigPath();
    QCOMPARE(json, path(QStringLiteral("config/omarchy/shell.json")));
    QVERIFY(!Installer::isInBar(json)); // no file
    writeFile(json, R"({"bar": {"layout": {"right": [{"id": "omarchy.audio"}]}},
                        "disabledPlugins": ["chaves.omaseed"]})");
    QVERIFY(!Installer::isInBar(json));
    writeFile(json, R"({"bar": {"layout": {"right": [{"id": "chaves.omaseed"}]}}})");
    QVERIFY(Installer::isInBar(json));
    // Inside a container widget, as a bare string (Diogo's sidebar holds both forms).
    writeFile(json, R"({"plugins": [{"id": "chaves.sidebar", "bottom": ["chaves.omaseed"]}]})");
    QVERIFY(Installer::isInBar(json));
}

namespace {

/// A stand-in for `omarchy` / `omarchy-shell`: logs its arguments, and as `omarchy plugin
/// enable` says "is not known" `unknownTimes` times before it writes the id into shell.json.
QString fakeProgram(const QString &dir, const QString &name, int unknownTimes)
{
    const QString file = QDir(dir).filePath(name);
    const QByteArray script =
        "#!/bin/sh\n"
        "echo \"" +
        name.toUtf8() +
        " $*\" >> \"$FAKE_LOG\"\n"
        "case \"$1 $2\" in\n"
        "'plugin enable')\n"
        "  n=$(cat \"$FAKE_LOG.count\" 2>/dev/null || echo 0)\n"
        "  echo $((n + 1)) > \"$FAKE_LOG.count\"\n"
        "  if [ \"$n\" -lt " +
        QByteArray::number(unknownTimes) +
        " ]; then\n"
        "    echo \"plugin '$3' is not known\" >&2; exit 1; fi\n"
        "  printf '{\"bar\":{\"layout\":{\"right\":[{\"id\":\"%s\"}]}}}' \"$3\" "
        "> \"$XDG_CONFIG_HOME/omarchy/shell.json\"\n"
        "  echo \"Enabled $3\" ;;\n"
        "'plugin disable')\n"
        "  echo '{}' > \"$XDG_CONFIG_HOME/omarchy/shell.json\"\n"
        "  echo \"Disabled $3\" ;;\n"
        "esac\n";
    writeFile(file, script);
    QFile::setPermissions(file,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return file;
}

} // namespace

void TestBarWidgetInstaller::installLinksRescansAndEnables()
{
    const QString log = path(QStringLiteral("calls.log"));
    qputenv("FAKE_LOG", log.toUtf8());
    writeFile(Installer::shellConfigPath(), "{}");
    makePlugin(path(QStringLiteral("usr-share")), "1");

    BarWidget widget;
    widget.setPrograms(
        fakeProgram(path(QStringLiteral("bin")), QStringLiteral("omarchy"), 2),
        fakeProgram(path(QStringLiteral("bin")), QStringLiteral("omarchy-shell"), 0));
    widget.setRetry(10, 20);
    QVERIFY(widget.available());
    QVERIFY(!widget.installed());
    QVERIFY(!widget.inBar());

    QSignalSpy finished(&widget, &BarWidget::finished);
    widget.install();
    QVERIFY(widget.busy());
    QVERIFY(finished.wait(5000));
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
    QVERIFY(widget.installed());
    QVERIFY(widget.inBar());
    QVERIFY(!widget.busy());
    QVERIFY(widget.lastOk());

    // The rescan first, then enable until the shell knows the widget (twice "not known").
    const QList<QByteArray> calls = readFile(log).trimmed().split('\n');
    QCOMPARE(calls.size(), 4);
    QVERIFY(calls.at(0).endsWith("omarchy-shell shell rescanPlugins"));
    for (int i = 1; i < 4; ++i)
        QVERIFY(calls.at(i).endsWith("omarchy plugin enable chaves.omaseed"));

    // Out again: disabled, then unlinked.
    finished.clear();
    widget.remove();
    QVERIFY(finished.wait(5000));
    QVERIFY(finished.first().at(0).toBool());
    QVERIFY(!widget.inBar());
    QVERIFY(!widget.installed());
    QVERIFY(readFile(log).trimmed().endsWith("omarchy plugin disable chaves.omaseed"));
}

void TestBarWidgetInstaller::installStopsWhenTheShellNeverKnowsIt()
{
    qputenv("FAKE_LOG", path(QStringLiteral("calls.log")).toUtf8());
    makePlugin(path(QStringLiteral("usr-share")), "1");
    BarWidget widget;
    widget.setPrograms(
        fakeProgram(path(QStringLiteral("bin")), QStringLiteral("omarchy"), 100),
        fakeProgram(path(QStringLiteral("bin")), QStringLiteral("omarchy-shell"), 0));
    widget.setRetry(5, 3);
    QSignalSpy finished(&widget, &BarWidget::finished);
    widget.install();
    QVERIFY(finished.wait(5000));
    QVERIFY(!finished.first().at(0).toBool());
    QVERIFY2(finished.first().at(1).toString().contains(QStringLiteral("is not known")),
             qPrintable(finished.first().at(1).toString()));
    // The link stays: the shell can still be told by hand (`omarchy plugin enable`).
    QVERIFY(widget.installed());
    QVERIFY(!widget.inBar());
}

void TestBarWidgetInstaller::unavailableWithoutOmarchyOrAWidget()
{
    BarWidget widget;
    widget.setPrograms(path(QStringLiteral("bin/none")), path(QStringLiteral("bin/none")));
    QVERIFY(!widget.available());
    makePlugin(path(QStringLiteral("usr-share")), "1");
    widget.refresh();
    QVERIFY(!widget.available()); // a widget, but no omarchy
    widget.setPrograms(QStringLiteral("true"), QStringLiteral("true"));
    QVERIFY(widget.available());
}

QTEST_GUILESS_MAIN(TestBarWidgetInstaller)
/// #21: `omaseed --install-bar-widget` from a terminal, or `omarchy plugin disable`, while
/// Settings shows the switch: the state follows without a refresh() from the view. The
/// folders do not exist yet when the widget starts, as on a desktop that never had a plugin.
void TestBarWidgetInstaller::followsChangesMadeElsewhere()
{
    const QString source = makePlugin(path(QStringLiteral("usr-share")), "1");
    QDir().mkpath(path(QStringLiteral("config")));
    BarWidget widget;
    QVERIFY(!widget.installed());
    QVERIFY(!widget.inBar());

    // Elsewhere: linked, then enabled (the shell rewrites shell.json).
    QVERIFY(Installer::link(source, Installer::targetDir()).ok);
    QTRY_VERIFY_WITH_TIMEOUT(widget.installed(), 5000);
    writeFile(Installer::shellConfigPath(),
              R"({"bar": {"layout": {"right": [{"id": "chaves.omaseed"}]}}})");
    QTRY_VERIFY_WITH_TIMEOUT(widget.inBar(), 5000);

    // Disabled elsewhere, written the way the shell does: a new file renamed over the old one.
    const QString next = Installer::shellConfigPath() + QStringLiteral(".new");
    writeFile(next, R"({"bar": {"layout": {"right": [{"id": "omarchy.audio"}]}}})");
    QVERIFY(QFile::remove(Installer::shellConfigPath()));
    QVERIFY(QFile::rename(next, Installer::shellConfigPath()));
    QTRY_VERIFY_WITH_TIMEOUT(!widget.inBar(), 5000);
    // And it keeps watching the new file.
    writeFile(Installer::shellConfigPath(),
              R"({"bar": {"layout": {"right": [{"id": "chaves.omaseed"}]}}})");
    QTRY_VERIFY_WITH_TIMEOUT(widget.inBar(), 5000);

    QVERIFY(Installer::unlink(Installer::targetDir()).ok);
    QTRY_VERIFY_WITH_TIMEOUT(!widget.installed(), 5000);
}

#include "tst_barwidgetinstaller.moc"
