#include "core/MoveJob.hpp"
#include "core/TorrentModel.hpp"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using omaseed::core::ApiError;
using omaseed::core::MoveJob;
using omaseed::core::MovePlan;
using omaseed::core::Torrent;
using omaseed::core::TorrentModel;

namespace {

const QString kRoot = QStringLiteral("/mnt/archive/games");
const QString kTo = QStringLiteral("/mnt/fast/games");
constexpr qint64 kMiB = 1024 * 1024;

QString hashOf(int i)
{
    return QStringLiteral("%1").arg(i, 40, 16, QLatin1Char('0'));
}

/// A small server: torrents with a save path and a state, applied to the model as a poll.
struct Library
{
    struct Row
    {
        QString name;
        QString savePath;
        QString state = QStringLiteral("stalledUP");
        qint64 bytes = 0;
    };
    QMap<QString, Row> rows;

    void add(int i, qint64 bytes, const QString &savePath = kRoot)
    {
        rows.insert(hashOf(i),
                    {QStringLiteral("t%1").arg(i), savePath, QStringLiteral("stalledUP"), bytes});
    }
    void moving(int i) { rows[hashOf(i)].state = QStringLiteral("moving"); }
    void land(int i, const QString &at = kTo)
    {
        rows[hashOf(i)].savePath = at;
        rows[hashOf(i)].state = QStringLiteral("stalledUP");
    }
    void state(int i, const QString &state) { rows[hashOf(i)].state = state; }

    QJsonObject json() const
    {
        QJsonObject out;
        for (auto it = rows.cbegin(); it != rows.cend(); ++it) {
            out.insert(it.key(),
                       QJsonObject{{QStringLiteral("name"), it->name},
                                   {QStringLiteral("save_path"), it->savePath},
                                   {QStringLiteral("content_path"), it->savePath + "/" + it->name},
                                   {QStringLiteral("state"), it->state},
                                   {QStringLiteral("completed"), it->bytes},
                                   {QStringLiteral("size"), it->bytes},
                                   {QStringLiteral("added_on"), 1792075500}});
        }
        return out;
    }
    QList<Torrent> torrents() const
    {
        TorrentModel model;
        model.applyFullUpdate(json());
        QList<Torrent> out;
        for (int row = 0; row < model.count(); ++row)
            out << model.torrentAt(row);
        return out;
    }
};

} // namespace

// Board 42 §4–§6 (DECISIONS C15 M6, M10–M16, M18): the job.
class TestMoveJob : public QObject
{
    Q_OBJECT

    Library m_library;
    TorrentModel *m_model = nullptr;
    MoveJob *m_job = nullptr;
    QDateTime m_now;
    QList<QPair<QStringList, QString>> m_sent;

    void poll(bool full = false)
    {
        m_model->applyFullUpdate(m_library.json());
        m_job->onSynced(full);
    }
    void wait(qint64 seconds) { m_now = m_now.addSecs(seconds); }
    MovePlan plan(const QString &to = kTo) const
    {
        MovePlan::Input in;
        in.torrents = m_library.torrents();
        in.root = kRoot;
        in.newPath = to;
        return MovePlan::make(in);
    }
    QStringList sentHashes() const
    {
        QStringList out;
        for (const auto &one : m_sent)
            out += one.first;
        return out;
    }
    void newJob()
    {
        delete m_job;
        m_job = new MoveJob;
        m_job->setTorrents(m_model);
        m_job->setClock([this] { return m_now; });
        connect(m_job,
                &MoveJob::sendRequested,
                m_job,
                [this](const QStringList &h, const QString &l) { m_sent.append({h, l}); });
    }

private slots:
    void init()
    {
        m_library = Library();
        m_model = new TorrentModel;
        m_now = QDateTime(QDate(2026, 10, 18), QTime(14, 0), QTimeZone::UTC);
        m_sent.clear();
        newJob();
    }
    void cleanup()
    {
        delete m_job;
        m_job = nullptr;
        delete m_model;
        m_model = nullptr;
    }

