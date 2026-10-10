#pragma once

#include "core/Beeswarm.hpp"

#include <QList>
#include <QObject>
#include <QQmlEngine>

/// `Beeswarm.place(xs, rs, midY, step, tries)`: core::Beeswarm, for RatioLadder's dots. Placing
/// a library's worth of them in JavaScript took seconds (#45).
class BeeswarmForeign : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Beeswarm)
    QML_SINGLETON

public:
    using QObject::QObject;

    Q_INVOKABLE QList<qreal>
    place(const QList<qreal> &xs, const QList<qreal> &rs, qreal midY, qreal step, int tries) const
    {
        return omaseed::core::Beeswarm::place(xs, rs, midY, step, tries);
    }
};
