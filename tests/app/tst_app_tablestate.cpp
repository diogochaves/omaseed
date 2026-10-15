// T4.4 — the table-state round trip through `state.json` (`App::saveTableState()` /
// `tableState()` / `tableStateKey()`), plus the small App helpers the palettes call in demo
// mode: `jumpMatches()`/`jumpMatchCount()`, `tagsOf()`, and the demo-mode no-op paths of
// `createCategory()`/`applyCategory()`/`createTag()`/`applyTags()` (they must not crash or
// reach a client with no server configured).

#include "core/OmaseedBackup.hpp"
#include "core/StateFile.hpp"
#include "ui/app/App.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

using omaseed::app::App;

class TestAppTableState : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void tableStateKeyIsDemoUnderDemoMode();
    void saveThenLoadRoundTrips();
    void unknownKeyGivesEmptyMap();
    void jumpMatchesFindsByNameHashAndTracker();
    void errorReasonTextKeepsOnlyTheReason();
    void tagsOfSplitsTheJoinedText();
    void demoModeCategoryAndTagCallsDoNotCrash();
    void restoringSettingsAppliesTheLayoutsAtOnce();

private:
    QTemporaryDir m_dir;
    QByteArray m_previousConfigHome;
};

void TestAppTableState::init()
{
    QVERIFY(m_dir.isValid());
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_dir.path().toUtf8());
}

