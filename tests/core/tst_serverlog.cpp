// ServerLog and LogFilterModel (docs/32 §6, D9–D15, #56): qBittorrent's whole log for the log
// face. The model is fed log/main-shaped entries through applyEntries(); the reads themselves
// go to a small HTTP server of our own, which answers log/main and records what was asked.

#include "QbtFeed.hpp"
#include "core/LogFilterModel.hpp"
#include "core/QbtClient.hpp"
#include "core/ServerLog.hpp"
#include "core/TorrentModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

#include <functional>
#include <memory>

using omaseed::core::LogFilterModel;
using omaseed::core::QbtClient;
using omaseed::core::ServerLog;
using omaseed::core::TorrentModel;

namespace {

const QString kMove = QStringLiteral("45857c0000000000000000000000000000000000");
const QString kOther = QStringLiteral("ad91d50000000000000000000000000000000000");

QJsonObject entry(int id, int type, const QString &message, qint64 at = 1791928475)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("timestamp"), at},
            {QStringLiteral("type"), type},
            {QStringLiteral("message"), message}};
}

// docs/32 §2 and L1's lines, as qBittorrent 5.2.3 wrote them on the playground.
const QString kListening =
    QStringLiteral("Successfully listening on IP. IP: \"127.0.0.1\". Port: \"TCP/26999\"");
const QString kAdded = QStringLiteral("Added new torrent. Torrent: \"log55-move.bin\"");
const QString kMoving = QStringLiteral(
    "Start moving torrent. Torrent: \"log55-move.bin\". Destination: \"/d/log55/dest\"");
const QString kMoveFailed = QStringLiteral(
    "Failed to move torrent. Torrent: \"log55-move.bin\". Source: \"/d/log55/src\". "
    "Destination: \"/d/log55/dest\". Reason: \"45857c storage move failed. file_rename "
    "(/d/log55/src/log55-move.bin): Permission denied\"");
const QString kRenamed = QStringLiteral("Torrent renamed. Torrent: \"log55-move.bin.old\"");
const QString kFileError = QStringLiteral(
    "File error alert. Torrent: \"other.bin\". File: \"/d/other.bin\". Reason: \"ad91d5 "
    "file_open (/d/other.bin) error: Permission denied\"");
const QString kNoName =
    QStringLiteral("File error alert. Torrent: \"renamed-since\". File: \"/d/x\". Reason: \"45857c "
                   "file_open (/d/x) error: Permission denied\"");

QJsonArray l1()
{
    return {entry(0, 2, kListening),
            entry(1, 1, kAdded),
            entry(2, 1, kMoving),
            entry(3, 4, kMoveFailed),
            entry(4, 4, kFileError),
            entry(5, 8, QStringLiteral("Permission denied: something critical"))};
}

QJsonObject torrent(const QString &name)
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("state"), QStringLiteral("stalledUP")},
            {QStringLiteral("save_path"), QStringLiteral("/d")}};
}

/// Answers log/main with whatever `lines` holds after the asked last_known_id, as qBittorrent
/// does, and keeps every last_known_id it was asked for.
class LogServer : public QTcpServer
{
public:
    explicit LogServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {
        [this]() { QVERIFY(listen(QHostAddress::LocalHost)); }();
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

    QJsonArray lines;
    QList<int> asked;
    QList<QUrlQuery> queries;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            if (buffer->indexOf("\r\n\r\n") < 0)
                return;
            const QByteArray target = buffer->split(' ').value(1);
            buffer->clear();
            const QUrl url(QString::fromLatin1(target));
            QByteArray body = "[]";
            if (url.path().endsWith(QLatin1String("log/main"))) {
                const QUrlQuery query(url);
                queries << query;
                const int last = query.queryItemValue(QStringLiteral("last_known_id")).toInt();
                asked << last;
                QJsonArray after;
                for (const QJsonValue &line : std::as_const(lines)) {
                    if (line.toObject().value(QStringLiteral("id")).toInt() > last)
                        after.append(line);
                }
                body = QJsonDocument(after).toJson(QJsonDocument::Compact);
            }
            QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
            response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
            response += "Connection: close\r\n\r\n" + body;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    }
};

