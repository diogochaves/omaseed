// The qBittorrent golden test (#81, docs/36 §6 E1): recorded replies from the playground,
// fed through the models, give exactly the rows, roles, signals, rail counts, server state,
// details and completion events that main gave before the engine-neutral layer existed.
//
// tests/fixtures/qbt/ holds a real sequence from the 5.2.3 playground and its swarm (paths
// under /home/user): a full sync/maindata, then partial ones across a removal, an add, a
// download, stop and start, category and tag changes, completion, an errored torrent, a
// recheck, force start and limits, and the details of one torrent (properties, files,
// trackers, peers full and partial, web seeds, piece states) and log/main.
//
// golden.json is what main produced; it must not change while the models are rewritten
// underneath. OMASEED_WRITE_GOLDEN=1 rewrites it, which is only right when a behaviour change
// is intended and reviewed. Texts that depend on today's date (Format::relativeTime) are left
// out; every one of them is computed from a raw value that is in the snapshot.

#include "QbtFeed.hpp"
#include "core/CategoryModel.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/ProblemLog.hpp"
#include "core/ServerLog.hpp"
#include "core/StatusDigest.hpp"
#include "core/SyncEngine.hpp"
#include "core/TagModel.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentPeersModel.hpp"
#include "core/TorrentTrackersModel.hpp"
#include "core/TorrentWebSeedsModel.hpp"
#include "core/TrackerModel.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaProperty>
#include <QTest>

#include <cmath>
#include <limits>

using namespace omaseed::core;

