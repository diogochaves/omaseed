#include "ui/app/AddRoute.hpp"

namespace omaseed::app {

AddRouting routeAdd(AddSource source, bool firstRun, bool mainActive, qint64 msSinceMainActive)
{
    // C35 W6: first run keeps the main window on the first-run page, which holds the adds.
    if (firstRun)
        return {AddRoute::Main, true};
    // C35 W2: the window had focus just before, so the add opens in it, as today.
    if (mainActive)
        return {AddRoute::Main, false};
    // Settled at #218 (C35): a launch's own URIs go to the main window, which is mapping
    // fresh where the user is anyway. It comes up even when launched `--hidden`, as before.
    if (source == AddSource::Startup)
        return {AddRoute::Main, true};
    // C35 W2: a menu or the bus may have taken focus from the window a moment ago. It is
    // raised, as the menu's add always did: the user may have left its workspace within the
    // grace, and the sheet must not open out of sight (settled at #218).
    const bool viaPopup = source == AddSource::Menu || source == AddSource::Bus;
    if (viaPopup && msSinceMainActive >= 0 && msSinceMainActive <= kAddFocusGrace.count())
        return {AddRoute::Main, true};
    // C35 W1: anywhere else, the add window, where the user is.
    return {AddRoute::Window, false};
}

QString addSourceName(AddSource source)
{
    switch (source) {
    case AddSource::Handoff:
        return QStringLiteral("handoff");
    case AddSource::Startup:
        return QStringLiteral("startup");
    case AddSource::Menu:
        return QStringLiteral("menu");
    case AddSource::Bus:
        return QStringLiteral("bus");
    }
    return QString();
}

} // namespace omaseed::app
