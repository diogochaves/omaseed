// Manual check for T1.7: reveal a real file in the real file manager over the real session
// bus. Not a test, not registered with ctest — see docs/06-implementation-plan.md T1.7.
// Usage: omaseed-reveal-manual <path>

#include "integration/Xdg.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        qWarning() << "usage: omaseed-reveal-manual <path>";
        return 1;
    }

    omaseed::integration::Xdg xdg;
    QObject::connect(&xdg, &omaseed::integration::Xdg::revealFinished, &app, [](bool dbusWorked) {
        qInfo() << "reveal finished; dbus worked:" << dbusWorked;
        QCoreApplication::quit();
    });

    xdg.showInFileManager(QString::fromLocal8Bit(argv[1]));

    QTimer::singleShot(5000, &app, &QCoreApplication::quit);
    return app.exec();
}
