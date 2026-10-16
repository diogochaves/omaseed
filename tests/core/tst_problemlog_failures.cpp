// ProblemLog's docs/32 side (#55): failures that left a torrent working, on its row until
// fixed, dismissed or the server restarts; the 30 s watch after a move, rename or delete
// omaseed asked for; web seed and export lines; and how often the log is read. Lines go in
// through applyEntries(), as in tst_problemlog, and the cadence is checked against a small
// HTTP server of our own that answers log/main and torrents/files and records what it was
// asked. The lines are the playground's (tst_failurelines).

#include "QbtFeed.hpp"
#include "core/ProblemLog.hpp"
#include "core/QbtClient.hpp"
#include "core/TorrentModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <memory>

using omaseed::core::ProblemLog;
using omaseed::core::QbtClient;
using omaseed::core::TorrentModel;
using omaseed::core::WebSeedLogLine;

namespace {

const QString kRoot = QStringLiteral("/home/chaves/.local/share/omaseed-test/downloads/log55");
const QString kMoveHash = QStringLiteral("37a783e4a8f6f3a92c4226d38ba1d69bdbfa6164");
const QString kRenameHash = QStringLiteral("5b7364d1603791663cb56b37c5111cb5016c59fa");
const QString kTwinHash = QStringLiteral("ffff64d1603791663cb56b37c5111cb5016c59fa");
const QString kSeedHash = QStringLiteral("0143964d6fbb92648d1829d16668338054f6a4f3");
const QString kDeleteHash = QStringLiteral("11daa001943d95587b236d2d91ce2e6b6986f0ba");
const qint64 kAddedAt = 1792075500;
const qint64 kLoggedAt = 1792075569;

QString moveLine(const QString &to = kRoot + QStringLiteral("/dest"))
{
    return QStringLiteral(
               "Failed to move torrent. Torrent: \"log55-move.bin\". Source: \"%1/src\". "
               "Destination: \"%2\". Reason: \"37a783 storage move failed. file_rename "
               "(%1/src/log55-move.bin): Permission denied\"")
        .arg(kRoot, to);
}

QString renameLine(const QString &file)
{
    return QStringLiteral("File rename failed. Torrent: \"log55-rename\", file: \"%1\", reason: "
                          "\"Permission denied\"")
        .arg(file);
}

QJsonObject torrent(const QString &name, const QString &savePath, const QString &v2 = QString())
{
    QJsonObject json{{QStringLiteral("name"), name},
                     {QStringLiteral("state"), QStringLiteral("stalledUP")},
                     {QStringLiteral("save_path"), savePath},
                     {QStringLiteral("added_on"), kAddedAt}};
    if (!v2.isEmpty())
        json.insert(QStringLiteral("infohash_v2"), v2);
    return json;
}

QJsonObject entry(int id, const QString &message, qint64 at = kLoggedAt)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("timestamp"), at},
            {QStringLiteral("type"), 4},
            {QStringLiteral("message"), message}};
}

QByteArray httpResponse(const QByteArray &body)
{
    return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
           QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

/// Answers log/main with `log` (only the lines after the last_known_id asked for, as
/// qBittorrent does) and torrents/files with `files`, and records each log/main's id.
class LogServer : public QTcpServer
{
public:
    QJsonArray log;
    QJsonArray files;
    QList<int> askedFrom;
    int filesRequests = 0;

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype end = buffer->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            const QUrl target(
                QString::fromLatin1(buffer->left(end).split('\n').value(0).split(' ').value(1)));
            buffer->clear();
            QByteArray body = "[]";
            if (target.path() == QLatin1String("/api/v2/log/main")) {
                const int from =
                    QUrlQuery(target).queryItemValue(QStringLiteral("last_known_id")).toInt();
                askedFrom << from;
                QJsonArray after;
                for (const QJsonValue &value : std::as_const(log)) {
                    if (value.toObject().value(QStringLiteral("id")).toInt() > from)
                        after << value;
                }
                body = QJsonDocument(after).toJson(QJsonDocument::Compact);
            } else if (target.path() == QLatin1String("/api/v2/torrents/files")) {
                ++filesRequests;
                body = QJsonDocument(files).toJson(QJsonDocument::Compact);
            }
            socket->write(httpResponse(body));
            socket->disconnectFromHost();
        });
    }
};

