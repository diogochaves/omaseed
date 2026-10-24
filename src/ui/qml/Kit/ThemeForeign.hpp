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

/// The type behind each of `Theme.on`'s grounds (docs/DECISIONS.md C30 F7): the text roles as
/// they read on one ground. Never created from QML.
struct ThemeInkForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::integration::ThemeInk)
    QML_NAMED_ELEMENT(ThemeInk)
    QML_UNCREATABLE("ThemeInk is a ground of Theme.on; it is not created from QML.")
};

/// The type behind `Theme.on`. Never created from QML.
struct ThemeGroundsForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::integration::ThemeGrounds)
    QML_NAMED_ELEMENT(ThemeGrounds)
    QML_UNCREATABLE("ThemeGrounds is the type of Theme.on; it is not created from QML.")
};

/// The type behind `Theme.skin` (#171): the skin's tokens and why it is Omarchy's when another
/// was asked for. Never created from QML.
struct ThemeSkinForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::integration::ThemeSkin)
    QML_NAMED_ELEMENT(ThemeSkin)
    QML_UNCREATABLE("ThemeSkin is the type of Theme.skin; it is not created from QML.")
};
