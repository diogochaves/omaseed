#include "integration/AppIdentity.hpp"
#include "integration/Xdg.hpp"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using omaseed::integration::Addable;
using omaseed::integration::Xdg;

namespace {

QString writeExecutableScript(const QString &dir, const QString &name, const QString &body)
{
    const QString path = dir + QLatin1Char('/') + name;
    QFile file(path);
    [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    Q_ASSERT(opened);
    file.write(body.toUtf8());
    file.close();
    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser |
                        QFileDevice::ExeGroup);
    return path;
}

} // namespace

/// A minimal `org.freedesktop.FileManager1` stand-in registered on a private session bus, so
/// `Xdg::showInFileManager()` can be tested without touching the real Nautilus/FileManager1.
class FakeFileManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    QStringList lastUris;
    QString lastMethod;

public slots:
    void ShowItems(const QStringList &uris, const QString &)
    {
        lastMethod = QStringLiteral("ShowItems");
        lastUris = uris;
    }

    void ShowFolders(const QStringList &uris, const QString &)
    {
        lastMethod = QStringLiteral("ShowFolders");
        lastUris = uris;
    }
};

class TestXdg : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void parseAddableRecognisesMagnetWithDnAndTrackers();
    void parseAddableRecognisesUppercaseHex();
    void parseAddableRecognisesBase32Hash();
    void parseAddableRecognisesBtmh();
    void parseAddableRecognisesHttpTorrentUrl();
    void parseAddableRecognisesPlainHttpsUrl();
    void parseAddableRecognisesFileUrlToTorrent();
    void parseAddableRejectsGarbage();
    void parseAddableTrimsWhitespace();
    void parseAddableListKeepsOrderAndDropsJunk();
    void parseAddableListSplitsLinksOnOneLine(); // INT-14
    void magnetXtMustBeItsOwnParameter();        // INT-14
    void dropListsAFileOnceWhenItsTextNamesItToo();

    void stateFileRoundTrips();

    void isDefaultMagnetHandlerMatchesFakeXdgMime();
    void isDefaultMagnetHandlerReportsMismatch();

    void showInFileManagerCallsShowItemsOverDBus();

private:
    QTemporaryDir m_configDir;
    QByteArray m_originalConfigHome;
    QByteArray m_originalPath;
};

void TestXdg::init()
{
    m_originalConfigHome = qgetenv("XDG_CONFIG_HOME");
    m_originalPath = qgetenv("PATH");
    QVERIFY(m_configDir.isValid());
    qputenv("XDG_CONFIG_HOME", m_configDir.path().toUtf8());
}

void TestXdg::cleanup()
{
    if (m_originalConfigHome.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_originalConfigHome);

    qputenv("PATH", m_originalPath);
}

void TestXdg::parseAddableRecognisesMagnetWithDnAndTrackers()
{
    const QString magnet =
        QStringLiteral("magnet:?xt=urn:btih:c12fe1c06bba254a9dc9f519b335aa7c1367a88a&dn=Some+Name"
                       "&tr=udp%3A%2F%2Ftracker.example%3A80");
    const Addable addable = Xdg::parseAddable(magnet);
    QCOMPARE(addable.kind, Addable::Kind::Magnet);
    QCOMPARE(addable.value, magnet);
}

void TestXdg::parseAddableRecognisesUppercaseHex()
{
    const QString magnet =
        QStringLiteral("magnet:?xt=urn:btih:C12FE1C06BBA254A9DC9F519B335AA7C1367A88A");
    const Addable addable = Xdg::parseAddable(magnet);
    QCOMPARE(addable.kind, Addable::Kind::Magnet);
}

void TestXdg::parseAddableRecognisesBase32Hash()
{
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
    const Addable addable = Xdg::parseAddable(magnet);
    QCOMPARE(addable.kind, Addable::Kind::Magnet);
}

void TestXdg::parseAddableRecognisesBtmh()
{
    const QString magnet = QStringLiteral(
        "magnet:?xt=urn:btmh:1220caf1e1c06bba254a9dc9f519b335aa7c1367a88ac12fe1c06bba254a9dc9f5");
    const Addable addable = Xdg::parseAddable(magnet);
    QCOMPARE(addable.kind, Addable::Kind::Magnet);
}

void TestXdg::parseAddableRecognisesHttpTorrentUrl()
{
    const QString url = QStringLiteral("https://example.com/foo/bar.torrent");
    const Addable addable = Xdg::parseAddable(url);
    QCOMPARE(addable.kind, Addable::Kind::HttpUrl);
    QCOMPARE(addable.value, url);
}

