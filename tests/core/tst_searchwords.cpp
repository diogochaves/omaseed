// SearchWords: the fold, word matching in any order, and the rank (DECISIONS C22, #125).

#include "core/SearchWords.hpp"

#include <QTest>

using namespace omaseed::core;

class TestSearchWords : public QObject
{
    Q_OBJECT

private slots:
    void foldsSeparatorsAndCase_data();
    void foldsSeparatorsAndCase();
    void matchesEveryWordInAnyOrder_data();
    void matchesEveryWordInAnyOrder();
    void ranksPhraseThenOrderThenAny_data();
    void ranksPhraseThenOrderThenAny();
    void emptyQueryMatchesEverything();
};

void TestSearchWords::foldsSeparatorsAndCase_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("folded");

    QTest::newRow("dots") << QStringLiteral("Big.Buck.Bunny.2008.1080p")
                          << QStringLiteral("big buck bunny 2008 1080p");
    QTest::newRow("underscores") << QStringLiteral("big_buck_bunny")
                                 << QStringLiteral("big buck bunny");
    QTest::newRow("dashes") << QStringLiteral("Sintel-2010-4K") << QStringLiteral("sintel 2010 4k");
    QTest::newRow("a run of mixed") << QStringLiteral("a . _-  b") << QStringLiteral("a b");
    QTest::newRow("ends trimmed") << QStringLiteral("  .x_ ") << QStringLiteral("x");
    QTest::newRow("tabs") << QStringLiteral("a\tb") << QStringLiteral("a b");
    QTest::newRow("other punctuation kept")
        << QStringLiteral("Show [2026] (x264)/subs") << QStringLiteral("show [2026] (x264)/subs");
    QTest::newRow("only separators") << QStringLiteral("._-") << QString();
}

void TestSearchWords::foldsSeparatorsAndCase()
{
    QFETCH(QString, text);
    QFETCH(QString, folded);
    QCOMPARE(SearchWords::fold(text), folded);
}

void TestSearchWords::matchesEveryWordInAnyOrder_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("matches");

    const QString release = QStringLiteral("Big.Buck.Bunny.2008.1080p");
    QTest::newRow("the phrase, spaced") << QStringLiteral("big buck bunny") << release << true;
    QTest::newRow("the phrase, dotted") << QStringLiteral("Big.Buck") << release << true;
    QTest::newRow("words out of order") << QStringLiteral("bunny 1080p") << release << true;
    QTest::newRow("a word's prefix") << QStringLiteral("bunny 1080") << release << true;
    QTest::newRow("one word missing") << QStringLiteral("bunny 720p") << release << false;
    QTest::newRow("underscored name")
        << QStringLiteral("buck big") << QStringLiteral("big_buck_bunny") << true;
    QTest::newRow("spaces run") << QStringLiteral("big   buck") << release << true;
}

void TestSearchWords::matchesEveryWordInAnyOrder()
{
    QFETCH(QString, query);
    QFETCH(QString, name);
    QFETCH(bool, matches);
    QCOMPARE(SearchWords::matches(SearchWords::parse(query), SearchWords::fold(name)), matches);
}

void TestSearchWords::ranksPhraseThenOrderThenAny_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("rank");

    QTest::newRow("phrase") << QStringLiteral("big buck bunny")
                            << QStringLiteral("Big.Buck.Bunny.2008") << int(SearchWords::Phrase);
    QTest::newRow("phrase across a dash")
        << QStringLiteral("buck-bunny") << QStringLiteral("big_buck_bunny")
        << int(SearchWords::Phrase);
    QTest::newRow("in order, apart")
        << QStringLiteral("big bunny") << QStringLiteral("Big.Buck.Bunny")
        << int(SearchWords::InOrder);
    QTest::newRow("any order") << QStringLiteral("bunny big") << QStringLiteral("Big.Buck.Bunny")
                               << int(SearchWords::AnyOrder);
    QTest::newRow("a repeated word in order")
        << QStringLiteral("a b") << QStringLiteral("b x a y b") << int(SearchWords::InOrder);
    QTest::newRow("no match") << QStringLiteral("sintel") << QStringLiteral("Big.Buck.Bunny")
                              << int(SearchWords::NoMatch);
}

void TestSearchWords::ranksPhraseThenOrderThenAny()
{
    QFETCH(QString, query);
    QFETCH(QString, name);
    QFETCH(int, rank);
    QCOMPARE(int(SearchWords::rank(SearchWords::parse(query), SearchWords::fold(name))), rank);
}

void TestSearchWords::emptyQueryMatchesEverything()
{
    const SearchWords::Query query = SearchWords::parse(QStringLiteral(" .-_ "));
    QVERIFY(query.isEmpty());
    QVERIFY(SearchWords::matches(query, QStringLiteral("anything")));
    QCOMPARE(SearchWords::rank(query, QStringLiteral("anything")), SearchWords::Phrase);
}

QTEST_GUILESS_MAIN(TestSearchWords)
#include "tst_searchwords.moc"
