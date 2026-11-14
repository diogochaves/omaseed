// The filing edits the story tells (board 58, DECISIONS C43, #248), through App against a small
// fake qBittorrent: omaseed's own category and tag edits are written to the server's record as
// the server answers OK (Y2, Y3), one line per torrent saying what it was in or which tags it
// gained and lost, from every place omaseed sends them; a torrent already in place is no line,
// a refused edit none, and a tag edit half refused keeps the half that went. Kept across a
// restart beside the renames (C38 R9, Y10). What a poll sees (step D, Y4–Y8): an edit made
// outside omaseed is a line, the echo of omaseed's own none, a save path omaseed's move changed
// none, automatic management's move the server's, and only on the server the window is on.

#include "core/MoveHistory.hpp"
#include "core/StateFile.hpp"
#include "core/StoryRecord.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"
#include "ui/app/MoveController.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <memory>

using omaseed::app::App;
using omaseed::core::StoryEntry;

namespace {

/// Two torrents, "Pack" and "Single", each with a category, tags, a save path, an incomplete
/// folder and automatic management, every maindata answer a full update of them (with the
/// category and tag lists). torrents/setCategory, addTags, removeTags, createCategory and
/// setAutoManagement change what it lists at once and answer "Ok.", but the paths in `refuse`,
/// which answer 409. When `categoriesRemoved` or `tagsRemoved` is set, the next maindata is a
/// partial update carrying them (`categories_removed`, `tags_removed`), then they are cleared.
class EditServer : public QTcpServer
{
public:
    struct Torrent
    {
        QString name;
        QString category;
        QStringList tags;
        QString savePath = QStringLiteral("/srv/nowhere");
        QString downloadPath;
        bool autoTmm = false;
        double progress = 1.0;
    };
    static Torrent named(const QString &name)
    {
        Torrent t;
        t.name = name;
        return t;
    }

    QString pack = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    QString single = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    QStringList hashes{pack, single};
    QHash<QString, Torrent> torrents{{pack, named(QStringLiteral("Pack"))},
                                     {single, named(QStringLiteral("Single"))}};
    QStringList categories{QStringLiteral("inbox"), QStringLiteral("music")};
    QStringList tags{QStringLiteral("old"), QStringLiteral("new")};
    QStringList categoriesRemoved;
    QStringList tagsRemoved;
    QStringList refuse;
    QStringList posts;
    int polls = 0;
    /// The edits' answers wait this long (the change is listed at once): a poll sees an edit
    /// before its OK.
    int answerDelayMs = 0;
    /// torrents/setLocation: the save path changes after this many polls (0: at once), the
    /// torrent `moving` meanwhile. qBittorrent's own set location changes it as the move lands
    /// (#248 step A).
    int landAfterPolls = 0;
    QHash<QString, std::pair<QString, int>> landing; ///< hash → its new path, polls left

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            qsizetype length = 0;
            for (const QByteArray &line : buffer->left(headerEnd).split('\n'))
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toLongLong();
            if (buffer->size() < headerEnd + 4 + length)
                return;
            const QByteArray target = buffer->left(buffer->indexOf('\n')).split(' ').value(1);
            QByteArray body = buffer->mid(headerEnd + 4, length);
            body.replace('+', ' ');
            buffer->clear();
            const QUrl url(QString::fromUtf8(target));
            answer(socket, url.path().mid(8), QUrlQuery(QString::fromUtf8(body)));
        });
    }

