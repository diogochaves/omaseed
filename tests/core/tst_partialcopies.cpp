// docs/44 D13 (#121): what a failed move left in a destination omaseed can see.

#include "core/PartialCopies.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::PartialCopies;

class TestPartialCopies : public QObject
{
    Q_OBJECT

private slots:
    void aFolderLeftBehindIsCounted();
    void aSingleFile();
    void nothingThereIsSeen();
    void aDestinationNotHereIsNotSeen();

private:
    static void write(const QString &path, qsizetype bytes)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(bytes, 'x'));
    }
};

void TestPartialCopies::aFolderLeftBehindIsCounted()
{
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("Album/01.flac")), 1000);
    write(dir.filePath(QStringLiteral("Album/disc 2/02.flac")), 500);
    const PartialCopies copies = PartialCopies::read(dir.filePath(QStringLiteral("Album")));
    QVERIFY(copies.seen);
    QCOMPARE(copies.files, 2);
    QCOMPARE(copies.bytes, 1500);
}

void TestPartialCopies::aSingleFile()
{
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("image.iso")), 700);
    const PartialCopies copies = PartialCopies::read(dir.filePath(QStringLiteral("image.iso")));
    QVERIFY(copies.seen);
    QCOMPARE(copies.files, 1);
    QCOMPARE(copies.bytes, 700);
}

void TestPartialCopies::nothingThereIsSeen()
{
    QTemporaryDir dir;
    const PartialCopies copies = PartialCopies::read(dir.filePath(QStringLiteral("Album")));
    QVERIFY(copies.seen);
    QCOMPARE(copies.files, 0);
}

void TestPartialCopies::aDestinationNotHereIsNotSeen()
{
    QTemporaryDir dir;
    QVERIFY(!PartialCopies::read(dir.filePath(QStringLiteral("gone/Album"))).seen);
    QVERIFY(!PartialCopies::read(QString()).seen);
}

QTEST_GUILESS_MAIN(TestPartialCopies)
#include "tst_partialcopies.moc"
