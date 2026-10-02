// TorrentWebSeedsModel: the HTTP sources tab's rows (#10), from `torrents/webseeds`.

#include "core/TorrentWebSeedsModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

using omaseed::core::TorrentWebSeedsModel;

namespace {

QJsonArray seeds(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).array();
}

} // namespace

class TestTorrentWebSeedsModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void rowsKeepTheServersOrderAndDropTheScheme();
    void plainHttpKeepsItsScheme();
    void entriesWithoutAUrlAreSkipped();
    void loadedFollowsTheAnswerNotTheCount();
    void countChangesOnlyWhenTheCountDoes();
    void urlAtIsEmptyOutOfRange();
    void displayText_data();
    void displayText();
};

void TestTorrentWebSeedsModel::rowsKeepTheServersOrderAndDropTheScheme()
{
    TorrentWebSeedsModel model;
    model.applyWebSeeds(seeds(R"([{"url":"https://mirror.example/pub/"},
                                  {"url":"http://archive.example/download/item/a.iso"}])"));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.rowCount(), 2);
    const QModelIndex first = model.index(0);
    QCOMPARE(first.data(TorrentWebSeedsModel::UrlRole).toString(),
             QStringLiteral("https://mirror.example/pub/"));
    QCOMPARE(first.data(TorrentWebSeedsModel::TextRole).toString(),
             QStringLiteral("mirror.example/pub/"));
    QCOMPARE(first.data(TorrentWebSeedsModel::HostRole).toString(),
             QStringLiteral("mirror.example"));
    QCOMPARE(model.index(1).data(TorrentWebSeedsModel::TextRole).toString(),
             QStringLiteral("http://archive.example/download/item/a.iso"));
    QCOMPARE(model.roleNames().value(TorrentWebSeedsModel::TextRole), QByteArray("text"));
}

void TestTorrentWebSeedsModel::plainHttpKeepsItsScheme()
{
    TorrentWebSeedsModel model;
    model.applyWebSeeds(seeds(R"([{"url":"https://a.example/"},{"url":"HTTPS://b.example/"},
                                  {"url":"http://c.example/"}])"));
    QCOMPARE(model.index(0).data(TorrentWebSeedsModel::TextRole).toString(),
             QStringLiteral("a.example/"));
    QCOMPARE(model.index(1).data(TorrentWebSeedsModel::TextRole).toString(),
             QStringLiteral("b.example/"));
    QCOMPARE(model.index(2).data(TorrentWebSeedsModel::TextRole).toString(),
             QStringLiteral("http://c.example/"));
}

void TestTorrentWebSeedsModel::entriesWithoutAUrlAreSkipped()
{
    TorrentWebSeedsModel model;
    model.applyWebSeeds(seeds(R"([{"url":""},{},{"url":"https://a.example/"}])"));
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.urlAt(0), QStringLiteral("https://a.example/"));
}

// The pane keeps the tab while a newly selected torrent's list is on its way: `loaded`
// tells "none" from "not known yet".
void TestTorrentWebSeedsModel::loadedFollowsTheAnswerNotTheCount()
{
    TorrentWebSeedsModel model;
    QSignalSpy loaded(&model, &TorrentWebSeedsModel::loadedChanged);
    QCOMPARE(model.loaded(), false);
    model.applyWebSeeds(QJsonArray());
    QCOMPARE(model.loaded(), true);
    QCOMPARE(model.count(), 0);
    QCOMPARE(loaded.count(), 1);
    model.applyWebSeeds(QJsonArray());
    QCOMPARE(loaded.count(), 1);
    model.clear();
    QCOMPARE(model.loaded(), false);
    QCOMPARE(loaded.count(), 2);
}

void TestTorrentWebSeedsModel::countChangesOnlyWhenTheCountDoes()
{
    TorrentWebSeedsModel model;
    QSignalSpy count(&model, &TorrentWebSeedsModel::countChanged);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    model.applyWebSeeds(seeds(R"([{"url":"https://a.example/"}])"));
    QCOMPARE(count.count(), 1);
    QCOMPARE(reset.count(), 1);
    // The same answer again (every tick while the tab is on show): nothing moves.
    model.applyWebSeeds(seeds(R"([{"url":"https://a.example/"}])"));
    QCOMPARE(count.count(), 1);
    QCOMPARE(reset.count(), 1);
    // An edit: the rows change, the count does not.
    model.applyWebSeeds(seeds(R"([{"url":"https://b.example/"}])"));
    QCOMPARE(count.count(), 1);
    QCOMPARE(reset.count(), 2);
    model.clear();
    QCOMPARE(count.count(), 2);
}

void TestTorrentWebSeedsModel::urlAtIsEmptyOutOfRange()
{
    TorrentWebSeedsModel model;
    QCOMPARE(model.urlAt(0), QString());
    model.applyWebSeeds(seeds(R"([{"url":"https://a.example/"}])"));
    QCOMPARE(model.urlAt(-1), QString());
    QCOMPARE(model.urlAt(1), QString());
}

void TestTorrentWebSeedsModel::displayText_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("text");
    QTest::newRow("https") << QStringLiteral("https://m.example/pub/")
                           << QStringLiteral("m.example/pub/");
    QTest::newRow("http") << QStringLiteral("http://m.example:8000/a")
                          << QStringLiteral("http://m.example:8000/a");
    QTest::newRow("no scheme") << QStringLiteral("m.example/pub/")
                               << QStringLiteral("m.example/pub/");
    QTest::newRow("empty") << QString() << QString();
}

void TestTorrentWebSeedsModel::displayText()
{
    QFETCH(QString, url);
    QFETCH(QString, text);
    QCOMPARE(TorrentWebSeedsModel::displayText(url), text);
}

QTEST_GUILESS_MAIN(TestTorrentWebSeedsModel)

#include "tst_torrentwebseedsmodel.moc"
