// Every test binary runs on Qt's offscreen platform, set before main() builds any
// QGuiApplication. ctest already sets QT_QPA_PLATFORM=offscreen, but a test run directly
// (`./build/tests/tst_qml -input …`) inherited the desktop's own QT_QPA_PLATFORM (Omarchy
// exports `wayland;xcb`) and opened its windows on the developer's screen. So this does not
// defer to QT_QPA_PLATFORM: to see a test's windows on purpose (inside a nested session such
// as omabox), name the platform in OMASEED_TEST_PLATFORM instead.
//
// The same goes for the XDG homes. ctest points tst_qml's config and state at
// build/tests/qml-env and every test's data at build/tests/data-home; run directly, a test
// read and wrote the developer's real ~/.config/omaseed/state.json (an agent's run did,
// 2026-10-12). So a home that does not already point inside the build tree is pointed at the
// tests' own folder here, before anything reads it, and the desktop's `omarchy` is never run.

#include <QByteArray>
#include <QtGlobal>

namespace {

void keepInsideBuild(const char *name, const QByteArray &fallback)
{
    if (!qgetenv(name).startsWith(QByteArrayLiteral(OMASEED_TEST_BUILD_DIR)))
        qputenv(name, fallback);
}

const bool offscreenByDefault = [] {
    const QByteArray wanted = qgetenv("OMASEED_TEST_PLATFORM");
    qputenv("QT_QPA_PLATFORM", wanted.isEmpty() ? QByteArrayLiteral("offscreen") : wanted);

    const QByteArray homes = QByteArrayLiteral(OMASEED_TEST_XDG_DIR);
    keepInsideBuild("XDG_CONFIG_HOME", homes);
    keepInsideBuild("XDG_STATE_HOME", homes);
    keepInsideBuild("XDG_CACHE_HOME", homes + QByteArrayLiteral("/cache"));
    keepInsideBuild("XDG_DATA_HOME", QByteArrayLiteral(OMASEED_TEST_BUILD_DIR "/tests/data-home"));
    if (!qEnvironmentVariableIsSet("OMASEED_OMARCHY"))
        qputenv("OMASEED_OMARCHY", "omaseed-tests-have-no-omarchy");
    if (!qEnvironmentVariableIsSet("OMASEED_OMARCHY_SHELL"))
        qputenv("OMASEED_OMARCHY_SHELL", "omaseed-tests-have-no-omarchy");
    return true;
}();

} // namespace