QVariantMap failureOf(const TorrentModel &model, const QString &hash)
{
    return model.data(model.index(model.indexOfHash(hash)), TorrentModel::FailureRole).toMap();
}

} // namespace

class TestProblemLogFailures : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aFailedMoveShowsOnItsTorrent();
    void aMoveFailureGoesWhenTheSavePathChanges();
    void aRenameFailureGoesWhenTheFileIsRenamed();
    void twinsOfOneNameAreToldApartByTheRenameAsked();
    void theNewestFailureIsShown();
    void anErrorKeepsItsProblemAndTheFailureComesToo();
    void aDismissalHidesItForGood();
    void dismissalsSurviveARestartOfOmaseed();
    void aRestartOfQbittorrentForgetsTheFailures();
    void aLineFromBeforeATorrentWasAddedIsNotAboutIt();
    void webSeedLinesGoToTheirTorrent();
    void theNewestExportFailureIsKept();

    void aWatchedMoveTellsAtOnce();
    void aBatchMoveTellsOnce();
    void aLineFromBeforeTheWatchIsNotTold();
    void aWatchedRenameAndDeleteTell();
    void aWatchEndsAfterItsTimeOrWhenTheMoveLanded();

    void readsEvery15sWhileShownAndNotWhileHidden();
    void aWatchReadsOnEveryPoll();
    void aFullUpdateReadsFromTheTopAgain();
    void standingRenamesAskForTheFiles();

private:
    void sync(bool full = false) { m_log->onSynced(full); }

    TorrentModel *m_model = nullptr;
    ProblemLog *m_log = nullptr;
};

void TestProblemLogFailures::init()
{
    m_model = new TorrentModel;
    m_log = new ProblemLog;
    m_log->setTorrents(m_model);
    qbtfeed::full(
        *m_model,
        {{kMoveHash,
          torrent(QStringLiteral("log55-move.bin"), kRoot + QStringLiteral("/src"), kMoveHash)},
         {kRenameHash,
          torrent(QStringLiteral("log55-rename"), kRoot + QStringLiteral("/src"), kRenameHash)},
         {kSeedHash,
          torrent(QStringLiteral("log55-webseed.bin"), kRoot + QStringLiteral("/wdl"), kSeedHash)},
         {kDeleteHash,
          torrent(QStringLiteral("log55-delete.bin"), kRoot + QStringLiteral("/rodel"))}});
    sync(true);
}

void TestProblemLogFailures::cleanup()
{
    delete m_log;
    delete m_model;
}

void TestProblemLogFailures::aFailedMoveShowsOnItsTorrent()
{
    QSignalSpy changed(m_model, &TorrentModel::problemsChanged);
    qbtfeed::entries(*m_log, {entry(220, moveLine())});
    QCOMPARE(changed.count(), 1);

    const QVariantMap failure = failureOf(*m_model, kMoveHash);
    QCOMPARE(failure.value(QStringLiteral("kind")).toString(), QStringLiteral("move"));
    QCOMPARE(failure.value(QStringLiteral("headline")).toString(),
             QStringLiteral("The move to log55/dest didn't happen"));
    QCOMPARE(failure.value(QStringLiteral("detail")).toString(),
             QStringLiteral("Permission denied, moving log55-move.bin"));
    QCOMPARE(failure.value(QStringLiteral("destination")).toString(), kRoot + u"/dest");
    QCOMPARE(failure.value(QStringLiteral("count")).toInt(), 1);
    QCOMPARE(failure.value(QStringLiteral("line")).toString(), moveLine());
    // Not an error: the problem and the status cause stay empty, and nothing else changed.
    QVERIFY(m_model->problem(kMoveHash).isEmpty());
    QVERIFY(failureOf(*m_model, kRenameHash).isEmpty());

    // Tried again, failed again: one block, "2 times".
    qbtfeed::entries(*m_log, {entry(230, moveLine())});
    QCOMPARE(failureOf(*m_model, kMoveHash).value(QStringLiteral("count")).toInt(), 2);
}

