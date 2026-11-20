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
    void tempFolderOfOwnNoneSession();
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
    // The row is said changed, with no role, so the add's room reads its temp folder again
    // (DECISIONS C47 I12); the save path is not among what moved.
    QCOMPARE(changed.count(), 1);
    QVERIFY(changed.at(0).at(2).value<QList<int>>().isEmpty());
    QCOMPARE(model.tempFolderOf(QStringLiteral("movies")).value(QStringLiteral("path")).toString(),
             QStringLiteral("/x"));
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

// #271 (2), DECISIONS C47 I12: a category's own temp folder, none, or the session's rule, kept
// through a partial that leaves it out and changed by one that carries it.
void TestCategoryModel::tempFolderOfOwnNoneSession()
{
    CategoryModel model;
    QJsonObject own = category(QStringLiteral("films"), QStringLiteral("/data/films"));
    own.insert(QStringLiteral("download_path"), QStringLiteral("/fast/films"));
    QJsonObject none = category(QStringLiteral("music"), QStringLiteral("/data/music"));
    none.insert(QStringLiteral("download_path"), false);
    QJsonObject session = category(QStringLiteral("tv"), QStringLiteral("/data/tv"));
    session.insert(QStringLiteral("download_path"), QJsonValue::Null);
    qbtfeed::full(model,
                  QJsonObject{{QStringLiteral("films"), own},
                              {QStringLiteral("music"), none},
                              {QStringLiteral("tv"), session},
                              {QStringLiteral("old"),
                               category(QStringLiteral("old"), QStringLiteral("/data/old"))}});

    const auto ruleOf = [&model](const QString &name) {
        return model.tempFolderOf(name).value(QStringLiteral("rule")).toString();
    };
    QCOMPARE(ruleOf(QStringLiteral("films")), QStringLiteral("own"));
    QCOMPARE(model.tempFolderOf(QStringLiteral("films")).value(QStringLiteral("path")).toString(),
             QStringLiteral("/fast/films"));
    QCOMPARE(ruleOf(QStringLiteral("music")), QStringLiteral("none"));
    QCOMPARE(ruleOf(QStringLiteral("tv")), QStringLiteral("session"));
    QCOMPARE(ruleOf(QStringLiteral("old")), QStringLiteral("session")); // nothing said
    QCOMPARE(ruleOf(QStringLiteral("nobody")), QStringLiteral("session"));

    // A partial with only the save path keeps the temp folder; one with it changes it, quietly.
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    qbtfeed::partial(
        model,
        QJsonObject{{QStringLiteral("films"),
                     QJsonObject{{QStringLiteral("savePath"), QStringLiteral("/data/f2")}}},
                    {QStringLiteral("tv"),
                     QJsonObject{{QStringLiteral("download_path"), QStringLiteral("/fast/tv")}}}},
        QJsonArray());
    QCOMPARE(ruleOf(QStringLiteral("films")), QStringLiteral("own"));
    QCOMPARE(model.tempFolderOf(QStringLiteral("tv")).value(QStringLiteral("path")).toString(),
             QStringLiteral("/fast/tv"));
    QCOMPARE(reset.count(), 0);
}

QTEST_MAIN(TestCategoryModel)
#include "tst_categorymodel.moc"
