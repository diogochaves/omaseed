#pragma once

#include "integration/Theme.hpp"

#include <QQmlEngine>

/// `Theme` is a C++ object in omaseed-integration, registered into `Omaseed.Kit` as
/// `ThemeBase`. Every QML file reaches it through `Theme.qml`, the module's `Theme` singleton,
/// which derives from it and adds a `space()` a binding follows (#156, docs/DECISIONS.md C28).
/// This declaration is what puts the type in the module's type registry, so qmllint and qmlls
/// resolve `Theme.background` statically instead of guessing at runtime. Nothing but
/// `Theme.qml` creates one.
struct ThemeForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::integration::Theme)
    QML_NAMED_ELEMENT(ThemeBase)
};

/// The type behind `Theme.font`. It is never instantiated from QML; it only needs a name in
/// the type registry so `Theme.font.body` type-checks.
struct ThemeFontForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::integration::ThemeFont)
    QML_NAMED_ELEMENT(ThemeFont)
    QML_UNCREATABLE("ThemeFont is the type of Theme.font; it is not created from QML.")
};
