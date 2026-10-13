// docs/28 §2 and §5 (#54): qBittorrent's log lines taken apart, tied to their torrent and
// worded. The first lines are copied from the :8081 playground (qBittorrent 5.2.3,
// libtorrent 2.1.1) after `swarm.sh break error` and `break missing`.

#include "core/TorrentProblem.hpp"

#include <QTest>

using omaseed::core::ProblemLine;
using omaseed::core::Torrent;
using omaseed::core::TorrentProblem;
namespace ProblemMatch = omaseed::core::ProblemMatch;

namespace {

const QString kFileError = QStringLiteral(
    "File error alert. Torrent: \"unwritable-fixture.bin\". File: "
    "\"/home/chaves/.local/share/omaseed-test/downloads/swarm-readonly/unwritable-fixture.bin\". "
    "Reason: \"ad91d5 file_open "
    "(/home/chaves/.local/share/omaseed-test/downloads/swarm-readonly/unwritable-fixture.bin) "
    "error: Permission denied\"");

const QString kRestoreFailed = QStringLiteral(
    "Failed to restore torrent. Files were probably moved or storage isn't accessible. Torrent: "
    "\"tiny-notes.txt\". Reason: \"81f2da fast resume rejected. "
    "file_stat(/home/chaves/.local/share/omaseed-test/downloads/swarm/tiny-notes.txt): "
    "mismatching file size\"");

Torrent torrent(const QString &hash,
                const QString &name,
                const QString &savePath,
                const QString &v2 = QString())
{
    Torrent t;
    t.hash = hash;
    t.infohashV1 = hash;
    t.infohashV2 = v2;
    t.name = name;
    t.savePath = savePath;
    t.contentPath = savePath + QLatin1Char('/') + name;
    return t;
}

} // namespace

class TestTorrentProblem : public QObject
{
    Q_OBJECT

private slots:
    void parsesAFileError();
    void parsesARestoreFailure();
    void parsesATranslatedLine();
    void parsesLibtorrent20sNameHead();
    void parsesTheEnglishTorrentErroredLine();
    void ignoresOtherWarnings();
    void matchesByHashPrefixOverTheName();
    void prefixUsesV2WhenThereIsOne();
    void matchesByNameWithoutAPrefix();
    void twinsAreToldApartByTheFilePath();
    void twinsInOneFolderAreAmbiguous();
    void aLineForAGoneTorrentMatchesNothing();
    void wordsEachCause_data();
    void wordsEachCause();
    void wordsAMissingFilesLine();
    void unknownMessagesKeepQbittorrentsWords();
    void notLoggedSaysSo();
};

void TestTorrentProblem::parsesAFileError()
{
    const ProblemLine line = ProblemLine::parse(kFileError);
    QCOMPARE(line.kind, ProblemLine::Kind::FileError);
    QCOMPARE(line.hashPrefix, QStringLiteral("ad91d5"));
    QVERIFY(line.libtorrentName.isEmpty());
    QCOMPARE(line.operation, QStringLiteral("file_open"));
    QCOMPARE(line.file,
             QStringLiteral("/home/chaves/.local/share/omaseed-test/downloads/swarm-readonly/"
                            "unwritable-fixture.bin"));
    QCOMPARE(line.systemMessage, QStringLiteral("Permission denied"));
    QVERIFY(line.quoted.contains(QStringLiteral("unwritable-fixture.bin")));
}

void TestTorrentProblem::parsesARestoreFailure()
{
    const ProblemLine line = ProblemLine::parse(kRestoreFailed);
    QCOMPARE(line.kind, ProblemLine::Kind::RestoreFailed);
    QCOMPARE(line.hashPrefix, QStringLiteral("81f2da"));
    QCOMPARE(line.operation, QStringLiteral("file_stat"));
    QCOMPARE(
        line.file,
        QStringLiteral("/home/chaves/.local/share/omaseed-test/downloads/swarm/tiny-notes.txt"));
    QCOMPARE(line.systemMessage, QStringLiteral("mismatching file size"));
    QCOMPARE(line.quoted, QStringList{QStringLiteral("tiny-notes.txt")});
}