private:
    static QByteArray json(const QJsonValue &value)
    {
        return value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                               : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    }

    static QStringList listOf(const QUrlQuery &form, const QString &key, QChar separator)
    {
        QStringList out;
        for (const QString &item : form.queryItemValue(key, QUrl::FullyDecoded).split(separator))
            if (!item.trimmed().isEmpty())
                out << item.trimmed();
        return out;
    }

    QByteArray maindata()
    {
        ++polls;
        for (auto it = landing.begin(); it != landing.end();) {
            if (--it->second > 0) {
                ++it;
                continue;
            }
            torrents[it.key()].savePath = it->first;
            it = landing.erase(it);
        }
        QJsonObject list;
        for (const QString &hash : std::as_const(hashes)) {
            const Torrent t = torrents.value(hash);
            list.insert(hash,
                        QJsonObject{{QStringLiteral("name"), t.name},
                                    {QStringLiteral("state"),
                                     landing.contains(hash) ? QStringLiteral("moving")
                                                            : QStringLiteral("uploading")},
                                    {QStringLiteral("progress"), t.progress},
                                    {QStringLiteral("size"), 4096},
                                    {QStringLiteral("total_size"), 4096},
                                    {QStringLiteral("category"), t.category},
                                    {QStringLiteral("tags"), t.tags.join(QStringLiteral(", "))},
                                    {QStringLiteral("save_path"), t.savePath},
                                    {QStringLiteral("download_path"), t.downloadPath},
                                    {QStringLiteral("auto_tmm"), t.autoTmm},
                                    {QStringLiteral("content_path"),
                                     t.savePath + QLatin1Char('/') + t.name}});
        }
        QJsonObject categoryList;
        for (const QString &name : std::as_const(categories))
            categoryList.insert(name,
                                QJsonObject{{QStringLiteral("name"), name},
                                            {QStringLiteral("savePath"), QString()}});
        QJsonObject out{{QStringLiteral("rid"), polls},
                        {QStringLiteral("torrents"), list},
                        {QStringLiteral("categories"), categoryList},
                        {QStringLiteral("tags"), QJsonArray::fromStringList(tags)}};
        const bool partial = !categoriesRemoved.isEmpty() || !tagsRemoved.isEmpty();
        out.insert(QStringLiteral("full_update"), !partial);
        if (!categoriesRemoved.isEmpty())
            out.insert(QStringLiteral("categories_removed"),
                       QJsonArray::fromStringList(categoriesRemoved));
        if (!tagsRemoved.isEmpty())
            out.insert(QStringLiteral("tags_removed"), QJsonArray::fromStringList(tagsRemoved));
        categoriesRemoved.clear();
        tagsRemoved.clear();
        return json(out);
    }

    void edit(const QString &path, const QUrlQuery &form)
    {
        const QStringList on = listOf(form, QStringLiteral("hashes"), QLatin1Char('|'));
        if (path == QLatin1String("torrents/createCategory")) {
            const QString name =
                form.queryItemValue(QStringLiteral("category"), QUrl::FullyDecoded);
            if (!categories.contains(name))
                categories << name;
            return;
        }
        for (const QString &hash : on) {
            if (!torrents.contains(hash))
                continue;
            Torrent &t = torrents[hash];
            if (path == QLatin1String("torrents/setCategory")) {
                t.category = form.queryItemValue(QStringLiteral("category"), QUrl::FullyDecoded);
            } else if (path == QLatin1String("torrents/addTags")) {
                for (const QString &tag : listOf(form, QStringLiteral("tags"), QLatin1Char(','))) {
                    if (!t.tags.contains(tag))
                        t.tags << tag;
                    if (!tags.contains(tag))
                        tags << tag;
                }
            } else if (path == QLatin1String("torrents/removeTags")) {
                for (const QString &tag : listOf(form, QStringLiteral("tags"), QLatin1Char(',')))
                    t.tags.removeAll(tag);
            } else if (path == QLatin1String("torrents/setAutoManagement")) {
                t.autoTmm = form.queryItemValue(QStringLiteral("enable")) == QLatin1String("true");
            } else if (path == QLatin1String("torrents/setLocation")) {
                const QString to =
                    form.queryItemValue(QStringLiteral("location"), QUrl::FullyDecoded);
                t.autoTmm = false;
                if (landAfterPolls <= 0)
                    t.savePath = to;
                else
                    landing.insert(hash, {to, landAfterPolls});
            }
        }
    }

    void answer(QTcpSocket *socket, const QString &path, const QUrlQuery &form)
    {
        QByteArray status = "200 OK";
        QByteArray body = "Ok.";
        if (path == QLatin1String("app/version")) {
            body = "v5.2.4";
        } else if (path == QLatin1String("app/webapiVersion")) {
            body = "2.15.1";
        } else if (path == QLatin1String("sync/maindata")) {
            body = maindata();
        } else if (path == QLatin1String("torrents/setCategory") ||
                   path == QLatin1String("torrents/addTags") ||
                   path == QLatin1String("torrents/removeTags") ||
                   path == QLatin1String("torrents/createCategory") ||
                   path == QLatin1String("torrents/setAutoManagement") ||
                   path == QLatin1String("torrents/setLocation")) {
            posts << path;
            if (refuse.contains(path)) {
                status = "409 Conflict";
                body = "Refused";
            } else {
                edit(path, form);
            }
        } else if (path == QLatin1String("torrents/properties")) {
            body = json(QJsonObject{{QStringLiteral("save_path"), QStringLiteral("/srv/nowhere")},
                                    {QStringLiteral("total_size"), 4096}});
        } else if (path == QLatin1String("sync/torrentPeers")) {
            body = "{}";
        } else if (path.startsWith(QLatin1String("log/")) ||
                   path.startsWith(QLatin1String("torrents/"))) {
            body = "[]"; // info, files, trackers, web seeds, piece states, the log
        }
        const QByteArray reply = "HTTP/1.1 " + status +
                                 "\r\nContent-Length: " + QByteArray::number(body.size()) +
                                 "\r\nConnection: close\r\n\r\n" + body;
        const QPointer<QTcpSocket> to(socket);
        const auto send = [to, reply]() {
            if (to.isNull())
                return;
            to->write(reply);
            to->disconnectFromHost();
        };
        if (answerDelayMs > 0 && posts.contains(path) && !path.endsWith(QLatin1String("Location")))
            QTimer::singleShot(answerDelayMs, socket, send);
        else
            send();
    }
};

