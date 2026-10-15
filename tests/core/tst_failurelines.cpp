// docs/32 §2 and §5 (#55): the failures qBittorrent only writes to its log, taken apart and
// worded. Every English line below was copied from the :8081 playground (qBittorrent 5.2.3,
// libtorrent 2.1.1) on 2026-10-15, after a move out of a read-only folder, a file and a
// folder rename inside one, a web seed on a local server that answers 404, an export folder
// that can't be written and a delete with files from a read-only folder. The part file line
// is qBittorrent's template (sessionimpl.cpp), filled in by hand: nothing here could cause it.

#include "core/TorrentProblem.hpp"

#include <QTest>

using omaseed::core::ProblemLine;
using omaseed::core::TorrentFailure;
namespace ProblemMatch = omaseed::core::ProblemMatch;

Q_DECLARE_METATYPE(omaseed::core::ProblemLine::Kind)

namespace {

const QString kRoot = QStringLiteral("/home/chaves/.local/share/omaseed-test/downloads/log55");

const QString kMove = QStringLiteral(
    "Failed to move torrent. Torrent: \"log55-move.bin\". Source: "
    "\"/home/chaves/.local/share/omaseed-test/downloads/log55/src\". Destination: "
    "\"/home/chaves/.local/share/omaseed-test/downloads/log55/dest\". Reason: \"37a783 storage "
    "move failed. file_rename "
    "(/home/chaves/.local/share/omaseed-test/downloads/log55/src/log55-move.bin): Permission "
    "denied\"");

const QString kRename =
    QStringLiteral("File rename failed. Torrent: \"log55-rename\", file: \"log55-rename/b.txt\", "
                   "reason: \"Permission denied\"");

const QString kUrlSeedError = QStringLiteral(
    "Received error message from URL seed. Torrent: \"log55-webseed.bin\". URL: "
    "\"http://127.0.0.1:26955/pub/log55-webseed.bin\". Message: \"404 File not found\"");

const QString kUrlSeedConnection = QStringLiteral(
    "URL seed connection failed. Torrent: \"omaseed-webseed-live.txt\". URL: "
    "\"https://mirror.invalid/omaseed-webseed-live/\". Error: \"a67c70 url seed "
    "(https://mirror.invalid/omaseed-webseed-live/) failed: Host not found (authoritative)\"");

const QString kExport =
    QStringLiteral("Failed to export torrent. Torrent: \"log55-webseed.bin\". Destination: "
                   "\"/home/chaves/.local/share/omaseed-test/downloads/log55/ro-export/"
                   "log55-webseed.bin.torrent\". Reason: \"Permission denied\"");

const QString kContent = QStringLiteral(
    "Failed to remove torrent content. Torrent: \"log55-delete.bin\". Error: \"Permission "
    "denied\"");

const QString kPartfile = QStringLiteral("Failed to remove partfile. Torrent: \"Field Recordings "
                                         "Vol. 3 [FLAC]\". Reason: \"Directory not empty\".");

ProblemLine renameOf(const QString &file)
{
    return ProblemLine::parse(
        QStringLiteral("File rename failed. Torrent: \"log55-rename\", file: \"%1\", reason: "
                       "\"Permission denied\"")
            .arg(file));
}

} // namespace

class TestFailureLines : public QObject
{
    Q_OBJECT

private slots:
    void parsesEachLine_data();
    void parsesEachLine();
    void otherLinesStayUnknown();
    void aMoveTellsSourceFromDestinationByTheFile();
    void wordsAMove();
    void wordsAMoveWithNoFile();
    void wordsARename();
    void wordsAFolderRename();
    void wordsAPartfile();
    void webSeedStatusIsShort_data();
    void webSeedStatusIsShort();
    void webSeedUrlsMatchTheirFiles();
    void matchesAMoveByItsPrefix();
};

