#pragma once

// ErrorWords — the one table of plain words for what went wrong (docs/33 §10, D23; DECISIONS
// C12). An `ApiError` and what it was about (the server's name and address, a torrent, a
// folder) become a headline that names the subject, one line on what omaseed does next or
// what to change, and the technical text for "details". The API's and Qt's own words only
// ever reach the details.
//
// Per request where it is known (setLocation's 409, add's 415), per kind otherwise:
//
//   connection refused   "NAME didn't answer"            "Nothing is listening at ADDRESS."
//   host not found       "NAME couldn't be found"        "ADDRESS doesn't resolve on this
//   computer." timeout              "NAME is taking too long"       "No answer in N s." certificate
//   Q27's words                     review it in Settings › Servers 401, sign-in         "NAME
//   refused the sign-in"      credentialRefusedText, in a sentence 403                  "NAME
//   refused omaseed"          banned, or too many failed sign-ins 409 on setLocation   "TORRENT
//   wasn't moved"          "qBittorrent can't create or write FOLDER." 415 on add "qBittorrent
//   couldn't read the .torrent of TORRENT" 5xx                  "NAME had an internal error"
//   "qBittorrent answered N; its log may say why." can't parse          "NAME answered something
//   omaseed can't read" anything else        "NAME refused: <qBittorrent's own text>"
//
// A server's problem adds the retry (`retryLine`): one figure, the engine's countdown, read
// by the window's notice and the desktop notice alike (Q16). The look of the notice that
// carries these words is board 35's; the words are this table's.
//
// Pure Qt Core, no QML; `tst_errorwords` has a row per line of the table.

#include "core/ApiError.hpp"
#include "core/Server.hpp"

#include <QString>
#include <QVariantMap>

namespace omaseed::core {

class ErrorWords
{
public:
    /// What the error is about. Only the fields a row needs are read.
    struct Subject
    {
        /// The server's name ("private"); its address when it has none.
        QString server;
        /// "127.0.0.1:8091", for the line and the details.
        QString address;
        AuthMode authMode = AuthMode::None;
        /// The certificate trusted for the server (CertificatePin), for Q27's words.
        QString pinnedCertificate;
        /// What is left of the engine's retry, in seconds: > 0 a countdown, 0 "now", < 0 no
        /// retry to speak of (an action, a refused sign-in).
        int retrySeconds = -1;
        /// The torrent a request was about ("Field Recordings Vol. 3"), or empty.
        QString torrent;
        /// setLocation's destination folder, or empty.
        QString folder;
    };

    /// The words, as plain data.
    struct Text
    {
        QString headline;
        QString line;
        QString details;
        /// "down" (didn't answer, can't be found, too slow), "refused" (the sign-in), "banned"
        /// (403), "certificate", "server" (5xx), "parse", "request" (an action refused).
        QString kind;

        /// The footer's one sentence: "Field Recordings wasn't moved: qBittorrent can't
        /// create or write /proc/omaseed".
        QString sentence() const;
        /// {headline, line, details, kind, sentence} for QML.
        QVariantMap toMap() const;
    };

    /// A server's own problem (the sync failed): the headline names the server, the line says
    /// what omaseed does next.
    static Text forServer(const ApiError &error, const Subject &subject);
    /// An action that failed (a request about a torrent, an add): per request where it is
    /// known, else per kind.
    static Text forRequest(const ApiError &error, const Subject &subject);

    /// "omaseed tries again in 7 s." / "omaseed tries again now."; empty for < 0.
    static QString retryLine(int seconds);
    /// The short reason a server row prints for its last failed try: "didn't answer",
    /// "sign-in refused", "certificate refused", "refused omaseed", "internal error",
    /// "unreadable answer", "error".
    static QString shortReason(const ApiError &error);
    /// "network error · GET sync/maindata · Connection refused · 127.0.0.1:8091": the kind
    /// (and status), the request, the API's or Qt's own text, the address.
    static QString details(const ApiError &error, const QString &address = QString());
    /// "GET sync/maindata", the request the error names; empty when it names none.
    static QString request(const ApiError &error);
    /// The read-only guard (D23): the key it stops says so. "private is unreachable: p waits
    /// until it answers" / "private refused the sign-in: p waits until the login is fixed".
    static QString guard(const QString &server, const QString &key, bool refused);
    /// True when the error is a refused sign-in (401, or 403 to a key or to no credential).
    static bool isRefusedSignIn(const ApiError &error, AuthMode mode);
};

} // namespace omaseed::core
