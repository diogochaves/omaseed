// TorrentWebSeedsModel: the HTTP sources tab's rows (#10), from `torrents/webseeds`.

#include "core/TorrentWebSeedsModel.hpp"

#include "core/Format.hpp"

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
    void eachUrlSaysWhatTheLogSays();
    void logLinesOutliveARowChange();
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

// docs/32 W1 (#55): the playground's 404 (a file's URL under a folder seed) and dead mirror,
// once and ten times, and a seed nothing was logged about.
void TestTorrentWebSeedsModel::eachUrlSaysWhatTheLogSays()
{
    using omaseed::core::WebSeedLogLine;
    TorrentWebSeedsModel model;
    model.applyWebSeeds(seeds(R"([{"url":"http://127.0.0.1:26955/pub/"},
                                  {"url":"https://mirror.invalid/omaseed-webseed-live/"},
                                  {"url":"https://archive.example.org/pub/"}])"));
    const QModelIndex first = model.index(0);
    QCOMPARE(first.data(TorrentWebSeedsModel::StatusErrorRole).toString(), QString());
    QCOMPARE(first.data(TorrentWebSeedsModel::StatusMetaRole).toString(),
             QStringLiteral("no errors logged"));

    const QDateTime at = QDateTime::currentDateTime().addSecs(-60);
    QList<WebSeedLogLine> lines{{QStringLiteral("http://127.0.0.1:26955/pub/log55-webseed.bin"),
                                 QStringLiteral("404 File not found"),
                                 at}};
    for (int i = 0; i < 10; ++i)
        lines.append({QStringLiteral("https://mirror.invalid/omaseed-webseed-live/"),
                      QStringLiteral("host not found"),
                      at.addSecs(i)});
    QSignalSpy changed(&model, &TorrentWebSeedsModel::dataChanged);
    model.setLogLines(lines);
    QCOMPARE(changed.count(), 2); // the third row says what it said

    QCOMPARE(first.data(TorrentWebSeedsModel::StatusErrorRole).toString(),
             QStringLiteral("404 File not found"));
    QCOMPARE(first.data(TorrentWebSeedsModel::StatusMetaRole).toString(),
             omaseed::core::Format::relativeTime(at));
    QCOMPARE(model.index(1).data(TorrentWebSeedsModel::StatusErrorRole).toString(),
             QStringLiteral("host not found"));
    QCOMPARE(model.index(1).data(TorrentWebSeedsModel::StatusMetaRole).toString(),
             QStringLiteral("10 times, last %1")
                 .arg(omaseed::core::Format::relativeTime(at.addSecs(9))));
    QCOMPARE(model.index(2).data(TorrentWebSeedsModel::StatusMetaRole).toString(),
             QStringLiteral("no errors logged"));
    QCOMPARE(model.roleNames().value(TorrentWebSeedsModel::StatusErrorRole),
             QByteArray("statusError"));

    // The same lines again: nothing to repaint.
    model.setLogLines(lines);
    QCOMPARE(changed.count(), 2);
}

// The pane clears the rows on every selection change; the lines, handed over per torrent,
// apply to the rows that come next.
void TestTorrentWebSeedsModel::logLinesOutliveARowChange()
{
    TorrentWebSeedsModel model;
    model.setLogLines({{QStringLiteral("https://m.example/a.iso"),
                        QStringLiteral("timed out"),
                        QDateTime::currentDateTime()}});
    model.clear();
    model.applyWebSeeds(seeds(R"([{"url":"https://m.example/a.iso"}])"));
    QCOMPARE(model.index(0).data(TorrentWebSeedsModel::StatusErrorRole).toString(),
             QStringLiteral("timed out"));
}

QTEST_GUILESS_MAIN(TestTorrentWebSeedsModel)

#include "tst_torrentwebseedsmodel.moc"
