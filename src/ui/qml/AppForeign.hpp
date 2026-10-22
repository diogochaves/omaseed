#pragma once

#include "ui/app/App.hpp"

#include <QQmlEngine>

/// `App` is a C++ object in omaseed-app, but every QML file reaches it as the `Omaseed`
/// module's `App` singleton. These declarations are the whole bridge: they put the type in
/// the module's type registry, so qmllint and qmlls resolve `App.filtered`, `App.counts` and
/// the rest statically instead of treating them as unqualified context properties. Mirrors
/// `Kit/ThemeForeign.hpp` and `XdgForeign.hpp`.
struct AppForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::app::App)
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

public:
    static omaseed::app::App *create(QQmlEngine *engine, QJSEngine *)
    {
        return omaseed::app::App::createForEngine(engine);
    }
};

/// Board 42 (#97): `App.move`, the folder's move: the sheet, the job, its band.
struct MoveControllerForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::app::MoveController)
    QML_NAMED_ELEMENT(MoveController)
    QML_UNCREATABLE("MoveController is App.move; it is not created from QML.")
};