    void wavesSmallestFirstThreeAhead()
    {
        for (int i = 1; i <= 6; ++i)
            m_library.add(i, i * kMiB, i % 2 ? kRoot : kRoot + "/handheld");
        poll();
        QSignalSpy finished(m_job, &MoveJob::finished);
        QVERIFY(m_job->start(plan(), QStringLiteral("home")));
        QCOMPARE(m_job->state(), MoveJob::State::Running);
        // M6: the smallest goes alone.
        QCOMPARE(sentHashes(), QStringList({hashOf(1)}));
        QCOMPARE(m_sent.at(0).second, kTo);
        m_library.moving(1);
        poll();
        QCOMPARE(m_job->current(), hashOf(1));
        QCOMPARE(m_job->entry(hashOf(1))->step, MoveJob::Step::Moving);
        QCOMPARE(m_sent.size(), 1);

        wait(5);
        m_library.land(1);
        poll();
        QCOMPARE(m_job->moved(), 1);
        // Then three with the server: one moving, two behind.
        QCOMPARE(sentHashes(), QStringList({hashOf(1), hashOf(2), hashOf(3), hashOf(4)}));
        QCOMPARE(m_sent.at(1).second, kTo + "/handheld"); // keeps its place below the folder
        QCOMPARE(m_job->withServer(), 3);
        m_library.moving(2);
        m_library.moving(3);
        m_library.moving(4);
        poll();
        QCOMPARE(m_job->current(), hashOf(2));
        QCOMPARE(m_job->entry(hashOf(3))->step, MoveJob::Step::Queued);

        wait(5);
        m_library.land(2, kTo + "/handheld");
        poll();
        QCOMPARE(m_sent.size(), 5);
        QCOMPARE(m_job->current(), hashOf(3));
        for (int i = 3; i <= 6; ++i) {
            wait(5);
            m_library.land(i, i % 2 ? kTo : kTo + "/handheld");
            poll();
        }
        QCOMPARE(m_job->state(), MoveJob::State::Done);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(m_job->moved(), 6);
        QCOMPARE(m_job->percent(), 100);
        const MoveJob::Summary summary = m_job->summary();
        QCOMPARE(summary.moved, 6);
        QCOMPARE(summary.failed, 0);
        QVERIFY(!summary.endedEarly);
    }

    void theFirstFailingStopsTheJob()
    {
        for (int i = 1; i <= 3; ++i)
            m_library.add(i, i * kMiB);
        poll();
        QSignalSpy paused(m_job, &MoveJob::pausedItself);
        m_job->start(plan(), QStringLiteral("home"));
        m_job->onFailed(hashOf(1),
                        m_now,
                        QStringLiteral("Permission denied"),
                        QStringLiteral("noAccess"),
                        QStringLiteral("Failed to move torrent. …"));
        QCOMPARE(m_job->state(), MoveJob::State::Paused);
        QCOMPARE(m_job->pauseReason(), MoveJob::PauseReason::FirstFailed);
        QCOMPARE(paused.count(), 1);
        QCOMPARE(m_job->entry(hashOf(1))->reason, QStringLiteral("Permission denied"));
        poll();
        QCOMPARE(m_sent.size(), 1); // nothing else was sent
    }

    void aLaterFailureMarksAndGoesOn()
    {
        for (int i = 1; i <= 4; ++i)
            m_library.add(i, i * kMiB);
        poll();
        m_job->start(plan(), QStringLiteral("home"));
        m_library.land(1);
        poll();
        QCOMPARE(m_sent.size(), 4);
        m_job->onFailed(hashOf(3),
                        m_now,
                        QStringLiteral("Input/output error"),
                        QStringLiteral("diskError"),
                        QString());
        QCOMPARE(m_job->state(), MoveJob::State::Running);
        QCOMPARE(m_job->failed(), 1);
        // A line from before the send is about an earlier one.
        m_job->onFailed(hashOf(4), m_now.addSecs(-60), QStringLiteral("x"), QString(), QString());
        QCOMPARE(m_job->failed(), 1);
        // No space pauses it (T3).
        QSignalSpy paused(m_job, &MoveJob::pausedItself);
        m_job->onFailed(hashOf(4),
                        m_now,
                        QStringLiteral("No space left on device"),
                        QStringLiteral("diskFull"),
                        QString());
        QCOMPARE(paused.count(), 1);
        QCOMPARE(m_job->pauseReason(), MoveJob::PauseReason::NoSpace);
    }