QList<StoryEntry> recordsOf(const App &app, const QString &hash)
{
    return app.storyRecord().recordsFor(hash);
}

/// The story's lines of `kind` for `hash`, newest first, as "Title / detail".
QStringList storyLines(App &app, const QString &hash, const QString &kind)
{
    QStringList out;
    for (const QVariant &event : app.torrentStory(hash)) {
        const QVariantMap map = event.toMap();
        if (map.value(QStringLiteral("kind")).toString() == kind)
            out << map.value(QStringLiteral("title")).toString() + QStringLiteral(" / ") +
                       map.value(QStringLiteral("detail")).toString();
    }
    return out;
}

} // namespace

class TestAppEdits : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aCategoryIsALineWithWhatItWasIn();
    void aRefusedCategoryWritesNothing();
    void aSelectionGivesEachTorrentItsLine();
    void aTorrentAlreadyInItIsNoLine();
    void tagsAndUntagsAtOnceAreOneLine();
    void aTagAlreadyThereIsNoLine();
    void aHalfRefusedTagEditKeepsTheHalfThatWent();
    void theCategoryPaletteAndTheBarBothWrite();
    void autoManagementWithCategoriesWrites();
    void theRecordOutlivesARestart();

    // Step D: what a poll sees (Y2–Y8).
    void ownCategorySeenBeforeItsOkIsOneLine();
    void ownCategorySeenAfterItsOkIsOneLine();
    void aCategoryChangedOutsideIsALine();
    void theCategoryWasRemovedOnEveryTorrent();
    void tagEchoesLeaveOnlyTheRest();
    void theTagWasRemoved();
    void aLocationSetOutsideIsAPlace();
    void omaseedsOwnMoveIsNoPlace();
    void omaseedsMoveLandingInThePollIsNoPlace();
    void aMoveLandedAMinuteAgoIsNoPlace();
    void autoManagementsMoveIsTheServersByCategory();
    void willLandInWhileWaiting();
    void theBlockSaysMovedSinceOutside();
    void noLineWhileOnAnotherServer();

private:
    bool startOn(App &app, EditServer &server);
    static bool waitPolls(EditServer &server, int count);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppEdits::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppEdits::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

bool TestAppEdits::startOn(App &app, EditServer &server)
{
    if (!server.isListening() && !server.listen(QHostAddress::LocalHost))
        return false;
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.details()->setPollInterval(150);
    app.start();
    return QTest::qWaitFor(
        [&app, &server]() { return app.torrents()->count() == int(server.hashes.size()); }, 5000);
}

bool TestAppEdits::waitPolls(EditServer &server, int count)
{
    const int until = server.polls + count;
    return QTest::qWaitFor([&server, until]() { return server.polls >= until; }, 5000);
}

