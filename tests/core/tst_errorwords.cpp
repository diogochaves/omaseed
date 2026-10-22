#include "core/ErrorWords.hpp"
#include "core/Format.hpp"

#include <QTest>
#include <QTimeZone>

using omaseed::core::ApiError;
using omaseed::core::AuthMode;
using omaseed::core::ErrorWords;

// One row per line of docs/33 §10's table (D23), plus the retry figure, the details, the
// short reason a server row prints and the read-only guard's sentence.
class TestErrorWords : public QObject
{
    Q_OBJECT

private:
    static ErrorWords::Subject server(AuthMode mode = AuthMode::Cookie, int retry = 7)
    {
        ErrorWords::Subject subject;
        subject.server = QStringLiteral("private");
        subject.address = QStringLiteral("127.0.0.1:8091");
        subject.authMode = mode;
        subject.retrySeconds = retry;
        return subject;
    }

private slots:
    // docs/44 D11 (DECISIONS C17): nothing there is "unreachable", red, retried.
    void connectionRefused()
    {
        const ApiError error(ApiError::Kind::Network,
                             QStringLiteral("GET sync/maindata: Connection refused"));
        const ErrorWords::Text text = ErrorWords::forServer(error, server());
        QCOMPARE(text.headline, QStringLiteral("private is unreachable"));
        QCOMPARE(text.line,
                 QStringLiteral("Connection refused at 127.0.0.1:8091. Is qBittorrent running? "
                                "omaseed tries again in 7 s."));
        QCOMPARE(text.kind, QStringLiteral("unreachable"));
        QCOMPARE(text.details,
                 QStringLiteral("network error · GET sync/maindata · Connection refused · "
                                "127.0.0.1:8091"));
    }

    void noRoute()
    {
        const ApiError error(ApiError::Kind::Network,
                             QStringLiteral("GET sync/maindata: Network unreachable"));
        const ErrorWords::Text text = ErrorWords::forServer(error, server(AuthMode::Cookie, -1));
        QCOMPARE(text.headline, QStringLiteral("private is unreachable"));
        QCOMPARE(text.line, QStringLiteral("No route to 127.0.0.1:8091. Is qBittorrent running?"));
        QCOMPARE(text.kind, QStringLiteral("unreachable"));
        QCOMPARE(ErrorWords::shortReason(error), QStringLiteral("unreachable"));
    }

    void hostNotFound()
    {
        ErrorWords::Subject subject = server();
        subject.address = QStringLiteral("seedbox.lan:8080");
        const ApiError error(ApiError::Kind::Network,
                             QStringLiteral("GET sync/maindata: Host seedbox.lan not found"));
        const ErrorWords::Text text = ErrorWords::forServer(error, subject);
        QCOMPARE(text.headline, QStringLiteral("private couldn't be found"));
        QCOMPARE(text.line,
                 QStringLiteral("seedbox.lan:8080 doesn't resolve on this computer. "
                                "omaseed tries again in 7 s."));
        QCOMPARE(text.kind, QStringLiteral("unreachable"));
        QCOMPARE(ErrorWords::shortReason(error), QStringLiteral("not found"));
    }

    // docs/44 W1 (D11): a timeout is a server that is there but slow: yellow, waiting, the
    // seconds it has been silent, and no countdown (omaseed keeps asking).
    void timeout()
    {
        const ApiError error(ApiError::Kind::Timeout,
                             QStringLiteral("GET sync/maindata: no reply within 8000 ms"));
        ErrorWords::Subject subject = server(AuthMode::Cookie, 0);
        subject.silentSeconds = 18;
        const ErrorWords::Text text = ErrorWords::forServer(error, subject);
        QCOMPARE(text.headline, QStringLiteral("private isn't answering"));
        QCOMPARE(text.line,
                 QStringLiteral("qBittorrent is running but hasn't replied for 18 s. It may be "
                                "busy writing to its disk. omaseed keeps asking."));
        QCOMPARE(text.kind, QStringLiteral("waiting"));
        QCOMPARE(ErrorWords::waitingLine(18), QStringLiteral("waiting · 18 s"));
        QCOMPARE(ErrorWords::shortReason(error), QStringLiteral("not answering"));
        // Without the silence known, the timeout's own figure.
        subject.silentSeconds = -1;
        QVERIFY(ErrorWords::forServer(error, subject).line.contains(QStringLiteral("for 8 s.")));
    }

