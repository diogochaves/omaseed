#pragma once

#include <QObject>
#include <QProcessEnvironment>
#include <QQmlEngine>
#include <QString>

/// A one-method env-var reader, scoped to `Omaseed.Views` because only `DemoStates.qml`
/// needs it: `--demo states` reads `OMASEED_DEMO_STATE=empty|disconnected|filtered|loading`
/// to show one state full-window instead of the four-quadrant comparison layout, for the
/// phase-exit screenshot procedure. No production view touches this type.
class DemoEnv : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    Q_INVOKABLE QString value(const QString &name) const
    {
        return QProcessEnvironment::systemEnvironment().value(name);
    }
};