// qBittorrent translates its part; libtorrent's part stays English.
void TestTorrentProblem::parsesATranslatedLine()
{
    const ProblemLine line = ProblemLine::parse(
        QStringLiteral("Alerte d'erreur de fichier. Torrent : « x » ignored, Torrent : \"Film "
                       "(2026)\". Fichier : \"/m/Film (2026)/a.mkv\". Raison : \"0a1b2c file_write "
                       "(/m/Film (2026)/a.mkv) error: No space left on device\""));
    QCOMPARE(line.kind, ProblemLine::Kind::FileError);
    QCOMPARE(line.hashPrefix, QStringLiteral("0a1b2c"));
    QCOMPARE(line.file, QStringLiteral("/m/Film (2026)/a.mkv"));
    QCOMPARE(line.systemMessage, QStringLiteral("No space left on device"));
    QVERIFY(line.quoted.contains(QStringLiteral("Film (2026)")));
}

void TestTorrentProblem::parsesLibtorrent20sNameHead()
{
    const ProblemLine line = ProblemLine::parse(QStringLiteral(
        "File error alert. Torrent: \"Open Movie Night\". File: \"/m/a.mkv\". "
        "Reason: \"Open Movie Night file_read (/m/a.mkv) error: Input/output error\""));
    QCOMPARE(line.kind, ProblemLine::Kind::FileError);
    QVERIFY(line.hashPrefix.isEmpty());
    QCOMPARE(line.libtorrentName, QStringLiteral("Open Movie Night"));
    QCOMPARE(line.operation, QStringLiteral("file_read"));
}

void TestTorrentProblem::parsesTheEnglishTorrentErroredLine()
{
    ProblemLine line = ProblemLine::parse(QStringLiteral(
        "Torrent errored. Torrent: \"x.iso\". Error: \"No such file or directory\""));
    QCOMPARE(line.kind, ProblemLine::Kind::TorrentErrored);
    QCOMPARE(line.quoted, QStringList{QStringLiteral("x.iso")});
    QCOMPARE(line.systemMessage, QStringLiteral("No such file or directory"));

    // Upload-only mode wraps the reason once more.
    line = ProblemLine::parse(QStringLiteral(
        "Torrent errored. Torrent: \"x.iso\". Error: \"Couldn't write to file. Reason: "
        "\"Read-only file system\". Torrent is now in \"upload only\" mode.\""));
    QCOMPARE(line.kind, ProblemLine::Kind::TorrentErrored);
    QCOMPARE(line.systemMessage, QStringLiteral("Read-only file system"));
}

void TestTorrentProblem::ignoresOtherWarnings()
{
    for (const QString &message :
         {QStringLiteral("Detected inconsistent data: invalid category. Torrent: \"a\". "
                         "Category: \"b\""),
          QStringLiteral("URL seed connection failed. Torrent: \"a\". URL: \"http://x\". "
                         "Error: \"timed out\""),
          QStringLiteral("WebUI: Now listening on IP: 127.0.0.1, port: 8081"),
          QString()}) {
        QCOMPARE(ProblemLine::parse(message).kind, ProblemLine::Kind::Unknown);
    }
}

void TestTorrentProblem::matchesByHashPrefixOverTheName()
{
    // Two torrents of the same name: the prefix alone says which.
    const QList<Torrent> torrents{
        torrent(QStringLiteral("ad91d5aa00"),
                QStringLiteral("unwritable-fixture.bin"),
                QStringLiteral("/a")),
        torrent(QStringLiteral("ffffff0000"),
                QStringLiteral("unwritable-fixture.bin"),
                QStringLiteral("/b")),
    };
    bool ambiguous = true;
    QCOMPARE(ProblemMatch::match(ProblemLine::parse(kFileError), torrents, &ambiguous),
             QStringList{QStringLiteral("ad91d5aa00")});
    QVERIFY(!ambiguous);
}