void TestXdg::parseAddableRecognisesPlainHttpsUrl()
{
    const QString url = QStringLiteral("https://example.com/some/page");
    const Addable addable = Xdg::parseAddable(url);
    QCOMPARE(addable.kind, Addable::Kind::HttpUrl);
}

void TestXdg::parseAddableRecognisesFileUrlToTorrent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/example.torrent");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("d4:info1:xe");
    file.close();

    const QString url = QUrl::fromLocalFile(path).toString();
    const Addable addable = Xdg::parseAddable(url);
    QCOMPARE(addable.kind, Addable::Kind::TorrentFile);
    QCOMPARE(addable.value, path);
}

void TestXdg::parseAddableRejectsGarbage()
{
    QCOMPARE(Xdg::parseAddable(QStringLiteral("not a magnet or url")).kind, Addable::Kind::None);
    QCOMPARE(Xdg::parseAddable(QStringLiteral("magnet:?dn=NoHashHere")).kind, Addable::Kind::None);
    QCOMPARE(Xdg::parseAddable(QStringLiteral("ftp://example.com/file.torrent")).kind,
             Addable::Kind::None);
    QCOMPARE(Xdg::parseAddable(QStringLiteral("/no/such/file.torrent")).kind, Addable::Kind::None);
    QCOMPARE(Xdg::parseAddable(QString()).kind, Addable::Kind::None);
}

void TestXdg::parseAddableTrimsWhitespace()
{
    const QString url = QStringLiteral("  https://example.com/page  \t");
    const Addable addable = Xdg::parseAddable(url);
    QCOMPARE(addable.kind, Addable::Kind::HttpUrl);
    QCOMPARE(addable.value, QStringLiteral("https://example.com/page"));
}

void TestXdg::parseAddableListKeepsOrderAndDropsJunk()
{
    const QString magnet =
        QStringLiteral("magnet:?xt=urn:btih:c12fe1c06bba254a9dc9f519b335aa7c1367a88a");
    const QString url = QStringLiteral("https://example.com/a.torrent");
    const QString text = QStringLiteral("%1\njust some junk\n%2\n\n").arg(magnet, url);

    const QList<Addable> addables = Xdg::parseAddableList(text);
    QCOMPARE(addables.size(), 2);
    QCOMPARE(addables.at(0).kind, Addable::Kind::Magnet);
    QCOMPARE(addables.at(0).value, magnet);
    QCOMPARE(addables.at(1).kind, Addable::Kind::HttpUrl);
    QCOMPARE(addables.at(1).value, url);
}

void TestXdg::dropListsAFileOnceWhenItsTextNamesItToo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/dropped.torrent");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("d4:info1:xe");
    file.close();
    const QString magnet =
        QStringLiteral("magnet:?xt=urn:btih:c12fe1c06bba254a9dc9f519b335aa7c1367a88a");

    // As Nautilus fills a drop: the file in the URLs and again, as a URL, in the text.
    const QUrl url = QUrl::fromLocalFile(path);
    Xdg xdg;
    const QList<Addable> addables =
        xdg.addablesFromDrop({url}, url.toString() + QStringLiteral("\r\n") + magnet);
    QCOMPARE(addables.size(), 2);
    QCOMPARE(addables.at(0).kind, Addable::Kind::TorrentFile);
    QCOMPARE(addables.at(0).value, path);
    QCOMPARE(addables.at(1).kind, Addable::Kind::Magnet);
}

void TestXdg::parseAddableListSplitsLinksOnOneLine()
{
    const QString a =
        QStringLiteral("magnet:?xt=urn:btih:c12fe1c06bba254a9dc9f519b335aa7c1367a88a");
    const QString b =
        QStringLiteral("magnet:?xt=urn:btih:d12fe1c06bba254a9dc9f519b335aa7c1367a88b");
    const QList<Addable> addables =
        Xdg::parseAddableList(QStringLiteral("%1 %2\thttps://example.com/c.torrent").arg(a, b));
    QCOMPARE(addables.size(), 3);
    QCOMPARE(addables.at(0).value, a);
    QCOMPARE(addables.at(1).value, b);
    QCOMPARE(addables.at(2).kind, Addable::Kind::HttpUrl);

    // A local path with spaces stays one file.
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/my movie.torrent");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    const QList<Addable> one = Xdg::parseAddableList(path);
    QCOMPARE(one.size(), 1);
    QCOMPARE(one.at(0).kind, Addable::Kind::TorrentFile);
    QCOMPARE(one.at(0).value, path);
}