QStringList messages(const QAbstractItemModel &model)
{
    QStringList list;
    for (int row = 0; row < model.rowCount(); ++row)
        list << model.data(model.index(row, 0), ServerLog::MessageRole).toString();
    return list;
}

} // namespace

class TestServerLog : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // ServerLog
    void linesArriveInOrderWithTheirLevels();
    void eachReadAddsOnlyWhatFollows();
    void theCapDropsTheOldest();
    void aFullUpdateStartsOver();
    void idsGoingBackwardsStartOver();
    void fromStartSaysWhereTheLogBegins();
    void timesCarryTheDateOnAnotherDay();
    void linesNameTheirTorrent();
    void aLineLeadsToItsTorrent();
    void copyLineIsTimeLevelMessage();

    // reading
    void nothingIsReadWhileInactive();
    void activeReadsAtOnceThenAfterEachPoll();
    void aRestartReadsFromTheTop();
    void releaseDropsTheLines();

    // LogFilterModel
    void levelsHideAndShow();
    void everyWordMustMatch();
    void aTorrentNarrowsToItsLines();
    void newLinesAreFilteredAsTheyCome();
    void nextProblemSkipsTheRest();
    void marksAndCopy();
    void marksGoWhenTheLogStartsOver();

private:
    TorrentModel *m_torrents = nullptr;
    ServerLog *m_log = nullptr;
    LogFilterModel *m_lines = nullptr;
};

void TestServerLog::init()
{
    m_torrents = new TorrentModel;
    qbtfeed::full(*m_torrents,
                  QJsonObject{{kMove, torrent(QStringLiteral("log55-move.bin"))},
                              {kOther, torrent(QStringLiteral("other.bin"))}});
    m_log = new ServerLog;
    m_log->setTorrents(m_torrents);
    m_lines = new LogFilterModel;
    m_lines->setLog(m_log);
}

void TestServerLog::cleanup()
{
    delete m_lines;
    delete m_log;
    delete m_torrents;
}

void TestServerLog::linesArriveInOrderWithTheirLevels()
{
    QSignalSpy loaded(m_log, &ServerLog::loadedChanged);
    m_log->applyEntries(l1());
    QCOMPARE(loaded.count(), 1);
    QVERIFY(m_log->isLoaded());
    QCOMPARE(m_log->rowCount(), 6);
    QCOMPARE(m_log->lastId(), 5);
    QCOMPARE(m_log->levelCounts(), QList<int>({2, 1, 2, 1}));
    QCOMPARE(m_log->countAt(ServerLog::Warning | ServerLog::Critical), 3);
    const QModelIndex first = m_log->index(0, 0);
    QCOMPARE(m_log->data(first, ServerLog::LevelRole).toInt(), int(ServerLog::Info));
    QCOMPARE(m_log->data(first, ServerLog::LevelNameRole).toString(), QStringLiteral("info"));
    QCOMPARE(m_log->data(first, ServerLog::MessageRole).toString(), kListening);
    QCOMPARE(m_log->data(m_log->index(5, 0), ServerLog::LevelNameRole).toString(),
             QStringLiteral("critical"));
}

void TestServerLog::eachReadAddsOnlyWhatFollows()
{
    m_log->applyEntries(l1());
    QSignalSpy inserted(m_log, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(m_log, &QAbstractItemModel::modelReset);
    m_log->applyEntries({entry(6, 1, kRenamed)});
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 6);
    QCOMPARE(inserted.first().at(2).toInt(), 6);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(m_log->lastId(), 6);
    // Nothing new: nothing happens.
    m_log->applyEntries({});
    QCOMPARE(inserted.count(), 1);
}

