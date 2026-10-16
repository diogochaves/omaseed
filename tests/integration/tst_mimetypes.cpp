// docs/34 D7 a and D9 a (#70): omaseed's two archives as MIME types. The shared-mime-info file
// declares application/x-omaseed-backup (*.omaseed) and application/x-omaseed-torrents
// (*.omaseed-torrents), each a sub-class of application/x-tar with omaseed's icon; `cmake
// --install` puts it in share/mime/packages and, outside a package's DESTDIR, builds that
// prefix's database, which Qt then reads; the .desktop entry does not name either type, so
// omaseed is not their opener. The install goes into a temporary prefix (the "mime" component
// only): nothing outside the build tree is written.

#include <QDir>
#include <QFile>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

namespace {

const QString kAppId = QStringLiteral("io.github.diogochaves.omaseed");

struct Declared
{
    QString comment;
    QStringList parents;
    QStringList globs;
    QString icon;
};

QHash<QString, Declared> readPackage(const QString &path, QString *error)
{
    QHash<QString, Declared> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read %1").arg(path);
        return out;
    }
    QXmlStreamReader xml(&file);
    QString current;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        const QString name = xml.name().toString();
        const QXmlStreamAttributes attributes = xml.attributes();
        if (name == QStringLiteral("mime-type")) {
            current = attributes.value(QStringLiteral("type")).toString();
            out.insert(current, {});
        } else if (current.isEmpty()) {
            continue;
        } else if (name == QStringLiteral("comment")) {
            out[current].comment = xml.readElementText();
        } else if (name == QStringLiteral("sub-class-of")) {
            out[current].parents.append(attributes.value(QStringLiteral("type")).toString());
        } else if (name == QStringLiteral("glob")) {
            out[current].globs.append(attributes.value(QStringLiteral("pattern")).toString());
        } else if (name == QStringLiteral("icon")) {
            out[current].icon = attributes.value(QStringLiteral("name")).toString();
        }
    }
    if (xml.hasError())
        *error = xml.errorString();
    return out;
}

} // namespace

class TestMimeTypes : public QObject
{
    Q_OBJECT

    QTemporaryDir m_prefix;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_prefix.isValid());
        QProcess install;
        install.setProcessChannelMode(QProcess::MergedChannels);
        // A DESTDIR left in the environment would turn the database step off.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("DESTDIR"));
        install.setProcessEnvironment(env);
        install.start(QStringLiteral(OMASEED_CMAKE_COMMAND),
                      {QStringLiteral("--install"),
                       QStringLiteral(OMASEED_TEST_BUILD_DIR),
                       QStringLiteral("--prefix"),
                       m_prefix.path(),
                       QStringLiteral("--component"),
                       QStringLiteral("mime")});
        QVERIFY(install.waitForFinished(60000));
        QVERIFY2(install.exitCode() == 0, install.readAll().constData());
    }

    void thePackageIsInstalled()
    {
        QFile installed(m_prefix.filePath(QStringLiteral("share/mime/packages/") + kAppId +
                                          QStringLiteral(".xml")));
        QFile source(QStringLiteral(OMASEED_DATA_DIR "/") + kAppId + QStringLiteral(".xml"));
        QVERIFY(installed.open(QIODevice::ReadOnly));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(installed.readAll(), source.readAll());
    }

    void declaresTwoTarsWithOmaseedsIcon()
    {
        QString error;
        const QHash<QString, Declared> types = readPackage(
            QStringLiteral(OMASEED_DATA_DIR "/") + kAppId + QStringLiteral(".xml"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(types.size(), 2);
        const Declared backup = types.value(QStringLiteral("application/x-omaseed-backup"));
        QCOMPARE(backup.comment, QStringLiteral("omaseed backup"));
        QCOMPARE(backup.globs, QStringList{QStringLiteral("*.omaseed")});
        QCOMPARE(backup.parents, QStringList{QStringLiteral("application/x-tar")});
        QCOMPARE(backup.icon, kAppId);
        const Declared torrents = types.value(QStringLiteral("application/x-omaseed-torrents"));
        QCOMPARE(torrents.comment, QStringLiteral("omaseed torrents"));
        QCOMPARE(torrents.globs, QStringList{QStringLiteral("*.omaseed-torrents")});
        QCOMPARE(torrents.parents, QStringList{QStringLiteral("application/x-tar")});
        QCOMPARE(torrents.icon, kAppId);
    }

    // D9 a: no double click opens omaseed on a backup.
    void theDesktopEntryDoesNotOpenThem()
    {
        QFile desktop(QStringLiteral(OMASEED_DATA_DIR "/") + kAppId + QStringLiteral(".desktop"));
        QVERIFY(desktop.open(QIODevice::ReadOnly | QIODevice::Text));
        QString mimeLine;
        for (const QByteArray &line : desktop.readAll().split('\n')) {
            if (line.startsWith("MimeType="))
                mimeLine = QString::fromUtf8(line);
        }
        QCOMPARE(mimeLine,
                 QStringLiteral("MimeType=x-scheme-handler/magnet;application/x-bittorrent;"));
    }

    // The database the install built, read the way the desktop reads it (Qt here, through the
    // same shared-mime-info files GIO uses).
    void theDatabaseNamesTheFiles()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("update-mime-database")).isEmpty())
            QSKIP("update-mime-database is not installed (shared-mime-info)");
        const QString mime = m_prefix.filePath(QStringLiteral("share/mime"));
        QVERIFY2(QFile::exists(mime + QStringLiteral("/mime.cache")),
                 "the install did not build the database");
        // Only the temporary prefix: neither this machine's database nor the user's.
        qputenv("XDG_DATA_HOME", m_prefix.filePath(QStringLiteral("share")).toUtf8());
        qputenv("XDG_DATA_DIRS", m_prefix.filePath(QStringLiteral("share")).toUtf8());
        const QMimeDatabase db;
        const QMimeType backup = db.mimeTypeForFile(QStringLiteral("omaseed-2026-10-15.omaseed"),
                                                    QMimeDatabase::MatchExtension);
        QCOMPARE(backup.name(), QStringLiteral("application/x-omaseed-backup"));
        QVERIFY(backup.inherits(QStringLiteral("application/x-tar")));
        QCOMPARE(backup.iconName(), kAppId);
        const QMimeType torrents =
            db.mimeTypeForFile(QStringLiteral("torrents-private-2026-10-15.omaseed-torrents"),
                               QMimeDatabase::MatchExtension);
        QCOMPARE(torrents.name(), QStringLiteral("application/x-omaseed-torrents"));
        QVERIFY(torrents.inherits(QStringLiteral("application/x-tar")));
    }
};

QTEST_GUILESS_MAIN(TestMimeTypes)
#include "tst_mimetypes.moc"
