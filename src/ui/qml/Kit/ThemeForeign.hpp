#pragma once

#include "integration/Theme.hpp"

#include <QQmlEngine>

/// `Theme` is a C++ object in omaseed-integration, but every QML file reaches it as
/// `Omaseed.Kit`'s `Theme` singleton. These two declarations are the whole bridge: they are
/// what puts the type in the module's type registry, so qmllint and qmlls resolve
/// `Theme.background` statically instead of guessing at runtime.
struct ThemeForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::integration::Theme)
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

public:
    static omaseed::integration::Theme *create(QQmlEngine *engine, QJSEngine *)
    {
        return omaseed::integration::Theme::createForEngine(engine);
    }
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
