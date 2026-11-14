// SyncEngine::torrentsEdited (DECISIONS C43 Y2–Y8, board 58 §8): the category, tags and save
// path changes a poll sees on known torrents, and the silences around them.
#include "QbtFeed.hpp"
#include "core/SyncEngine.hpp"
#include "core/engines/qbt/QbtTranslate.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace omaseed;
using core::SeenEdit;
using core::SyncEngine;

namespace {

const QString kA = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
const QString kB = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

QJsonObject row(const QString &name, const QJsonObject &more = {})
{
    QJsonObject out{{QStringLiteral("name"), name},
                    {QStringLiteral("state"), QStringLiteral("uploading")},
                    {QStringLiteral("total_size"), 4096},
                    {QStringLiteral("progress"), 1.0},
                    {QStringLiteral("category"), QString()},
                    {QStringLiteral("tags"), QString()},
                    {QStringLiteral("save_path"), QStringLiteral("/srv/inbox")},
                    {QStringLiteral("download_path"), QString()}};
    for (auto it = more.begin(); it != more.end(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QJsonObject poll(bool full, const QJsonObject &torrents, const QJsonObject &more = {})
{
    QJsonObject out{{QStringLiteral("rid"), 1},
                    {QStringLiteral("full_update"), full},
                    {QStringLiteral("torrents"), torrents}};
    for (auto it = more.begin(); it != more.end(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QJsonObject change(const QString &hash, const QJsonObject &fields)
{
    return QJsonObject{{hash, fields}};
}

QList<SeenEdit> editsOf(const QSignalSpy &spy, qsizetype index)
{
    return spy.at(index).at(0).value<QList<SeenEdit>>();
}

} // namespace

class TestSyncEngineEdits : public QObject
{
    Q_OBJECT

    qint64 m_now = 1'000'000;

    /// An engine holding kA (and kB) from a first full update, a minute ago.
    void start(SyncEngine &engine, const QJsonObject &a = {}, const QJsonObject &b = {})
    {
        engine.setNowForTest([this]() { return m_now; });
        qbtfeed::demo(engine,
                      poll(true,
                           {{kA, row(QStringLiteral("Alpha"), a)},
                            {kB, row(QStringLiteral("Beta"), b)}}));
    }

private Q_SLOTS:
    void initTestCase() { qRegisterMetaType<QList<core::SeenEdit>>(); }

    void aCategoryChangeIsSeen()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine, {{QStringLiteral("category"), QStringLiteral("inbox")}});
        qbtfeed::demo(engine,
                      poll(false, change(kA, {{QStringLiteral("category"), QStringLiteral("music")}})));
        QCOMPARE(edited.count(), 1);
        const QList<SeenEdit> edits = editsOf(edited, 0);
        QCOMPARE(edits.size(), 1);
        QCOMPARE(edits.at(0).hash, kA);
        QCOMPARE(edits.at(0).what, SeenEdit::What::Category);
        QCOMPARE(edits.at(0).was, QStringLiteral("inbox"));
        QCOMPARE(edits.at(0).now, QStringLiteral("music"));
        QVERIFY(!edits.at(0).removed);
        // The same category again is no change.
        qbtfeed::demo(engine,
                      poll(false, change(kA, {{QStringLiteral("category"), QStringLiteral("music")}})));
        QCOMPARE(edited.count(), 1);
    }

    void neverFromTheFirstFullUpdate()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine, {{QStringLiteral("category"), QStringLiteral("inbox")}});
        QCOMPARE(edited.count(), 0);
        // A later full update compares against the rows held.
        qbtfeed::demo(engine,
                      poll(true,
                           {{kA, row(QStringLiteral("Alpha"),
                                     {{QStringLiteral("category"), QStringLiteral("music")}})},
                            {kB, row(QStringLiteral("Beta"))}}));
        QCOMPARE(edited.count(), 1);
    }

    void neverForAFirstAppearance()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine);
        const QString added = QStringLiteral("cccccccccccccccccccccccccccccccccccccccc");
        qbtfeed::demo(engine,
                      poll(false,
                           {{added, row(QStringLiteral("Gamma"),
                                        {{QStringLiteral("category"), QStringLiteral("music")},
                                         {QStringLiteral("tags"), QStringLiteral("x")}})}}));
        QCOMPARE(edited.count(), 0);
    }