void TestXdg::magnetXtMustBeItsOwnParameter()
{
    const QString hash = QStringLiteral("c12fe1c06bba254a9dc9f519b335aa7c1367a88a");
    QCOMPARE(Xdg::parseAddable(QStringLiteral("magnet:?foo_xt=urn:btih:%1").arg(hash)).kind,
             Addable::Kind::None);
    QCOMPARE(Xdg::parseAddable(QStringLiteral("magnet:?dn=x&xt=urn:btih:%1").arg(hash)).kind,
             Addable::Kind::Magnet);
}

void TestXdg::stateFileRoundTrips()
{
    Xdg xdg;

    const QString path = m_configDir.path() + QStringLiteral("/omaseed/state.json");
    QVERIFY(!QFile::exists(path));

    QVERIFY(xdg.firstRunPromptNeeded());
    QVERIFY(QFile::exists(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject initial = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QCOMPARE(initial.value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(!initial.contains(QStringLiteral("magnetHandlerAsked")));

    xdg.setFirstRunPromptAsked();
    QVERIFY(!xdg.firstRunPromptNeeded());

    QFile fileAfter(path);
    QVERIFY(fileAfter.open(QIODevice::ReadOnly));
    const QJsonObject after = QJsonDocument::fromJson(fileAfter.readAll()).object();
    QCOMPARE(after.value(QStringLiteral("magnetHandlerAsked")).toBool(), true);
    QCOMPARE(after.value(QStringLiteral("version")).toInt(), 1);
}

void TestXdg::isDefaultMagnetHandlerMatchesFakeXdgMime()
{
    QTemporaryDir binDir;
    QVERIFY(binDir.isValid());
    const QString ourId =
        omaseed::integration::AppIdentity::desktopFileName() + QStringLiteral(".desktop");
    writeExecutableScript(binDir.path(),
                          QStringLiteral("xdg-mime"),
                          QStringLiteral("#!/bin/sh\necho %1\n").arg(ourId));
    qputenv("PATH",
            (binDir.path() + QStringLiteral(":") + QString::fromUtf8(m_originalPath)).toUtf8());

    Xdg xdg;
    QSignalSpy spy(&xdg, &Xdg::isDefaultMagnetHandlerResult);
    xdg.isDefaultMagnetHandler();
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.at(0).at(0).toBool(), true);
    QCOMPARE(spy.at(0).at(1).toString(), ourId);
}

void TestXdg::isDefaultMagnetHandlerReportsMismatch()
{
    QTemporaryDir binDir;
    QVERIFY(binDir.isValid());
    writeExecutableScript(binDir.path(),
                          QStringLiteral("xdg-mime"),
                          QStringLiteral("#!/bin/sh\necho someother.desktop\n"));
    qputenv("PATH",
            (binDir.path() + QStringLiteral(":") + QString::fromUtf8(m_originalPath)).toUtf8());

    Xdg xdg;
    QSignalSpy spy(&xdg, &Xdg::isDefaultMagnetHandlerResult);
    xdg.isDefaultMagnetHandler();
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.at(0).at(0).toBool(), false);
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("someother.desktop"));
}

void TestXdg::showInFileManagerCallsShowItemsOverDBus()
{
    QProcess daemon;
    daemon.start(QStringLiteral("dbus-daemon"),
                 {QStringLiteral("--session"),
                  QStringLiteral("--print-address"),
                  QStringLiteral("--nofork")});
    QVERIFY(daemon.waitForStarted(5000));
    QVERIFY(daemon.waitForReadyRead(5000));
    const QString address = QString::fromUtf8(daemon.readLine()).trimmed();
    QVERIFY(!address.isEmpty());

    QDBusConnection bus = QDBusConnection::connectToBus(address, QStringLiteral("tst_xdg"));
    QVERIFY(bus.isConnected());
    QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.FileManager1")));

    FakeFileManager manager;
    QVERIFY(bus.registerObject(QStringLiteral("/org/freedesktop/FileManager1"),
                               &manager,
                               QDBusConnection::ExportAllSlots));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/highlighted.torrent");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("d4:info1:xe");
    file.close();

    Xdg xdg(bus);
    QSignalSpy spy(&xdg, &Xdg::revealFinished);
    xdg.showInFileManager(path);
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    QCOMPARE(manager.lastMethod, QStringLiteral("ShowItems"));
    QCOMPARE(manager.lastUris, QStringList{QUrl::fromLocalFile(path).toString()});

    bus.unregisterObject(QStringLiteral("/org/freedesktop/FileManager1"));
    bus.unregisterService(QStringLiteral("org.freedesktop.FileManager1"));
    QDBusConnection::disconnectFromBus(QStringLiteral("tst_xdg"));

    daemon.terminate();
    daemon.waitForFinished(2000);
}

QTEST_MAIN(TestXdg)

#include "tst_xdg.moc"