namespace {

const QString kDir = QStringLiteral(OMASEED_FIXTURES_DIR "/qbt/");
/// A fixed "now" for everything that takes one, so the snapshot does not age.
constexpr qint64 kNow = 1792180000; // 2026-10-16

QJsonValue readJson(const QString &name)
{
    QFile file(kDir + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

/// qBittorrent's "no estimate" and "infinite ratio", however the model spells them.
QJsonValue etaValue(const QVariant &value)
{
    const qint64 seconds = value.toLongLong();
    return seconds >= 8640000 ? QJsonValue(QStringLiteral("none")) : QJsonValue(seconds);
}

QJsonValue ratioValue(const QVariant &value)
{
    const double ratio = value.toDouble();
    return ratio >= 9999.0 ? QJsonValue(QStringLiteral("inf")) : QJsonValue(ratio);
}

QJsonValue toJson(const QVariant &value)
{
    if (!value.isValid())
        return QJsonValue(QJsonValue::Undefined);
    switch (value.typeId()) {
    case QMetaType::QDateTime: {
        const QDateTime when = value.toDateTime();
        return when.isValid() ? QJsonValue(when.toSecsSinceEpoch()) : QJsonValue();
    }
    case QMetaType::QStringList:
        return QJsonArray::fromStringList(value.toStringList());
    case QMetaType::QVariantMap: {
        // A problem's "today 20:00" ages; its timestamp does not.
        QJsonObject object = QJsonObject::fromVariantMap(value.toMap());
        object.remove(QStringLiteral("loggedAtText"));
        return object;
    }
    case QMetaType::QVariantList:
        return QJsonArray::fromVariantList(value.toList());
    case QMetaType::Double:
        return value.toDouble();
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        return value.toLongLong();
    default:
        break;
    }
    if (value.canConvert<QList<int>>()) {
        QJsonArray array;
        for (const int item : value.value<QList<int>>())
            array.append(item);
        return array;
    }
    return value.toString();
}

/// The native state word of the `state` role (main's int enum became the word itself).
QJsonValue stateWord(const QVariant &value)
{
    return value.toString();
}

QJsonArray rowsOf(const QAbstractItemModel &model, const QSet<QByteArray> &skip = {})
{
    const QHash<int, QByteArray> names = model.roleNames();
    QList<int> roles = names.keys();
    std::sort(roles.begin(), roles.end(), [&names](int a, int b) { return names[a] < names[b]; });
    QJsonArray rows;
    for (int row = 0; row < model.rowCount(); ++row) {
        QJsonObject object;
        const QModelIndex index = model.index(row, 0);
        for (const int role : roles) {
            const QByteArray &name = names[role];
            if (skip.contains(name))
                continue;
            const QVariant value = model.data(index, role);
            const QString key = QString::fromUtf8(name);
            if (name == "eta")
                object.insert(key, etaValue(value));
            else if (name == "ratio")
                object.insert(key, ratioValue(value));
            else if (name == "state" && dynamic_cast<const TorrentModel *>(&model) != nullptr)
                object.insert(key, stateWord(value));
            else
                object.insert(key, toJson(value));
        }
        rows.append(object);
    }
    return rows;
}

QJsonObject propertiesOf(const QObject &object, const QSet<QByteArray> &skip = {})
{
    QJsonObject out;
    const QMetaObject *meta = object.metaObject();
    for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        if (skip.contains(property.name()))
            continue;
        out.insert(QString::fromUtf8(property.name()), toJson(property.read(&object)));
    }
    return out;
}

/// Day-relative texts and the whole-value role.
QSet<QByteArray> skippedTorrentRoles()
{
    QSet<QByteArray> skip{"torrent"};
    TorrentModel model;
    const QHash<int, QByteArray> names = model.roleNames();
    for (const int role : TorrentModel::dayRelativeRoles())
        skip.insert(names.value(role));
    return skip;
}

/// Records what a model announced during one step.
class SignalLog : public QObject
{
public:
    explicit SignalLog(QAbstractItemModel *model)
        : m_model(model)
    {
        const QHash<int, QByteArray> names = model->roleNames();
        connect(model,
                &QAbstractItemModel::dataChanged,
                this,
                [this, names](
                    const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
                    QStringList roleNames;
                    for (const int role : roles)
                        roleNames.append(QString::fromUtf8(names.value(role)));
                    roleNames.sort();
                    m_events.append(QJsonObject{
                        {QStringLiteral("changed"), QJsonArray{first.row(), last.row()}},
                        {QStringLiteral("roles"), QJsonArray::fromStringList(roleNames)}});
                });
        connect(model,
                &QAbstractItemModel::rowsInserted,
                this,
                [this](const QModelIndex &, int a, int b) {
                    m_events.append(QJsonObject{{QStringLiteral("inserted"), QJsonArray{a, b}}});
                });
        connect(model,
                &QAbstractItemModel::rowsRemoved,
                this,
                [this](const QModelIndex &, int a, int b) {
                    m_events.append(QJsonObject{{QStringLiteral("removed"), QJsonArray{a, b}}});
                });
        connect(model, &QAbstractItemModel::modelReset, this, [this]() {
            m_events.append(QJsonObject{{QStringLiteral("reset"), true}});
        });
    }

    QJsonArray take()
    {
        const QJsonArray events = m_events;
        m_events = {};
        return events;
    }

private:
    QAbstractItemModel *m_model;
    QJsonArray m_events;
};

/// The first path where `a` and `b` part, for a readable failure.
QString firstDifference(const QJsonValue &a, const QJsonValue &b, const QString &path)
{
    if (a.type() != b.type())
        return path + QStringLiteral(": type differs");
    if (a.isObject()) {
        const QJsonObject oa = a.toObject();
        const QJsonObject ob = b.toObject();
        QStringList keys = oa.keys() + ob.keys();
        keys.removeDuplicates();
        for (const QString &key : keys) {
            if (oa.value(key) != ob.value(key))
                return firstDifference(oa.value(key), ob.value(key), path + QLatin1Char('.') + key);
        }
        return {};
    }
    if (a.isArray()) {
        const QJsonArray aa = a.toArray();
        const QJsonArray ab = b.toArray();
        for (qsizetype i = 0; i < std::max(aa.size(), ab.size()); ++i) {
            if (i >= aa.size() || i >= ab.size())
                return path +
                       QStringLiteral("[%1]: length %2 vs %3").arg(i).arg(aa.size()).arg(ab.size());
            if (aa.at(i) != ab.at(i))
                return firstDifference(aa.at(i), ab.at(i), path + QStringLiteral("[%1]").arg(i));
        }
        return {};
    }
    if (a != b) {
        return path + QStringLiteral(": golden %1, now %2")
                          .arg(QString::fromUtf8(
                                   QJsonDocument(QJsonArray{a}).toJson(QJsonDocument::Compact)),
                               QString::fromUtf8(
                                   QJsonDocument(QJsonArray{b}).toJson(QJsonDocument::Compact)));
    }
    return {};
}

/// Only what moved since the previous snapshot, so the golden file stays readable.
QJsonObject diffObject(const QJsonObject &before, const QJsonObject &after)
{
    QJsonObject out;
    for (auto it = after.constBegin(); it != after.constEnd(); ++it) {
        if (before.value(it.key()) != it.value())
            out.insert(it.key(), it.value());
    }
    for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
        if (!after.contains(it.key()))
            out.insert(it.key(), QStringLiteral("<gone>"));
    }
    return out;
}

QJsonObject rowsByHash(const QJsonArray &rows)
{
    QJsonObject out;
    for (const QJsonValue &row : rows)
        out.insert(row.toObject().value(QStringLiteral("hash")).toString(), row);
    return out;
}

} // namespace