void TestFailureLines::parsesEachLine_data()
{
    QTest::addColumn<QString>("message");
    QTest::addColumn<ProblemLine::Kind>("kind");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("prefix");
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("destination");
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("torrentFile");
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("operation");
    QTest::addColumn<QString>("systemMessage");

    QTest::newRow("move") << kMove << ProblemLine::Kind::MoveFailed
                          << QStringLiteral("log55-move.bin") << QStringLiteral("37a783")
                          << kRoot + QStringLiteral("/src") << kRoot + QStringLiteral("/dest")
                          << kRoot + QStringLiteral("/src/log55-move.bin") << QString() << QString()
                          << QStringLiteral("file_rename") << QStringLiteral("Permission denied");
    QTest::newRow("rename") << kRename << ProblemLine::Kind::RenameFailed
                            << QStringLiteral("log55-rename") << QString() << QString() << QString()
                            << QString() << QStringLiteral("log55-rename/b.txt") << QString()
                            << QString() << QStringLiteral("Permission denied");
    // A single-file torrent's file is named as the torrent is.
    QTest::newRow("rename, one file")
        << QStringLiteral("File rename failed. Torrent: \"a.bin\", file: \"a.bin\", reason: "
                          "\"Permission denied\"")
        << ProblemLine::Kind::RenameFailed << QStringLiteral("a.bin") << QString() << QString()
        << QString() << QString() << QStringLiteral("a.bin") << QString() << QString()
        << QStringLiteral("Permission denied");
    QTest::newRow("url seed, 404")
        << kUrlSeedError << ProblemLine::Kind::UrlSeedFailed << QStringLiteral("log55-webseed.bin")
        << QString() << QString() << QString() << QString() << QString()
        << QStringLiteral("http://127.0.0.1:26955/pub/log55-webseed.bin") << QString()
        << QStringLiteral("404 File not found");
    QTest::newRow("url seed, no host")
        << kUrlSeedConnection << ProblemLine::Kind::UrlSeedFailed
        << QStringLiteral("omaseed-webseed-live.txt") << QStringLiteral("a67c70") << QString()
        << QString() << QString() << QString()
        << QStringLiteral("https://mirror.invalid/omaseed-webseed-live/") << QString()
        << QStringLiteral("Host not found (authoritative)");
    QTest::newRow("export") << kExport << ProblemLine::Kind::ExportFailed
                            << QStringLiteral("log55-webseed.bin") << QString() << QString()
                            << QString()
                            << kRoot + QStringLiteral("/ro-export/log55-webseed.bin.torrent")
                            << QString() << QString() << QString()
                            << QStringLiteral("Permission denied");
    QTest::newRow("content") << kContent << ProblemLine::Kind::ContentRemovalFailed
                             << QStringLiteral("log55-delete.bin") << QString() << QString()
                             << QString() << QString() << QString() << QString() << QString()
                             << QStringLiteral("Permission denied");
    QTest::newRow("partfile") << kPartfile << ProblemLine::Kind::PartfileFailed
                              << QStringLiteral("Field Recordings Vol. 3 [FLAC]") << QString()
                              << QString() << QString() << QString() << QString() << QString()
                              << QString() << QStringLiteral("Directory not empty");

    // qBittorrent in French: its words translated (these are made up here; what matters is
    // that none of them is read), its quoted segments and libtorrent's text as they are. The
    // translation may put the destination before the source.
    QTest::newRow("move, French")
        << QStringLiteral("Échec du déplacement du torrent. Torrent : \"Film\". Destination : "
                          "\"/m/neuf\". Source : \"/m/vieux\". Raison : \"0a1b2c storage move "
                          "failed. file_rename (/m/vieux/Film/a.mkv): Permission denied\"")
        << ProblemLine::Kind::MoveFailed << QStringLiteral("Film") << QStringLiteral("0a1b2c")
        << QStringLiteral("/m/vieux") << QStringLiteral("/m/neuf")
        << QStringLiteral("/m/vieux/Film/a.mkv") << QString() << QString()
        << QStringLiteral("file_rename") << QStringLiteral("Permission denied");
    QTest::newRow("rename, French")
        << QStringLiteral("Échec du renommage du fichier. Torrent : \"Film\", fichier : "
                          "\"Film/a.mkv\", raison : \"Permission non accordée\"")
        << ProblemLine::Kind::RenameFailed << QStringLiteral("Film") << QString() << QString()
        << QString() << QString() << QStringLiteral("Film/a.mkv") << QString() << QString()
        << QStringLiteral("Permission non accordée");
    QTest::newRow("url seed, French")
        << QStringLiteral("Message d'erreur reçu de la source URL. Torrent : \"Film\". URL : "
                          "\"https://miroir.example/pub/Film/a.mkv\". Message : \"404 Not "
                          "Found\"")
        << ProblemLine::Kind::UrlSeedFailed << QStringLiteral("Film") << QString() << QString()
        << QString() << QString() << QString()
        << QStringLiteral("https://miroir.example/pub/Film/a.mkv") << QString()
        << QStringLiteral("404 Not Found");
    QTest::newRow("url seed connection, French")
        << QStringLiteral("La connexion à la source URL a échoué. Torrent : \"Film\". URL : "
                          "\"https://miroir.example/pub/\". Erreur : \"0a1b2c url seed "
                          "(https://miroir.example/pub/) failed: Connection refused\"")
        << ProblemLine::Kind::UrlSeedFailed << QStringLiteral("Film") << QStringLiteral("0a1b2c")
        << QString() << QString() << QString() << QString()
        << QStringLiteral("https://miroir.example/pub/") << QString()
        << QStringLiteral("Connection refused");
    QTest::newRow("export, French")
        << QStringLiteral("Échec de l'exportation du torrent. Torrent : \"Film\". Destination : "
                          "\"/m/export/Film.torrent\". Raison : \"Permission non accordée\"")
        << ProblemLine::Kind::ExportFailed << QStringLiteral("Film") << QString() << QString()
        << QString() << QStringLiteral("/m/export/Film.torrent") << QString() << QString()
        << QString() << QStringLiteral("Permission non accordée");
}