void TestTorrentProblem::prefixUsesV2WhenThereIsOne()
{
    // A hybrid: libtorrent prints the v2 hash's prefix, not the v1 the API keys it by.
    const QList<Torrent> torrents{
        torrent(QStringLiteral("88f8d6f9ae"),
                QStringLiteral("other-name"),
                QStringLiteral("/a"),
                QStringLiteral("ad91d5c42f9226")),
    };
    QCOMPARE(ProblemMatch::match(ProblemLine::parse(kFileError), torrents, nullptr),
             QStringList{QStringLiteral("88f8d6f9ae")});
}

void TestTorrentProblem::matchesByNameWithoutAPrefix()
{
    const ProblemLine line = ProblemLine::parse(
        QStringLiteral("File error alert. Torrent: \"b\". File: \"/x/b\". "
                       "Reason: \"b file_open (/x/b) error: Permission denied\""));
    const QList<Torrent> torrents{
        torrent(QStringLiteral("1"), QStringLiteral("a"), QStringLiteral("/x")),
        torrent(QStringLiteral("2"), QStringLiteral("b"), QStringLiteral("/x"))};
    QCOMPARE(ProblemMatch::match(line, torrents, nullptr), QStringList{QStringLiteral("2")});
}

void TestTorrentProblem::twinsAreToldApartByTheFilePath()
{
    const ProblemLine line = ProblemLine::parse(
        QStringLiteral("File error alert. Torrent: \"Album\". File: \"/music/b/Album/01.flac\". "
                       "Reason: \"Album file_read (/music/b/Album/01.flac) error: Input/output "
                       "error\""));
    const QList<Torrent> torrents{
        torrent(QStringLiteral("1"), QStringLiteral("Album"), QStringLiteral("/music/a")),
        torrent(QStringLiteral("2"), QStringLiteral("Album"), QStringLiteral("/music/b")),
    };
    bool ambiguous = true;
    QCOMPARE(ProblemMatch::match(line, torrents, &ambiguous), QStringList{QStringLiteral("2")});
    QVERIFY(!ambiguous);
}

void TestTorrentProblem::twinsInOneFolderAreAmbiguous()
{
    const ProblemLine line = ProblemLine::parse(
        QStringLiteral("File error alert. Torrent: \"Album\". File: \"/music/Album/01.flac\". "
                       "Reason: \"Album file_read (/music/Album/01.flac) error: Input/output "
                       "error\""));
    const QList<Torrent> torrents{
        torrent(QStringLiteral("1"), QStringLiteral("Album"), QStringLiteral("/music")),
        torrent(QStringLiteral("2"), QStringLiteral("Album"), QStringLiteral("/music")),
    };
    bool ambiguous = false;
    QCOMPARE(ProblemMatch::match(line, torrents, &ambiguous),
             (QStringList{QStringLiteral("1"), QStringLiteral("2")}));
    QVERIFY(ambiguous);
}

void TestTorrentProblem::aLineForAGoneTorrentMatchesNothing()
{
    const QList<Torrent> torrents{
        torrent(QStringLiteral("123456"), QStringLiteral("something else"), QStringLiteral("/a"))};
    QVERIFY(ProblemMatch::match(ProblemLine::parse(kFileError), torrents, nullptr).isEmpty());
}

