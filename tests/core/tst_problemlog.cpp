// ProblemLog (docs/28, #54) with no server: the torrent model is fed maindata-shaped objects,
// the log lines go in through applyEntries(), and what comes out is read off the model's
// `problem` and `statusCause` roles.

#include "core/ProblemLog.hpp"
#include "core/TorrentModel.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using omaseed::core::ProblemLog;
using omaseed::core::TorrentModel;
using omaseed::core::TorrentProblem;

namespace {

const QString kA = QStringLiteral("ad91d5aa00000000000000000000000000000000");
const QString kB = QStringLiteral("81f2da0000000000000000000000000000000000");

QJsonObject torrent(const QString &name, const QString &state)
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("state"), state},
            {QStringLiteral("save_path"), QStringLiteral("/d")},
            {QStringLiteral("content_path"), QStringLiteral("/d/") + name}};
}

QJsonObject entry(int id, const QString &message)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("timestamp"), 1791928475},
            {QStringLiteral("type"), 4},
            {QStringLiteral("message"), message}};
}

QString fileError(const QString &prefix, const QString &name, const QString &error)
{
    return QStringLiteral("File error alert. Torrent: \"%2\". File: \"/d/%2\". Reason: \"%1 "
                          "file_open (/d/%2) error: %3\"")
        .arg(prefix, name, error);
}

QVariantMap problemOf(const TorrentModel &model, const QString &hash)
{
    return model.data(model.index(model.indexOfHash(hash)), TorrentModel::ProblemRole).toMap();
}

QString causeOf(const TorrentModel &model, const QString &hash)
{
    return model.data(model.index(model.indexOfHash(hash)), TorrentModel::StatusCauseRole)
        .toString();
}

} // namespace

class TestProblemLog : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void anErroredTorrentGetsItsLine();
    void noLineSaysNotLogged();
    void leavingErrorDropsTheProblem();
    void repeatedLinesAreCounted();
    void missingFilesGetsTheRestoreLine();
    void noticeOnlyForErrorsWhileWatching();
    void aFullUpdateReadsTheLogAgain();
    void healthyTorrentsAreLeftAlone();

private:
    TorrentModel *m_model = nullptr;
    ProblemLog *m_log = nullptr;
};

void TestProblemLog::init()
{
    m_model = new TorrentModel;
    m_log = new ProblemLog;
    m_log->setTorrents(m_model);
    m_model->applyFullUpdate({{kA, torrent(QStringLiteral("a.bin"), QStringLiteral("error"))},
                              {kB, torrent(QStringLiteral("b.txt"), QStringLiteral("stalledUP"))}});
    m_log->onSynced(true);
}

void TestProblemLog::cleanup()
{
    delete m_log;
    delete m_model;
}

void TestProblemLog::anErroredTorrentGetsItsLine()
{
    QSignalSpy changed(m_model, &TorrentModel::problemsChanged);
    m_log->applyEntries({entry(30,
                               fileError(QStringLiteral("ad91d5"),
                                         QStringLiteral("a.bin"),
                                         QStringLiteral("Permission denied")))});
    QCOMPARE(m_log->lastId(), 30);
    QCOMPARE(changed.count(), 1);

    const QVariantMap problem = problemOf(*m_model, kA);
    QCOMPARE(problem.value(QStringLiteral("found")).toBool(), true);
    QCOMPARE(problem.value(QStringLiteral("headline")).toString(),
             QStringLiteral("Can't write to its save folder"));
    QCOMPARE(problem.value(QStringLiteral("detail")).toString(),
             QStringLiteral("Permission denied, opening a.bin"));
    QCOMPARE(problem.value(QStringLiteral("folder")).toString(), QStringLiteral("/d"));
    QCOMPARE(problem.value(QStringLiteral("count")).toInt(), 1);
    QVERIFY(problem.value(QStringLiteral("line")).toString().startsWith(u"File error alert."));
    QCOMPARE(causeOf(*m_model, kA), QStringLiteral("no access"));

    // The same read again changes nothing.
    m_log->applyEntries({});
    QCOMPARE(changed.count(), 1);
}

void TestProblemLog::noLineSaysNotLogged()
{
    m_log->applyEntries({entry(3, QStringLiteral("WebUI: Now listening on IP: 127.0.0.1"))});
    QCOMPARE(m_log->lineCount(), 0);
    const QVariantMap problem = problemOf(*m_model, kA);
    QCOMPARE(problem.value(QStringLiteral("found")).toBool(), false);
    QCOMPARE(problem.value(QStringLiteral("headline")).toString(),
             QStringLiteral("qBittorrent didn't log a reason"));
    QCOMPARE(causeOf(*m_model, kA), QStringLiteral("error"));
}

