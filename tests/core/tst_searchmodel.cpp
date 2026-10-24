// core/SearchModel (#145, DECISIONS C26 SR3–SR8): the rows' order, hide dead, `/`, the marks
// and words, new and +1 copy, what lands above the cursor, and rows that move instead of a
// reset. Invented names.

#include "core/SearchModel.hpp"

#include <QAbstractItemModelTester>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using omaseed::core::SearchHit;
using omaseed::core::SearchModel;
using omaseed::core::SearchRow;

namespace {

constexpr qint64 now = 2'000'000'000;

SearchHit hit(const QString &plugin,
              int n,
              int seeds,
              qint64 published = now - 3 * 86400,
              const QString &name = {})
{
    return SearchHit::fromJson(
        QJsonObject{{QStringLiteral("fileName"),
                     name.isEmpty() ? QStringLiteral("Open Movie Night %1").arg(n) : name},
                    {QStringLiteral("fileSize"), 1024 * 1024 * (n + 1)},
                    {QStringLiteral("fileUrl"),
                     QStringLiteral("magnet:?xt=urn:btih:%1&tr=udp%3A%2F%2F%2.invalid%3A1")
                         .arg(QString::number(n).rightJustified(40, QLatin1Char('0')), plugin)},
                    {QStringLiteral("nbSeeders"), seeds},
                    {QStringLiteral("nbLeechers"), 1},
                    {QStringLiteral("pubDate"), published},
                    {QStringLiteral("engineName"), plugin}});
}

QStringList names(const SearchModel &model)
{
    QStringList list;
    for (int i = 0; i < model.rowCount(); ++i)
        list.append(model.data(model.index(i), SearchModel::NameRole).toString());
    return list;
}

QString role(const SearchModel &model, int row, int role)
{
    return model.data(model.index(row), role).toString();
}

} // namespace

class TestSearchModel : public QObject
{
    Q_OBJECT

private slots:
    void sortsMostSeedsThenUnknownThenDead();
    void otherOrders();
    void hideDeadKeepsTheUnknown();
    void filterNarrowsWithoutAsking();
    void wordsOfARow();
    void marksFromTheLibrary();
    void newAndCopyMarksFade();
    void countsWhatLandsAboveTheCursor();
    void rowsMoveRatherThanReset();
};

void TestSearchModel::sortsMostSeedsThenUnknownThenDead()
{
    SearchModel model;
    QAbstractItemModelTester tester(&model);
    model.setHits({hit(QStringLiteral("a"), 1, 0),
                   hit(QStringLiteral("a"), 2, -1),
                   hit(QStringLiteral("a"), 3, 41),
                   hit(QStringLiteral("a"), 4, 3)});
    QCOMPARE(names(model),
             (QStringList{QStringLiteral("Open Movie Night 3"),
                          QStringLiteral("Open Movie Night 4"),
                          QStringLiteral("Open Movie Night 2"),
                          QStringLiteral("Open Movie Night 1")}));
    QCOMPARE(role(model, 2, SearchModel::SeedsTextRole), QStringLiteral("?"));
    QCOMPARE(role(model, 2, SearchModel::HealthRole), QStringLiteral("unknown"));
    QCOMPARE(role(model, 1, SearchModel::SeedsCaptionRole), QStringLiteral("few seeds"));
    QCOMPARE(role(model, 3, SearchModel::HealthRole), QStringLiteral("dead"));
    QCOMPARE(model.torrentCount(), 4);
}

void TestSearchModel::otherOrders()
{
    SearchModel model;
    model.setHits(
        {hit(QStringLiteral("a"), 1, 9, now - 100), hit(QStringLiteral("a"), 5, 3, now - 10)});
    model.setOrder(QStringLiteral("newest"));
    QCOMPARE(names(model).first(), QStringLiteral("Open Movie Night 5"));
    model.setOrder(QStringLiteral("size"));
    QCOMPARE(names(model).first(), QStringLiteral("Open Movie Night 5"));
    model.setOrder(QStringLiteral("name"));
    QCOMPARE(names(model).first(), QStringLiteral("Open Movie Night 1"));
    QCOMPARE(model.order(), QStringLiteral("name"));
}

// SR4: hide dead hides a reported 0 only.
void TestSearchModel::hideDeadKeepsTheUnknown()
{
    SearchModel model;
    model.setHits({hit(QStringLiteral("a"), 1, 0),
                   hit(QStringLiteral("a"), 2, -1),
                   hit(QStringLiteral("a"), 3, 4)});
    model.setHideDead(true);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.hiddenDead(), 1);
    model.setHideDead(false);
    QCOMPARE(model.hiddenDead(), 0);
}