// Y2: the category it was in when sent, the new one, omaseed's, timed at the OK; the story
// reads "Put in music / was in inbox".
void TestAppEdits::aCategoryIsALineWithWhatItWasIn()
{
    EditServer server;
    server.torrents[server.single].category = QStringLiteral("inbox");
    App app;
    QVERIFY(startOn(app, server));
    const QDateTime before = QDateTime::currentDateTimeUtc().addSecs(-1);
    app.setCategory({server.single}, QStringLiteral("music"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    const StoryEntry line = recordsOf(app, server.single).constFirst();
    QCOMPARE(line.what, StoryEntry::What::Category);
    QCOMPARE(line.by, StoryEntry::By::Omaseed);
    QCOMPARE(line.was, QStringLiteral("inbox"));
    QCOMPARE(line.now, QStringLiteral("music"));
    QVERIFY(line.at >= before);
    QVERIFY(recordsOf(app, server.pack).isEmpty());
    // The strip is the caller's, as before.
    QTRY_COMPARE_WITH_TIMEOUT(app.outcomes()->current().value(QStringLiteral("kind")).toString(),
                              QStringLiteral("done"),
                              3000);
    QCOMPARE(storyLines(app, server.single, QStringLiteral("edit")),
             QStringList{QStringLiteral("Put in music / was in inbox")});
    app.engine()->stop();
}

void TestAppEdits::aRefusedCategoryWritesNothing()
{
    EditServer server;
    server.refuse << QStringLiteral("torrents/setCategory");
    App app;
    QVERIFY(startOn(app, server));
    app.setCategory({server.single}, QStringLiteral("music"));
    QTRY_COMPARE_WITH_TIMEOUT(app.outcomes()->current().value(QStringLiteral("kind")).toString(),
                              QStringLiteral("failed"),
                              3000);
    QVERIFY(waitPolls(server, 2));
    QVERIFY(recordsOf(app, server.single).isEmpty());
    app.engine()->stop();
}

// Y2: a selection gives each torrent its own line, each with what it was in ("" for none).
void TestAppEdits::aSelectionGivesEachTorrentItsLine()
{
    EditServer server;
    server.torrents[server.single].category = QStringLiteral("inbox");
    App app;
    QVERIFY(startOn(app, server));
    app.setCategory({server.pack, server.single}, QStringLiteral("music"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 3000);
    QCOMPARE(recordsOf(app, server.single).size(), 1);
    QCOMPARE(recordsOf(app, server.pack).constFirst().was, QString());
    QCOMPARE(recordsOf(app, server.pack).constFirst().now, QStringLiteral("music"));
    QCOMPARE(recordsOf(app, server.single).constFirst().was, QStringLiteral("inbox"));
    QCOMPARE(storyLines(app, server.pack, QStringLiteral("edit")),
             QStringList{QStringLiteral("Put in music / had no category")});
    app.engine()->stop();
}

void TestAppEdits::aTorrentAlreadyInItIsNoLine()
{
    EditServer server;
    server.torrents[server.single].category = QStringLiteral("inbox");
    App app;
    QVERIFY(startOn(app, server));
    app.setCategory({server.pack, server.single}, QStringLiteral("inbox"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 3000);
    QVERIFY(waitPolls(server, 2));
    QVERIFY(recordsOf(app, server.single).isEmpty());
    app.engine()->stop();
}

// Y3: tagged and untagged in one edit (the palette's diff) are one line, "Tags changed".
void TestAppEdits::tagsAndUntagsAtOnceAreOneLine()
{
    EditServer server;
    server.torrents[server.single].tags = {QStringLiteral("old")};
    App app;
    QVERIFY(startOn(app, server));
    app.applyTags({server.single}, {QStringLiteral("new")}, {QStringLiteral("old")});
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    QVERIFY(server.posts.contains(QStringLiteral("torrents/addTags")));
    QVERIFY(server.posts.contains(QStringLiteral("torrents/removeTags")));
    const StoryEntry line = recordsOf(app, server.single).constFirst();
    QCOMPARE(line.what, StoryEntry::What::Tags);
    QCOMPARE(line.by, StoryEntry::By::Omaseed);
    QCOMPARE(line.tagged, QStringList{QStringLiteral("new")});
    QCOMPARE(line.untagged, QStringList{QStringLiteral("old")});
    QCOMPARE(storyLines(app, server.single, QStringLiteral("edit")),
             QStringList{QStringLiteral("Tags changed / tagged new · untagged old")});
    app.engine()->stop();
}

// Y3: a tag the torrent had already is no part of its line; one it didn't have is no untag.
void TestAppEdits::aTagAlreadyThereIsNoLine()
{
    EditServer server;
    server.torrents[server.single].tags = {QStringLiteral("old")};
    App app;
    QVERIFY(startOn(app, server));
    app.addTags({server.pack, server.single}, {QStringLiteral("old")});
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 3000);
    QCOMPARE(recordsOf(app, server.pack).constFirst().tagged, QStringList{QStringLiteral("old")});
    QVERIFY(recordsOf(app, server.single).isEmpty());
    QCOMPARE(storyLines(app, server.pack, QStringLiteral("edit")),
             QStringList{QStringLiteral("Tagged old / ")});

    app.removeTags({server.pack, server.single}, {QStringLiteral("new")});
    QTRY_VERIFY_WITH_TIMEOUT(server.posts.contains(QStringLiteral("torrents/removeTags")), 3000);
    QVERIFY(waitPolls(server, 2));
    QCOMPARE(recordsOf(app, server.pack).size(), 1);
    QVERIFY(recordsOf(app, server.single).isEmpty());
    app.engine()->stop();
}

void TestAppEdits::aHalfRefusedTagEditKeepsTheHalfThatWent()
{
    EditServer server;
    server.refuse << QStringLiteral("torrents/removeTags");
    server.torrents[server.single].tags = {QStringLiteral("old")};
    App app;
    QVERIFY(startOn(app, server));
    app.applyTags({server.single}, {QStringLiteral("new")}, {QStringLiteral("old")});
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    const StoryEntry line = recordsOf(app, server.single).constFirst();
    QCOMPARE(line.tagged, QStringList{QStringLiteral("new")});
    QVERIFY(line.untagged.isEmpty());
    app.engine()->stop();
}

// Y2: the category palette (applyCategory, createAndApplyCategory) writes as the table does.
void TestAppEdits::theCategoryPaletteAndTheBarBothWrite()
{
    EditServer server;
    App app;
    QVERIFY(startOn(app, server));
    app.applyCategory({server.single}, QStringLiteral("music"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    QCOMPARE(recordsOf(app, server.single).constFirst().now, QStringLiteral("music"));

    app.createAndApplyCategory({server.pack}, QStringLiteral("fresh"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 3000);
    QVERIFY(server.posts.contains(QStringLiteral("torrents/createCategory")));
    QCOMPARE(recordsOf(app, server.pack).constFirst().was, QString());
    QCOMPARE(recordsOf(app, server.pack).constFirst().now, QStringLiteral("fresh"));
    app.engine()->stop();
}

// The shared-move confirm's "automatic management, with categories": each category set is a
// line, and management goes on after.
void TestAppEdits::autoManagementWithCategoriesWrites()
{
    EditServer server;
    server.torrents[server.pack].category = QStringLiteral("inbox");
    App app;
    QVERIFY(startOn(app, server));
    app.setAutoManagementWithCategories({server.pack, server.single},
                                        QVariantMap{{server.pack, QStringLiteral("music")},
                                                    {server.single, QStringLiteral("music")}});
    QTRY_VERIFY_WITH_TIMEOUT(server.posts.contains(QStringLiteral("torrents/setAutoManagement")),
                             3000);
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 3000);
    QCOMPARE(recordsOf(app, server.pack).constFirst().was, QStringLiteral("inbox"));
    QCOMPARE(recordsOf(app, server.single).size(), 1);
    QCOMPARE(recordsOf(app, server.single).constFirst().now, QStringLiteral("music"));
    app.engine()->stop();
}

// C38 R9, C43 Y10: the record is the server's, in state.json, beside the renames.
void TestAppEdits::theRecordOutlivesARestart()
{
    EditServer server;
    {
        App app;
        QVERIFY(startOn(app, server));
        app.setCategory({server.single}, QStringLiteral("music"));
        app.addTags({server.pack}, {QStringLiteral("new")});
        QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 3000);
        app.engine()->stop();
    }
    App app;
    QVERIFY(startOn(app, server));
    QCOMPARE(recordsOf(app, server.single).size(), 1);
    QCOMPARE(recordsOf(app, server.single).constFirst().what, StoryEntry::What::Category);
    QCOMPARE(recordsOf(app, server.single).constFirst().now, QStringLiteral("music"));
    QCOMPARE(recordsOf(app, server.pack).size(), 1);
    QCOMPARE(recordsOf(app, server.pack).constFirst().what, StoryEntry::What::Tags);
    QCOMPARE(recordsOf(app, server.pack).constFirst().tagged, QStringList{QStringLiteral("new")});
    app.engine()->stop();
}

// --- Step D: what a poll sees (Y2–Y8) ------------------------------------------------------------

namespace {

/// `hash`'s record entries of `what`.
QList<StoryEntry> entriesOf(const App &app, const QString &hash, StoryEntry::What what)
{
    QList<StoryEntry> out;
    for (const StoryEntry &entry : app.storyRecord().recordsFor(hash))
        if (entry.what == what)
            out << entry;
    return out;
}

} // namespace

// Y8: omaseed's own category, listed before its OK lands: the poll's sight of it is the echo,
// the OK its one line.
void TestAppEdits::ownCategorySeenBeforeItsOkIsOneLine()
{
    EditServer server;
    server.answerDelayMs = 800;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    app.setCategory({server.single}, QStringLiteral("music"));
    QTRY_COMPARE_WITH_TIMEOUT(
        app.torrents()->torrent(server.single).category, QStringLiteral("music"), 3000);
    QVERIFY(waitPolls(server, 2));
    QVERIFY(recordsOf(app, server.single).isEmpty()); // the OK hasn't landed
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    QVERIFY(waitPolls(server, 3));
    QCOMPARE(recordsOf(app, server.single).size(), 1);
    QCOMPARE(recordsOf(app, server.single).constFirst().by, StoryEntry::By::Omaseed);
    app.engine()->stop();
}

// The other order: the OK first, the poll that lists it after.
void TestAppEdits::ownCategorySeenAfterItsOkIsOneLine()
{
    EditServer server;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    app.addTags({server.single}, {QStringLiteral("new")});
    app.setCategory({server.single}, QStringLiteral("music"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 2, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(
        app.torrents()->torrent(server.single).category, QStringLiteral("music"), 3000);
    QVERIFY(waitPolls(server, 3));
    QCOMPARE(recordsOf(app, server.single).size(), 2);
    for (const StoryEntry &entry : recordsOf(app, server.single))
        QCOMPARE(entry.by, StoryEntry::By::Omaseed);
    app.engine()->stop();
}

// Y2, Y8: a category changed outside omaseed is a line, "changed outside omaseed".
void TestAppEdits::aCategoryChangedOutsideIsALine()
{
    EditServer server;
    server.torrents[server.single].category = QStringLiteral("inbox");
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    server.torrents[server.single].category = QStringLiteral("music");
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    const StoryEntry line = recordsOf(app, server.single).constFirst();
    QCOMPARE(line.what, StoryEntry::What::Category);
    QCOMPARE(line.by, StoryEntry::By::Elsewhere);
    QCOMPARE(line.was, QStringLiteral("inbox"));
    QCOMPARE(line.now, QStringLiteral("music"));
    QVERIFY(!line.removed);
    QCOMPARE(storyLines(app, server.single, QStringLiteral("edit")),
             QStringList{QStringLiteral("Put in music / was in inbox · changed outside omaseed")});
    QVERIFY(recordsOf(app, server.pack).isEmpty());
    app.engine()->stop();
}

// Y8: the category removed from the server takes every torrent in it out: a line each, saying so.
void TestAppEdits::theCategoryWasRemovedOnEveryTorrent()
{
    EditServer server;
    server.torrents[server.single].category = QStringLiteral("inbox");
    server.torrents[server.pack].category = QStringLiteral("inbox");
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    server.torrents[server.single].category.clear();
    server.torrents[server.pack].category.clear();
    server.categories.removeAll(QStringLiteral("inbox"));
    server.categoriesRemoved = {QStringLiteral("inbox")};
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 3000);
    for (const QString &hash : {server.single, server.pack}) {
        const StoryEntry line = recordsOf(app, hash).constFirst();
        QCOMPARE(line.by, StoryEntry::By::Elsewhere);
        QCOMPARE(line.was, QStringLiteral("inbox"));
        QCOMPARE(line.now, QString());
        QVERIFY(line.removed);
    }
    QCOMPARE(storyLines(app, server.pack, QStringLiteral("edit")),
             QStringList{
                 QStringLiteral("Taken out of its category / was in inbox · the category was removed")});
    app.engine()->stop();
}

// Y8: a poll shows omaseed's tag and someone else's together: only the other one is seen.
void TestAppEdits::tagEchoesLeaveOnlyTheRest()
{
    EditServer server;
    server.answerDelayMs = 600;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    app.addTags({server.single}, {QStringLiteral("new")});
    server.torrents[server.single].tags << QStringLiteral("other");
    QTRY_COMPARE_WITH_TIMEOUT(entriesOf(app, server.single, StoryEntry::What::Tags).size(), 2, 3000);
    QVERIFY(waitPolls(server, 2));
    const QList<StoryEntry> lines = entriesOf(app, server.single, StoryEntry::What::Tags);
    QCOMPARE(lines.size(), 2);
    QStringList seen;
    for (const StoryEntry &line : lines)
        seen << (line.by == StoryEntry::By::Omaseed ? QStringLiteral("omaseed ")
                                                    : QStringLiteral("elsewhere ")) +
                    line.tagged.join(QLatin1Char(','));
    seen.sort();
    QCOMPARE(seen, QStringList({QStringLiteral("elsewhere other"), QStringLiteral("omaseed new")}));
    app.engine()->stop();
}

// Y8: a tag deleted on the server is "Untagged NAME / the tag was removed".
void TestAppEdits::theTagWasRemoved()
{
    EditServer server;
    server.torrents[server.single].tags = {QStringLiteral("old")};
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    server.torrents[server.single].tags.clear();
    server.tags.removeAll(QStringLiteral("old"));
    server.tagsRemoved = {QStringLiteral("old")};
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    const StoryEntry line = recordsOf(app, server.single).constFirst();
    QCOMPARE(line.what, StoryEntry::What::Tags);
    QCOMPARE(line.untagged, QStringList{QStringLiteral("old")});
    QVERIFY(line.removed);
    QCOMPARE(storyLines(app, server.single, QStringLiteral("edit")),
             QStringList{QStringLiteral("Untagged old / the tag was removed")});
    app.engine()->stop();
}

// Y4: a location set outside omaseed is "Moved to PATH / moved outside omaseed".
void TestAppEdits::aLocationSetOutsideIsAPlace()
{
    EditServer server;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    server.torrents[server.single].savePath = QStringLiteral("/srv/outside");
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    const StoryEntry line = recordsOf(app, server.single).constFirst();
    QCOMPARE(line.what, StoryEntry::What::Place);
    QCOMPARE(line.by, StoryEntry::By::Elsewhere);
    QCOMPARE(line.was, QStringLiteral("/srv/nowhere"));
    QCOMPARE(line.now, QStringLiteral("/srv/outside"));
    QVERIFY(line.landed);
    QCOMPARE(storyLines(app, server.single, QStringLiteral("move")),
             QStringList{QStringLiteral("Moved to /srv/outside / moved outside omaseed")});
    app.engine()->stop();
}

// Y6: omaseed's own move changes the save path at its start (a server that moves at once):
// the move's story tells it, no place line.
void TestAppEdits::omaseedsOwnMoveIsNoPlace()
{
    EditServer server;
    const QString to = QStringLiteral("/srv/moved");
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    QVERIFY(app.move()->startSelection({server.single}, to, QString()));
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->torrent(server.single).savePath, to, 3000);
    // Landed: its record is in the history (a quick move leaves no summary, C16 L7).
    QTRY_VERIFY_WITH_TIMEOUT(app.move()->history().lastFor(server.single) != nullptr, 3000);
    QVERIFY(waitPolls(server, 2));
    QVERIFY(entriesOf(app, server.single, StoryEntry::What::Place).isEmpty());
    app.engine()->stop();
}

// The main session's call (#248 step A): qBittorrent's own set location changes save_path as
// the move lands, in the poll the job learns it landed: still the move's, whichever is told of
// that poll first.
void TestAppEdits::omaseedsMoveLandingInThePollIsNoPlace()
{
    EditServer server;
    server.landAfterPolls = 3;
    const QString to = QStringLiteral("/srv/moved");
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    QVERIFY(app.move()->startSelection({server.single}, to, QString()));
    QTRY_VERIFY_WITH_TIMEOUT(server.posts.contains(QStringLiteral("torrents/setLocation")), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->torrent(server.single).savePath, to, 3000);
    // Landed: its record is in the history (a quick move leaves no summary, C16 L7).
    QTRY_VERIFY_WITH_TIMEOUT(app.move()->history().lastFor(server.single) != nullptr, 3000);
    QVERIFY(waitPolls(server, 2));
    QVERIFY(entriesOf(app, server.single, StoryEntry::What::Place).isEmpty());
    app.engine()->stop();
}

// Y6: a reading straggling in after the move landed (the old path for a poll, then the new one
// again) is still the move's, for a minute (MoveController::kOwnMoveSeconds).
void TestAppEdits::aMoveLandedAMinuteAgoIsNoPlace()
{
    EditServer server;
    const QString to = QStringLiteral("/srv/moved");
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    QVERIFY(app.move()->startSelection({server.single}, to, QString()));
    // Landed: its record is in the history (a quick move leaves no summary, C16 L7).
    QTRY_VERIFY_WITH_TIMEOUT(app.move()->history().lastFor(server.single) != nullptr, 3000);
    server.torrents[server.single].savePath = QStringLiteral("/srv/nowhere");
    QTRY_COMPARE_WITH_TIMEOUT(
        app.torrents()->torrent(server.single).savePath, QStringLiteral("/srv/nowhere"), 3000);
    server.torrents[server.single].savePath = to;
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->torrent(server.single).savePath, to, 3000);
    QVERIFY(waitPolls(server, 2));
    QVERIFY(entriesOf(app, server.single, StoryEntry::What::Place).isEmpty());
    app.engine()->stop();
}

// Y7: automatic management moved it (its category's folder changed, or the category did): the
// server's, "by its category NAME · automatic management".
void TestAppEdits::autoManagementsMoveIsTheServersByCategory()
{
    EditServer server;
    server.torrents[server.single].category = QStringLiteral("music");
    server.torrents[server.single].autoTmm = true;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    server.torrents[server.single].savePath = QStringLiteral("/srv/music");
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    const StoryEntry line = recordsOf(app, server.single).constFirst();
    QCOMPARE(line.what, StoryEntry::What::Place);
    QCOMPARE(line.by, StoryEntry::By::Server);
    QCOMPARE(line.cause, QStringLiteral("category"));
    QCOMPARE(line.causeName, QStringLiteral("music"));
    QCOMPARE(storyLines(app, server.single, QStringLiteral("move")),
             QStringList{QStringLiteral(
                 "Moved to /srv/music / by its category music · automatic management")});
    app.engine()->stop();
}

// Y4: unfinished with an incomplete folder, a new save path is where it will land.
void TestAppEdits::willLandInWhileWaiting()
{
    EditServer server;
    server.torrents[server.single].progress = 0.5;
    server.torrents[server.single].downloadPath = QStringLiteral("/srv/waiting");
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    server.torrents[server.single].savePath = QStringLiteral("/srv/far");
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    const StoryEntry line = recordsOf(app, server.single).constFirst();
    QVERIFY(!line.landed);
    QCOMPARE(storyLines(app, server.single, QStringLiteral("move")),
             QStringList{QStringLiteral("Will land in /srv/far / moved outside omaseed")});
    app.engine()->stop();
}

// Y6: a place line newer than the last move: the block's "moved since outside omaseed".
void TestAppEdits::theBlockSaysMovedSinceOutside()
{
    EditServer server;
    const QString to = QStringLiteral("/srv/moved");
    {
        App app;
        QVERIFY(startOn(app, server));
        QVERIFY(waitPolls(server, 1));
        QVERIFY(app.move()->startSelection({server.single}, to, QString()));
        QTRY_VERIFY_WITH_TIMEOUT(app.move()->history().lastFor(server.single) != nullptr, 3000);
        QVERIFY(!app.placeMovedSinceLastMove(server.single));
        app.engine()->stop();
    }
    // The move ended two minutes ago: past its own minute.
    QJsonObject moves = omaseed::core::StateFile::readSection(QStringLiteral("moves"));
    for (const QString &id : moves.keys()) {
        QJsonObject serverState = moves.value(id).toObject();
        QJsonArray history = serverState.value(QStringLiteral("history")).toArray();
        for (qsizetype i = 0; i < history.size(); ++i) {
            QJsonObject record = history.at(i).toObject();
            for (const QString &key : {QStringLiteral("startedAt"), QStringLiteral("finishedAt")})
                record.insert(key,
                              QDateTime::fromString(record.value(key).toString(), Qt::ISODate)
                                  .addSecs(-120)
                                  .toString(Qt::ISODate));
            history.replace(i, record);
        }
        serverState.insert(QStringLiteral("history"), history);
        moves.insert(id, serverState);
    }
    omaseed::core::StateFile::writeSection(QStringLiteral("moves"), moves);

    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    QVERIFY(!app.placeMovedSinceLastMove(server.single));
    server.torrents[server.single].savePath = QStringLiteral("/srv/outside");
    QTRY_COMPARE_WITH_TIMEOUT(entriesOf(app, server.single, StoryEntry::What::Place).size(), 1, 3000);
    QVERIFY(app.placeMovedSinceLastMove(server.single));
    QVERIFY(!app.placeMovedSinceLastMove(server.pack));
    app.engine()->stop();
}

// Only the server the window is on writes to its record; the first full update after a switch
// has no "before" (C38 R5).
void TestAppEdits::noLineWhileOnAnotherServer()
{
    qunsetenv("OMASEED_SERVER_URL");
    EditServer first;
    EditServer second;
    QVERIFY(first.listen(QHostAddress::LocalHost));
    QVERIFY(second.listen(QHostAddress::LocalHost));
    App app;
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    const auto add = [&app](const QString &name, const EditServer &server) {
        return app.addServer({{QStringLiteral("name"), name},
                              {QStringLiteral("url"),
                               QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())},
                              {QStringLiteral("authMode"), QStringLiteral("none")}});
    };
    const QString firstId = add(QStringLiteral("one"), first);
    QVERIFY(!firstId.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 2, 5000);
    const QString secondId = add(QStringLiteral("two"), second);
    QVERIFY(!secondId.isEmpty());
    QVERIFY(app.switchServer(secondId));
    QTRY_COMPARE_WITH_TIMEOUT(app.serverName(), QStringLiteral("two"), 3000);
    QVERIFY(waitPolls(second, 2));

    // Filed on the first while the window is on the second: no line on either.
    first.torrents[first.single].category = QStringLiteral("music");
    first.torrents[first.single].savePath = QStringLiteral("/srv/outside");
    QVERIFY(waitPolls(second, 2));
    QVERIFY(recordsOf(app, second.single).isEmpty());
    QVERIFY(app.switchServer(firstId));
    QVERIFY(waitPolls(first, 3));
    QCOMPARE(app.torrents()->torrent(first.single).category, QStringLiteral("music"));
    QVERIFY(recordsOf(app, first.single).isEmpty());
    app.engine()->stop();
}

QTEST_MAIN(TestAppEdits)
#include "tst_app_edits.moc"