void TestProblemLogFailures::aMoveFailureGoesWhenTheSavePathChanges()
{
    qbtfeed::entries(*m_log, {entry(220, moveLine())});
    QVERIFY(!failureOf(*m_model, kMoveHash).isEmpty());

    qbtfeed::partial(
        *m_model,
        {{kMoveHash, QJsonObject{{QStringLiteral("save_path"), kRoot + QStringLiteral("/dest")}}}},
        {});
    sync();
    QVERIFY(failureOf(*m_model, kMoveHash).isEmpty());

    // Moved back to where it was: the old failure is history, it does not come back.
    qbtfeed::partial(
        *m_model,
        {{kMoveHash, QJsonObject{{QStringLiteral("save_path"), kRoot + QStringLiteral("/src")}}}},
        {});
    sync();
    QVERIFY(failureOf(*m_model, kMoveHash).isEmpty());
}

void TestProblemLogFailures::aRenameFailureGoesWhenTheFileIsRenamed()
{
    // The playground's own lines: b.txt alone, then the whole folder.
    qbtfeed::entries(*m_log,
                     {entry(221, renameLine(QStringLiteral("log55-rename/b.txt"))),
                      entry(222, renameLine(QStringLiteral("log55-rename/a.txt"))),
                      entry(223, renameLine(QStringLiteral("log55-rename/b.txt"))),
                      entry(224, renameLine(QStringLiteral("log55-rename/c.txt")))});
    QVariantMap failure = failureOf(*m_model, kRenameHash);
    QCOMPARE(failure.value(QStringLiteral("headline")).toString(),
             QStringLiteral("3 files kept their old names"));
    QCOMPARE(failure.value(QStringLiteral("detail")).toString(),
             QStringLiteral("Permission denied, renaming log55-rename/a.txt and 2 more"));
    QCOMPARE(failure.value(QStringLiteral("torrentFile")).toString(),
             QStringLiteral("log55-rename/a.txt"));
    QCOMPARE(failure.value(QStringLiteral("folder")).toString(), kRoot + u"/src/log55-rename");

    // The files as they still are: it stands.
    m_log->applyFiles(kRenameHash,
                      {QStringLiteral("log55-rename/a.txt"),
                       QStringLiteral("log55-rename/b.txt"),
                       QStringLiteral("log55-rename/c.txt")});
    QVERIFY(!failureOf(*m_model, kRenameHash).isEmpty());

    // a.txt was renamed since: two are left.
    m_log->applyFiles(kRenameHash,
                      {QStringLiteral("log55-rename/a2.txt"),
                       QStringLiteral("log55-rename/b.txt"),
                       QStringLiteral("log55-rename/c.txt")});
    failure = failureOf(*m_model, kRenameHash);
    QCOMPARE(failure.value(QStringLiteral("headline")).toString(),
             QStringLiteral("2 files kept their old names"));
    QCOMPARE(failure.value(QStringLiteral("torrentFile")).toString(),
             QStringLiteral("log55-rename/b.txt"));

    // The whole folder renamed: nothing kept its name any more.
    m_log->applyFiles(kRenameHash,
                      {QStringLiteral("renamed/a2.txt"),
                       QStringLiteral("renamed/b.txt"),
                       QStringLiteral("renamed/c.txt")});
    QVERIFY(failureOf(*m_model, kRenameHash).isEmpty());
}

void TestProblemLogFailures::twinsOfOneNameAreToldApartByTheRenameAsked()
{
    qbtfeed::partial(
        *m_model,
        {{kTwinHash, torrent(QStringLiteral("log55-rename"), QStringLiteral("/elsewhere"))}},
        {});
    sync();

    // Nothing says which of the two the line is about: both show it, and say so.
    qbtfeed::entries(*m_log, {entry(221, renameLine(QStringLiteral("log55-rename/b.txt")))});
    QCOMPARE(failureOf(*m_model, kRenameHash).value(QStringLiteral("ambiguous")).toBool(), true);
    QCOMPARE(failureOf(*m_model, kTwinHash).value(QStringLiteral("ambiguous")).toBool(), true);

    // omaseed asked to rename in one of them: that one.
    m_log->watchRename(kRenameHash, 1);
    qbtfeed::entries(*m_log, {entry(225, renameLine(QStringLiteral("log55-rename/c.txt")))});
    QCOMPARE(failureOf(*m_model, kRenameHash).value(QStringLiteral("ambiguous")).toBool(), false);
    QCOMPARE(failureOf(*m_model, kRenameHash).value(QStringLiteral("detail")).toString(),
             QStringLiteral("Permission denied, renaming log55-rename/b.txt and 1 more"));
    // The twin keeps only the line that may be its own.
    QCOMPARE(failureOf(*m_model, kTwinHash).value(QStringLiteral("headline")).toString(),
             QStringLiteral("A file kept its old name"));
}