void TestSearchModel::filterNarrowsWithoutAsking()
{
    SearchModel model;
    model.setHits(
        {hit(QStringLiteral("a"), 1, 4, now, QStringLiteral("Open.Movie.Night.1080p")),
         hit(QStringLiteral("a"), 2, 4, now, QStringLiteral("open movie night poster pack"))});
    model.setFilterText(QStringLiteral("night 1080"));
    QCOMPARE(names(model), QStringList{QStringLiteral("Open.Movie.Night.1080p")});
    model.setFilterText(QString());
    QCOMPARE(model.rowCount(), 2);
}

void TestSearchModel::wordsOfARow()
{
    SearchModel model;
    model.setNow([]() { return now; });
    model.setPluginNames({{QStringLiteral("a"), QStringLiteral("plugin a")},
                          {QStringLiteral("c"), QStringLiteral("plugin c")}});
    SearchHit copy = hit(QStringLiteral("c"), 1, -1);
    copy.trackers = {QStringLiteral("udp://four.invalid:80")};
    copy.link =
        QStringLiteral("magnet:?xt=urn:btih:%1&tr=udp%3A%2F%2Ffour.invalid%3A80").arg(copy.hashV1);
    const SearchHit link = SearchHit::fromJson(QJsonObject{
        {QStringLiteral("fileName"), QStringLiteral("extras")},
        {QStringLiteral("fileUrl"), QStringLiteral("http://plugin-a.invalid/1.torrent")},
        {QStringLiteral("nbSeeders"), 12},
        {QStringLiteral("engineName"), QStringLiteral("a")}});
    model.setHits({hit(QStringLiteral("a"), 1, 41),
                   copy,
                   link,
                   hit(QStringLiteral("c"), 7, -1),
                   hit(QStringLiteral("a"), 8, 0)});
    QCOMPARE(role(model, 0, SearchModel::MetaRole), QStringLiteral("2.00 MB · 3 days ago"));
    QCOMPARE(role(model, 0, SearchModel::SentenceRole),
             QStringLiteral("plugin a and plugin c · 2 copies folded, 2 trackers · 1 leecher"));
    QCOMPARE(role(model, 0, SearchModel::SentenceShortRole),
             QStringLiteral("plugin a and plugin c · 2 copies"));
    QCOMPARE(role(model, 0, SearchModel::AddWordRole), QStringLiteral("add…"));
    QCOMPARE(role(model, 1, SearchModel::SentenceRole),
             QStringLiteral("plugin a · a .torrent link: its info hash is known at add"));
    QVERIFY(model.data(model.index(1), SearchModel::IsLinkRole).toBool());
    QCOMPARE(role(model, 1, SearchModel::MetaRole), QStringLiteral("size unknown · no date"));
    QCOMPARE(role(model, 2, SearchModel::SentenceRole),
             QStringLiteral("plugin c · this plugin doesn't report seeds: unknown, not dead"));
    QCOMPARE(role(model, 3, SearchModel::SentenceRole),
             QStringLiteral("plugin a · no seeds when the plugin looked · 1 leecher"));
    // A plugin uninstalled later keeps its own name on its rows.
    model.setPluginNames({{QStringLiteral("c"), QStringLiteral("plugin c")}});
    QCOMPARE(role(model, 1, SearchModel::SentenceRole),
             QStringLiteral("plugin a · a .torrent link: its info hash is known at add"));
    const QVariantMap details = model.details(role(model, 0, SearchModel::KeyRole));
    QCOMPARE(details.value(QStringLiteral("copyList")).toList().size(), 2);
    QCOMPARE(details.value(QStringLiteral("trackers")).toList().size(), 2);
    QCOMPARE(details.value(QStringLiteral("trackers"))
                 .toList()
                 .at(1)
                 .toMap()
                 .value(QStringLiteral("from"))
                 .toString(),
             QStringLiteral("plugin c"));
    QCOMPARE(model.foldedCount(), 1);
}