void TestFailureLines::parsesEachLine()
{
    QFETCH(QString, message);
    QFETCH(ProblemLine::Kind, kind);
    QFETCH(QString, name);
    QFETCH(QString, prefix);
    QFETCH(QString, source);
    QFETCH(QString, destination);
    QFETCH(QString, file);
    QFETCH(QString, torrentFile);
    QFETCH(QString, url);
    QFETCH(QString, operation);
    QFETCH(QString, systemMessage);

    const ProblemLine line = ProblemLine::parse(message);
    QCOMPARE(line.kind, kind);
    QVERIFY(line.isFailure());
    QCOMPARE(line.quoted.value(0), name);
    QCOMPARE(line.hashPrefix, prefix);
    QCOMPARE(line.source, source);
    QCOMPARE(line.destination, destination);
    QCOMPARE(line.file, file);
    QCOMPARE(line.torrentFile, torrentFile);
    QCOMPARE(line.url, url);
    QCOMPARE(line.operation, operation);
    QCOMPARE(line.systemMessage, systemMessage);
}

void TestFailureLines::otherLinesStayUnknown()
{
    for (const QString &message :
         {QStringLiteral("Detected inconsistent data: invalid category. Torrent: \"a\". "
                         "Category: \"b\""),
          // A translated part file or content line has no shape of its own: left alone.
          QStringLiteral("Échec de la suppression du fichier partiel. Torrent : \"a\". Raison : "
                         "\"x\"."),
          // Three segments whose middle is neither a URL, a .torrent nor a file of the torrent.
          QStringLiteral("Failed to add peer. Torrent: \"a\". Peer: \"1.2.3.4:5\". Error: "
                         "\"x\""),
          QStringLiteral("Moved torrent successfully. Torrent: \"a\". Destination: \"/b\""),
          QStringLiteral("WebUI Set location: moving \"a\", from \"/b\" to \"/c\"")}) {
        const ProblemLine line = ProblemLine::parse(message);
        QVERIFY2(line.kind == ProblemLine::Kind::Unknown, qPrintable(message));
        QVERIFY(!line.isFailure());
    }
    // #54's lines are not failures.
    QVERIFY(!ProblemLine::parse(QStringLiteral("File error alert. Torrent: \"a\". File: \"/d/a\". "
                                               "Reason: \"ad91d5 file_open (/d/a) error: x\""))
                 .isFailure());
}

