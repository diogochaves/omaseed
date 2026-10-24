// core/SearchResults (#145, DECISIONS C26 SR3–SR8): one row per hash, the private rule, the
// seed order and health words, ages and plugin words. Invented names and trackers.

#include "core/MagnetLink.hpp"
#include "core/SearchResults.hpp"

#include <QJsonObject>
#include <QTest>

using omaseed::core::MagnetLink;
using omaseed::core::SearchHit;
using omaseed::core::SearchRow;
namespace SearchResults = omaseed::core::SearchResults;

namespace {

const QString hashA = QStringLiteral("a3f1c09e00000000000000000000000000077d2a");
const QString hashB = QStringLiteral("b000000000000000000000000000000000000001");

SearchHit hit(const QString &plugin,
              const QString &link,
              int seeds,
              const QString &name = QStringLiteral("Open Movie Night"))
{
    QJsonObject json{{QStringLiteral("fileName"), name},
                     {QStringLiteral("fileSize"), 1000},
                     {QStringLiteral("fileUrl"), link},
                     {QStringLiteral("descrLink"), QStringLiteral("http://site.invalid/t")},
                     {QStringLiteral("nbSeeders"), seeds},
                     {QStringLiteral("nbLeechers"), 2},
                     {QStringLiteral("pubDate"), 1000},
                     {QStringLiteral("engineName"), plugin}};
    return SearchHit::fromJson(json);
}

QString magnet(const QString &hash, const QStringList &trackers)
{
    QString text = QStringLiteral("magnet:?xt=urn:btih:%1&dn=Open%20Movie%20Night").arg(hash);
    for (const QString &tracker : trackers)
        text += QStringLiteral("&tr=") + QString::fromUtf8(QUrl::toPercentEncoding(tracker));
    return text;
}

} // namespace

class TestSearchResults : public QObject
{
    Q_OBJECT

private slots:
    void aHitReadsItsMagnet();
    void unreportedIsMinusOne();
    void copiesOfOneHashFold();
    void aLinkIsItsOwnRow();
    void aPrivateCopyIsNeverFolded();
    void seedOrderKeepsUnknownAboveDead();
    void healthWords();
    void ages();
    void pluginWords();
    void orders();
};

void TestSearchResults::aHitReadsItsMagnet()
{
    const SearchHit one = hit(QStringLiteral("plugin_a"),
                              magnet(hashA.toUpper(), {QStringLiteral("udp://t.invalid:1337")}),
                              41);
    QVERIFY(one.magnet);
    QCOMPARE(one.hash(), hashA);
    QCOMPARE(one.trackers, QStringList{QStringLiteral("udp://t.invalid:1337")});
    QCOMPARE(one.plugin, QStringLiteral("plugin_a"));
    QCOMPARE(one.seeds, 41);
    QCOMPARE(one.page, QStringLiteral("http://site.invalid/t"));
}

void TestSearchResults::unreportedIsMinusOne()
{
    QJsonObject json{{QStringLiteral("fileName"), QStringLiteral("x")},
                     {QStringLiteral("fileUrl"), QStringLiteral("http://site.invalid/x.torrent")},
                     {QStringLiteral("nbSeeders"), -1},
                     {QStringLiteral("nbLeechers"), -1},
                     {QStringLiteral("pubDate"), -1}};
    const SearchHit one = SearchHit::fromJson(json, QStringLiteral("plugin_c"));
    QCOMPARE(one.seeds, -1);
    QCOMPARE(one.leechers, -1);
    QCOMPARE(one.published, -1);
    QVERIFY(!one.magnet);
    QVERIFY(one.hash().isEmpty());
    QCOMPARE(one.plugin, QStringLiteral("plugin_c"));
}

// SR3: the most-seeded copy names the row, every copy's trackers fold into its magnet once.
void TestSearchResults::copiesOfOneHashFold()
{
    const QList<SearchHit> hits = {
        hit(QStringLiteral("plugin_c"),
            magnet(hashA,
                   {QStringLiteral("udp://one.invalid:1337"),
                    QStringLiteral("udp://four.invalid:80")}),
            -1,
            QStringLiteral("Open.Movie.Night")),
        hit(QStringLiteral("plugin_a"),
            magnet(hashA,
                   {QStringLiteral("udp://ONE.invalid:1337/"),
                    QStringLiteral("udp://two.invalid:6969")}),
            41),
        hit(QStringLiteral("plugin_a"), magnet(hashB, {}), 3, QStringLiteral("Something else")),
    };
    const QList<SearchRow> rows = SearchResults::fold(hits);
    QCOMPARE(rows.size(), 2);
    const SearchRow &row = rows.at(0);
    QCOMPARE(row.key, hashA);
    QCOMPARE(row.copies, 2);
    QCOMPARE(row.name, QStringLiteral("Open Movie Night"));
    QCOMPARE(row.seeds, 41);
    QCOMPARE(row.plugin, QStringLiteral("plugin_a"));
    QCOMPARE(row.plugins, (QStringList{QStringLiteral("plugin_a"), QStringLiteral("plugin_c")}));
    QCOMPARE(row.trackers.size(), 3);
    QCOMPARE(row.trackers.at(0), QStringLiteral("udp://ONE.invalid:1337/"));
    const MagnetLink folded = MagnetLink::parse(row.link);
    QVERIFY(folded.isValid());
    QCOMPARE(folded.infoHashV1, hashA);
    QCOMPARE(folded.trackers, row.trackers);
    QCOMPARE(rows.at(1).copies, 1);
}

