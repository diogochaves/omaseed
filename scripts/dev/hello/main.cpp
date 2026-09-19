#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDBusConnection>
#include <QFontInfo>
#include <QDebug>
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    qInfo() << "qt" << qVersion() << "monospace ->" << QFontInfo(QFont(QStringLiteral("monospace"))).family()
            << "session bus" << QDBusConnection::sessionBus().isConnected();
    if (app.arguments().contains(QStringLiteral("--check"))) return 0;
    QQmlApplicationEngine engine;
    engine.loadFromModule("Hello", "Main");
    return engine.rootObjects().isEmpty() ? 1 : app.exec();
}
