#include <QtQuickTest>

// qmltestrunner over the bar widget's pure JavaScript (plugin/Format.js, plugin/Status.js).
// The tests import those files by relative path, so nothing of the shell (qs.Commons,
// Quickshell) is needed: the widget's QML itself is checked in an omabox (#15).

// tst_Status reads plugin/manifest.json with a local XMLHttpRequest, to check that its
// schema and defaults match Status.settingRows() and barOptions(). Qt refuses file reads
// through XHR unless this is set before the engine starts.
static const bool s_allowFileRead = qputenv("QML_XHR_ALLOW_FILE_READ", "1");

QUICK_TEST_MAIN(omaseed_plugin)