void TestServerLog::theCapDropsTheOldest()
{
    m_log->setCapacity(4);
    m_log->applyEntries(l1()); // six lines into four: the first two go
    QCOMPARE(m_log->rowCount(), 4);
    QCOMPARE(m_log->data(m_log->index(0, 0), ServerLog::IdRole).toInt(), 2);
    QCOMPARE(m_log->levelCounts(), QList<int>({1, 0, 2, 1}));
    QVERIFY(!m_log->fromStart());

    QSignalSpy removed(m_log, &QAbstractItemModel::rowsRemoved);
    m_log->applyEntries({entry(6, 1, kRenamed), entry(7, 2, kListening)});
    QCOMPARE(removed.count(), 1);
    QCOMPARE(m_log->rowCount(), 4);
    QCOMPARE(m_log->data(m_log->index(0, 0), ServerLog::IdRole).toInt(), 4);
    QCOMPARE(m_log->data(m_log->index(3, 0), ServerLog::IdRole).toInt(), 7);
    QCOMPARE(m_log->levelCounts(), QList<int>({1, 1, 1, 1}));
    QCOMPARE(m_log->total(), 4);
}

void TestServerLog::aFullUpdateStartsOver()
{
    m_log->applyEntries(l1());
    QSignalSpy restarted(m_log, &ServerLog::restarted);
    m_log->onSynced(false, false);
    QCOMPARE(restarted.count(), 0);
    QCOMPARE(m_log->rowCount(), 6);

    // qBittorrent restarted (a restart always brings a full update): its ids start from 0.
    m_log->onSynced(true, false);
    QCOMPARE(restarted.count(), 1);
    QCOMPARE(m_log->rowCount(), 0);
    QCOMPARE(m_log->lastId(), -1);
    QVERIFY(!m_log->isLoaded());
    QCOMPARE(m_log->levelCounts(), QList<int>({0, 0, 0, 0}));
    m_log->applyEntries({entry(0, 2, kListening), entry(1, 1, kAdded)});
    QCOMPARE(m_log->rowCount(), 2);
    QCOMPARE(m_log->lastId(), 1);
    QVERIFY(m_log->fromStart());
}

void TestServerLog::idsGoingBackwardsStartOver()
{
    m_log->applyEntries(l1());
    QSignalSpy restarted(m_log, &ServerLog::restarted);
    // Not something qBittorrent sends after last_known_id 5: another log.
    m_log->applyEntries({entry(2, 1, kRenamed)});
    QCOMPARE(restarted.count(), 1);
    QCOMPARE(m_log->rowCount(), 0);
    QCOMPARE(m_log->lastId(), -1);
}

void TestServerLog::fromStartSaysWhereTheLogBegins()
{
    QVERIFY(m_log->firstAtText().isEmpty());
    const QDateTime started(QDate::currentDate(), QTime(19, 52, 44));
    m_log->applyEntries({entry(0, 2, kListening, started.toSecsSinceEpoch())});
    QVERIFY(m_log->fromStart());
    QCOMPARE(m_log->firstAtText(), QStringLiteral("19:52"));

    ServerLog wrapped;
    wrapped.applyEntries({entry(4120, 1, kAdded, started.addDays(-2).toSecsSinceEpoch())});
    QVERIFY(!wrapped.fromStart());
    QCOMPARE(wrapped.firstAtText(),
             QLocale::c().toString(started.addDays(-2), QStringLiteral("d MMM HH:mm")));
}

void TestServerLog::timesCarryTheDateOnAnotherDay()
{
    const QDateTime now(QDate(2026, 10, 15), QTime(12, 0));
    const QDateTime today(QDate(2026, 10, 15), QTime(9, 41, 36));
    const QDateTime before(QDate(2026, 10, 13), QTime(19, 52, 44));
    const QDateTime lastYear(QDate(2025, 12, 31), QTime(23, 0, 1));
    QCOMPARE(ServerLog::timeText(today.toSecsSinceEpoch(), now), QStringLiteral("09:41:36"));
    QCOMPARE(ServerLog::timeText(before.toSecsSinceEpoch(), now),
             QStringLiteral("13 Oct 19:52:44"));
    QCOMPARE(ServerLog::timeText(lastYear.toSecsSinceEpoch(), now),
             QStringLiteral("31 Dec 2025 23:00:01"));
}

