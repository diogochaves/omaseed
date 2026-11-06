// LibraryDisplays: the display each scope shows (board 46 H4, DECISIONS C24), remembered per
// scope, folders included; rows for a new install, the table for an existing one.

#include "core/LibraryDisplays.hpp"
#include "core/TorrentFilterModel.hpp"

#include <QJsonObject>
#include <QTest>

using namespace omaseed::core;

class TestLibraryDisplays : public QObject
{
    Q_OBJECT

private slots:
    void aScopesKeyIsItsNarrowestPlace();
    void newInstallsGetRowsExistingOnesTheTable_data();
    void newInstallsGetRowsExistingOnesTheTable();
    void aScopeWithoutItsOwnShowsTheLibrarys();
    void picksAreRememberedPerScope();
    void theJsonRoundTrips();
    void unknownNamesAreDropped();
    void theShelvesReadAsSpace();
};

void TestLibraryDisplays::aScopesKeyIsItsNarrowestPlace()
{
    const QVariant unset;
    const int any = TorrentFilterModel::AnyQuestion;
    QCOMPARE(LibraryDisplays::scopeKey(any, unset, unset, unset, {}), QStringLiteral("library"));
    QCOMPARE(LibraryDisplays::scopeKey(TorrentFilterModel::Arriving, unset, unset, unset, {}),
             QStringLiteral("question:arriving"));
    QCOMPARE(LibraryDisplays::scopeKey(any, QStringLiteral("films"), unset, unset, {}),
             QStringLiteral("category:films"));
    // The rail's "none" row is a place of its own.
    QCOMPARE(LibraryDisplays::scopeKey(any, QString(""), unset, unset, {}),
             QStringLiteral("category:"));
    QCOMPARE(LibraryDisplays::scopeKey(any, unset, QStringLiteral("keep"), unset, {}),
             QStringLiteral("tag:keep"));
    QCOMPARE(LibraryDisplays::scopeKey(any, unset, unset, QStringLiteral("example.org"), {}),
             QStringLiteral("tracker:example.org"));
    QCOMPARE(LibraryDisplays::scopeKey(any, unset, unset, unset, QStringLiteral("/data/films")),
             QStringLiteral("folder:/data/films"));
    // A question narrowing a place shares the place's display; a folder wins over the rest.
    QCOMPARE(LibraryDisplays::scopeKey(
                 TorrentFilterModel::Resting, QStringLiteral("films"), unset, unset, {}),
             QStringLiteral("category:films"));
    QCOMPARE(LibraryDisplays::scopeKey(
                 any, QStringLiteral("films"), unset, unset, QStringLiteral("/data/films")),
             QStringLiteral("folder:/data/films"));
}

void TestLibraryDisplays::newInstallsGetRowsExistingOnesTheTable_data()
{
    QTest::addColumn<bool>("fileExists");
    QTest::addColumn<QJsonObject>("state");
    QTest::addColumn<QString>("library");
    QTest::newRow("no state.json") << false << QJsonObject() << "rows";
    QTest::newRow("only its version") << true << QJsonObject{{"version", 1}} << "rows";
    QTest::newRow("an unreadable file") << true << QJsonObject() << "rows";
    QTest::newRow("a table's layout")
        << true << QJsonObject{{"version", 1}, {"tables", QJsonObject()}} << "table";
    QTest::newRow("any setting") << true
                                 << QJsonObject{{"version", 1}, {"appearance", QJsonObject()}}
                                 << "table";
}

void TestLibraryDisplays::newInstallsGetRowsExistingOnesTheTable()
{
    QFETCH(bool, fileExists);
    QFETCH(QJsonObject, state);
    QFETCH(QString, library);
    QCOMPARE(LibraryDisplays::initialLibrary(LibraryDisplays::isExistingInstall(state, fileExists)),
             library);
}

void TestLibraryDisplays::aScopeWithoutItsOwnShowsTheLibrarys()
{
    LibraryDisplays displays(QStringLiteral("table"));
    QCOMPARE(displays.displayFor(QStringLiteral("library")), QStringLiteral("table"));
    QCOMPARE(displays.displayFor(QStringLiteral("folder:/data/films")), QStringLiteral("table"));
    QVERIFY(displays.set(QStringLiteral("library"), QStringLiteral("rows")));
    QCOMPARE(displays.displayFor(QStringLiteral("folder:/data/films")), QStringLiteral("rows"));
    QVERIFY(!displays.hasOwn(QStringLiteral("folder:/data/films")));
}