// SR7: in your library by hash, likely on disk by name and size, added after the hand-off.
void TestSearchModel::marksFromTheLibrary()
{
    SearchModel model;
    const SearchHit mine = hit(QStringLiteral("a"), 1, 9);
    model.setLibraryLookup([&mine](const SearchRow &row) {
        SearchModel::LibraryMatch match;
        if (row.hash() == mine.hashV1) {
            match.hash = mine.hashV1;
            match.words = QStringLiteral("arriving at 87 %");
        } else if (row.name == QStringLiteral("Open Movie Night 2")) {
            match.name = QStringLiteral("Open Movie Night (2026)");
        }
        return match;
    });
    model.setHits({mine, hit(QStringLiteral("a"), 2, 5), hit(QStringLiteral("a"), 3, 3)});
    QCOMPARE(role(model, 0, SearchModel::MarkRole), QStringLiteral("library"));
    QCOMPARE(role(model, 0, SearchModel::MarkTextRole), QStringLiteral("in your library"));
    QCOMPARE(role(model, 0, SearchModel::AddWordRole), QStringLiteral("trackers…"));
    QVERIFY(role(model, 0, SearchModel::SentenceRole)
                .contains(QStringLiteral("in your library, arriving at 87 %")));
    QCOMPARE(role(model, 1, SearchModel::MarkTextRole), QStringLiteral("likely on disk"));
    QVERIFY(role(model, 1, SearchModel::SentenceRole)
                .contains(QStringLiteral("same name and size as Open Movie Night (2026)")));
    const QString key = role(model, 2, SearchModel::KeyRole);
    model.markAdded(key);
    QCOMPARE(role(model, 2, SearchModel::MarkRole), QStringLiteral("added"));
    QVERIFY(role(model, 2, SearchModel::SentenceRole).contains(QStringLiteral("added ")));
    model.startOver();
    model.setHits({});
    QCOMPARE(model.rowCount(), 0);
}

void TestSearchModel::newAndCopyMarksFade()
{
    SearchModel model;
    model.setFreshMs(150);
    model.setHits({hit(QStringLiteral("a"), 1, 9)});
    QCOMPARE(role(model, 0, SearchModel::FreshRole), QString()); // the first answer is no news
    model.setHits({hit(QStringLiteral("a"), 1, 9),
                   hit(QStringLiteral("c"), 1, -1),
                   hit(QStringLiteral("c"), 2, 3)});
    QCOMPARE(role(model, 0, SearchModel::FreshRole), QStringLiteral("copy"));
    QCOMPARE(role(model, 1, SearchModel::FreshRole), QStringLiteral("new"));
    QTRY_COMPARE(role(model, 1, SearchModel::FreshRole), QString());
    QCOMPARE(role(model, 0, SearchModel::FreshRole), QString());
}

// SR5: "3 new and 1 folded" above the cursor's row.
void TestSearchModel::countsWhatLandsAboveTheCursor()
{
    SearchModel model;
    model.setHits({hit(QStringLiteral("a"), 1, 9), hit(QStringLiteral("a"), 2, 3)});
    model.setAnchorKey(role(model, 1, SearchModel::KeyRole));
    QSignalSpy landed(&model, &SearchModel::landedChanged);
    model.setHits({hit(QStringLiteral("a"), 1, 9),
                   hit(QStringLiteral("a"), 2, 3),
                   hit(QStringLiteral("c"), 1, -1),
                   hit(QStringLiteral("c"), 5, 40),
                   hit(QStringLiteral("c"), 6, 1)});
    QCOMPARE(model.landedAbove(), 1);
    QCOMPARE(model.foldedAbove(), 1);
    QCOMPARE(landed.count(), 1);
    model.clearLanded();
    QCOMPARE(model.landedAbove(), 0);
}

void TestSearchModel::rowsMoveRatherThanReset()
{
    SearchModel model;
    QAbstractItemModelTester tester(&model);
    model.setHits({hit(QStringLiteral("a"), 1, 9), hit(QStringLiteral("a"), 2, 3)});
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy around(&model, &SearchModel::aboutToRefresh);
    model.setHits({hit(QStringLiteral("a"), 1, 9),
                   hit(QStringLiteral("a"), 2, 3),
                   hit(QStringLiteral("c"), 3, 50)});
    QCOMPARE(reset.count(), 0);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(around.count(), 1);
    QCOMPARE(names(model).first(), QStringLiteral("Open Movie Night 3"));
    QCOMPARE(model.indexOfKey(role(model, 2, SearchModel::KeyRole)), 2);
    QCOMPARE(model.get(0).value(QStringLiteral("seeds")).toInt(), 50);
}

QTEST_GUILESS_MAIN(TestSearchModel)
#include "tst_searchmodel.moc"
