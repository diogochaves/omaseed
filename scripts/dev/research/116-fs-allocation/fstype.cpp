#include <QCoreApplication>
#include <QStorageInfo>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    for (int i = 1; i < argc; ++i) {
        QStorageInfo s(QString::fromLocal8Bit(argv[i]));
        std::printf("%s\t%s\t%s\n", argv[i], s.fileSystemType().constData(), s.device().constData());
    }
}
