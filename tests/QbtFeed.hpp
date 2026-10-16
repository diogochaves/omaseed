#pragma once

// QbtFeed — the tests' shortcut for feeding qBittorrent-shaped JSON into the neutral models:
// every call goes through QbtTranslate, the way a real reply does (docs/36 §4, #81). The
// tests keep writing what qBittorrent sends; the models never see it.

#include "core/CategoryModel.hpp"
#include "core/GlobalState.hpp"
#include "core/ProblemLog.hpp"
#include "core/SyncEngine.hpp"
#include "core/TagModel.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentPeersModel.hpp"
#include "core/TorrentTrackersModel.hpp"
#include "core/TorrentWebSeedsModel.hpp"
#include "core/TrackerModel.hpp"
#include "core/engines/qbt/QbtTranslate.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace qbtfeed {

namespace core = omaseed::core;

inline QStringList strings(const QJsonArray &array)
{
    QStringList list;
    for (const QJsonValue &value : array)
        list.append(value.toString());
    return list;
}

/// A `torrents` map of sync/maindata.
inline void full(core::TorrentModel &model, const QJsonObject &torrents)
{
    model.applyFullUpdate(core::QbtTranslate::torrents(torrents));
}
inline void
partial(core::TorrentModel &model, const QJsonObject &torrents, const QJsonArray &removed = {})
{
    model.applyPartial(core::QbtTranslate::torrents(torrents), strings(removed));
}

/// A `categories` map.
inline void full(core::CategoryModel &model, const QJsonObject &categories)
{
    model.applyFullUpdate(core::QbtTranslate::categories(categories));
}
inline void
partial(core::CategoryModel &model, const QJsonObject &categories, const QJsonArray &removed = {})
{
    model.applyPartial(core::QbtTranslate::categories(categories), strings(removed));
}

/// `tags` and `tags_removed`.
inline void full(core::TagModel &model, const QJsonArray &tags)
{
    model.applyFullUpdate(strings(tags));
}
inline void partial(core::TagModel &model, const QJsonArray &added, const QJsonArray &removed = {})
{
    model.applyPartial(strings(added), strings(removed));
}

/// A `trackers` map.
inline void full(core::TrackerModel &model, const QJsonObject &trackers)
{
    model.applyFullUpdate(core::QbtTranslate::trackerMap(trackers));
}
inline void
partial(core::TrackerModel &model, const QJsonObject &trackers, const QJsonArray &removed = {})
{
    model.applyPartial(core::QbtTranslate::trackerMap(trackers), strings(removed));
}

/// A `server_state` object.
inline void serverState(core::GlobalState &state, const QJsonObject &json)
{
    state.applyServerState(core::QbtTranslate::serverState(json));
}

/// A whole sync/maindata answer.
inline void demo(core::SyncEngine &engine, const QJsonObject &maindata)
{
    engine.applyDemoData(core::QbtTranslate::syncDelta(maindata));
}

/// torrents/files.
inline void files(core::TorrentFilesModel &model, const QJsonArray &files)
{
    model.applyFiles(core::QbtTranslate::files(files));
}

/// torrents/trackers.
inline void trackers(core::TorrentTrackersModel &model, const QJsonArray &trackers)
{
    model.applyTrackers(core::QbtTranslate::trackers(trackers));
}
inline void trackers(core::TorrentTrackersModel &model, const QJsonArray &trackers, qint64 nowSecs)
{
    model.applyTrackers(core::QbtTranslate::trackers(trackers, nowSecs), nowSecs);
}

/// sync/torrentPeers.
inline void peers(core::TorrentPeersModel &model, const QJsonObject &answer)
{
    model.applyPeers(core::QbtTranslate::peers(answer));
}

/// torrents/webseeds.
inline void webSeeds(core::TorrentWebSeedsModel &model, const QJsonArray &webSeeds)
{
    model.applyWebSeeds(core::QbtTranslate::webSeeds(webSeeds));
}

/// log/main.
inline void entries(core::ProblemLog &log, const QJsonArray &entries)
{
    log.applyEntries(core::QbtTranslate::log(entries));
}

/// `{hash, properties, files, trackers, peers, pieceStates, webSeeds}`.
inline void details(core::TorrentDetails &details, const QJsonObject &json)
{
    details.applyDemo(core::QbtTranslate::details(json));
}

} // namespace qbtfeed
