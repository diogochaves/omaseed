#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest>

// qmltestrunner over the bar widget's pure JavaScript (plugin/Format.js, plugin/Status.js)
// and its rows (#80). The JavaScript tests import those files by relative path. The rows
// import the shell's qs.Commons and qs.Ui, which only the Omarchy shell has: kit/ holds the
// least of each they load against, with each kit's accessibility as it is, so the tests can
// check what a screen reader gets. The rest of the widget's QML (Widget.qml, Popup.qml) needs
// Quickshell and is checked in an omabox (#15).
//
//   kit/common          qs.Commons: the sizes and colours the rows read
//   kit/omarchy-4.0.4   qs.Ui as the installed shell has it: no accessibility
//   kit/pr-13212        qs.Ui with omacom/omarchy PR #13212 (at 30ed618f3): roles,
//                       `accessibleName` and the kit's own press actions, as the PR has them
//
// OMASEED_PLUGIN_KIT picks the qs.Ui (omarchy-4.0.4 by default); ctest runs both. The tests
// read it as `PluginKit`.

// tst_Status reads plugin/manifest.json with a local XMLHttpRequest, to check that its
// schema and defaults match Status.settingRows() and barOptions(). Qt refuses file reads
// through XHR unless this is set before the engine starts.
static const bool s_allowFileRead = qputenv("QML_XHR_ALLOW_FILE_READ", "1");

class Setup : public QObject
{
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        const QString kit = qEnvironmentVariableIsEmpty("OMASEED_PLUGIN_KIT")
                                ? QStringLiteral("omarchy-4.0.4")
                                : qEnvironmentVariable("OMASEED_PLUGIN_KIT");
        const QString root = QStringLiteral(QUICK_TEST_SOURCE_DIR "/kit/");
        engine->addImportPath(root + QStringLiteral("common"));
        engine->addImportPath(root + kit);
        engine->rootContext()->setContextProperty(QStringLiteral("PluginKit"), kit);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(omaseed_plugin, Setup)

#include "tst_plugin.moc"