    void notInATorrentsFirstMinute()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine);
        const QString added = QStringLiteral("cccccccccccccccccccccccccccccccccccccccc");
        qbtfeed::demo(engine, poll(false, {{added, row(QStringLiteral("Gamma"))}}));
        // Deluge labels the add right after it: still the add.
        m_now += 5'000;
        qbtfeed::demo(engine,
                      poll(false,
                           change(added, {{QStringLiteral("category"), QStringLiteral("music")}})));
        QCOMPARE(edited.count(), 0);
        // A minute on, an edit is an edit.
        m_now += 60'000;
        qbtfeed::demo(engine,
                      poll(false,
                           change(added, {{QStringLiteral("category"), QStringLiteral("books")}})));
        QCOMPARE(edited.count(), 1);
        QCOMPARE(editsOf(edited, 0).at(0).was, QStringLiteral("music"));
    }

    void tagsAreSeenAsTaggedAndUntagged()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine, {{QStringLiteral("tags"), QStringLiteral("one, two")}});
        qbtfeed::demo(engine,
                      poll(false, change(kA, {{QStringLiteral("tags"), QStringLiteral("two, three")}})));
        QCOMPARE(edited.count(), 1);
        const QList<SeenEdit> edits = editsOf(edited, 0);
        QCOMPARE(edits.size(), 1);
        QCOMPARE(edits.at(0).what, SeenEdit::What::Tags);
        QCOMPARE(edits.at(0).tagged, QStringList{QStringLiteral("three")});
        QCOMPARE(edits.at(0).untagged, QStringList{QStringLiteral("one")});
        QVERIFY(!edits.at(0).removed);
    }

    void aCategoryLeavingTheListIsRemoved()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine, {{QStringLiteral("category"), QStringLiteral("inbox")}});
        qbtfeed::demo(engine,
                      poll(false,
                           change(kA, {{QStringLiteral("category"), QString()}}),
                           {{QStringLiteral("categories_removed"),
                             QJsonArray{QStringLiteral("inbox")}}}));
        QCOMPARE(edited.count(), 1);
        const SeenEdit edit = editsOf(edited, 0).at(0);
        QCOMPARE(edit.was, QStringLiteral("inbox"));
        QCOMPARE(edit.now, QString());
        QVERIFY(edit.removed);
    }

    void aTagLeavingTheListIsItsOwnRemovedEdit()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine, {{QStringLiteral("tags"), QStringLiteral("one, two")}});
        qbtfeed::demo(engine,
                      poll(false,
                           change(kA, {{QStringLiteral("tags"), QStringLiteral("three")}}),
                           {{QStringLiteral("tags_removed"), QJsonArray{QStringLiteral("two")}}}));
        QCOMPARE(edited.count(), 1);
        const QList<SeenEdit> edits = editsOf(edited, 0);
        QCOMPARE(edits.size(), 2);
        QCOMPARE(edits.at(0).tagged, QStringList{QStringLiteral("three")});
        QCOMPARE(edits.at(0).untagged, QStringList{QStringLiteral("one")});
        QVERIFY(!edits.at(0).removed);
        QCOMPARE(edits.at(1).tagged, QStringList());
        QCOMPARE(edits.at(1).untagged, QStringList{QStringLiteral("two")});
        QVERIFY(edits.at(1).removed);
    }

    void aSavePathChangeIsAPlace()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine);
        qbtfeed::demo(engine,
                      poll(false,
                           change(kA, {{QStringLiteral("save_path"), QStringLiteral("/srv/far/")}})));
        QCOMPARE(edited.count(), 1);
        const SeenEdit edit = editsOf(edited, 0).at(0);
        QCOMPARE(edit.what, SeenEdit::What::Place);
        QCOMPARE(edit.was, QStringLiteral("/srv/inbox"));
        QCOMPARE(edit.now, QStringLiteral("/srv/far"));
        QVERIFY(edit.landed);
    }

    void waitingInTheIncompleteFolderIsNotLanded()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine,
              {{QStringLiteral("progress"), 0.5},
               {QStringLiteral("state"), QStringLiteral("downloading")},
               {QStringLiteral("download_path"), QStringLiteral("/srv/waiting")}});
        qbtfeed::demo(engine,
                      poll(false,
                           change(kA, {{QStringLiteral("save_path"), QStringLiteral("/srv/far")}})));
        QCOMPARE(edited.count(), 1);
        QVERIFY(!editsOf(edited, 0).at(0).landed);
    }

    void aMagnetsFirstPathIsNoPlace()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine, {{QStringLiteral("total_size"), -1}, {QStringLiteral("progress"), 0.0}});
        qbtfeed::demo(engine,
                      poll(false,
                           change(kA,
                                  {{QStringLiteral("save_path"), QStringLiteral("/srv/far")},
                                   {QStringLiteral("total_size"), 8192}})));
        QCOMPARE(edited.count(), 0);
        // An empty path either side is no place either.
        qbtfeed::demo(engine,
                      poll(false, change(kB, {{QStringLiteral("save_path"), QString()}})));
        QCOMPARE(edited.count(), 0);
    }

    void delugesFinishTransitIsNoPlace()
    {
        // Deluge through DelugeTranslate: unfinished with move_completed on, the save path is
        // the folder it will land in and the download path where it waits (C43 Y6).
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine,
              {{QStringLiteral("progress"), 0.5},
               {QStringLiteral("state"), QStringLiteral("downloading")},
               {QStringLiteral("save_path"), QStringLiteral("/srv/far")},
               {QStringLiteral("download_path"), QStringLiteral("/srv/waiting")}});
        // A reading that is finished at the waiting folder, then at the folder it lands in.
        qbtfeed::demo(engine,
                      poll(false,
                           change(kA,
                                  {{QStringLiteral("progress"), 1.0},
                                   {QStringLiteral("save_path"), QStringLiteral("/srv/waiting")},
                                   {QStringLiteral("download_path"), QString()}})));
        qbtfeed::demo(engine,
                      poll(false,
                           change(kA, {{QStringLiteral("save_path"), QStringLiteral("/srv/far")}})));
        QCOMPARE(edited.count(), 0);
        // Remembered once only: a later move is a move.
        qbtfeed::demo(engine,
                      poll(false,
                           change(kA, {{QStringLiteral("save_path"), QStringLiteral("/srv/else")}})));
        QCOMPARE(edited.count(), 1);
    }

    void filingResetSkipsCategories()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine, {{QStringLiteral("category"), QStringLiteral("music")}});
        core::SyncDelta delta = core::QbtTranslate::syncDelta(
            poll(false, change(kA, {{QStringLiteral("category"), QString()}})));
        delta.filingReset = true;
        engine.applyDemoData(delta);
        QCOMPARE(edited.count(), 0);
    }

    void aTrailingSlashIsNoChange()
    {
        SyncEngine engine;
        QSignalSpy edited(&engine, &SyncEngine::torrentsEdited);
        start(engine);
        qbtfeed::demo(engine,
                      poll(false,
                           change(kA, {{QStringLiteral("save_path"), QStringLiteral("/srv/inbox/")}})));
        QCOMPARE(edited.count(), 0);
    }
};

QTEST_MAIN(TestSyncEngineEdits)
#include "tst_syncengine_edits.moc"
