#pragma once

#include "integration/BarWidget.hpp"
#include "integration/Xdg.hpp"

#include <QQmlEngine>

/// `Xdg` is a C++ object in omaseed-integration, but every QML file reaches it as the
/// `Omaseed` module's `Xdg` singleton. These two declarations are the whole bridge: they are
/// what puts the type in the module's type registry, so qmllint and qmlls resolve
/// `Xdg.addablesFromDrop(...)` statically instead of treating it as an unqualified context
/// property. Mirrors `Kit/ThemeForeign.hpp`.
struct XdgForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::integration::Xdg)
    QML_NAMED_ELEMENT(Xdg)
    QML_SINGLETON

public:
    static omaseed::integration::Xdg *create(QQmlEngine *engine, QJSEngine *)
    {
        return omaseed::integration::Xdg::createForEngine(engine);
    }
};

/// The type behind `drop.kind`/`drop.value` in `addablesFromDrop()`'s result. It is never
/// named from QML (a lowercase-first QML_NAMED_ELEMENT is required for value types, and
/// nothing needs to spell "Addable" in QML anyway), so QML_ANONYMOUS is enough to put it in
/// the type registry and let qmllint type-check member access on it.
struct AddableForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::integration::Addable)
    QML_ANONYMOUS
};

/// `App.barWidget` (#17): registered so qmllint type-checks `App.barWidget.inBar` and friends.
/// It is App's; QML never creates one.
struct BarWidgetForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::integration::BarWidget)
    QML_NAMED_ELEMENT(BarWidget)
    QML_UNCREATABLE("BarWidget is App.barWidget; it is not created from QML.")
};