    // docs/33 N5, docs/35 N7: a server sheet's failed test. No retry; what to check.
    void aFailedTest()
    {
        ErrorWords::Subject subject = server(AuthMode::Cookie, 7);
        subject.address = QStringLiteral("127.0.0.1:8095");
        const ApiError refused(ApiError::Kind::Network,
                               QStringLiteral("GET app/version: Connection refused"));
        ErrorWords::Text text = ErrorWords::forTest(refused, subject);
        QCOMPARE(text.headline, QStringLiteral("Nothing answered at 127.0.0.1:8095"));
        QCOMPARE(text.line,
                 QStringLiteral("Check the address and port, and that qBittorrent's Web UI is on "
                                "(Tools › Options › Web UI)."));
        QCOMPARE(text.kind, QStringLiteral("unreachable"));
        QCOMPARE(text.details,
                 QStringLiteral("network error · GET app/version · Connection refused · "
                                "127.0.0.1:8095"));

        const ApiError slow(ApiError::Kind::Timeout,
                            QStringLiteral("GET app/version: no reply within 8000 ms"));
        text = ErrorWords::forTest(slow, subject);
        QCOMPARE(text.headline, QStringLiteral("private isn't answering"));
        QCOMPARE(text.line,
                 QStringLiteral("It took the connection but didn't reply in 8 s. Test again in a "
                                "moment."));
        QCOMPARE(text.kind, QStringLiteral("waiting"));

        const ApiError login =
            ApiError::fromHttpStatus(401, QStringLiteral("auth/login: Unauthorized"));
        text = ErrorWords::forTest(login, subject, QStringLiteral("wrong username or password"));
        QCOMPARE(text.headline, QStringLiteral("private refused the sign-in"));
        QCOMPARE(text.line, QStringLiteral("Wrong username or password."));
        QCOMPARE(text.kind, QStringLiteral("refused"));
        QVERIFY(!text.line.contains(QStringLiteral("tries again")));
    }

    void certificate()
    {
        const ApiError error = ApiError::certificate(
            QStringLiteral("GET sync/maindata: self-signed"), QStringLiteral("ab"));
        ErrorWords::Subject subject = server();
        QCOMPARE(ErrorWords::forServer(error, subject).headline,
                 QStringLiteral("private's certificate isn't trusted"));
        subject.pinnedCertificate = QStringLiteral("cd");
        const ErrorWords::Text changed = ErrorWords::forServer(error, subject);
        QCOMPARE(changed.headline, QStringLiteral("private's certificate changed"));
        QCOMPARE(changed.kind, QStringLiteral("certificate"));
        QVERIFY(changed.line.contains(QStringLiteral("Settings › Servers")));
    }

    void signInRefused_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("mode");
        QTest::addColumn<QString>("headline");
        QTest::addColumn<QString>("lineStart");