void TestProblemLog::leavingErrorDropsTheProblem()
{
    m_log->applyEntries({entry(1,
                               fileError(QStringLiteral("ad91d5"),
                                         QStringLiteral("a.bin"),
                                         QStringLiteral("No space left on device")))});
    QCOMPARE(causeOf(*m_model, kA), QStringLiteral("disk full"));

    m_model->applyPartial(
        {{kA, QJsonObject{{QStringLiteral("state"), QStringLiteral("downloading")}}}}, {});
    m_log->onSynced(false);
    QVERIFY(problemOf(*m_model, kA).isEmpty());
    QVERIFY(causeOf(*m_model, kA).isEmpty());
}

void TestProblemLog::repeatedLinesAreCounted()
{
    const QString line = fileError(
        QStringLiteral("ad91d5"), QStringLiteral("a.bin"), QStringLiteral("Permission denied"));
    m_log->applyEntries({entry(1, line), entry(2, line)});
    QCOMPARE(problemOf(*m_model, kA).value(QStringLiteral("count")).toInt(), 2);
}

void TestProblemLog::missingFilesGetsTheRestoreLine()
{
    m_model->applyPartial(
        {{kB, QJsonObject{{QStringLiteral("state"), QStringLiteral("missingFiles")}}}}, {});
    m_log->onSynced(false);
    m_log->applyEntries({entry(28,
                               QStringLiteral("Failed to restore torrent. Files were probably "
                                              "moved or storage isn't accessible. Torrent: "
                                              "\"b.txt\". Reason: \"81f2da fast resume "
                                              "rejected. file_stat(/d/b.txt): mismatching file "
                                              "size\""))});
    const QVariantMap problem = problemOf(*m_model, kB);
    QCOMPARE(problem.value(QStringLiteral("missingFiles")).toBool(), true);
    QCOMPARE(problem.value(QStringLiteral("actions")).toStringList(),
             (QStringList{QStringLiteral("location"), QStringLiteral("recheck")}));
    QCOMPARE(causeOf(*m_model, kB), QStringLiteral("missing files"));
}

void TestProblemLog::noticeOnlyForErrorsWhileWatching()
{
    QSignalSpy errored(m_log, &ProblemLog::torrentErrored);
    // kA was already errored at the first sync: no notice for it.
    m_log->applyEntries({});
    QCOMPARE(errored.count(), 0);

    m_model->applyPartial({{kB, QJsonObject{{QStringLiteral("state"), QStringLiteral("error")}}}},
                          {});
    m_log->onSynced(false);
    QCOMPARE(errored.count(), 0); // waits for the log to say why
    m_log->applyEntries({entry(40,
                               fileError(QStringLiteral("81f2da"),
                                         QStringLiteral("b.txt"),
                                         QStringLiteral("Read-only file system")))});
    QCOMPARE(errored.count(), 1);
    QCOMPARE(errored.at(0).at(0).toString(), kB);
    QCOMPARE(errored.at(0).at(1).toString(), QStringLiteral("b.txt"));
    QCOMPARE(errored.at(0).at(2).value<TorrentProblem>().cause, QStringLiteral("read-only"));

    // Once per entry into error, not once per read.
    m_log->applyEntries({});
    QCOMPARE(errored.count(), 1);
}

void TestProblemLog::aFullUpdateReadsTheLogAgain()
{
    m_log->applyEntries({entry(33, QStringLiteral("x"))});
    QCOMPARE(m_log->lastId(), 33);
    m_model->applyFullUpdate({{kA, torrent(QStringLiteral("a.bin"), QStringLiteral("error"))}});
    m_log->onSynced(true);
    QCOMPARE(m_log->lastId(), -1);
    QVERIFY(problemOf(*m_model, kA).isEmpty()); // until the log has been read again
    m_log->applyEntries({entry(28,
                               fileError(QStringLiteral("ad91d5"),
                                         QStringLiteral("a.bin"),
                                         QStringLiteral("Permission denied")))});
    QCOMPARE(causeOf(*m_model, kA), QStringLiteral("no access"));
}

void TestProblemLog::healthyTorrentsAreLeftAlone()
{
    m_log->applyEntries({entry(1,
                               fileError(QStringLiteral("81f2da"),
                                         QStringLiteral("b.txt"),
                                         QStringLiteral("Permission denied")))});
    QVERIFY(problemOf(*m_model, kB).isEmpty());
    QVERIFY(causeOf(*m_model, kB).isEmpty());
}

QTEST_GUILESS_MAIN(TestProblemLog)
#include "tst_problemlog.moc"
