#include <QObject>
#include <QQmlEngine>
#include <QtQuickTest>

/// Hook for the QML test engine. Empty for now; T1.2 will register the real Theme here so the
/// kit tests run against the reader instead of the stub.
class Setup : public QObject
{
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine *engine) { Q_UNUSED(engine); }
};

QUICK_TEST_MAIN_WITH_SETUP(omaseed_qml, Setup)

#include "tst_qml.moc"
