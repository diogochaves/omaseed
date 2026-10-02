#include <QtQuickTest>

// qmltestrunner over the bar widget's pure JavaScript (plugin/Format.js, plugin/Status.js).
// The tests import those files by relative path, so nothing of the shell (qs.Commons,
// Quickshell) is needed: the widget's QML itself is checked in an omabox (#15).
QUICK_TEST_MAIN(omaseed_plugin)