class TestQbtGolden : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void matchesGolden();
};

void TestQbtGolden::matchesGolden()
{
    QStringList steps =
        QDir(kDir).entryList({QStringLiteral("maindata-*.json")}, QDir::Files, QDir::Name);
    QVERIFY(steps.size() >= 10);

    SyncEngine engine;
    TorrentModel *torrents = engine.torrents();
    SignalLog torrentSignals(torrents);
    const QSet<QByteArray> skip = skippedTorrentRoles();

    QJsonArray completed;
    connect(&engine,
            &SyncEngine::torrentCompleted,
            this,
            [&completed](const QString &hash,
                         const QString &name,
                         qint64 size,
                         const QString &category,
                         const QString &contentPath) {
                completed.append(QJsonArray{hash, name, size, category, contentPath});
            });

    const QSet<QByteArray> globalSkip{"lastSyncTime", "lastSyncText"};
    const QSet<QByteArray> countsSkip{"revision"};

    QJsonArray snapshots;
    QJsonObject previousRows;
    QJsonObject previousRest;
    for (const QString &step : std::as_const(steps)) {
        qbtfeed::demo(engine, readJson(step).toObject());
        if (step.contains(QLatin1String("-error.json")))
            qbtfeed::entries(*engine.problems(),
                             readJson(QStringLiteral("log-main.json")).toArray());

        QJsonObject snapshot{{QStringLiteral("step"), step}};
        const QJsonObject rows = rowsByHash(rowsOf(*torrents, skip));
        QJsonObject rowChanges;
        for (auto it = rows.constBegin(); it != rows.constEnd(); ++it) {
            const QJsonObject diff =
                diffObject(previousRows.value(it.key()).toObject(), it.value().toObject());
            if (!diff.isEmpty())
                rowChanges.insert(it.key(), diff);
        }
        for (auto it = previousRows.constBegin(); it != previousRows.constEnd(); ++it) {
            if (!rows.contains(it.key()))
                rowChanges.insert(it.key(), QStringLiteral("<gone>"));
        }
        previousRows = rows;
        snapshot.insert(QStringLiteral("rows"), rowChanges);
        snapshot.insert(QStringLiteral("order"), QJsonArray::fromStringList(torrents->hashes()));
        snapshot.insert(QStringLiteral("signals"), torrentSignals.take());

        FilterCounts *counts = engine.counts();
        QJsonObject rest;
        QJsonObject countsJson = propertiesOf(*counts, countsSkip);
        for (const QString &name : engine.categories()->names())
            countsJson.insert(QStringLiteral("category:") + name, counts->categoryCount(name));
        for (const QString &tag : engine.tags()->tags())
            countsJson.insert(QStringLiteral("tag:") + tag, counts->tagCount(tag));
        for (const QString &host : engine.trackers()->hosts())
            countsJson.insert(QStringLiteral("tracker:") + host, counts->trackerCount(host));
        rest.insert(QStringLiteral("counts"), countsJson);
        rest.insert(QStringLiteral("global"), propertiesOf(*engine.globalState(), globalSkip));
        rest.insert(QStringLiteral("categories"), rowsOf(*engine.categories()));
        rest.insert(QStringLiteral("tags"), rowsOf(*engine.tags()));
        rest.insert(QStringLiteral("trackers"), rowsOf(*engine.trackers()));
        rest.insert(QStringLiteral("completed"), completed);
        completed = {};

        const QList<Torrent> list = StatusDigest::torrentsOf(*torrents);
        qint64 quietAt = 0;
        rest.insert(QStringLiteral("digestAlive"),
                    StatusDigest::alive(list, QDateTime::fromSecsSinceEpoch(kNow), &quietAt));
        rest.insert(QStringLiteral("digestQuietAt"), quietAt);
        rest.insert(QStringLiteral("digestActive"), StatusDigest::active(list));
        rest.insert(QStringLiteral("digestOnOffer"), StatusDigest::onOffer(list));

        snapshot.insert(QStringLiteral("rest"), diffObject(previousRest, rest));
        previousRest = rest;
        snapshots.append(snapshot);
    }

    // The details of the torrent the recording followed.
    const QJsonObject properties = readJson(QStringLiteral("properties.json")).toObject();
    const QString hash = properties.value(QStringLiteral("hash")).toString();
    QVERIFY(!hash.isEmpty());
    TorrentDetails details;
    details.setTorrentModel(torrents);
    const QJsonArray trackers = readJson(QStringLiteral("trackers.json")).toArray();
    qbtfeed::details(
        details,
        QJsonObject{
            {QStringLiteral("hash"), hash},
            {QStringLiteral("properties"), properties},
            {QStringLiteral("files"), readJson(QStringLiteral("files.json"))},
            {QStringLiteral("trackers"), trackers},
            {QStringLiteral("peers"), readJson(QStringLiteral("peers-01-full.json"))},
            {QStringLiteral("webSeeds"), readJson(QStringLiteral("webseeds.json"))},
            {QStringLiteral("pieceStates"), readJson(QStringLiteral("piecestates.json"))},
        });
    qbtfeed::trackers(*details.trackers(), trackers, kNow);

    const QSet<QByteArray> dayRelativeProperties{"createdOn", "lastSeen", "completedOn"};
    const auto detailsSnapshot = [&]() {
        QJsonObject props = QJsonObject::fromVariantMap(details.properties());
        for (const QByteArray &key : dayRelativeProperties)
            props.remove(QString::fromUtf8(key));
        QJsonArray buckets;
        for (const int cell : details.pieceBuckets(64))
            buckets.append(cell);
        return QJsonObject{
            {QStringLiteral("properties"), props},
            {QStringLiteral("hasTrackerError"), details.hasTrackerError()},
            {QStringLiteral("pieceCount"), details.pieceCount()},
            {QStringLiteral("piecesHave"), details.piecesHaveCount()},
            {QStringLiteral("pieceBuckets"), buckets},
            {QStringLiteral("files"), rowsOf(*details.files())},
            {QStringLiteral("trackers"), rowsOf(*details.trackers())},
            {QStringLiteral("peers"), rowsOf(*details.peers())},
            {QStringLiteral("peersRid"), details.peers()->rid()},
            {QStringLiteral("webSeeds"), rowsOf(*details.webSeeds())},
        };
    };
    const QJsonObject detailsFull = detailsSnapshot();

    // The partial peers answer, and files with every priority and the seeding
    // properties, on top.
    qbtfeed::details(
        details,
        QJsonObject{
            {QStringLiteral("hash"), hash},
            {QStringLiteral("properties"), readJson(QStringLiteral("properties-seeding.json"))},
            {QStringLiteral("files"), readJson(QStringLiteral("files-priorities.json"))},
            {QStringLiteral("trackers"), trackers},
            {QStringLiteral("peers"), readJson(QStringLiteral("peers-02-partial.json"))},
            {QStringLiteral("webSeeds"), readJson(QStringLiteral("webseeds.json"))},
            {QStringLiteral("pieceStates"), readJson(QStringLiteral("piecestates.json"))},
        });
    qbtfeed::trackers(*details.trackers(), trackers, kNow);
    const QJsonObject detailsAfter = detailsSnapshot();

    // The log face.
    ServerLog log;
    log.applyEntries(readJson(QStringLiteral("log-main.json")).toArray());
    const QJsonObject logJson{
        {QStringLiteral("rows"), rowsOf(log, {"timeText"})},
        {QStringLiteral("total"), log.total()},
        {QStringLiteral("lastId"), log.lastId()},
    };

    const QJsonObject result{
        {QStringLiteral("steps"), snapshots},
        {QStringLiteral("details"), detailsFull},
        {QStringLiteral("detailsAfter"), diffObject(detailsFull, detailsAfter)},
        {QStringLiteral("log"), logJson},
    };

    const QString goldenPath = kDir + QStringLiteral("golden.json");
    if (qEnvironmentVariableIntValue("OMASEED_WRITE_GOLDEN") == 1) {
        QFile out(goldenPath);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
        QSKIP("golden.json written");
    }
    const QJsonObject golden = readJson(QStringLiteral("golden.json")).toObject();
    const QString difference = firstDifference(golden, result, QStringLiteral("golden"));
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
}

QTEST_GUILESS_MAIN(TestQbtGolden)
#include "tst_qbt_golden.moc"