void TestServerLog::linesNameTheirTorrent()
{
    const QString name = QStringLiteral("log55-move.bin");
    const QString prefix = QStringLiteral("45857c");
    QVERIFY(ServerLog::names(kAdded, name, prefix));
    QVERIFY(ServerLog::names(kMoveFailed, name, prefix));
    // By libtorrent's hash prefix alone, the torrent having been renamed since.
    QVERIFY(ServerLog::names(kNoName, name, prefix));
    QVERIFY(!ServerLog::names(kFileError, name, prefix));
    // A longer name that starts with this one is another torrent.
    QVERIFY(!ServerLog::names(kRenamed, name, prefix));
    QVERIFY(!ServerLog::names(kListening, name, prefix));
    // libtorrent 2.0 puts the name where 2.1 puts the prefix.
    QVERIFY(
        ServerLog::names(QStringLiteral("x. Reason: \"log55-move.bin file_open (/d/x) error: y\""),
                         name,
                         QString()));
}

void TestServerLog::aLineLeadsToItsTorrent()
{
    m_log->applyEntries(l1());
    QCOMPARE(m_log->torrentAt(0), QString()); // "127.0.0.1" is no torrent
    QCOMPARE(m_log->torrentAt(1), kMove);
    QCOMPARE(m_log->torrentAt(3), kMove);
    QCOMPARE(m_log->torrentAt(4), kOther);
    QCOMPARE(m_log->torrentAt(5), QString());
    m_log->applyEntries({entry(6, 4, kNoName)});
    QCOMPARE(m_log->torrentAt(6), kMove); // by its prefix
    QCOMPARE(m_log->torrentAt(99), QString());
}

void TestServerLog::copyLineIsTimeLevelMessage()
{
    const QDateTime at(QDate(2026, 10, 15), QTime(9, 41, 36));
    m_log->applyEntries({entry(0, 4, kMoveFailed, at.toSecsSinceEpoch())});
    QCOMPARE(m_log->copyLine(0), QStringLiteral("2026-10-15 09:41:36 warning ") + kMoveFailed);
}

void TestServerLog::nothingIsReadWhileInactive()
{
    LogServer server;
    server.lines = l1();
    QbtClient client;
    client.setBaseUrl(server.url());
    m_log->setClient(&client);
    for (int i = 0; i < 3; ++i)
        m_log->onSynced(false);
    QTest::qWait(150);
    QVERIFY(server.asked.isEmpty());
    QCOMPARE(m_log->rowCount(), 0);
}

void TestServerLog::activeReadsAtOnceThenAfterEachPoll()
{
    LogServer server;
    server.lines = l1();
    QbtClient client;
    client.setBaseUrl(server.url());
    m_log->setClient(&client);

    m_log->setActive(true);
    QTRY_COMPARE(m_log->rowCount(), 6);
    QCOMPARE(server.asked, QList<int>({-1}));
    const QUrlQuery query = server.queries.constFirst();
    for (const char *level : {"normal", "info", "warning", "critical"})
        QCOMPARE(query.queryItemValue(QString::fromLatin1(level)), QStringLiteral("true"));

    server.lines.append(entry(6, 1, kRenamed));
    m_log->onSynced(false);
    QTRY_COMPARE(m_log->rowCount(), 7);
    QCOMPARE(server.asked, QList<int>({-1, 5}));

    // Off screen again: the next polls read nothing.
    m_log->setActive(false);
    m_log->onSynced(false);
    QTest::qWait(150);
    QCOMPARE(server.asked.size(), 2);
}

void TestServerLog::aRestartReadsFromTheTop()
{
    LogServer server;
    server.lines = l1();
    QbtClient client;
    client.setBaseUrl(server.url());
    m_log->setClient(&client);
    m_log->setActive(true);
    QTRY_COMPARE(m_log->rowCount(), 6);

    // qBittorrent restarts: two lines so far, ids from 0, and asked after 5 it says nothing.
    server.lines = {entry(0, 2, kListening), entry(1, 1, kAdded)};
    m_log->onSynced(true);
    QTRY_COMPARE(m_log->rowCount(), 2);
    QCOMPARE(server.asked.constLast(), -1);
    QCOMPARE(m_log->lastId(), 1);
    QVERIFY(m_log->fromStart());
}