    void pauseLetsTheQueuedFinish()
    {
        for (int i = 1; i <= 8; ++i)
            m_library.add(i, kMiB);
        poll();
        m_job->start(plan(), QStringLiteral("home"));
        m_library.land(1);
        poll();
        QCOMPARE(m_job->withServer(), 3);
        m_job->pause();
        QCOMPARE(m_job->state(), MoveJob::State::Pausing);
        for (int i = 2; i <= 4; ++i)
            m_library.land(i);
        poll();
        QCOMPARE(m_job->state(), MoveJob::State::Paused);
        QCOMPARE(m_sent.size(), 4);
        m_job->resume();
        QCOMPARE(m_job->state(), MoveJob::State::Running);
        QCOMPARE(m_sent.size(), 7);
    }

    void endTheMove()
    {
        for (int i = 1; i <= 6; ++i)
            m_library.add(i, kMiB);
        poll();
        QSignalSpy finished(m_job, &MoveJob::finished);
        m_job->start(plan(), QStringLiteral("home"));
        m_library.land(1);
        poll();
        m_job->end();
        QCOMPARE(m_job->state(), MoveJob::State::Ending);
        for (int i = 2; i <= 4; ++i)
            m_library.land(i);
        poll();
        QCOMPARE(m_job->state(), MoveJob::State::Ended);
        QCOMPARE(finished.count(), 1);
        const MoveJob::Summary summary = m_job->summary();
        QVERIFY(summary.endedEarly);
        QCOMPARE(summary.moved, 4);
        QCOMPARE(summary.notMoved, 2);
        QCOMPARE(m_sent.size(), 4);
    }

    void nearlyFullPausesBeforeSending()
    {
        for (int i = 1; i <= 3; ++i)
            m_library.add(i, 100 * kMiB * i);
        poll();
        qint64 free = 10LL * 1024 * kMiB;
        m_job->setLowDiskThreshold(2LL * 1024 * kMiB);
        m_job->setRoomProbe([&free](const QString &) { return free; });
        QSignalSpy paused(m_job, &MoveJob::pausedItself);
        m_job->start(plan(), QStringLiteral("home"));
        m_library.land(1);
        free = 2LL * 1024 * kMiB + 250 * kMiB; // room for the second, not for the third
        poll();
        QCOMPARE(m_sent.size(), 2);
        QCOMPARE(m_job->pauseReason(), MoveJob::PauseReason::NearlyFull);
        QCOMPARE(m_job->nearlyFullTorrent(), hashOf(3));
        QCOMPARE(paused.count(), 1);
    }

    void stuckAfterFourTimesTheExpectedTime()
    {
        m_library.add(1, 10 * kMiB);
        m_library.add(2, 600 * kMiB);
        m_library.add(3, 700 * kMiB);
        poll();
        QSignalSpy paused(m_job, &MoveJob::pausedItself);
        m_job->start(plan(), QStringLiteral("home"));
        wait(1);
        m_library.land(1); // 10 MiB in 1 s
        poll();
        m_library.moving(2);
        m_library.moving(3);
        poll();
        QCOMPARE(m_job->current(), hashOf(2));
        // Nothing lands; never stuck before 10 min whatever the speed says.
        wait(9 * 60);
        poll();
        QCOMPARE(m_job->state(), MoveJob::State::Running);
        wait(2 * 60);
        poll();
        QCOMPARE(m_job->state(), MoveJob::State::Paused);
        QCOMPARE(m_job->pauseReason(), MoveJob::PauseReason::Stuck);
        QCOMPARE(paused.count(), 1);
        m_job->keepWaiting();
        QCOMPARE(m_job->state(), MoveJob::State::Running);
        wait(60);
        poll();
        QCOMPARE(m_job->state(), MoveJob::State::Running);
    }

    void aGrowingFileIsProgress()
    {
        m_library.add(1, kMiB);
        m_library.add(2, 900 * kMiB);
        poll();
        qint64 grown = 0;
        m_job->setGrowthProbe([&grown](const QString &, const QString &) { return grown; });
        m_job->start(plan(), QStringLiteral("home"));
        m_library.land(1);
        poll();
        m_library.moving(2);
        poll();
        for (int minute = 0; minute < 30; ++minute) {
            wait(60);
            grown += 10 * kMiB;
            poll();
        }
        QCOMPARE(m_job->state(), MoveJob::State::Running);
    }

