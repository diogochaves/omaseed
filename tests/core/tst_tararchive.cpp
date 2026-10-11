#include "core/TarArchive.hpp"

#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::TarArchive;

class TstTarArchive : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip()
    {
        QByteArray big(70000, 'x');
        big[12345] = '\0';
        const QList<TarArchive::Entry> in{{QStringLiteral("manifest.json"), "{}", 1759500000},
                                          {QStringLiteral("ledger/abc.sqlite"), big, 1759500000},
                                          {QStringLiteral("empty.txt"), QByteArray(), 0}};
        QString error;
        const QByteArray archive = TarArchive::write(in, &error);
        QVERIFY2(!archive.isEmpty(), qPrintable(error));
        QCOMPARE(archive.size() % 512, 0);
        const auto out = TarArchive::read(archive, &error);
        QVERIFY2(out.has_value(), qPrintable(error));
        QCOMPARE(out->size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(out->at(i).name, in.at(i).name);
            QCOMPARE(out->at(i).data, in.at(i).data);
            QCOMPARE(out->at(i).mtime, in.at(i).mtime);
        }
    }

    void longNameUsesThePrefix()
    {
        const QString name = QString(120, QLatin1Char('d')) + QStringLiteral("/file.json");
        const QByteArray archive = TarArchive::write({{name, "1", 0}});
        const auto out = TarArchive::read(archive);
        QVERIFY(out.has_value());
        QCOMPARE(out->constFirst().name, name);
    }

    void refusesUnsafeNames_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("absolute") << QStringLiteral("/etc/passwd");
        QTest::newRow("climbs") << QStringLiteral("ledger/../../x");
        QTest::newRow("empty") << QString();
        QTest::newRow("control") << QStringLiteral("a\nb");
    }
    void refusesUnsafeNames()
    {
        QFETCH(QString, name);
        QString error;
        QVERIFY(TarArchive::write({{name, "x", 0}}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void refusesDamage()
    {
        const QByteArray good = TarArchive::write({{QStringLiteral("a.json"), "hello", 0}});
        QString error;

        QByteArray flipped = good;
        flipped[3] = 'Z'; // a name byte: the checksum no longer matches
        QVERIFY(!TarArchive::read(flipped, &error));
        QVERIFY(error.contains(QStringLiteral("damaged")));

        QVERIFY(!TarArchive::read(good.left(600), &error)); // cut in the middle
        QVERIFY(!TarArchive::read(
            QByteArray("PK\x03\x04 not a tar at all") + QByteArray(1024, '\0'), &error));
        QVERIFY(!TarArchive::read(QByteArray(), &error));
    }

    void readsWhatTarWrote()
    {
        // Any tool can open a backup, and a tar made by the system tool reads back here.
        const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
        if (tar.isEmpty())
            QSKIP("no tar on this machine");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray mine = TarArchive::write({{QStringLiteral("sub/a.txt"), "made here", 0}});
        QFile out(dir.filePath(QStringLiteral("mine.tar")));
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(mine);
        out.close();
        QProcess list;
        list.start(tar, {QStringLiteral("-xOf"), out.fileName(), QStringLiteral("sub/a.txt")});
        QVERIFY(list.waitForFinished(10000));
        QCOMPARE(list.readAllStandardOutput(), QByteArray("made here"));

        QFile in(dir.filePath(QStringLiteral("x.txt")));
        QVERIFY(in.open(QIODevice::WriteOnly));
        in.write("from tar");
        in.close();
        QProcess make;
        make.setWorkingDirectory(dir.path());
        make.start(tar,
                   {QStringLiteral("--format=ustar"),
                    QStringLiteral("-cf"),
                    QStringLiteral("theirs.tar"),
                    QStringLiteral("x.txt")});
        QVERIFY(make.waitForFinished(10000));
        QFile theirs(dir.filePath(QStringLiteral("theirs.tar")));
        QVERIFY(theirs.open(QIODevice::ReadOnly));
        const auto read = TarArchive::read(theirs.readAll());
        QVERIFY(read.has_value());
        QCOMPARE(read->constFirst().data, QByteArray("from tar"));
    }
};

QTEST_GUILESS_MAIN(TstTarArchive)
#include "tst_tararchive.moc"