void TestServerLog::releaseDropsTheLines()
{
    LogServer server;
    server.lines = l1();
    QbtClient client;
    client.setBaseUrl(server.url());
    m_log->setClient(&client);
    m_log->setActive(true);
    QTRY_COMPARE(m_log->rowCount(), 6);
    m_log->setActive(false);
    m_log->release();
    QCOMPARE(m_log->rowCount(), 0);
    QCOMPARE(m_log->lastId(), -1);
    // Opened again: the whole log, from the top.
    m_log->setActive(true);
    QTRY_COMPARE(m_log->rowCount(), 6);
    QCOMPARE(server.asked.constLast(), -1);
}

void TestServerLog::levelsHideAndShow()
{
    m_log->applyEntries(l1());
    QCOMPARE(m_lines->count(), 6);
    QCOMPARE(m_lines->levelTotal(), 6);
    QSignalSpy count(m_lines, &LogFilterModel::countChanged);
    m_lines->toggleLevel(ServerLog::Normal);
    m_lines->toggleLevel(ServerLog::Info);
    QCOMPARE(m_lines->levels(), int(ServerLog::Warning | ServerLog::Critical));
    QCOMPARE(m_lines->count(), 3);
    QCOMPARE(m_lines->levelTotal(), 3);
    QVERIFY(count.count() > 0);
    QVERIFY(!m_lines->isNarrowed()); // levels alone don't count as narrowing
    m_lines->toggleLevel(ServerLog::Info);
    QCOMPARE(m_lines->count(), 4);
    m_lines->setLevels(0);
    QCOMPARE(m_lines->count(), 0);
}

void TestServerLog::everyWordMustMatch()
{
    m_log->applyEntries(l1());
    QCOMPARE(LogFilterModel::words(QStringLiteral("  Permission   MOVE ")),
             QStringList({QStringLiteral("permission"), QStringLiteral("move")}));
    m_lines->setText(QStringLiteral("permission"));
    QVERIFY(m_lines->isNarrowed());
    QCOMPARE(m_lines->count(), 3);
    m_lines->setText(QStringLiteral("PERMISSION move"));
    QCOMPARE(messages(*m_lines), QStringList({kMoveFailed}));
    m_lines->setText(QStringLiteral("permission move nothing-like-this"));
    QCOMPARE(m_lines->count(), 0);
    QCOMPARE(m_lines->levelTotal(), 6); // the M of "0 of 6 lines"
    m_lines->setText(QString());
    QVERIFY(!m_lines->isNarrowed());
    QCOMPARE(m_lines->count(), 6);
}

void TestServerLog::aTorrentNarrowsToItsLines()
{
    m_log->applyEntries(l1());
    m_log->applyEntries({entry(6, 1, kRenamed), entry(7, 4, kNoName)});
    QVERIFY(!m_lines->setTorrent(QStringLiteral("not-in-the-library")));
    QVERIFY(m_lines->torrentHash().isEmpty());

    QSignalSpy narrowed(m_lines, &LogFilterModel::narrowedChanged);
    QVERIFY(m_lines->setTorrent(kMove));
    QCOMPARE(narrowed.count(), 1);
    QCOMPARE(m_lines->torrentName(), QStringLiteral("log55-move.bin"));
    QCOMPARE(messages(*m_lines), QStringList({kAdded, kMoving, kMoveFailed, kNoName}));
    // With the levels and words on top (L2: warnings, "permission").
    m_lines->setLevels(ServerLog::Warning | ServerLog::Critical);
    m_lines->setText(QStringLiteral("permission"));
    QCOMPARE(messages(*m_lines), QStringList({kMoveFailed, kNoName}));
    QCOMPARE(m_lines->torrentAt(0), kMove);

    m_lines->clearTorrent();
    QCOMPARE(m_lines->count(), 4);
}