void TestAppTableState::cleanup()
{
    if (m_previousConfigHome.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
}

void TestAppTableState::tableStateKeyIsDemoUnderDemoMode()
{
    App app;
    QCOMPARE(app.tableStateKey(), QString());
    app.startDemo(16);
    QCOMPARE(app.tableStateKey(), QStringLiteral("demo"));
}

void TestAppTableState::saveThenLoadRoundTrips()
{
    App app;
    app.startDemo(16);
    const QString key = app.tableStateKey();
    QVERIFY(!key.isEmpty());

    QVariantMap state;
    state.insert(QStringLiteral("columns"),
                 QStringList{QStringLiteral("name"), QStringLiteral("size")});
    state.insert(QStringLiteral("sortRole"), QStringLiteral("name"));
    state.insert(QStringLiteral("sortOrder"), int(Qt::AscendingOrder));
    app.saveTableState(key, state);

    const QVariantMap loaded = app.tableState(key);
    QCOMPARE(loaded.value(QStringLiteral("sortRole")).toString(), QStringLiteral("name"));
    QCOMPARE(loaded.value(QStringLiteral("sortOrder")).toInt(), int(Qt::AscendingOrder));
    const QStringList columns = loaded.value(QStringLiteral("columns")).toStringList();
    QCOMPARE(columns, (QStringList{QStringLiteral("name"), QStringLiteral("size")}));

    // APP-16: a demo keeps it in memory only. A second App, same XDG_CONFIG_HOME, does not
    // see it, and state.json was never written.
    App second;
    second.startDemo(16);
    QVERIFY(second.tableState(key).isEmpty());
    QVERIFY(!QFile::exists(m_dir.path() + QStringLiteral("/omaseed/state.json")));

    // The same holds for settings, notices and the delete default.
    app.setRailHidden(!app.railHidden());
    app.setDeleteFilesDefault(!app.deleteFilesDefault());
    QVERIFY(!QFile::exists(m_dir.path() + QStringLiteral("/omaseed/state.json")));
}

void TestAppTableState::unknownKeyGivesEmptyMap()
{
    App app;
    app.startDemo(16);
    QVERIFY(app.tableState(QStringLiteral("no-such-server")).isEmpty());
    // saveTableState() with an empty key (no server) is a no-op, not a crash.
    app.saveTableState(QString(), QVariantMap{{QStringLiteral("columns"), QStringList{}}});
}

void TestAppTableState::jumpMatchesFindsByNameHashAndTracker()
{
    App app;
    app.startDemo(16);

    // "bunny" only hits the one movie in the demo library (DemoData.cpp).
    QCOMPARE(app.jumpMatchCount(QStringLiteral("bunny")), 1);
    const QVariantList bunnyRows = app.jumpMatches(QStringLiteral("bunny"), 50);
    QCOMPARE(bunnyRows.size(), 1);
    const QVariantMap row = bunnyRows.first().toMap();
    QVERIFY(row.value(QStringLiteral("title")).toString().contains(QStringLiteral("Bunny")));
    QVERIFY(!row.value(QStringLiteral("key")).toString().isEmpty());
    QVERIFY(row.value(QStringLiteral("detail")).toString().contains(QStringLiteral("·")));

    // "fedora" is a substring hit on the one Fedora ISO ("fed" alone also hits other names
    // as a subsequence, e.g. any name with an f...e...d in order, so the test picks a query
    // specific enough to be exact).
    QCOMPARE(app.jumpMatchCount(QStringLiteral("fedora")), 1);

    // An empty query matches the whole library; the cap still applies.
    QCOMPARE(app.jumpMatchCount(QString()), app.torrents()->count());
    QCOMPARE(app.jumpMatches(QString(), 3).size(), 3);

    // The tracker host is searchable too: every "movies"-categorised row shares
    // tracker.example.org (DemoData.cpp's kMoviesTracker).
    QVERIFY(app.jumpMatchCount(QStringLiteral("example.org")) > 1);

    // A hash is matched by PREFIX only (spec §4 Fig. 12 "also matches hash"): a subsequence
    // test over 40 hex characters matched almost the whole library for a query like "fed".
    const QString hash = row.value(QStringLiteral("key")).toString();
    QCOMPARE(hash.size(), 40);
    QCOMPARE(app.jumpMatchCount(hash), 1);
    QCOMPARE(app.jumpMatches(hash, 50).first().toMap().value(QStringLiteral("key")).toString(),
             hash);
    QCOMPARE(app.jumpMatchCount(hash.left(12)), 1);
    // The same 12 characters taken from the middle of the hash are no longer a hash hit.
    const QVariantList middle = app.jumpMatches(hash.mid(14, 12), 50);
    for (const QVariant &candidate : middle)
        QVERIFY(candidate.toMap().value(QStringLiteral("key")).toString() != hash);
    // "fed" is hex, but no demo hash starts with it, and neither demo tracker host holds an
    // "f", so every row it still returns has to hit on the NAME as a subsequence. Before the
    // prefix rule it matched 6 of the 16 rows through their hashes.
    const auto subsequence = [](const QString &text, const QString &word) {
        qsizetype at = 0;
        for (const QChar ch : word) {
            const qsizetype found = text.toLower().indexOf(ch, at);
            if (found < 0)
                return false;
            at = found + 1;
        }
        return true;
    };
    const QVariantList fed = app.jumpMatches(QStringLiteral("fed"), 50);
    QVERIFY(!fed.isEmpty());
    for (const QVariant &candidate : fed) {
        const QString title = candidate.toMap().value(QStringLiteral("title")).toString();
        QVERIFY2(subsequence(title, QStringLiteral("fed")), qPrintable(title));
    }
    QCOMPARE(app.jumpMatchCount(QStringLiteral("fed")), fed.size());
}

void TestAppTableState::errorReasonTextKeepsOnlyTheReason()
{
    // Fig. 23's banner and Fig. 21's notice print the reason alone.
    QCOMPARE(App::errorReasonText(QStringLiteral("network: GET sync/maindata: Connection refused")),
             QStringLiteral("connection refused"));
    QCOMPARE(App::errorReasonText(QStringLiteral("GET sync/maindata: Host not found")),
             QStringLiteral("host not found"));
    QCOMPARE(App::errorReasonText(QStringLiteral("Connection refused")),
             QStringLiteral("connection refused"));
    QCOMPARE(App::errorReasonText(QString()), QString());
}

void TestAppTableState::tagsOfSplitsTheJoinedText()
{
    App app;
    app.startDemo(16);
    const QString hash = app.torrents()->hashes().constFirst();
    const QStringList tags = app.tagsOf(hash);
    // Every demo row with a tracker carries "private" (DemoData.cpp's tagsFor()).
    for (const QString &tag : tags)
        QVERIFY(!tag.trimmed().isEmpty());
    QCOMPARE(app.tagsOf(QStringLiteral("not-a-hash")), QStringList());
}

void TestAppTableState::demoModeCategoryAndTagCallsDoNotCrash()
{
    App app;
    app.startDemo(16);
    const QStringList hashes = {app.torrents()->hashes().constFirst()};
    app.createCategory(QStringLiteral("omaseed-test-cat"));
    app.applyCategory(hashes, QStringLiteral("omaseed-test-cat"));
    app.createTag(QStringLiteral("omaseed-test-tag"));
    app.applyTags(hashes, {QStringLiteral("omaseed-test-tag")}, {});
    app.applyTags(hashes, {}, {QStringLiteral("omaseed-test-tag")});
    // No server is configured in demo mode, so every call above is a log line, not a
    // network request; reaching this point without a crash is the assertion.
}

void TestAppTableState::restoringSettingsAppliesTheLayoutsAtOnce()
{
    // #34: a restore that brings the settings tells the window, which reads the table layout
    // again (MainView's onTableStatesRestored; tst_TableState.qml covers that half). One
    // without the settings changes no layout and says nothing.
    using omaseed::core::OmaseedBackup;
    using omaseed::core::StateFile;
    QTemporaryDir home;
    QVERIFY(home.isValid());
    const QByteArray previousData = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_DATA_HOME", home.filePath(QStringLiteral("data")).toUtf8());
    const auto restoreData = qScopeGuard([&] {
        if (previousData.isNull())
            qunsetenv("XDG_DATA_HOME");
        else
            qputenv("XDG_DATA_HOME", previousData);
    });

    // A backup with settings in it (no server: which layout lands where is tst_omaseedbackup's).
    StateFile::writeSection(
        QStringLiteral("appearance"),
        QJsonObject{{QStringLiteral("speedsAlignment"), QStringLiteral("right")}});
    const QString path = home.filePath(QStringLiteral("layouts.omaseed"));
    QString error;
    QVERIFY2(OmaseedBackup::createFile(
                 OmaseedBackup::defaultSources(), path, QDateTime::currentSecsSinceEpoch(), &error),
             qPrintable(error));

    App app;
    QSignalSpy restored(&app, &App::tableStatesRestored);
    QVERIFY(app.readBackup(path).value(QStringLiteral("ok")).toBool());
    QVariantMap result = app.restoreBackup({{QStringLiteral("settings"), false}});
    QVERIFY(result.value(QStringLiteral("ok")).toBool());
    QCOMPARE(restored.count(), 0);

    QVERIFY(app.readBackup(path).value(QStringLiteral("ok")).toBool());
    result = app.restoreBackup({{QStringLiteral("settings"), true}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(result.value(QStringLiteral("text")).toString()));
    QCOMPARE(restored.count(), 1);
}

QTEST_MAIN(TestAppTableState)
#include "tst_app_tablestate.moc"