    void aRestartOfTheServerSendsAgain()
    {
        for (int i = 1; i <= 2; ++i)
            m_library.add(i, kMiB);
        poll();
        m_job->start(plan(), QStringLiteral("home"));
        m_library.land(1);
        poll();
        QCOMPARE(m_sent.size(), 2);
        // qBittorrent restarted: its queue is gone, the torrent reads stalled at the old place.
        wait(30);
        poll(true);
        QCOMPARE(m_sent.size(), 3);
        QCOMPARE(m_sent.at(2).first, QStringList({hashOf(2)}));
    }

    void aQuietSendIsSentAgainAfterTwoPolls()
    {
        m_library.add(1, kMiB);
        poll();
        m_job->start(plan(), QStringLiteral("home"));
        wait(2);
        poll();
        QCOMPARE(m_sent.size(), 1); // too soon to tell
        wait(20);
        poll();
        QCOMPARE(m_sent.size(), 1); // one quiet poll: the log may still say why
        wait(2);
        poll();
        QCOMPARE(m_sent.size(), 2);
    }

    void aRefusedSendPauses()
    {
        for (int i = 1; i <= 3; ++i)
            m_library.add(i, i * kMiB);
        poll();
        m_job->start(plan(), QStringLiteral("home"));
        m_job->onSendResult(
            {hashOf(1)},
            ApiError::fromHttpStatus(409, QStringLiteral("POST torrents/setLocation: Conflict")));
        QCOMPARE(m_job->state(), MoveJob::State::Paused);
        QCOMPARE(m_job->pauseReason(), MoveJob::PauseReason::FirstFailed);
        QCOMPARE(m_job->entry(hashOf(1))->causeKey, QStringLiteral("cantCreate"));
        // No answer at all is not a failure: the polls tell.
        m_job->resume();
        m_job->onSendResult({hashOf(2)}, ApiError(ApiError::Kind::Timeout, QStringLiteral("t")));
        QCOMPARE(m_job->entry(hashOf(2))->step, MoveJob::Step::Queued);
        QVERIFY(m_job->entry(hashOf(2))->unsure);
    }

    void removedAndElsewhere()
    {
        for (int i = 1; i <= 4; ++i)
            m_library.add(i, i * kMiB);
        poll();
        m_job->start(plan(), QStringLiteral("home"));
        m_library.land(1);
        poll();
        m_library.rows.remove(hashOf(2));
        m_library.land(4, QStringLiteral("/srv/category-folder"));
        poll();
        QCOMPARE(m_job->entry(hashOf(2))->step, MoveJob::Step::Removed);
        QCOMPARE(m_job->entry(hashOf(4))->step, MoveJob::Step::Elsewhere);
    }

    void serverAwaySendsNothing()
    {
        for (int i = 1; i <= 3; ++i)
            m_library.add(i, kMiB);
        poll();
        m_job->setConnected(false);
        m_job->start(plan(), QStringLiteral("home"));
        QCOMPARE(m_sent.size(), 0);
        m_job->setConnected(true);
        QCOMPARE(m_sent.size(), 1);
    }

    void savedAndTakenUpAgain()
    {
        QTemporaryDir tmp;
        const QString file = tmp.filePath(QStringLiteral("moves/home.json"));
        for (int i = 1; i <= 6; ++i)
            m_library.add(i, kMiB);
        poll();
        m_job->setFilePath(file);
        m_job->start(plan(), QStringLiteral("home"));
        m_library.land(1);
        poll();
        m_job->saveNow();
        QVERIFY(QFile::exists(file));

        // omaseed closes for 9 min; qBittorrent goes on with the three it had.
        wait(9 * 60);
        for (int i = 2; i <= 4; ++i)
            m_library.land(i);
        m_sent.clear();
        newJob();
        m_job->setFilePath(file);
        QVERIFY(m_job->restore());
        QCOMPARE(m_job->state(), MoveJob::State::Running);
        QCOMPARE(m_job->root(), kRoot);
        poll();
        QCOMPARE(m_job->landedWhileAway(), 3);
        QCOMPARE(m_job->secondsAway(), 9 * 60);
        QCOMPARE(m_job->moved(), 4);
        QCOMPARE(sentHashes(), QStringList({hashOf(5), hashOf(6)}));
    }