// docs/32 §5: two failures on one torrent show the newest.
void TestProblemLogFailures::theNewestFailureIsShown()
{
    const QString partfile = QStringLiteral(
        "Failed to remove partfile. Torrent: \"log55-move.bin\". Reason: \"Directory not empty\".");
    qbtfeed::entries(*m_log, {entry(220, moveLine()), entry(240, partfile)});
    QCOMPARE(failureOf(*m_model, kMoveHash).value(QStringLiteral("kind")).toString(),
             QStringLiteral("partfile"));
    qbtfeed::entries(*m_log, {entry(250, moveLine())});
    QCOMPARE(failureOf(*m_model, kMoveHash).value(QStringLiteral("kind")).toString(),
             QStringLiteral("move"));
    // Dismissing the newest shows the one under it.
    m_log->dismiss(kMoveHash);
    QCOMPARE(failureOf(*m_model, kMoveHash).value(QStringLiteral("kind")).toString(),
             QStringLiteral("partfile"));
}

void TestProblemLogFailures::anErrorKeepsItsProblemAndTheFailureComesToo()
{
    qbtfeed::partial(*m_model,
                     {{kMoveHash, QJsonObject{{QStringLiteral("state"), QStringLiteral("error")}}}},
                     {});
    sync();
    qbtfeed::entries(*m_log, {entry(220, moveLine())});
    // #54's problem says it didn't log a reason (the move line is not one); the failure is
    // there beside it.
    QCOMPARE(m_model->problem(kMoveHash).value(QStringLiteral("causeKey")).toString(),
             QStringLiteral("notLogged"));
    QCOMPARE(failureOf(*m_model, kMoveHash).value(QStringLiteral("kind")).toString(),
             QStringLiteral("move"));
}

void TestProblemLogFailures::aDismissalHidesItForGood()
{
    QSignalSpy dismissed(m_log, &ProblemLog::dismissedChanged);
    qbtfeed::entries(*m_log, {entry(220, moveLine())});
    m_log->dismiss(kMoveHash);
    QCOMPARE(dismissed.count(), 1);
    QVERIFY(failureOf(*m_model, kMoveHash).isEmpty());
    QCOMPARE(m_log->dismissed(), QStringList{ProblemLog::lineKey(220, kLoggedAt, moveLine())});

    // A later poll, and the same line read again after a full update, keep it hidden.
    sync();
    QVERIFY(failureOf(*m_model, kMoveHash).isEmpty());
    sync(true);
    qbtfeed::entries(*m_log, {entry(220, moveLine())});
    QVERIFY(failureOf(*m_model, kMoveHash).isEmpty());
    QCOMPARE(m_log->dismissed().size(), 1);

    // A new failure is a new line: it shows.
    qbtfeed::entries(*m_log, {entry(260, moveLine())});
    QVERIFY(!failureOf(*m_model, kMoveHash).isEmpty());
}

// D4: what dismissed() said, handed to the next omaseed (App keeps it in state.json).
void TestProblemLogFailures::dismissalsSurviveARestartOfOmaseed()
{
    qbtfeed::entries(*m_log, {entry(220, moveLine())});
    m_log->dismiss(kMoveHash);
    const QStringList kept = m_log->dismissed();

    TorrentModel model;
    ProblemLog next;
    next.setTorrents(&model);
    qbtfeed::full(
        model,
        {{kMoveHash,
          torrent(QStringLiteral("log55-move.bin"), kRoot + QStringLiteral("/src"), kMoveHash)}});
    next.onSynced(true);
    next.setDismissed(kept);
    qbtfeed::entries(next, {entry(220, moveLine())});
    QVERIFY(failureOf(model, kMoveHash).isEmpty());
    QCOMPARE(next.dismissed(), kept);
}

