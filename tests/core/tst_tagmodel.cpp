// TagModel: the `tags` array of sync/maindata, its partial merges and `tags_removed`.

#include "QbtFeed.hpp"
#include "core/TagModel.hpp"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

using namespace omaseed::core;

class TestTagModel : public QObject
{
    Q_OBJECT

private slots:
    void fullUpdateSortsAndDeduplicates();
    void roleIsTheName();
    void partialAddsAndRemoves();
    void unchangedPartialIsSilent();
    void clearEmptiesIt();
};

void TestTagModel::fullUpdateSortsAndDeduplicates()
{
    TagModel model;
    qbtfeed::full(model,
                  QJsonArray({QStringLiteral("private"),
                              QStringLiteral("keep"),
                              QStringLiteral("4k"),
                              QStringLiteral("keep"),
                              QString()}));

    QCOMPARE(
        model.tags(),
        QStringList({QStringLiteral("4k"), QStringLiteral("keep"), QStringLiteral("private")}));
    QCOMPARE(model.count(), 3);
    QVERIFY(model.contains(QStringLiteral("keep")));
    QVERIFY(!model.contains(QStringLiteral("nope")));
}

void TestTagModel::roleIsTheName()
{
    TagModel model;
    qbtfeed::full(model, QJsonArray({QStringLiteral("keep")}));
    QCOMPARE(model.data(model.index(0), TagModel::NameRole).toString(), QStringLiteral("keep"));
    QCOMPARE(model.roleNames().value(TagModel::NameRole), QByteArray("name"));
}

void TestTagModel::partialAddsAndRemoves()
{
    TagModel model;
    qbtfeed::full(model, QJsonArray({QStringLiteral("keep"), QStringLiteral("private")}));

    qbtfeed::partial(model, QJsonArray({QStringLiteral("4k")}), QJsonArray());
    QCOMPARE(
        model.tags(),
        QStringList({QStringLiteral("4k"), QStringLiteral("keep"), QStringLiteral("private")}));

    qbtfeed::partial(model, QJsonArray(), QJsonArray({QStringLiteral("4k")}));
    QCOMPARE(model.tags(), QStringList({QStringLiteral("keep"), QStringLiteral("private")}));
}

void TestTagModel::unchangedPartialIsSilent()
{
    TagModel model;
    qbtfeed::full(model, QJsonArray({QStringLiteral("keep")}));

    QSignalSpy countSpy(&model, &TagModel::countChanged);
    qbtfeed::partial(model, QJsonArray(), QJsonArray());
    qbtfeed::partial(model, QJsonArray({QStringLiteral("keep")}), QJsonArray());
    QCOMPARE(countSpy.count(), 0);
}

void TestTagModel::clearEmptiesIt()
{
    TagModel model;
    qbtfeed::full(model, QJsonArray({QStringLiteral("keep")}));
    model.clear();
    QCOMPARE(model.count(), 0);
}

QTEST_MAIN(TestTagModel)
#include "tst_tagmodel.moc"