void TestLibraryDisplays::picksAreRememberedPerScope()
{
    LibraryDisplays displays(QStringLiteral("rows"));
    QVERIFY(displays.set(QStringLiteral("folder:/data/films"), QStringLiteral("space")));
    QVERIFY(!displays.set(QStringLiteral("folder:/data/films"), QStringLiteral("space")));
    QVERIFY(displays.set(QStringLiteral("question:arriving"), QStringLiteral("table")));
    QCOMPARE(displays.displayFor(QStringLiteral("folder:/data/films")), QStringLiteral("space"));
    QCOMPARE(displays.displayFor(QStringLiteral("question:arriving")), QStringLiteral("table"));
    QCOMPARE(displays.displayFor(QStringLiteral("library")), QStringLiteral("rows"));
    // The library's pick leaves a scope that has its own.
    QVERIFY(displays.set(QStringLiteral("library"), QStringLiteral("space")));
    QCOMPARE(displays.displayFor(QStringLiteral("question:arriving")), QStringLiteral("table"));
    QCOMPARE(displays.displayFor(QStringLiteral("category:films")), QStringLiteral("space"));
    QVERIFY(!displays.set(QStringLiteral("library"), QStringLiteral("cards")));
}

void TestLibraryDisplays::theJsonRoundTrips()
{
    LibraryDisplays displays(QStringLiteral("table"));
    displays.set(QStringLiteral("folder:/data/films"), QStringLiteral("space"));
    displays.set(QStringLiteral("tag:keep"), QStringLiteral("rows"));
    const QJsonObject json = displays.toJson();
    QCOMPARE(json.value(QStringLiteral("library")).toString(), QStringLiteral("table"));
    QCOMPARE(json.value(QStringLiteral("scopes")).toObject().size(), 2);
    QCOMPARE(LibraryDisplays::fromJson(json, QStringLiteral("rows")), displays);
}

void TestLibraryDisplays::unknownNamesAreDropped()
{
    const QJsonObject json{
        {"library", "cards"},
        {"scopes", QJsonObject{{"tag:keep", "rows"}, {"tag:old", "cards"}, {"library", "space"}}}};
    const LibraryDisplays displays = LibraryDisplays::fromJson(json, QStringLiteral("table"));
    QCOMPARE(displays.library(), QStringLiteral("table"));
    QVERIFY(displays.hasOwn(QStringLiteral("tag:keep")));
    QVERIFY(!displays.hasOwn(QStringLiteral("tag:old")));
    QCOMPARE(displays.displayFor(QStringLiteral("tag:old")), QStringLiteral("table"));
}

// Board 51 D12 a (C32): space replaced the shelves, saved as "overview"; a state.json written
// before reads it as space, and an old name picked (a walk's OMASEED_DISPLAY) is space too.
void TestLibraryDisplays::theShelvesReadAsSpace()
{
    QCOMPARE(
        LibraryDisplays::names(),
        QStringList({QStringLiteral("rows"), QStringLiteral("table"), QStringLiteral("space")}));
    QCOMPARE(LibraryDisplays::canonical(QStringLiteral("overview")), QStringLiteral("space"));
    QCOMPARE(LibraryDisplays::canonical(QStringLiteral("shelves")), QStringLiteral("space"));
    QCOMPARE(LibraryDisplays::canonical(QStringLiteral("rows")), QStringLiteral("rows"));
    QVERIFY(!LibraryDisplays::isName(QStringLiteral("overview")));
    const QJsonObject json{{"library", "overview"},
                           {"scopes", QJsonObject{{"folder:/data/films", "overview"}}}};
    LibraryDisplays displays = LibraryDisplays::fromJson(json, QStringLiteral("table"));
    QCOMPARE(displays.library(), QStringLiteral("space"));
    QCOMPARE(displays.displayFor(QStringLiteral("folder:/data/films")), QStringLiteral("space"));
    QCOMPARE(displays.toJson().value(QStringLiteral("library")).toString(),
             QStringLiteral("space"));
    QVERIFY(!displays.set(QStringLiteral("library"), QStringLiteral("overview")));
    QVERIFY(displays.set(QStringLiteral("tag:keep"), QStringLiteral("shelves")));
    QCOMPARE(displays.displayFor(QStringLiteral("tag:keep")), QStringLiteral("space"));
}

QTEST_GUILESS_MAIN(TestLibraryDisplays)
#include "tst_librarydisplays.moc"
