// Every test binary runs on Qt's offscreen platform, set before main() builds any
// QGuiApplication. ctest already sets QT_QPA_PLATFORM=offscreen, but a test run directly
// (`./build/tests/tst_qml -input …`) inherited the desktop's own QT_QPA_PLATFORM (Omarchy
// exports `wayland;xcb`) and opened its windows on the developer's screen. So this does not
// defer to QT_QPA_PLATFORM: to see a test's windows on purpose (inside a nested session such
// as omabox), name the platform in OMASEED_TEST_PLATFORM instead.

#include <QByteArray>
#include <QtGlobal>

namespace {

const bool offscreenByDefault = [] {
    const QByteArray wanted = qgetenv("OMASEED_TEST_PLATFORM");
    qputenv("QT_QPA_PLATFORM", wanted.isEmpty() ? QByteArrayLiteral("offscreen") : wanted);
    return true;
}();

} // namespace