// D3: qBittorrent's log is in memory and its ids start from 0 again after a restart. The
// restart's full update makes the next read start at the top; the line is not there any
// more, so the failure goes, and its dismissal is forgotten.
void TestProblemLogFailures::aRestartOfQbittorrentForgetsTheFailures()
{
    qbtfeed::entries(
        *m_log,
        {entry(220, moveLine()), entry(221, renameLine(QStringLiteral("log55-rename/b.txt")))});
    m_log->dismiss(kRenameHash);
    QVERIFY(!failureOf(*m_model, kMoveHash).isEmpty());
    QCOMPARE(m_log->lastId(), 221);

    QSignalSpy dismissed(m_log, &ProblemLog::dismissedChanged);
    sync(true);
    QCOMPARE(m_log->lastId(), -1);
    QVERIFY(failureOf(*m_model, kMoveHash).isEmpty());
    // The new log: a few lines, ids from 0, none of them the old ones.
    qbtfeed::entries(
        *m_log,
        {entry(3, QStringLiteral("Detected inconsistent data. Torrent: \"x\""), kLoggedAt + 600)});
    QVERIFY(failureOf(*m_model, kMoveHash).isEmpty());
    QCOMPARE(dismissed.count(), 1);
    QVERIFY(m_log->dismissed().isEmpty());
}

void TestProblemLogFailures::aLineFromBeforeATorrentWasAddedIsNotAboutIt()
{
    qbtfeed::entries(*m_log, {entry(220, moveLine(), kAddedAt - 60)});
    QVERIFY(failureOf(*m_model, kMoveHash).isEmpty());
}

void TestProblemLogFailures::webSeedLinesGoToTheirTorrent()
{
    QSignalSpy changed(m_log, &ProblemLog::logLinesChanged);
    const QString notFound = QStringLiteral(
        "Received error message from URL seed. Torrent: \"log55-webseed.bin\". URL: "
        "\"http://127.0.0.1:26955/pub/log55-webseed.bin\". Message: \"404 File not found\"");
    const QString refused = QStringLiteral(
        "URL seed connection failed. Torrent: \"log55-webseed.bin\". URL: "
        "\"http://127.0.0.1:26956/dead/\". Error: \"014396 url seed (http://127.0.0.1:26956/dead/) "
        "failed: Connection refused\"");
    qbtfeed::entries(*m_log, {entry(227, notFound), entry(232, refused, kLoggedAt + 60)});
    QCOMPARE(changed.count(), 1);
    const QList<WebSeedLogLine> lines = m_log->webSeedLines(kSeedHash);
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0).url, QStringLiteral("http://127.0.0.1:26955/pub/log55-webseed.bin"));
    QCOMPARE(lines.at(0).status, QStringLiteral("404 File not found"));
    QCOMPARE(lines.at(1).status, QStringLiteral("connection refused"));
    QCOMPARE(lines.at(1).at, QDateTime::fromSecsSinceEpoch(kLoggedAt + 60));
    // Never a block in General (D5).
    QVERIFY(failureOf(*m_model, kSeedHash).isEmpty());
    QVERIFY(m_log->webSeedLines(kMoveHash).isEmpty());
}

void TestProblemLogFailures::theNewestExportFailureIsKept()
{
    const QString line =
        QStringLiteral("Failed to export torrent. Torrent: \"log55-webseed.bin\". Destination: "
                       "\"%1/ro-export/log55-webseed.bin.torrent\". Reason: \"Permission denied\"")
            .arg(kRoot);
    QSignalSpy changed(m_log, &ProblemLog::logLinesChanged);
    qbtfeed::entries(*m_log, {entry(226, line)});
    QCOMPARE(changed.count(), 1);
    const QVariantMap failure = m_log->exportFailure();
    QCOMPARE(failure.value(QStringLiteral("folder")).toString(), kRoot + u"/ro-export");
    QCOMPARE(failure.value(QStringLiteral("text")).toString(),
             QStringLiteral("The last copy failed: Permission denied, writing "
                            "log55-webseed.bin.torrent"));
    QCOMPARE(failure.value(QStringLiteral("line")).toString(), line);
    QVERIFY(failureOf(*m_model, kSeedHash).isEmpty());

    // A restart of qBittorrent: gone with its log.
    sync(true);
    qbtfeed::entries(*m_log, {});
    QVERIFY(m_log->exportFailure().isEmpty());
}