// Moving out of a folder into one inside it: the file lies under both, so the deepest is the
// source. And the other way round.
void TestFailureLines::aMoveTellsSourceFromDestinationByTheFile()
{
    ProblemLine line = ProblemLine::parse(
        QStringLiteral("Failed to move torrent. Torrent: \"a\". Source: \"/d\". Destination: "
                       "\"/d/sub\". Reason: \"0a1b2c storage move failed. file_rename (/d/a): "
                       "Permission denied\""));
    QCOMPARE(line.source, QStringLiteral("/d"));
    QCOMPARE(line.destination, QStringLiteral("/d/sub"));

    line = ProblemLine::parse(
        QStringLiteral("Failed to move torrent. Torrent: \"a\". Source: \"/d/sub\". Destination: "
                       "\"/d\". Reason: \"0a1b2c storage move failed. file_rename (/d/sub/a): "
                       "Permission denied\""));
    QCOMPARE(line.source, QStringLiteral("/d/sub"));
    QCOMPARE(line.destination, QStringLiteral("/d"));
}

void TestFailureLines::wordsAMove()
{
    const TorrentFailure failure = ProblemMatch::describeMove(ProblemLine::parse(kMove));
    QCOMPARE(failure.kind, QStringLiteral("move"));
    QCOMPARE(failure.headline, QStringLiteral("The move to log55/dest didn't happen"));
    QCOMPARE(failure.detail, QStringLiteral("Permission denied, moving log55-move.bin"));
    QCOMPARE(failure.folder, QStringLiteral("Its files are still in ") + kRoot + u"/src");
    QCOMPARE(failure.destination, kRoot + QStringLiteral("/dest"));
    QCOMPARE(failure.actions, (QStringList{QStringLiteral("location"), QStringLiteral("folder")}));
    QCOMPARE(ProblemMatch::lastTwoFolders(QStringLiteral("/a/b/c/")), QStringLiteral("b/c"));
    QCOMPARE(ProblemMatch::lastTwoFolders(QStringLiteral("/c")), QStringLiteral("c"));
}

void TestFailureLines::wordsAMoveWithNoFile()
{
    const TorrentFailure failure = ProblemMatch::describeMove(ProblemLine::parse(
        QStringLiteral("Failed to move torrent. Torrent: \"a\". Source: \"/d\". Destination: "
                       "\"/e/f\". Reason: \"0a1b2c storage move failed. No space left on "
                       "device\"")));
    QCOMPARE(failure.headline, QStringLiteral("The move to e/f didn't happen"));
    QCOMPARE(failure.detail, QStringLiteral("No space left on device"));
    QCOMPARE(failure.folder, QStringLiteral("Its files are still in /d"));
}

void TestFailureLines::wordsARename()
{
    const TorrentFailure failure =
        ProblemMatch::describeRename({ProblemLine::parse(kRename)}, kRoot + QStringLiteral("/src"));
    QCOMPARE(failure.kind, QStringLiteral("rename"));
    QCOMPARE(failure.headline, QStringLiteral("A file kept its old name"));
    QCOMPARE(failure.detail, QStringLiteral("Permission denied, renaming log55-rename/b.txt"));
    QCOMPARE(failure.folder, kRoot + QStringLiteral("/src/log55-rename"));
    QCOMPARE(failure.torrentFile, QStringLiteral("log55-rename/b.txt"));
    QCOMPARE(failure.actions, (QStringList{QStringLiteral("rename"), QStringLiteral("folder")}));
}

// F2: a whole folder's rename names the first file and counts the rest.
void TestFailureLines::wordsAFolderRename()
{
    const TorrentFailure failure =
        ProblemMatch::describeRename({renameOf(QStringLiteral("log55-rename/a.txt")),
                                      renameOf(QStringLiteral("log55-rename/b.txt")),
                                      renameOf(QStringLiteral("log55-rename/c.txt"))},
                                     kRoot + QStringLiteral("/src/"));
    QCOMPARE(failure.headline, QStringLiteral("3 files kept their old names"));
    QCOMPARE(failure.detail,
             QStringLiteral("Permission denied, renaming log55-rename/a.txt and 2 more"));
    QCOMPARE(failure.torrentFile, QStringLiteral("log55-rename/a.txt"));
    QCOMPARE(failure.folder, kRoot + QStringLiteral("/src/log55-rename"));
}