void TestTorrentProblem::wordsEachCause_data()
{
    QTest::addColumn<QString>("operation");
    QTest::addColumn<QString>("message");
    QTest::addColumn<QString>("headline");
    QTest::addColumn<QString>("cause");
    QTest::addColumn<QStringList>("actions");

    const QStringList locationStart{QStringLiteral("location"), QStringLiteral("start")};
    QTest::newRow("no access") << "file_open" << "Permission denied"
                               << "Can't write to its save folder" << "no access" << locationStart;
    QTest::newRow("no read access") << "file_read" << "Permission denied" << "Can't read its files"
                                    << "no access" << locationStart;
    QTest::newRow("read-only") << "file_write" << "Read-only file system"
                               << "Its save folder is on a read-only disk" << "read-only"
                               << locationStart;
    QTest::newRow("full") << "file_write" << "No space left on device" << "The disk is full"
                          << "disk full" << locationStart;
    QTest::newRow("gone") << "file_open" << "No such file or directory"
                          << "A file or folder it needs is gone" << "missing"
                          << QStringList{QStringLiteral("location"), QStringLiteral("recheck")};
    QTest::newRow("read failed") << "file_read" << "Input/output error" << "The disk failed a read"
                                 << "disk error"
                                 << QStringList{QStringLiteral("recheck"), QStringLiteral("start")};
    QTest::newRow("write failed") << "file_write" << "Input/output error"
                                  << "The disk failed a write" << "disk error"
                                  << QStringList{QStringLiteral("recheck"),
                                                 QStringLiteral("start")};
    QTest::newRow("long name") << "file_open" << "File name too long"
                               << "A file name is too long for this disk" << "name too long"
                               << QStringList{QStringLiteral("rename"), QStringLiteral("start")};
    QTest::newRow("bad name") << "file_open" << "Invalid argument"
                              << "A file name isn't allowed on this disk" << "bad name"
                              << QStringList{QStringLiteral("rename"), QStringLiteral("start")};
}

void TestTorrentProblem::wordsEachCause()
{
    QFETCH(QString, operation);
    QFETCH(QString, message);
    QFETCH(QString, headline);
    QFETCH(QString, cause);
    QFETCH(QStringList, actions);

    const ProblemLine line = ProblemLine::parse(
        QStringLiteral("File error alert. Torrent: \"t\". File: \"/d/t/f.bin\". Reason: \"abcdef "
                       "%1 (/d/t/f.bin) error: %2\"")
            .arg(operation, message));
    const TorrentProblem problem = ProblemMatch::describe(line);
    QVERIFY(problem.found);
    QCOMPARE(problem.headline, headline);
    QCOMPARE(problem.cause, cause);
    QCOMPARE(problem.actions, actions);
    QCOMPARE(problem.folder, QStringLiteral("/d/t"));
    QVERIFY2(problem.detail.startsWith(message + QStringLiteral(", ")), qPrintable(problem.detail));
    QVERIFY(problem.detail.endsWith(QStringLiteral(" f.bin")));
}

void TestTorrentProblem::wordsAMissingFilesLine()
{
    const TorrentProblem problem = ProblemMatch::describe(ProblemLine::parse(kRestoreFailed));
    QVERIFY(problem.missingFiles);
    QCOMPARE(problem.headline, QStringLiteral("Its files aren't where qBittorrent left them"));
    QCOMPARE(problem.detail,
             QStringLiteral("Moved, deleted or changed since qBittorrent last ran (mismatching "
                            "file size)"));
    QCOMPARE(problem.actions, (QStringList{QStringLiteral("location"), QStringLiteral("recheck")}));
}

void TestTorrentProblem::unknownMessagesKeepQbittorrentsWords()
{
    const TorrentProblem problem = ProblemMatch::describe(ProblemLine::parse(
        QStringLiteral("File error alert. Torrent: \"t\". File: \"/d/f\". Reason: \"abcdef "
                       "file_open (/d/f) error: too many open files\"")));
    QCOMPARE(problem.headline, QStringLiteral("Too many open files"));
    QCOMPARE(problem.cause, QStringLiteral("error"));
    QCOMPARE(problem.causeKey, QStringLiteral("other"));
    QCOMPARE(problem.actions, (QStringList{QStringLiteral("start"), QStringLiteral("recheck")}));
}

void TestTorrentProblem::notLoggedSaysSo()
{
    const TorrentProblem problem = ProblemMatch::notLogged(false);
    QVERIFY(!problem.found);
    QCOMPARE(problem.headline, QStringLiteral("qBittorrent didn't log a reason"));
    QCOMPARE(problem.cause, QStringLiteral("error"));
    QCOMPARE(ProblemMatch::notLogged(true).cause, QStringLiteral("missing files"));
}

QTEST_GUILESS_MAIN(TestTorrentProblem)
#include "tst_torrentproblem.moc"