void TestProblemLogFailures::aWatchedMoveTellsAtOnce()
{
    QSignalSpy told(m_log, &ProblemLog::watchedFailure);
    m_log->watchMove({kMoveHash}, kRoot + QStringLiteral("/dest"));
    QVERIFY(m_log->isWatching());
    qbtfeed::entries(*m_log, {entry(220, moveLine())});
    QCOMPARE(told.count(), 1);
    QCOMPARE(told.at(0).at(0).toString(),
             QStringLiteral("log55-move.bin didn't move: Permission denied"));
    QCOMPARE(told.at(0).at(1).toStringList(), QStringList{kMoveHash});
    // Its only torrent failed: nothing more to wait for. It stays as the block.
    QVERIFY(!m_log->isWatching());
    QVERIFY(!failureOf(*m_model, kMoveHash).isEmpty());
}

void TestProblemLogFailures::aBatchMoveTellsOnce()
{
    QSignalSpy told(m_log, &ProblemLog::watchedFailure);
    m_log->watchMove({kMoveHash, kRenameHash, kSeedHash}, kRoot + QStringLiteral("/dest"));
    qbtfeed::entries(*m_log, {entry(220, moveLine())});
    QCOMPARE(told.count(), 1);
    QCOMPARE(told.at(0).at(0).toString(), QStringLiteral("1 of 3 didn't move"));
    QVERIFY(m_log->isWatching());
    // A read with nothing new says nothing.
    qbtfeed::entries(*m_log, {});
    QCOMPARE(told.count(), 1);
}

void TestProblemLogFailures::aLineFromBeforeTheWatchIsNotTold()
{
    QSignalSpy told(m_log, &ProblemLog::watchedFailure);
    qbtfeed::entries(*m_log, {entry(220, moveLine())});
    m_log->watchMove({kMoveHash}, kRoot + QStringLiteral("/dest"));
    qbtfeed::entries(*m_log, {});
    QCOMPARE(told.count(), 0);
}

void TestProblemLogFailures::aWatchedRenameAndDeleteTell()
{
    QSignalSpy told(m_log, &ProblemLog::watchedFailure);
    m_log->watchRename(kRenameHash, 1);
    qbtfeed::entries(*m_log, {entry(221, renameLine(QStringLiteral("log55-rename/b.txt")))});
    QCOMPARE(told.count(), 1);
    QCOMPARE(told.at(0).at(0).toString(),
             QStringLiteral("b.txt kept its old name: Permission denied"));
    QCOMPARE(told.at(0).at(1).toStringList(), QStringList{kRenameHash});

    // The delete's names are taken before the torrent leaves the list.
    m_log->watchDelete({kDeleteHash});
    qbtfeed::partial(*m_model, {}, QJsonArray{kDeleteHash});
    sync();
    qbtfeed::entries(*m_log,
                     {entry(231,
                            QStringLiteral("Failed to remove torrent content. Torrent: "
                                           "\"log55-delete.bin\". Error: \"Permission denied\""))});
    QCOMPARE(told.count(), 2);
    QCOMPARE(told.at(1).at(0).toString(),
             QStringLiteral("log55-delete.bin was removed, its files weren't: Permission denied"));
    QVERIFY(told.at(1).at(1).toStringList().isEmpty());
}

void TestProblemLogFailures::aWatchEndsAfterItsTimeOrWhenTheMoveLanded()
{
    m_log->setWatchDuration(std::chrono::milliseconds(0));
    m_log->watchRename(kRenameHash, 1);
    sync();
    QVERIFY(!m_log->isWatching());

    m_log->setWatchDuration(std::chrono::seconds(30));
    m_log->watchMove({kMoveHash}, kRoot + QStringLiteral("/dest/"));
    sync();
    QVERIFY(m_log->isWatching());
    qbtfeed::partial(
        *m_model,
        {{kMoveHash, QJsonObject{{QStringLiteral("save_path"), kRoot + QStringLiteral("/dest")}}}},
        {});
    sync();
    QVERIFY(!m_log->isWatching());
}

