#include "core/ErrorWords.hpp"

#include <QTest>

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
    void connectionRefused()
    {
        const ApiError error(ApiError::Kind::Network,
                             QStringLiteral("GET sync/maindata: Connection refused"));
        const ErrorWords::Text text = ErrorWords::forServer(error, server());
        QCOMPARE(text.headline, QStringLiteral("private didn't answer"));
        QCOMPARE(
            text.line,
            QStringLiteral("Nothing is listening at 127.0.0.1:8091. omaseed tries again in 7 s."));
        QCOMPARE(text.kind, QStringLiteral("down"));
        QCOMPARE(text.details,
                 QStringLiteral("network error · GET sync/maindata · Connection refused · "
                                "127.0.0.1:8091"));
        // The API's and Qt's words reach the details only.
        QVERIFY(!text.headline.contains(QStringLiteral("refused")));
        QVERIFY(!text.line.contains(QStringLiteral("Connection refused")));
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
        QCOMPARE(ErrorWords::shortReason(error), QStringLiteral("not found"));
    }

    void timeout()
    {
        const ApiError error(ApiError::Kind::Timeout,
                             QStringLiteral("GET sync/maindata: no reply within 8000 ms"));
        const ErrorWords::Text text = ErrorWords::forServer(error, server(AuthMode::Cookie, 0));
        QCOMPARE(text.headline, QStringLiteral("private is taking too long"));
        QCOMPARE(text.line, QStringLiteral("No answer in 8 s. omaseed tries again now."));
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
        QCOMPARE(text.headline, QStringLiteral("Field Recordings Vol. 3 [FLAC] wasn't moved"));
        QCOMPARE(text.line, QStringLiteral("qBittorrent can't create or write /proc/omaseed."));
        QCOMPARE(text.details,
                 QStringLiteral("conflict (409) · POST torrents/setLocation · server replied: "
                                "Conflict · 127.0.0.1:8090"));
        QCOMPARE(text.sentence(),
                 QStringLiteral("Field Recordings Vol. 3 [FLAC] wasn't moved: qBittorrent can't "
                                "create or write /proc/omaseed"));
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
                 QStringLiteral("didn't answer"));
        QCOMPARE(
            ErrorWords::shortReason(ApiError(ApiError::Kind::Unauthorized, QStringLiteral("x"))),
            QStringLiteral("sign-in refused"));
        QVERIFY(ErrorWords::shortReason(ApiError()).isEmpty());
    }

    void guard()
    {
        QCOMPARE(ErrorWords::guard(QStringLiteral("private"), QStringLiteral("p"), false),
                 QStringLiteral("private is unreachable: p waits until it answers"));
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
