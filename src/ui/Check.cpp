#include "ui/Check.hpp"

#include "core/Version.hpp"
#include "integration/AppIdentity.hpp"
#include "integration/SkinReader.hpp"
#include "integration/SkinWords.hpp"
#include "integration/Theme.hpp"
#include "integration/ThemeInk.hpp"
#include "integration/ThemeReader.hpp"
#include "ui/app/App.hpp"
#include "ui/i18n/Translations.hpp"

#include <QDateTime>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QQuickView>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QTimer>

namespace omaseed::ui {

namespace {

namespace SkinWords = integration::SkinWords;
using integration::Theme;
using integration::ThemeInk;
using integration::ThemeReader;

/// The frames to wait for once the sheet is loaded, before the grab: the skin, the fonts and
/// the theme are applied before the first, and the second shows anything a first frame set
/// off (herdr's stale-frame lesson, `8ab0a55`). Past the deadline the grab happens anyway.
constexpr int kFrames = 2;
constexpr int kDeadlineMs = 10000;

/// Every pair (role, ground) the floor lifted under `theme`, with the lift.
QHash<QString, ThemeInk::Lift> liftsOf(const Theme &theme)
{
    QHash<QString, ThemeInk::Lift> out;
    const ThemeInk::Roles &window = theme.on()->window()->roles();
    for (const ThemeInk *ink : theme.on()->all()) {
        for (const ThemeInk::Lift &lift : ThemeInk::lifts(window, ink->ground()))
            out.insert(ink->name() + QLatin1Char('\n') + lift.role, lift);
    }
    return out;
}

/// The lifts the drawn skin needed that Omarchy's skin in the same theme doesn't (#283: so a
/// copy of Omarchy's reports none), in Theme.on's ground order.
QList<SkinWords::CheckLift> liftsBeyondOmarchy(ThemeReader *reader)
{
    const Theme::Hyprland none{QString(), QString()};
    Theme drawn(reader, none);
    Theme omarchy(reader, none);
    omarchy.setSkin(QStringLiteral("omarchy"));
    const QHash<QString, ThemeInk::Lift> omarchys = liftsOf(omarchy);
    QList<SkinWords::CheckLift> out;
    const ThemeInk::Roles &window = drawn.on()->window()->roles();
    for (const ThemeInk *ink : drawn.on()->all()) {
        for (const ThemeInk::Lift &lift : ThemeInk::lifts(window, ink->ground())) {
            if (omarchys.contains(ink->name() + QLatin1Char('\n') + lift.role))
                continue;
            out.append({lift.role, ink->name(), lift.from, lift.to});
        }
    }
    return out;
}

/// Shows the sheet, waits for its frames, and grabs it; a null image when it didn't load.
QImage drawSheet()
{
    QQuickView view;
    view.setColor(Qt::black);
    view.loadFromModule("Omaseed.Views", "CheckSheet");
    if (view.status() != QQuickView::Ready || !view.rootObject())
        return {};
    view.show();

    QEventLoop loop;
    int frames = 0;
    QObject::connect(&view, &QQuickWindow::frameSwapped, &loop, [&]() {
        if (++frames >= kFrames)
            loop.quit();
        else
            view.update();
    });
    QTimer::singleShot(kDeadlineMs, &loop, &QEventLoop::quit);
    view.update();
    loop.exec();
    return view.grabWindow();
}

} // namespace

CheckResult runCheck(int argc, char *argv[], const QString &skin, const QString &theme)
{
    // C37 U5: no display, no bus, by construction. A session or system bus address that
    // leads nowhere (an unset one would fall back to $XDG_RUNTIME_DIR/bus), no Hyprland to
    // ask for its rounding, no QML disk cache, the software scene graph offscreen.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/dev/null/omaseed-check-has-no-bus");
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/dev/null/omaseed-check-has-no-bus");
    qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    qputenv("QML_DISABLE_DISK_CACHE", "1");
    qputenv("OMASEED_SKIN", skin.toUtf8());
    // The rows display, so its proxy is grouped by question as the person's rows are.
    qputenv("OMASEED_DISPLAY", "rows");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    // Its answer is all it says: the app's start-up notes ("demo: 16 fictional torrents")
    // stay out of the terminal unless someone asks for them.
    if (qEnvironmentVariableIsEmpty("QT_LOGGING_RULES"))
        QLoggingCategory::setFilterRules(QStringLiteral("*.info=false"));

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(integration::AppIdentity::applicationName());
    QCoreApplication::setApplicationVersion(core::version());
    QCoreApplication::setOrganizationDomain(integration::AppIdentity::organizationDomain());
    i18n::installTranslations(&app);

    CheckResult result;
    ThemeReader::Paths paths = ThemeReader::Paths::fromEnvironment();
    if (!theme.isEmpty()) {
        paths.themeDir = ThemeReader::Paths::installedThemeDir(theme);
        if (paths.themeDir.isEmpty()) {
            result.code = QStringLiteral("no-theme");
            result.message = QStringLiteral("No theme called %1. ls ~/.config/omarchy/themes "
                                            "/usr/share/omarchy/themes lists them.")
                                 .arg(theme);
            return result;
        }
    }

    ThemeReader reader(paths);
    Theme::setDefaultReader(&reader);
    // The person's base size and window ground from state.json apply, read only: a demo never
    // writes it (App::writeStateSection).
    app::App omaseedApp;
    app::App::setDefaultInstance(&omaseedApp);
    omaseedApp.attachTheme(&reader);
    omaseedApp.startDemo(16);

    const QImage picture = drawSheet();

    SkinWords::CheckFacts facts;
    facts.name = skin;
    facts.skin = reader.readSkin(skin);
    facts.missingFonts =
        integration::SkinReader::missingFonts(facts.skin, QFontDatabase::families());
    facts.lifted = liftsBeyondOmarchy(&reader);
    facts.theme = reader.themeName();
    facts.mode = reader.mode();
    {
        const Theme measured(&reader, Theme::Hyprland{QString(), QString()});
        facts.baseSize = measured.font()->baseSize();
    }
    facts.omabox = !QStandardPaths::findExecutable(QStringLiteral("omabox")).isEmpty();

    const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                           QStringLiteral("/omaseed/check");
    facts.picture = folder + QLatin1Char('/') + skin +
                    (theme.isEmpty() ? QString() : QLatin1Char('@') + theme) +
                    QStringLiteral(".png");
    if (picture.isNull() || !QDir().mkpath(folder) || !picture.save(facts.picture, "PNG")) {
        result.code = QStringLiteral("not-written");
        result.message =
            picture.isNull()
                ? QStringLiteral("The picture couldn't be drawn: omaseed's sheet didn't load.")
                : QStringLiteral("The picture can't be written in %1.").arg(folder);
        return result;
    }

    result.json = SkinWords::check(facts, QDateTime::currentDateTimeUtc(), core::version());
    result.exit = facts.skin.drawn() ? 0 : 1;
    return result;
}

} // namespace omaseed::ui