void TestSearchResults::aLinkIsItsOwnRow()
{
    const QString link = QStringLiteral("http://plugin-a.invalid/get/1.torrent");
    const QList<SearchRow> rows =
        SearchResults::fold({hit(QStringLiteral("plugin_a"), link, 12),
                             hit(QStringLiteral("plugin_a"), link, 12),
                             hit(QStringLiteral("plugin_a"), magnet(hashA, {}), 5)});
    QCOMPARE(rows.size(), 2);
    QVERIFY(rows.at(0).isLink);
    QCOMPARE(rows.at(0).key, QStringLiteral("link:") + link);
    QCOMPARE(rows.at(0).link, link);
    QVERIFY(rows.at(0).hash().isEmpty());
}

// C5 D7, board 25: a copy on a tracker the library knows as private keeps its own row.
void TestSearchResults::aPrivateCopyIsNeverFolded()
{
    const QString secret = QStringLiteral("https://members.invalid/announce/passkey");
    const auto privateHost = [](const QStringList &trackers) {
        for (const QString &tracker : trackers)
            if (tracker.contains(QStringLiteral("members.invalid")))
                return QStringLiteral("members.invalid");
        return QString();
    };
    const QList<SearchRow> rows =
        SearchResults::fold({hit(QStringLiteral("plugin_a"),
                                 magnet(hashA, {QStringLiteral("udp://one.invalid:1")}),
                                 41),
                             hit(QStringLiteral("plugin_b"), magnet(hashA, {secret}), 9),
                             hit(QStringLiteral("plugin_c"),
                                 magnet(hashA, {QStringLiteral("udp://two.invalid:2")}),
                                 3)},
                            privateHost);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).copies, 2);
    QVERIFY(!rows.at(0).trackers.contains(secret));
    QCOMPARE(rows.at(1).privateHost, QStringLiteral("members.invalid"));
    QCOMPARE(rows.at(1).trackers, QStringList{secret});
    QCOMPARE(rows.at(1).key, hashA + QStringLiteral("#1"));
}

// SR4: known counts, then "?", then 0.
void TestSearchResults::seedOrderKeepsUnknownAboveDead()
{
    QVERIFY(SearchResults::seedRank(1) > SearchResults::seedRank(-1));
    QVERIFY(SearchResults::seedRank(-1) > SearchResults::seedRank(0));
    QVERIFY(SearchResults::seedRank(41) > SearchResults::seedRank(5));
}

void TestSearchResults::healthWords()
{
    QCOMPARE(SearchResults::seedsText(-1), QStringLiteral("?"));
    QCOMPARE(SearchResults::seedsCaption(-1), QStringLiteral("not reported"));
    QCOMPARE(SearchResults::seedsCaption(0), QStringLiteral("no seeds"));
    QCOMPARE(SearchResults::seedsCaption(3), QStringLiteral("few seeds"));
    QCOMPARE(SearchResults::seedsCaption(41), QStringLiteral("seeds"));
    QCOMPARE(SearchResults::health(4), SearchResults::Health::Few);
    QCOMPARE(SearchResults::health(5), SearchResults::Health::Good);
}

void TestSearchResults::ages()
{
    const qint64 now = 2'000'000'000;
    QCOMPARE(SearchResults::ageText(-1, now), QStringLiteral("no date"));
    QCOMPARE(SearchResults::ageText(now - 30, now), QStringLiteral("just now"));
    QCOMPARE(SearchResults::ageText(now - 3 * 86400, now), QStringLiteral("3 days ago"));
    QCOMPARE(SearchResults::ageText(now - 86400, now), QStringLiteral("1 day ago"));
    QCOMPARE(SearchResults::ageText(now - 15 * 86400, now), QStringLiteral("2 weeks ago"));
    QCOMPARE(SearchResults::ageText(now - 120 * 86400, now), QStringLiteral("4 months ago"));
    QCOMPARE(SearchResults::ageText(now - 380 * 86400, now), QStringLiteral("1 year ago"));
}

void TestSearchResults::pluginWords()
{
    QCOMPARE(SearchResults::pluginWords({QStringLiteral("a")}), QStringLiteral("a"));
    QCOMPARE(SearchResults::pluginWords({QStringLiteral("a"), QStringLiteral("c")}),
             QStringLiteral("a and c"));
    QCOMPARE(SearchResults::pluginWords(
                 {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                 [](const QString &name) { return QStringLiteral("plugin ") + name; }),
             QStringLiteral("plugin a, plugin b and plugin c"));
}

void TestSearchResults::orders()
{
    SearchRow many, unknown, dead, newer;
    many.seeds = 41;
    unknown.seeds = -1;
    dead.seeds = 0;
    newer.seeds = 41;
    newer.published = 50;
    many.published = 10;
    using SearchResults::Order;
    QVERIFY(SearchResults::before(newer, many, Order::MostSeeds));
    QVERIFY(SearchResults::before(many, unknown, Order::MostSeeds));
    QVERIFY(SearchResults::before(unknown, dead, Order::MostSeeds));
    QVERIFY(SearchResults::before(newer, dead, Order::Newest));
    many.size = 10;
    dead.size = 20;
    QVERIFY(SearchResults::before(dead, many, Order::Size));
    many.name = QStringLiteral("b");
    dead.name = QStringLiteral("A");
    QVERIFY(SearchResults::before(dead, many, Order::Name));
    QCOMPARE(SearchResults::orderFromName(QStringLiteral("newest")), Order::Newest);
    QCOMPARE(SearchResults::orderName(Order::MostSeeds), QStringLiteral("seeds"));
}

QTEST_GUILESS_MAIN(TestSearchResults)
#include "tst_searchresults.moc"