void TestServerLog::newLinesAreFilteredAsTheyCome()
{
    m_log->applyEntries(l1());
    QVERIFY(m_lines->setTorrent(kOther));
    QCOMPARE(m_lines->count(), 1);
    QSignalSpy inserted(m_lines, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(m_lines, &QAbstractItemModel::modelReset);
    m_log->applyEntries({entry(6, 1, kRenamed),
                         entry(7, 1, QStringLiteral("Torrent resumed. Torrent: \"other.bin\""))});
    QCOMPARE(m_lines->count(), 2);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(reset.count(), 0);
    QSignalSpy arrived(m_lines, &LogFilterModel::linesArrived);
    m_log->applyEntries({entry(8, 4, kFileError), entry(9, 1, kAdded)});
    QCOMPARE(arrived.count(), 1);
    QCOMPARE(arrived.first().at(0).toInt(), 1); // one of the two is about other.bin
    // A change of filter adds rows, but no line arrived.
    m_lines->clearTorrent();
    QCOMPARE(arrived.count(), 1);
    m_log->applyEntries({entry(10, 1, kRenamed)});
    QCOMPARE(arrived.count(), 2);
}

void TestServerLog::nextProblemSkipsTheRest()
{
    m_log->applyEntries(l1()); // warnings at 3, 4; critical at 5
    QCOMPARE(m_lines->nextProblem(-1, 1), 3);
    QCOMPARE(m_lines->nextProblem(3, 1), 4);
    QCOMPARE(m_lines->nextProblem(4, 1), 5);
    QCOMPARE(m_lines->nextProblem(5, 1), -1);
    QCOMPARE(m_lines->nextProblem(5, -1), 4);
    QCOMPARE(m_lines->nextProblem(3, -1), -1);
}

void TestServerLog::marksAndCopy()
{
    const QDateTime at(QDate(2026, 10, 15), QTime(9, 41, 36));
    QJsonArray lines;
    for (const QJsonValue &line : l1()) {
        QJsonObject object = line.toObject();
        object.insert(QStringLiteral("timestamp"), at.toSecsSinceEpoch());
        lines.append(object);
    }
    m_log->applyEntries(lines);

    // Nothing marked: the cursor's line.
    QCOMPARE(m_lines->copyCount(3), 1);
    QCOMPARE(m_lines->copyText(3), QStringLiteral("2026-10-15 09:41:36 warning ") + kMoveFailed);
    QCOMPARE(m_lines->copyCount(-1), 0);

    QSignalSpy marks(m_lines, &LogFilterModel::marksChanged);
    m_lines->toggleMark(4);
    m_lines->toggleMark(1);
    QCOMPARE(marks.count(), 2);
    QCOMPARE(m_lines->markedCount(), 2);
    QVERIFY(m_lines->isMarked(1));
    QCOMPARE(m_lines->data(m_lines->index(4, 0), LogFilterModel::MarkedRole).toBool(), true);
    // Oldest first, whatever order they were marked in, and not the cursor's.
    QCOMPARE(m_lines->copyCount(3), 2);
    QCOMPARE(m_lines->copyText(3),
             QStringLiteral("2026-10-15 09:41:36 normal %1\n2026-10-15 09:41:36 warning %2")
                 .arg(kAdded, kFileError));
    m_lines->toggleMark(1);
    QCOMPARE(m_lines->markedCount(), 1);

    m_lines->markRange(2, 0);
    QCOMPARE(m_lines->markedCount(), 3);
    QVERIFY(!m_lines->isMarked(4));
    m_lines->markRange(5, 5, true);
    QCOMPARE(m_lines->markedCount(), 4);

    // Marks belong to lines, not rows: a filter hides some, and they come back marked.
    m_lines->setText(QStringLiteral("permission"));
    QCOMPARE(m_lines->copyCount(0), 1); // only the critical line is marked and shown
    m_lines->setText(QString());
    QCOMPARE(m_lines->markedCount(), 4);

    m_lines->markAll();
    QCOMPARE(m_lines->markedCount(), 6);
    m_lines->clearMarks();
    QCOMPARE(m_lines->markedCount(), 0);
}

void TestServerLog::marksGoWhenTheLogStartsOver()
{
    m_log->applyEntries(l1());
    m_lines->markAll();
    m_log->onSynced(true, false);
    QCOMPARE(m_lines->markedCount(), 0);
    QCOMPARE(m_lines->count(), 0);
}

QTEST_GUILESS_MAIN(TestServerLog)
#include "tst_serverlog.moc"