void TestProblemLogFailures::readsEvery15sWhileShownAndNotWhileHidden()
{
    LogServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QbtClient client;
    client.setBaseUrl(server.url());
    m_log->setClient(&client);
    sync(true);
    // Nothing errored, the window shows: the first poll reads the log, for failures.
    QTRY_COMPARE(server.askedFrom.size(), 1);
    QCOMPARE(server.askedFrom.at(0), -1);
    sync();
    sync();
    QTest::qWait(100);
    QCOMPARE(server.askedFrom.size(), 1); // not again before 15 s

    m_log->setIdleInterval(std::chrono::milliseconds(0));
    m_log->setVisible(false);
    sync();
    QTest::qWait(100);
    QCOMPARE(server.askedFrom.size(), 1); // hidden: never for failures alone
    m_log->setVisible(true);
    sync();
    QTRY_COMPARE(server.askedFrom.size(), 2);
}

void TestProblemLogFailures::aWatchReadsOnEveryPoll()
{
    LogServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QbtClient client;
    client.setBaseUrl(server.url());
    m_log->setClient(&client);
    m_log->setVisible(false);
    sync(true);
    QTest::qWait(100);
    QCOMPARE(server.askedFrom.size(), 0);

    // A poll every 20 ms until `done`: a poll while a read is on its way asks nothing.
    const auto pollUntil = [this](const std::function<bool()> &done) {
        for (int i = 0; i < 100 && !done(); ++i) {
            sync();
            QTest::qWait(20);
        }
        return done();
    };
    QSignalSpy told(m_log, &ProblemLog::watchedFailure);
    m_log->watchMove({kMoveHash}, kRoot + QStringLiteral("/dest"));
    QVERIFY(pollUntil([&server] { return server.askedFrom.size() >= 3; }));
    // The line appears in the log: the next poll's read tells it.
    server.log = {entry(220, moveLine())};
    QVERIFY(pollUntil([&told] { return told.count() == 1; }));
    QVERIFY(!m_log->isWatching());
}

void TestProblemLogFailures::aFullUpdateReadsFromTheTopAgain()
{
    LogServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.log = {entry(220, moveLine())};
    QbtClient client;
    client.setBaseUrl(server.url());
    m_log->setClient(&client);
    m_log->setIdleInterval(std::chrono::milliseconds(0));
    sync(true);
    QTRY_VERIFY(!failureOf(*m_model, kMoveHash).isEmpty());
    sync();
    QTRY_COMPARE(server.askedFrom.size(), 2);
    QCOMPARE(server.askedFrom.at(1), 220);

    // qBittorrent restarted: a short new log, ids from 0. Asked from 220, it would answer []
    // until its new ids passed 220; the full update makes the read start at the top.
    server.log = {entry(2, QStringLiteral("Detected inconsistent data. Torrent: \"x\""))};
    qbtfeed::full(
        *m_model,
        {{kMoveHash,
          torrent(QStringLiteral("log55-move.bin"), kRoot + QStringLiteral("/src"), kMoveHash)}});
    sync(true);
    QTRY_COMPARE(server.askedFrom.size(), 3);
    QCOMPARE(server.askedFrom.at(2), -1);
    QTRY_COMPARE(m_log->lastId(), 2);
    QVERIFY(failureOf(*m_model, kMoveHash).isEmpty());
}

void TestProblemLogFailures::standingRenamesAskForTheFiles()
{
    LogServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.log = {entry(221, renameLine(QStringLiteral("log55-rename/b.txt")))};
    server.files = {QJsonObject{{QStringLiteral("name"), QStringLiteral("log55-rename/b2.txt")}}};
    QbtClient client;
    client.setBaseUrl(server.url());
    m_log->setClient(&client);
    sync(true);
    // The read finds the rename failure, asks for the torrent's files, and finds b.txt gone.
    QTRY_COMPARE(server.filesRequests, 1);
    QTRY_VERIFY(failureOf(*m_model, kRenameHash).isEmpty());
}

QTEST_GUILESS_MAIN(TestProblemLogFailures)
#include "tst_problemlog_failures.moc"