void TestFailureLines::wordsAPartfile()
{
    const TorrentFailure failure =
        ProblemMatch::describePartfile(ProblemLine::parse(kPartfile), QStringLiteral("/mnt/music"));
    QCOMPARE(failure.kind, QStringLiteral("partfile"));
    QCOMPARE(failure.headline, QStringLiteral("Its part file couldn't be removed"));
    QCOMPARE(failure.detail, QStringLiteral("Directory not empty"));
    QCOMPARE(failure.folder,
             QStringLiteral("/mnt/music · the .parts file holds pieces of files you skipped"));
    QCOMPARE(failure.actions, QStringList{QStringLiteral("folder")});
}

void TestFailureLines::webSeedStatusIsShort_data()
{
    QTest::addColumn<QString>("message");
    QTest::addColumn<QString>("status");
    QTest::newRow("404") << kUrlSeedError << QStringLiteral("404 File not found");
    QTest::newRow("no host") << kUrlSeedConnection << QStringLiteral("host not found");
    QTest::newRow("refused")
        << QStringLiteral("URL seed connection failed. Torrent: \"a\". URL: \"http://h/\". Error: "
                          "\"0a1b2c url seed (http://h/) failed: Connection refused\"")
        << QStringLiteral("connection refused");
    QTest::newRow("no libtorrent text")
        << QStringLiteral("URL seed connection failed. Torrent: \"a\". URL: \"http://x\". "
                          "Error: \"timed out\"")
        << QStringLiteral("timed out");
}

void TestFailureLines::webSeedStatusIsShort()
{
    QFETCH(QString, message);
    QFETCH(QString, status);
    QCOMPARE(ProblemMatch::webSeedStatusText(ProblemLine::parse(message)), status);
}

void TestFailureLines::webSeedUrlsMatchTheirFiles()
{
    const QString seed = QStringLiteral("http://127.0.0.1:26955/pub/");
    QVERIFY(ProblemMatch::isWebSeedUrl(
        QStringLiteral("http://127.0.0.1:26955/pub/log55-webseed.bin"), seed));
    QVERIFY(ProblemMatch::isWebSeedUrl(seed, seed));
    QVERIFY(!ProblemMatch::isWebSeedUrl(QStringLiteral("http://127.0.0.1:26955/other/x"), seed));
    // A seed that names a file is matched by that URL alone.
    QVERIFY(!ProblemMatch::isWebSeedUrl(QStringLiteral("http://h/a.bin2"),
                                        QStringLiteral("http://h/a.bin")));
    QVERIFY(!ProblemMatch::isWebSeedUrl(QString(), seed));
}

// A move line carries libtorrent's prefix, so it finds its torrent among same-named ones.
void TestFailureLines::matchesAMoveByItsPrefix()
{
    omaseed::core::Torrent a;
    a.hash = QStringLiteral("37a783e4a8f6f3a92c4226d38ba1d69bdbfa6164");
    a.infohashV2 = QStringLiteral("37a783e4");
    a.name = QStringLiteral("log55-move.bin");
    a.savePath = kRoot + QStringLiteral("/src");
    omaseed::core::Torrent b = a;
    b.hash = QStringLiteral("ffffffe4a8f6f3a92c4226d38ba1d69bdbfa6164");
    b.infohashV2 = QStringLiteral("ffffff00");
    bool ambiguous = true;
    QCOMPARE(ProblemMatch::match(ProblemLine::parse(kMove), {a, b}, &ambiguous),
             QStringList{a.hash});
    QVERIFY(!ambiguous);
}

QTEST_GUILESS_MAIN(TestFailureLines)
#include "tst_failurelines.moc"