    void pauseAndQuitRunsAgainAtTheNextStart()
    {
        QTemporaryDir tmp;
        const QString file = tmp.filePath(QStringLiteral("home.json"));
        for (int i = 1; i <= 3; ++i)
            m_library.add(i, kMiB);
        poll();
        m_job->setFilePath(file);
        m_job->start(plan(), QStringLiteral("home"));
        m_job->pause(MoveJob::PauseReason::Quit);
        m_job->saveNow();
        newJob();
        m_job->setFilePath(file);
        QVERIFY(m_job->restore());
        QCOMPARE(m_job->state(), MoveJob::State::Running);
    }

    void aSecondMoveWaitsItsTurn()
    {
        m_library.add(1, kMiB);
        m_library.add(2, kMiB, QStringLiteral("/srv/media/films"));
        poll();
        QVERIFY(m_job->start(plan(), QStringLiteral("home")));
        MovePlan::Input in;
        in.torrents = m_library.torrents();
        in.root = QStringLiteral("/srv/media/films");
        in.newPath = QStringLiteral("/mnt/fast/films");
        QVERIFY(!m_job->start(MovePlan::make(in), QStringLiteral("home")));
        QCOMPARE(m_job->queuedRoots(), QStringList({QStringLiteral("/srv/media/films")}));
        m_library.land(1);
        poll();
        QCOMPARE(m_job->state(), MoveJob::State::Done);
        m_job->closeSummary();
        QCOMPARE(m_job->root(), QStringLiteral("/srv/media/films"));
        QCOMPARE(m_job->state(), MoveJob::State::Running);
        QCOMPARE(m_sent.last().second, QStringLiteral("/mnt/fast/films"));
    }

    void looseFilesComeAfter()
    {
        m_library.add(1, kMiB);
        poll();
        QSignalSpy carry(m_job, &MoveJob::carryWanted);
        QSignalSpy finished(m_job, &MoveJob::finished);
        m_job->start(plan(), QStringLiteral("home"), true, 5 * kMiB);
        m_library.land(1);
        poll();
        QCOMPARE(m_job->state(), MoveJob::State::Carrying);
        QCOMPARE(carry.count(), 1);
        QCOMPARE(finished.count(), 0);
        m_job->finishCarry(149, 5);
        QCOMPARE(m_job->state(), MoveJob::State::Done);
        QCOMPARE(finished.count(), 1);
    }

    void laterProblemsAreTiedToTheJob()
    {
        m_library.add(1, kMiB);
        poll();
        m_job->start(plan(), QStringLiteral("home"));
        m_library.land(1);
        poll();
        QCOMPARE(m_job->state(), MoveJob::State::Done);
        wait(3600);
        m_library.state(1, QStringLiteral("missingFiles"));
        poll();
        QCOMPARE(m_job->entry(hashOf(1))->later, QStringLiteral("missingFiles"));
    }

    void speedAndTimeLeft()
    {
        for (int i = 1; i <= 4; ++i)
            m_library.add(i, 100 * kMiB);
        poll();
        m_job->start(plan(), QStringLiteral("home"));
        QCOMPARE(m_job->secondsLeft(), qint64(-1));
        wait(10);
        m_library.land(1); // 100 MiB in 10 s
        poll();
        QCOMPARE(m_job->speed(), 10 * kMiB);
        QCOMPARE(m_job->secondsLeft(), qint64(30));
        QCOMPARE(m_job->percent(), 25);
    }

    void addedDuringIsNamed()
    {
        m_library.add(1, kMiB);
        poll();
        m_job->start(plan(), QStringLiteral("home"));
        m_library.add(9, kMiB);
        poll();
        QCOMPARE(m_job->addedDuring(), QStringList({hashOf(9)}));
    }
};

QTEST_GUILESS_MAIN(TestMoveJob)

#include "tst_movejob.moc"
