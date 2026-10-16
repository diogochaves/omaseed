// CategoryModel: the `categories` map of sync/maindata, its partial merges and
// `categories_removed`, kept sorted for the rail (spec §1) and the picker (spec §4 Fig. 13).

#include "QbtFeed.hpp"
#include "core/CategoryModel.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace omaseed::core;

namespace {

QJsonObject category(const QString &name, const QString &savePath)
{
    return QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("savePath"), savePath}};
}

} // namespace

class TestCategoryModel : public QObject
{
    Q_OBJECT

private slots:
    void fullUpdateSortsByName();
    void rolesCarryNameAndSavePath();
    void partialAddsAndRemoves();
    void partialSavePathChangeKeepsTheRows();
    void partialWithoutSavePathKeepsIt();
    void clearEmptiesIt();
};

void TestCategoryModel::fullUpdateSortsByName()
{
    CategoryModel model;
    QSignalSpy countSpy(&model, &CategoryModel::countChanged);

    qbtfeed::full(
        model,
        QJsonObject{
            {QStringLiteral("tv"), category(QStringLiteral("tv"), QStringLiteral("/data/tv"))},
            {QStringLiteral("Linux"),
             category(QStringLiteral("Linux"), QStringLiteral("/data/linux"))},
            {QStringLiteral("movies"),
             category(QStringLiteral("movies"), QStringLiteral("/data/movies"))},
        });

    QCOMPARE(model.count(), 3);
    QCOMPARE(model.names(),
             QStringList({QStringLiteral("Linux"),
                          QStringLiteral("movies"),
                          QStringLiteral("tv")})); // case-insensitive order
    QCOMPARE(countSpy.count(), 1);
}

void TestCategoryModel::rolesCarryNameAndSavePath()
{
    CategoryModel model;
    qbtfeed::full(model,
                  QJsonObject{{QStringLiteral("linux"),
                               category(QStringLiteral("linux"), QStringLiteral("/data/linux"))}});

    const QModelIndex first = model.index(0);
    QCOMPARE(model.data(first, CategoryModel::NameRole).toString(), QStringLiteral("linux"));
    QCOMPARE(model.data(first, CategoryModel::SavePathRole).toString(),
             QStringLiteral("/data/linux"));
    QCOMPARE(model.roleNames().value(CategoryModel::NameRole), QByteArray("name"));
    QCOMPARE(model.roleNames().value(CategoryModel::SavePathRole), QByteArray("savePath"));
    QCOMPARE(model.savePathOf(QStringLiteral("linux")), QStringLiteral("/data/linux"));
    QCOMPARE(model.savePathOf(QStringLiteral("nope")), QString());
}

void TestCategoryModel::partialAddsAndRemoves()
{
    CategoryModel model;
    qbtfeed::full(
        model,
        QJsonObject{{QStringLiteral("fixtures"), category(QStringLiteral("fixtures"), QString())},
                    {QStringLiteral("linux"),
                     category(QStringLiteral("linux"), QStringLiteral("/tmp/linux"))}});

    qbtfeed::partial(model,
                     QJsonObject{{QStringLiteral("music"),
                                  category(QStringLiteral("music"), QStringLiteral("/data/m"))}},
                     QJsonArray({QStringLiteral("linux")}));

    QCOMPARE(model.names(), QStringList({QStringLiteral("fixtures"), QStringLiteral("music")}));

    // An empty diff must not disturb anything.
    QSignalSpy countSpy(&model, &CategoryModel::countChanged);
    qbtfeed::partial(model, QJsonObject(), QJsonArray());
    QCOMPARE(countSpy.count(), 0);
    QCOMPARE(model.count(), 2);
}

void TestCategoryModel::partialSavePathChangeKeepsTheRows()
{
    CategoryModel model;
    qbtfeed::full(model,
                  QJsonObject{{QStringLiteral("linux"),
                               category(QStringLiteral("linux"), QStringLiteral("/old"))}});

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    qbtfeed::partial(model,
                     QJsonObject{{QStringLiteral("linux"),
                                  category(QStringLiteral("linux"), QStringLiteral("/new"))}},
                     QJsonArray());

    QCOMPARE(model.savePathOf(QStringLiteral("linux")), QStringLiteral("/new"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(changed.first().at(2).value<QList<int>>(),
             QList<int>({int(CategoryModel::SavePathRole)}));
}

// NET-2: qBittorrent's partial `categories` entry holds only the changed keys. Editing just
// the download path sends {"movies": {"download_path": "/x"}}, with no savePath and no name.
void TestCategoryModel::partialWithoutSavePathKeepsIt()
{
    CategoryModel model;
    qbtfeed::full(model,
                  QJsonObject{{QStringLiteral("movies"),
                               category(QStringLiteral("movies"), QStringLiteral("/data/m"))}});

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    qbtfeed::partial(
        model,
        QJsonObject{{QStringLiteral("movies"),
                     QJsonObject{{QStringLiteral("download_path"), QStringLiteral("/x")}}}},
        QJsonArray());

    QCOMPARE(model.names(), QStringList({QStringLiteral("movies")}));
    QCOMPARE(model.savePathOf(QStringLiteral("movies")), QStringLiteral("/data/m"));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(reset.count(), 0);
}

void TestCategoryModel::clearEmptiesIt()
{
    CategoryModel model;
    qbtfeed::full(
        model,
        QJsonObject{{QStringLiteral("linux"), category(QStringLiteral("linux"), QString())}});
    model.clear();
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.names(), QStringList());
}

QTEST_MAIN(TestCategoryModel)
#include "tst_categorymodel.moc"