        QTest::newRow("401, a cookie login")
            << int(ApiError::Kind::Unauthorized) << 401 << int(AuthMode::Cookie)
            << QStringLiteral("private refused the sign-in")
            << QStringLiteral("The username or password omaseed has for it is wrong.");
        QTest::newRow("403, an API key")
            << int(ApiError::Kind::Forbidden) << 403 << int(AuthMode::ApiKey)
            << QStringLiteral("private refused the sign-in")
            << QStringLiteral("The API key omaseed has for it is wrong.");
        QTest::newRow("403, no credential")
            << int(ApiError::Kind::Forbidden) << 403 << int(AuthMode::None)
            << QStringLiteral("private asks for a login") << QStringLiteral("omaseed has none");
    }

    void signInRefused()
    {
        QFETCH(int, kind);
        QFETCH(int, status);
        QFETCH(int, mode);
        QFETCH(QString, headline);
        QFETCH(QString, lineStart);
        const ApiError error(ApiError::Kind(kind), QStringLiteral("auth/login: Fails."), status);
        const ErrorWords::Text text = ErrorWords::forServer(error, server(AuthMode(mode)));
        QCOMPARE(text.headline, headline);
        QVERIFY2(text.line.startsWith(lineStart), qPrintable(text.line));
        QCOMPARE(text.kind, QStringLiteral("refused"));
        // No retry: omaseed won't keep trying a password it knows is wrong.
        QVERIFY(!text.line.contains(QStringLiteral("tries again")));
        QVERIFY(ErrorWords::isRefusedSignIn(error, AuthMode(mode)));
        QCOMPARE(ErrorWords::request(error), QStringLiteral("auth/login"));
    }

    void forbiddenCookieIsABan()
    {
        const ApiError error(
            ApiError::Kind::Forbidden, QStringLiteral("auth/login: Forbidden"), 403);
        const ErrorWords::Text text = ErrorWords::forServer(error, server(AuthMode::Cookie));
        QCOMPARE(text.headline, QStringLiteral("private refused omaseed"));
        QVERIFY(text.line.startsWith(QStringLiteral("Too many failed sign-ins")));
        QCOMPARE(text.kind, QStringLiteral("banned"));
        QVERIFY(!ErrorWords::isRefusedSignIn(error, AuthMode::Cookie));
    }

    // C15 M19 (board 42 §8): setLocation's refusals are about the folder.
    void setLocationConflict()
    {
        const ApiError error = ApiError::fromHttpStatus(
            409,
            QStringLiteral("POST torrents/setLocation: Error transferring "
                           "http://127.0.0.1:8090/api/v2/torrents/setLocation - server replied: "
                           "Conflict"));
        ErrorWords::Subject subject = server();
        subject.server = QStringLiteral("box");
        subject.address = QStringLiteral("127.0.0.1:8090");
        subject.torrent = QStringLiteral("Field Recordings Vol. 3 [FLAC]");
        subject.folder = QStringLiteral("/proc/omaseed");
        const ErrorWords::Text text = ErrorWords::forRequest(error, subject);
        QCOMPARE(text.headline, QStringLiteral("qBittorrent can't create /proc/omaseed"));
        QCOMPARE(text.line,
                 QStringLiteral("Check that the drive is mounted and its parent folder exists."));
        QCOMPARE(text.details,
                 QStringLiteral("conflict (409) · POST torrents/setLocation · server replied: "
                                "Conflict · 127.0.0.1:8090"));
        QCOMPARE(text.sentence(),
                 QStringLiteral("qBittorrent can't create /proc/omaseed: check that the drive is "
                                "mounted and its parent folder exists"));
    }

    void setLocationNoFolder()
    {
        const ApiError error = ApiError::fromHttpStatus(
            400, QStringLiteral("POST torrents/setLocation: Save path is empty"));
        const ErrorWords::Text text = ErrorWords::forRequest(error, server());
        QCOMPARE(text.headline, QStringLiteral("No folder given"));
        QCOMPARE(text.line, QStringLiteral("Type the folder's new path."));
    }

    void setLocationForbiddenFolder()
    {
        // What QbtClient makes of a 403 whose body is qBittorrent's own text.
        const ApiError error(ApiError::Kind::Forbidden,
                             QStringLiteral("POST torrents/setLocation: Cannot write to directory"),
                             403);
        ErrorWords::Subject subject = server(AuthMode::Cookie);
        subject.folder = QStringLiteral("/mnt/fast/games");
        const ErrorWords::Text text = ErrorWords::forRequest(error, subject);
        QCOMPARE(text.headline, QStringLiteral("qBittorrent can't write to /mnt/fast/games"));
        QCOMPARE(text.line,
                 QStringLiteral("Its user has no write access there. This is not about the "
                                "sign-in."));
        QCOMPARE(text.kind, QStringLiteral("request"));
    }

    void setLocationBareForbiddenIsStillTheSignIn()
    {
        // A session qBittorrent doesn't know answers the bare "Forbidden": not a folder word.
        const ApiError error(ApiError::Kind::Forbidden,
                             QStringLiteral("POST torrents/setLocation: Error transferring "
                                            "http://127.0.0.1:8090/api/v2/torrents/setLocation "
                                            "- server replied: Forbidden"),
                             403);
        ErrorWords::Subject subject = server(AuthMode::Cookie);
        subject.folder = QStringLiteral("/mnt/fast/games");
        const ErrorWords::Text text = ErrorWords::forRequest(error, subject);
        QVERIFY2(!text.headline.contains(QStringLiteral("can't write")), qPrintable(text.headline));
    }

    void moveFailurePermissionDenied()
    {
        // T2: 200, then "Failed to move torrent … Permission denied" in the log.
        const ErrorWords::Text text =
            ErrorWords::forMoveFailure(QStringLiteral("Tiny Platformers Bundle"),
                                       QStringLiteral("/mnt/fast/games"),
                                       QStringLiteral("/mnt/fast"),
                                       QStringLiteral("noAccess"),
                                       QStringLiteral("Permission denied"));
        QCOMPARE(text.headline, QStringLiteral("qBittorrent can't write to /mnt/fast/games"));
        QCOMPARE(text.line,
                 QStringLiteral("Tiny Platformers Bundle didn't move: “Permission denied”."));
    }

    void moveFailureDiskFull()
    {
        const ErrorWords::Text text =
            ErrorWords::forMoveFailure(QStringLiteral("Big Box Scans"),
                                       QStringLiteral("/mnt/fast/games"),
                                       QStringLiteral("/mnt/fast"),
                                       QStringLiteral("diskFull"),
                                       QStringLiteral("No space left on device"));
        QCOMPARE(text.headline, QStringLiteral("/mnt/fast is full"));
        QCOMPARE(text.line, QStringLiteral("Big Box Scans didn't move. The move is paused."));
    }

    void moveFailureMissing()
    {
        // #97's box: 200, then "No such file or directory" (a drive gone mid-move).
        const ErrorWords::Text text =
            ErrorWords::forMoveFailure(QStringLiteral("Open Movie Night (2026)"),
                                       QStringLiteral("/mnt/fast/games"),
                                       QStringLiteral("/mnt/fast"),
                                       QStringLiteral("missing"),
                                       QStringLiteral("No such file or directory"));
        QCOMPARE(text.headline, QStringLiteral("qBittorrent can't reach /mnt/fast/games"));
        QCOMPARE(text.line,
                 QStringLiteral("Open Movie Night (2026) didn't move: “No such file or "
                                "directory”. Check that both drives are mounted."));
        QVERIFY(!text.headline.contains(QStringLiteral("sign")));
    }

    void moveFailureReadOnly()
    {
        const ErrorWords::Text text =
            ErrorWords::forMoveFailure(QStringLiteral("x"),
                                       QStringLiteral("/mnt/fast/games"),
                                       QString(),
                                       QStringLiteral("readOnly"),
                                       QStringLiteral("Read-only file system"));
        QCOMPARE(text.headline, QStringLiteral("qBittorrent can't write to /mnt/fast/games"));
    }

    void moveFailureOther()
    {
        const ErrorWords::Text text =
            ErrorWords::forMoveFailure(QStringLiteral("x"),
                                       QStringLiteral("/d"),
                                       QString(),
                                       QStringLiteral("other"),
                                       QStringLiteral("Input/output error"));
        QCOMPARE(text.headline, QStringLiteral("x didn't move"));
        QCOMPARE(text.line, QStringLiteral("“Input/output error”."));
    }

    void nearlyFull()
    {
        const ErrorWords::Text text = ErrorWords::forNearlyFull(
            QStringLiteral("Big Box Scans"), QStringLiteral("/mnt/fast"), 1503238553);
        QCOMPARE(text.headline, QStringLiteral("/mnt/fast is nearly full"));
        QCOMPARE(text.line,
                 QStringLiteral("Big Box Scans would leave 1.40 GB free, under what "
                                "you keep free. The move is paused."));
    }

    void stuck()
    {
        const QDateTime landed(QDate(2026, 10, 18), QTime(14, 2), QTimeZone::LocalTime);
        const ErrorWords::Text text = ErrorWords::forStuck(QStringLiteral("Freeware Classics"),
                                                           std::chrono::minutes(38),
                                                           landed,
                                                           std::chrono::minutes(5));
        QCOMPARE(text.headline, QStringLiteral("qBittorrent stopped moving"));
        QCOMPARE(text.line,
                 QStringLiteral("Freeware Classics has been moving for 38 min and nothing has "
                                "landed since 14:02 (at this move's speed it needs %1). "
                                "Restarting qBittorrent usually frees it.")
                     .arg(omaseed::core::Format::eta(300)));
    }

    void leftOut()
    {
        const ErrorWords::Text text = ErrorWords::forLeftOut(2, QStringLiteral("/mnt/fast/games"));
        QCOMPARE(text.headline, QStringLiteral("2 left out"));
        QCOMPARE(text.line,
                 QStringLiteral("/mnt/fast/games already has a file with their file's "
                                "name. qBittorrent would keep that file and report "
                                "the move as done."));
    }

    void leftOutSaysWhatIsThere()
    {
        // #161 (a): the size of the file there and whether a torrent owns it.
        QCOMPARE(ErrorWords::whatIsThere(0, false, true, QString()),
                 QStringLiteral("an empty file no torrent owns"));
        QCOMPARE(ErrorWords::whatIsThere(0, true, false, QString()),
                 QStringLiteral("an empty folder"));
        QCOMPARE(ErrorWords::whatIsThere(-1, false, true, QStringLiteral("Feature")),
                 QStringLiteral("Feature owns it"));
        QCOMPARE(ErrorWords::whatIsThere(-1, true, true, QString()),
                 QStringLiteral("no torrent owns it"));
        QCOMPARE(ErrorWords::whatIsThere(5, false, true, QStringLiteral("Feature")),
                 QStringLiteral("a file of 5 B that Feature owns"));
        QVERIFY(ErrorWords::whatIsThere(-1, false, false, QString()).isEmpty());
        const QString sized = ErrorWords::whatIsThere(3000, true, true, QString());
        QVERIFY2(sized.startsWith(QStringLiteral("a folder of ")) &&
                     sized.endsWith(QStringLiteral(" no torrent owns")),
                 qPrintable(sized));
        const ErrorWords::Text text = ErrorWords::forLeftOut(
            1, QStringLiteral("/mnt/slow/movies"), QStringLiteral("an empty file no torrent owns"));
        QCOMPARE(text.headline, QStringLiteral("1 left out"));
        QCOMPARE(text.line,
                 QStringLiteral("/mnt/slow/movies already has a file with their file's name (an "
                                "empty file no torrent owns). qBittorrent would keep that file "
                                "and report the move as done."));
    }

    void addUnsupportedMediaType()
    {
        const ApiError error = ApiError::fromHttpStatus(
            415, QStringLiteral("POST torrents/add: Torrent file is not valid"));
        ErrorWords::Subject subject = server();
        subject.torrent = QStringLiteral("distro.iso");
        const ErrorWords::Text text = ErrorWords::forRequest(error, subject);
        QCOMPARE(text.headline,
                 QStringLiteral("qBittorrent couldn't read the .torrent of distro.iso"));
        QVERIFY(text.details.contains(QStringLiteral("Torrent file is not valid")));
    }

    void addWhileDown()
    {
        const ApiError error(ApiError::Kind::Network,
                             QStringLiteral("POST torrents/add: Connection refused"));
        const ErrorWords::Text text = ErrorWords::forRequest(error, server());
        QCOMPARE(text.headline, QStringLiteral("private didn't take the torrent"));
        QVERIFY(text.line.contains(QStringLiteral("Nothing was added")));
        QVERIFY(!text.line.contains(QStringLiteral("Connection refused")));
    }

    void serverError()
    {
        const ApiError error = ApiError::fromHttpStatus(
            500, QStringLiteral("GET sync/maindata: Internal Server Error"));
        const ErrorWords::Text text = ErrorWords::forServer(error, server());
        QCOMPARE(text.headline, QStringLiteral("private had an internal error"));
        QCOMPARE(text.line, QStringLiteral("qBittorrent answered 500; its log may say why."));
    }

    void cannotParse()
    {
        const ApiError error(ApiError::Kind::Parse,
                             QStringLiteral("GET sync/maindata: illegal value"));
        const ErrorWords::Text text = ErrorWords::forServer(error, server());
        QCOMPARE(text.headline, QStringLiteral("private answered something omaseed can't read"));
        QCOMPARE(text.line, QStringLiteral("Is 127.0.0.1:8091 a qBittorrent Web UI?"));
    }

    void anythingElse()
    {
        const ApiError error = ApiError::fromHttpStatus(
            409,
            QStringLiteral("POST torrents/rename: Error transferring http://x/api/v2/torrents/"
                           "rename - server replied: Conflict"));
        const ErrorWords::Text text = ErrorWords::forRequest(error, server());
        QCOMPARE(text.headline, QStringLiteral("private refused: server replied: Conflict"));
    }

    void retryFigure()
    {
        QCOMPARE(ErrorWords::retryLine(7), QStringLiteral("omaseed tries again in 7 s."));
        QCOMPARE(ErrorWords::retryLine(0), QStringLiteral("omaseed tries again now."));
        QVERIFY(ErrorWords::retryLine(-1).isEmpty());
    }

    void shortReasons()
    {
        QCOMPARE(ErrorWords::shortReason(ApiError(ApiError::Kind::Network, QStringLiteral("x"))),
                 QStringLiteral("unreachable"));
        QCOMPARE(
            ErrorWords::shortReason(ApiError(ApiError::Kind::Unauthorized, QStringLiteral("x"))),
            QStringLiteral("sign-in refused"));
        QVERIFY(ErrorWords::shortReason(ApiError()).isEmpty());
    }

    void guard()
    {
        QCOMPARE(ErrorWords::guard(QStringLiteral("private"), QStringLiteral("p"), false),
                 QStringLiteral("private is unreachable: p waits until it answers"));
        QCOMPARE(ErrorWords::guard(QStringLiteral("private"), QStringLiteral("p"), false, true),
                 QStringLiteral("private isn't answering: p waits until it answers"));
        QCOMPARE(ErrorWords::guard(QStringLiteral("seedbox"), QStringLiteral("d"), true),
                 QStringLiteral("seedbox refused the sign-in: d waits until the login is fixed"));
    }

    void noErrorNoWords()
    {
        QVERIFY(ErrorWords::forServer(ApiError(), server()).headline.isEmpty());
        QVERIFY(ErrorWords::forRequest(ApiError(), server()).headline.isEmpty());
        QVERIFY(ErrorWords::forServer(ApiError(ApiError::Kind::Cancelled, QStringLiteral("x")),
                                      server())
                    .headline.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestErrorWords)
#include "tst_errorwords.moc"
