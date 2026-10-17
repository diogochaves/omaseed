#include "core/engines/qbt/TorrentsExport.hpp"

#include "core/TorrentFile.hpp"
#include "core/engines/qbt/QbtClient.hpp"
#include "core/engines/qbt/QbtEngine.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUrlQuery>

namespace omaseed::core {

TorrentsExport::TorrentsExport(QbtEngine *engine, QObject *parent)
    : QObject(parent)
    , m_client(engine != nullptr ? engine->client() : nullptr)
{}

TorrentsExport::~TorrentsExport()
{
    if (m_file)
        m_file->cancelWriting();
}

void TorrentsExport::start(const QString &path,
                           const QString &serverName,
                           const QString &serverUrl,
                           const QString &appVersion)
{
    if (m_busy)
        return;
    m_busy = true;
    ++m_generation;
    m_path = path;
    m_manifest = TorrentsManifest();
    m_manifest.created = QDateTime::currentDateTimeUtc();
    m_manifest.appVersion = appVersion;
    m_manifest.serverName = serverName;
    m_manifest.serverUrl = serverUrl;
    m_infos = QJsonArray();
    m_batch.clear();
    m_outcome = Outcome();
    m_outcome.path = path;
    m_step = 0;
    m_next = 0;
    step();
}

void TorrentsExport::cancel()
{
    if (!m_busy)
        return;
    Outcome outcome = m_outcome;
    outcome.cancelled = true;
    outcome.error = QStringLiteral("stopped");
    finish(outcome);
}

void TorrentsExport::fail(const QString &error)
{
    Outcome outcome = m_outcome;
    outcome.error = error;
    finish(outcome);
}

void TorrentsExport::finish(Outcome outcome)
{
    ++m_generation; // answers still on their way are dropped
    m_busy = false;
    m_writer.reset();
    if (m_file) {
        if (outcome.ok) {
            if (!m_file->commit()) {
                outcome.ok = false;
                outcome.error =
                    QStringLiteral("cannot write %1: %2").arg(m_path, m_file->errorString());
            }
        } else {
            m_file->cancelWriting();
        }
        m_file.reset();
    }
    Q_EMIT finished(outcome);
}

void TorrentsExport::step()
{
    if (m_client.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    const auto live = [this, generation] { return m_busy && generation == m_generation; };
    switch (m_step++) {
    case 0:
        m_client->appVersion()->then(this, [this, live](const Result<QString> &r) {
            if (!live())
                return;
            if (r.isError())
                return fail(r.error().toString());
            m_manifest.version = r.value();
            step();
        });
        return;
    case 1:
        m_client->webApiVersion()->then(this, [this, live](const Result<QString> &r) {
            if (!live())
                return;
            if (r.isError())
                return fail(r.error().toString());
            m_manifest.apiVersion = r.value();
            step();
        });
        return;
    case 2:
        m_client->appPreferences()->then(this, [this, live](const Result<QJsonObject> &r) {
            if (!live())
                return;
            if (r.isError())
                return fail(r.error().toString());
            m_manifest.savePath = r.value().value(QStringLiteral("save_path")).toString();
            if (r.value().value(QStringLiteral("temp_path_enabled")).toBool())
                m_manifest.downloadPath = r.value().value(QStringLiteral("temp_path")).toString();
            step();
        });
        return;
    case 3:
        m_client->torrentsCategories()->then(this, [this, live](const Result<QJsonObject> &r) {
            if (!live())
                return;
            if (r.isError())
                return fail(r.error().toString());
            m_manifest.categories = r.value();
            step();
        });
        return;
    case 4:
        m_client->torrentsTags()->then(this, [this, live](const Result<QJsonArray> &r) {
            if (!live())
                return;
            if (r.isError())
                return fail(r.error().toString());
            for (const QJsonValue &tag : r.value())
                m_manifest.tags.append(tag.toString());
            step();
        });
        return;
    case 5:
        m_client->torrentsInfo()->then(this, [this, live](const Result<QJsonArray> &r) {
            if (!live())
                return;
            if (r.isError())
                return fail(r.error().toString());
            m_infos = r.value();
            step();
        });
        return;
    case 6: {
        m_manifest.count = int(m_infos.size());
        for (const QJsonValue &info : std::as_const(m_infos)) {
            if (!info.toObject().value(QStringLiteral("has_metadata")).toBool(true))
                ++m_manifest.magnets;
        }
        QDir().mkpath(QFileInfo(m_path).absolutePath());
        m_file = std::make_unique<QSaveFile>(m_path);
        if (!m_file->open(QIODevice::WriteOnly))
            return fail(QStringLiteral("cannot write %1: %2").arg(m_path, m_file->errorString()));
        // Private before a byte is in it: announce URLs can carry passkeys.
        m_file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        m_writer = std::make_unique<TarWriter>(m_file.get());
        QString error;
        const qint64 now = m_manifest.created.toSecsSinceEpoch();
        if (!m_writer->add(TorrentsBackup::kManifest,
                           QJsonDocument(m_manifest.toJson()).toJson(QJsonDocument::Indented),
                           now,
                           &error))
            return fail(error);
        Q_EMIT progress(0, m_manifest.count);
        nextBatch();
        return;
    }
    default:
        return;
    }
}

void TorrentsExport::nextBatch()
{
    if (m_next >= m_infos.size()) {
        QString error;
        if (!m_writer->finish(&error))
            return fail(error);
        Outcome outcome = m_outcome;
        outcome.ok = true;
        return finish(outcome);
    }
    m_batch.clear();
    const int end = std::min<int>(m_next + kBatch, int(m_infos.size()));
    for (int i = m_next; i < end; ++i)
        m_batch.append(Item{m_infos.at(i).toObject(), {}, {}, {}, false, 0});
    m_next = end;
    m_waiting = 0;

    const int generation = m_generation;
    const auto live = [this, generation] { return m_busy && generation == m_generation; };
    const auto settle = [this, live] {
        if (live() && --m_waiting == 0)
            writeBatch();
    };
    // `done` for an answer that is not the end of the export: a missing torrent (404) marks it
    // gone, anything else but 409 on the export fails the whole file.
    for (int i = 0; i < m_batch.size(); ++i) {
        const QString hash = m_batch.at(i).info.value(QStringLiteral("hash")).toString();
        const bool hasMetadata =
            m_batch.at(i).info.value(QStringLiteral("has_metadata")).toBool(true);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("hash"), hash);
        if (hasMetadata) {
            ++m_waiting;
            m_client->get(QStringLiteral("torrents/export"), query)
                ->then(this, [this, i, live, settle](const Result<QByteArray> &r) {
                    if (!live())
                        return;
                    if (r.isError()) {
                        const ApiError::Kind kind = r.error().kind();
                        if (kind == ApiError::Kind::NotFound)
                            m_batch[i].gone = true;
                        else if (kind != ApiError::Kind::Conflict) // 409: a magnet, kept as a link
                            return fail(r.error().toString());
                    } else {
                        // Its piece layers as libtorrent can load them again (#71).
                        m_batch[i].torrent = TorrentFile::repairPieceLayers(r.value());
                    }
                    settle();
                });
            ++m_waiting;
            m_client->torrentsFiles(hash)->then(
                this, [this, i, live, settle](const Result<QJsonArray> &r) {
                    if (!live())
                        return;
                    if (r.isError()) {
                        if (r.error().kind() != ApiError::Kind::NotFound)
                            return fail(r.error().toString());
                        m_batch[i].gone = true;
                    }
                    m_batch[i].files = r.value();
                    settle();
                });
        }
        ++m_waiting;
        m_client->torrentsTrackers(hash)->then(
            this, [this, i, live, settle](const Result<QJsonArray> &r) {
                if (!live())
                    return;
                if (r.isError()) {
                    if (r.error().kind() != ApiError::Kind::NotFound)
                        return fail(r.error().toString());
                    m_batch[i].gone = true;
                }
                m_batch[i].trackers = r.value();
                settle();
            });
    }
}

void TorrentsExport::writeBatch()
{
    const qint64 now = m_manifest.created.toSecsSinceEpoch();
    QString error;
    for (const Item &item : std::as_const(m_batch)) {
        if (item.gone) {
            ++m_outcome.gone;
            continue;
        }
        const TorrentRecord record =
            TorrentsBackup::recordFrom(item.info, item.files, item.trackers, item.torrent);
        if (!TorrentsBackup::isTorrentId(record.id) ||
            (record.isMagnet() && !record.magnet.startsWith(QLatin1String("magnet:?")))) {
            ++m_outcome.gone; // nothing we could add back
            continue;
        }
        if (!m_writer->add(TorrentsBackup::recordEntry(record.id),
                           QJsonDocument(record.toJson()).toJson(QJsonDocument::Indented),
                           now,
                           &error))
            return fail(error);
        if (!record.isMagnet() &&
            !m_writer->add(TorrentsBackup::torrentEntry(record.id), item.torrent, now, &error))
            return fail(error);
        ++m_outcome.torrents;
        if (record.isMagnet())
            ++m_outcome.magnets;
    }
    m_batch.clear();
    Q_EMIT progress(m_next, int(m_infos.size()));
    nextBatch();
}

} // namespace omaseed::core
