// TorrentsBackup's file, TorrentsImport's plan, path rows, journal and rename guard
// (docs/24 §2, §5, §7; #33), without a server; the add against a qBittorrent of its own that
// refuses what libtorrent can't load (#71). The runs against real qBittorrents are in
// tst_torrentsbackup_live.cpp.

#include "core/Bencode.hpp"
#include "core/PathMap.hpp"
#include "core/QbtClient.hpp"
#include "core/TarArchive.hpp"
#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/TorrentsImport.hpp"

#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QUrlQuery>

#include <functional>
#include <memory>

namespace bencode = omaseed::core::bencode;
using omaseed::core::PathMap;
using omaseed::core::QbtClient;
using omaseed::core::TarArchive;
using omaseed::core::TarReader;
using omaseed::core::TarWriter;
using omaseed::core::TorrentFile;
using omaseed::core::TorrentRecord;
using omaseed::core::TorrentsBackup;
using omaseed::core::TorrentsImport;
using omaseed::core::TorrentsManifest;

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString hashOf(char c)
{
    return QString(40, QLatin1Char(c));
}

TorrentRecord record(const QString &id,
                     const QString &savePath,
                     qint64 size = 100,
                     const QString &category = QString(),
                     const QStringList &tags = {})
{
    TorrentRecord r;
    r.id = id;
    r.infohashV1 = id;
    r.name = QStringLiteral("t-") + id.left(4);
    r.size = size;
    r.savePath = savePath;
    r.category = category;
    r.tags = tags;
    r.state = QStringLiteral("started");
    return r;
}

TorrentsManifest manifest()
{
    TorrentsManifest m;
    m.created = QDateTime(QDate(2026, 10, 3), QTime(21, 4), QTimeZone::UTC);
    m.appVersion = QStringLiteral("0.1.0");
    m.serverName = QStringLiteral("home");
    m.serverUrl = QStringLiteral("http://192.168.1.20:8080");
    m.version = QStringLiteral("v5.1.2");
    m.apiVersion = QStringLiteral("2.11.4");
    m.savePath = QStringLiteral("/data/torrents");
    m.categories = QJsonObject{
        {QStringLiteral("films"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("films")},
                     {QStringLiteral("savePath"), QStringLiteral("/data/torrents/films")}}}};
    m.tags = {QStringLiteral("keep"), QStringLiteral("hd")};
    return m;
}

/// An export file at `path` holding `records` (a .torrent `torrent` for each that has one).
bool writeExport(const QString &path,
                 const TorrentsManifest &m,
                 const QList<TorrentRecord> &records,
                 const QByteArray &torrent)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    TarWriter writer(&file);
    bool ok = writer.add(TorrentsBackup::kManifest, QJsonDocument(m.toJson()).toJson(), 0);
    for (const TorrentRecord &r : records) {
        ok = ok &&
             writer.add(TorrentsBackup::recordEntry(r.id), QJsonDocument(r.toJson()).toJson(), 0);
        if (!r.isMagnet())
            ok = ok && writer.add(TorrentsBackup::torrentEntry(r.id), torrent, 0);
    }
    return ok && writer.finish();
}

// ---- a qBittorrent of our own, for the add (#71) ----------------------------------------

/// A v2 .torrent named `name` whose two 48 KiB files have the same content (one "pieces root",
/// one layer of three hashes at 16 KiB pieces); `layer` is what "piece layers" holds for it.
QByteArray duplicateFilesTorrent(const QByteArray &name, const QByteArray &layer)
{
    const QByteArray root(32, 'R');
    const auto leaf = [&root] {
        return bencode::Value(bencode::Dict{
            {QByteArray(),
             bencode::Value(bencode::Dict{{"length", bencode::Value(qint64(3 * 16384))},
                                          {"pieces root", bencode::Value(root)}})}});
    };
    const bencode::Value info(bencode::Dict{
        {"file tree", bencode::Value(bencode::Dict{{"a.bin", leaf()}, {"b.bin", leaf()}})},
        {"meta version", bencode::Value(qint64(2))},
        {"name", bencode::Value(name)},
        {"piece length", bencode::Value(qint64(16384))}});
    return bencode::encode(bencode::Value(bencode::Dict{
        {"info", info},
        {"piece layers", bencode::Value(bencode::Dict{{root, bencode::Value(layer)}})}}));
}

QByteArray threeHashes()
{
    return QByteArray(32, '1') + QByteArray(32, '2') + QByteArray(32, '3');
}

/// What libtorrent checks before it loads a v2 .torrent, and qBittorrent's torrents/add with
/// it (415 otherwise): each piece layer is ceil(length / piece length) hashes of its files.
bool loadsLikeLibtorrent(const QByteArray &bytes)
{
    if (!TorrentFile::parse(bytes).isValid())
        return false;
    const bencode::Value root = bencode::decode(bytes);
    const qint64 pieceLength = root.value("info").value("piece length").toInteger();
    const bencode::Value layers = root.value("piece layers");
    std::function<bool(const bencode::Value &)> walk = [&](const bencode::Value &node) {
        for (const auto &[name, child] : node.toDict()) {
            const bencode::Value leaf = child.value(QByteArray());
            if (!leaf.isDict()) {
                if (!walk(child))
                    return false;
                continue;
            }
            const qint64 length = leaf.value("length").toInteger();
            if (length <= pieceLength)
                continue;
            const QByteArray layer = layers.value(leaf.value("pieces root").toString()).toString();
            if (layer.size() != (length + pieceLength - 1) / pieceLength * 32)
                return false;
        }
        return true;
    };
    return walk(root.value("info").value("file tree"));
}

QByteArray httpResponse(int code, const QByteArray &reason, const QByteArray &body)
{
    return "HTTP/1.1 " + QByteArray::number(code) + ' ' + reason +
           "\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Length: " +
           QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

/// The files of a multipart/form-data body, in order: (filename, bytes).
QList<QPair<QByteArray, QByteArray>> uploads(const QByteArray &contentType, const QByteArray &body)
{
    QList<QPair<QByteArray, QByteArray>> out;
    const qsizetype at = contentType.indexOf("boundary=");
    if (at < 0)
        return out;
    QByteArray boundary = contentType.mid(at + 9).trimmed();
    if (boundary.startsWith('"'))
        boundary = boundary.mid(1, boundary.size() - 2);
    const QByteArray mark = "--" + boundary;
    qsizetype from = body.indexOf(mark);
    while (from >= 0) {
        const qsizetype start = from + mark.size() + 2; // past "\r\n"
        const qsizetype end = body.indexOf("\r\n" + mark, start);
        if (end < 0)
            break;
        const QByteArray part = body.mid(start, end - start);
        const qsizetype split = part.indexOf("\r\n\r\n");
        const QByteArray headers = part.left(split);
        const qsizetype name = headers.indexOf("filename=\"");
        if (split > 0 && name >= 0) {
            const qsizetype close = headers.indexOf('"', name + 10);
            out.append({headers.mid(name + 10, close - name - 10), part.mid(split + 4)});
        }
        from = end + 2;
    }
    return out;
}

/// Speaks just enough of qBittorrent's API for an import's add: torrents/add loads each file
/// as libtorrent would, adds the good ones until it comes to one it can't load, and answers
/// that with 415 (as 5.2.3 does); a torrent already there is 409. Everything else is "".
class FakeQbt : public QTcpServer
{
public:
    FakeQbt()
    {
        [this] { QVERIFY(listen(QHostAddress::LocalHost)); }();
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

    QHash<QString, QByteArray> added; ///< id -> the bytes it was added from
    int adds = 0;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
            QByteArray contentType;
            qsizetype length = 0;
            for (const QByteArray &line : lines) {
                const QByteArray lower = line.trimmed().toLower();
                if (lower.startsWith("content-length:"))
                    length = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
                else if (lower.startsWith("content-type:"))
                    contentType = line.mid(line.indexOf(':') + 1).trimmed();
            }
            if (buffer->size() - (headerEnd + 4) < length)
                return;
            const QByteArray target = lines.constFirst().split(' ').value(1);
            const QByteArray body = buffer->mid(headerEnd + 4, length);
            buffer->clear();
            socket->write(answer(target, contentType, body));
            socket->flush();
            socket->disconnectFromHost();
        });
    }

private:
    QByteArray
    answer(const QByteArray &target, const QByteArray &contentType, const QByteArray &body)
    {
        const QUrl url(QString::fromLatin1(target));
        const QString path = url.path();
        if (path.endsWith(u"/app/webapiVersion"))
            return httpResponse(200, "OK", "2.15.1");
        if (path.endsWith(u"/app/preferences"))
            return httpResponse(200, "OK", R"({"save_path":"/d"})");
        if (path.endsWith(u"/torrents/categories"))
            return httpResponse(200, "OK", "{}");
        if (path.endsWith(u"/torrents/info")) {
            const QStringList asked = QUrlQuery(url)
                                          .queryItemValue(QStringLiteral("hashes"))
                                          .split(QLatin1Char('|'), Qt::SkipEmptyParts);
            QJsonArray listed;
            for (const QString &id : asked) {
                if (added.contains(id))
                    listed.append(QJsonObject{{QStringLiteral("hash"), id}});
            }
            return httpResponse(200, "OK", QJsonDocument(listed).toJson(QJsonDocument::Compact));
        }
        if (path.endsWith(u"/torrents/add")) {
            ++adds;
            bool any = false;
            for (const auto &[name, bytes] : uploads(contentType, body)) {
                if (!loadsLikeLibtorrent(bytes))
                    return httpResponse(415,
                                        "Unsupported Media Type",
                                        "Error: '" + name + "' is not a valid torrent file.");
                const QString id = TorrentFile::parse(bytes).torrentId();
                if (added.contains(id))
                    continue;
                added.insert(id, bytes);
                any = true;
            }
            return any ? httpResponse(200, "OK", "Ok.") : httpResponse(409, "Conflict", "Conflict");
        }
        return httpResponse(200, "OK", "");
    }
};

} // namespace

class TstTorrentsBackup : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ---- the tar stream ----------------------------------------------------------------

    // A few thousand entries written one by one and read back by header, each one's data
    // fetched only when asked: what a large library's export and import do.
    void tarStreamsManyEntries()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        TarWriter writer(&file);
        for (int i = 0; i < 3000; ++i) {
            const QByteArray data = QByteArray::number(i).repeated(i % 7 + 1);
            QVERIFY(writer.add(QStringLiteral("torrents/%1.json").arg(i), data, 1759500000));
        }
        QVERIFY(writer.finish());
        file.flush();

        QVERIFY(file.seek(0));
        TarReader reader(&file);
        QList<TarReader::Header> headers;
        while (const auto h = reader.next())
            headers.append(*h);
        QVERIFY2(reader.atEnd(), qPrintable(reader.error()));
        QCOMPARE(headers.size(), 3000);
        QCOMPARE(headers.at(1234).name, QStringLiteral("torrents/1234.json"));
        QCOMPARE(reader.read(headers.at(1234)), QByteArray("1234").repeated(1234 % 7 + 1));
        QCOMPARE(reader.read(headers.at(0)), QByteArray("0"));

        // The in-memory reader takes what the stream wrote, and the other way round.
        QVERIFY(file.seek(0));
        const auto all = TarArchive::read(file.readAll());
        QVERIFY(all.has_value());
        QCOMPARE(all->size(), 3000);
        const QByteArray memory = TarArchive::write({{QStringLiteral("a/b.json"), "{}", 1}});
        QBuffer buffer;
        buffer.setData(memory);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        TarReader again(&buffer);
        const auto one = again.next();
        QVERIFY(one.has_value());
        QCOMPARE(again.read(*one), QByteArray("{}"));
        QVERIFY(!again.next());
        QVERIFY(again.atEnd());
    }

    void tarReaderRefusesACutFile()
    {
        QByteArray bytes;
        QBuffer out(&bytes);
        QVERIFY(out.open(QIODevice::WriteOnly));
        TarWriter writer(&out);
        QVERIFY(writer.add(QStringLiteral("x.json"), QByteArray(700, 'x'), 0));
        out.close(); // no end marker
        QBuffer in;
        in.setData(bytes);
        QVERIFY(in.open(QIODevice::ReadOnly));
        TarReader reader(&in);
        QVERIFY(reader.next().has_value());
        QVERIFY(!reader.next().has_value());
        QVERIFY(!reader.atEnd());
        QVERIFY(reader.error().contains(QStringLiteral("ends before")));
        QVERIFY(!writer.add(QStringLiteral("../x"), "x", 0));
    }

    // ---- the manifest and the records --------------------------------------------------

    void manifestRoundTrip()
    {
        const TorrentsManifest m = manifest();
        const auto back = TorrentsManifest::fromJson(m.toJson());
        QVERIFY(back.has_value());
        QCOMPARE(back->created, m.created);
        QCOMPARE(back->serverUrl, m.serverUrl);
        QCOMPARE(back->savePath, m.savePath);
        QCOMPARE(back->categories, m.categories);
        QCOMPARE(back->tags, m.tags);

        QString error;
        QJsonObject other = m.toJson();
        other.insert(QStringLiteral("format"), QStringLiteral("omaseed-backup"));
        QVERIFY(!TorrentsManifest::fromJson(other, &error));
        QVERIFY(error.contains(QStringLiteral("not an omaseed torrents export")));
        QJsonObject newer = m.toJson();
        newer.insert(QStringLiteral("version"), 9);
        QVERIFY(!TorrentsManifest::fromJson(newer, &error));
        QVERIFY(error.contains(QStringLiteral("newer")));
    }

    // docs/24 §2: everything the record holds comes back from its JSON.
    void recordRoundTrip()
    {
        TorrentRecord r = record(hashOf('a'),
                                 QStringLiteral("/data/torrents"),
                                 5000,
                                 QStringLiteral("films"),
                                 {QStringLiteral("hd")});
        r.infohashV2 = QString(64, QLatin1Char('b'));
        r.downloadPath = QStringLiteral("/data/incomplete");
        r.progress = 0.5;
        r.state = QStringLiteral("forced");
        r.queuePosition = 3;
        r.upLimit = 1024;
        r.dlLimit = 2048;
        r.ratioLimit = 1.5;
        r.seedingTimeLimit = 60;
        r.inactiveSeedingTimeLimit = 30;
        r.shareLimitAction = QStringLiteral("Stop");
        r.sequential = true;
        r.firstLastPiecePrio = true;
        r.layout = QStringLiteral("NoSubfolder");
        r.renamed = {{2, QStringLiteral("a/b.mkv"), QStringLiteral("a/B.mkv")}};
        r.priorities = {1, 0, 7};
        r.trackers = {QStringLiteral("udp://t.example:1337/announce")};
        const auto back = TorrentRecord::fromJson(r.toJson());
        QVERIFY(back.has_value());
        QCOMPARE(back->toJson(), r.toJson());
        QCOMPARE(back->ids(), (QStringList{hashOf('a'), QString(64, QLatin1Char('b'))}));
    }

    void recordRefusesDamage_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("id") << "id" << QJsonValue(QStringLiteral("../../etc"));
        QTest::newRow("magnet") << "magnet" << QJsonValue(QStringLiteral("http://x"));
        QTest::newRow("rename climbs out")
            << "renamed"
            << QJsonValue(QJsonArray{QJsonObject{{QStringLiteral("index"), 0},
                                                 {QStringLiteral("from"), QStringLiteral("a")},
                                                 {QStringLiteral("to"), QStringLiteral("../a")}}});
        QTest::newRow("rename absolute")
            << "renamed"
            << QJsonValue(
                   QJsonArray{QJsonObject{{QStringLiteral("index"), 0},
                                          {QStringLiteral("from"), QStringLiteral("a")},
                                          {QStringLiteral("to"), QStringLiteral("/etc/a")}}});
    }

    void recordRefusesDamage()
    {
        QFETCH(QString, key);
        QFETCH(QJsonValue, value);
        QJsonObject o = record(hashOf('a'), QStringLiteral("/d")).toJson();
        o.insert(key, value);
        QVERIFY(!TorrentRecord::fromJson(o).has_value());
    }

    // The record built from what the server says: state, limits, trackers without DHT/PeX/LSD,
    // priorities only when one differs, renames against the .torrent's names.
    void recordFromTheServer()
    {
        const QByteArray bytes = fixture(QStringLiteral("multi_file_v1_unicode.torrent"));
        const TorrentFile parsed = TorrentFile::parse(bytes);
        QVERIFY(parsed.isValid() && parsed.multiFile && parsed.files.size() >= 2);
        QJsonArray files;
        const QStringList names = TorrentsBackup::namesUnder(parsed, QStringLiteral("Original"));
        for (int i = 0; i < names.size(); ++i) {
            QString name = names.at(i);
            if (i == 1)
                name = parsed.name + QStringLiteral("/renamed.txt");
            files.append(QJsonObject{{QStringLiteral("name"), name},
                                     {QStringLiteral("priority"), i == 0 ? 0 : 1}});
        }
        const QJsonObject info{{QStringLiteral("hash"), parsed.infoHashV1},
                               {QStringLiteral("infohash_v1"), parsed.infoHashV1},
                               {QStringLiteral("name"), parsed.name},
                               {QStringLiteral("size"), 4000},
                               {QStringLiteral("progress"), 1.0},
                               {QStringLiteral("save_path"), QStringLiteral("/data/torrents")},
                               {QStringLiteral("download_path"), QString()},
                               {QStringLiteral("category"), QStringLiteral("films")},
                               {QStringLiteral("tags"), QStringLiteral("hd, keep")},
                               {QStringLiteral("auto_tmm"), true},
                               {QStringLiteral("state"), QStringLiteral("stoppedUP")},
                               {QStringLiteral("priority"), 0},
                               {QStringLiteral("up_limit"), 1000},
                               {QStringLiteral("dl_limit"), -1},
                               {QStringLiteral("ratio_limit"), 2.0},
                               {QStringLiteral("seeding_time_limit"), -2},
                               {QStringLiteral("inactive_seeding_time_limit"), -2},
                               {QStringLiteral("share_limit_action"), QStringLiteral("Default")},
                               {QStringLiteral("has_metadata"), true}};
        const QJsonArray trackers{
            QJsonObject{{QStringLiteral("url"), QStringLiteral("** [DHT] **")}},
            QJsonObject{{QStringLiteral("url"), QStringLiteral("http://t.example/announce")}}};
        const TorrentRecord r = TorrentsBackup::recordFrom(info, files, trackers, bytes);
        QCOMPARE(r.id, parsed.infoHashV1);
        QCOMPARE(r.state, QStringLiteral("stopped"));
        QCOMPARE(r.tags, (QStringList{QStringLiteral("hd"), QStringLiteral("keep")}));
        QCOMPARE(r.dlLimit, 0); // -1, no limit
        QCOMPARE(r.upLimit, 1000);
        QCOMPARE(r.ratioLimit, 2.0);
        QCOMPARE(r.trackers, QStringList{QStringLiteral("http://t.example/announce")});
        QCOMPARE(r.layout, QStringLiteral("Original"));
        QCOMPARE(r.renamed.size(), 1);
        QCOMPARE(r.renamed.constFirst().index, 1);
        QCOMPARE(r.renamed.constFirst().from, names.at(1));
        QCOMPARE(r.renamed.constFirst().to, parsed.name + QStringLiteral("/renamed.txt"));
        QCOMPARE(r.priorities.size(), names.size());
        QCOMPARE(r.priorities.constFirst(), 0);
        QVERIFY(!r.isMagnet());

        // A torrent without metadata: its link, nothing else to rename.
        QJsonObject magnet = info;
        magnet.insert(QStringLiteral("magnet_uri"),
                      QStringLiteral("magnet:?xt=urn:btih:") + parsed.infoHashV1);
        magnet.insert(QStringLiteral("state"), QStringLiteral("metaDL"));
        magnet.insert(QStringLiteral("force_start"), true);
        const TorrentRecord link = TorrentsBackup::recordFrom(magnet, {}, {}, {});
        QVERIFY(link.isMagnet());
        QCOMPARE(link.state, QStringLiteral("forced"));
    }

    // A whole torrent moved out of its folder (NoSubfolder), or a single file put in one
    // (Subfolder), is one layout, not a rename per file.
    void layoutsLeaveFewestRenames()
    {
        const TorrentFile multi =
            TorrentFile::parse(fixture(QStringLiteral("multi_file_v1_unicode.torrent")));
        QStringList flat;
        for (const auto &f : multi.files)
            flat.append(f.path);
        auto [layout, renames] = TorrentsBackup::renamesOf(multi, flat);
        QCOMPARE(layout, QStringLiteral("NoSubfolder"));
        QVERIFY(renames.isEmpty());

        const TorrentFile single =
            TorrentFile::parse(fixture(QStringLiteral("single_file_v1.torrent")));
        std::tie(layout, renames) =
            TorrentsBackup::renamesOf(single, {QStringLiteral("Single/Single.bin")});
        QCOMPARE(layout, QStringLiteral("Subfolder"));
        QVERIFY(renames.isEmpty());
        std::tie(layout, renames) =
            TorrentsBackup::renamesOf(single, {QStringLiteral("other.bin")});
        QCOMPARE(layout, QStringLiteral("Original"));
        QCOMPARE(renames.size(), 1);
    }

    // The file read back for import: manifest, records, the .torrent fetched on demand; a
    // file that is not one, or is damaged, refused with the reason.
    void readsAnExport()
    {
        QTemporaryDir dir;
        const QByteArray torrent = fixture(QStringLiteral("single_file_v1.torrent"));
        TorrentRecord magnet = record(hashOf('c'), QStringLiteral("/data/torrents"));
        magnet.magnet = QStringLiteral("magnet:?xt=urn:btih:") + magnet.id;
        const QList<TorrentRecord> records{record(hashOf('a'), QStringLiteral("/data/torrents")),
                                           record(hashOf('b'), QStringLiteral("/data/other")),
                                           magnet};
        const QString path = dir.filePath(QStringLiteral("t.omaseed-torrents"));
        QVERIFY(writeExport(path, manifest(), records, torrent));
        QString error;
        const auto contents = TorrentsBackup::read(path, &error);
        QVERIFY2(contents.has_value(), qPrintable(error));
        QCOMPARE(contents->records.size(), 3);
        QCOMPARE(contents->manifest.serverName, QStringLiteral("home"));
        QCOMPARE(contents->torrents.size(), 2);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(TorrentsBackup::torrentBytes(&file, *contents, hashOf('b')), torrent);
        QVERIFY(TorrentsBackup::torrentBytes(&file, *contents, hashOf('c')).isEmpty());

        // Not ours: a settings backup's tar, plain text, a cut file.
        const QString other = dir.filePath(QStringLiteral("other.tar"));
        QFile o(other);
        QVERIFY(o.open(QIODevice::WriteOnly));
        o.write(TarArchive::write(
            {{QStringLiteral("manifest.json"), "{\"format\":\"omaseed-backup\"}", 0}}));
        o.close();
        QVERIFY(!TorrentsBackup::read(other, &error));
        QVERIFY2(error.contains(QStringLiteral("not an omaseed torrents export")),
                 qPrintable(error));
        QFile text(other);
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write(QByteArray(2000, 'x'));
        text.close();
        QVERIFY(!TorrentsBackup::read(other, &error));
        QVERIFY2(error.contains(QStringLiteral("not an omaseed torrents export")),
                 qPrintable(error));
        QFile whole(path);
        QVERIFY(whole.open(QIODevice::ReadOnly));
        const QByteArray bytes = whole.readAll();
        QFile cut(other);
        QVERIFY(cut.open(QIODevice::WriteOnly));
        cut.write(bytes.left(bytes.size() - 1500));
        cut.close();
        QVERIFY(!TorrentsBackup::read(other, &error));
        QVERIFY2(error.contains(QStringLiteral("damaged")), qPrintable(error));
    }

    // ---- path rows (D7 a) ------------------------------------------------------------------

    void mapsPaths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("mapped");
        QTest::newRow("under") << "/data/torrents/films" << "/mnt/media/torrents/films";
        QTest::newRow("itself") << "/data/torrents" << "/mnt/media/torrents";
        QTest::newRow("trailing slash") << "/data/torrents/" << "/mnt/media/torrents";
        QTest::newRow("longest wins") << "/data/torrents/music/x" << "/srv/music/x";
        QTest::newRow("boundary") << "/data/torrentsX" << "/data/torrentsX";
        QTest::newRow("uncovered") << "/home/d/Torrents" << "/home/d/Torrents";
    }

    void mapsPaths()
    {
        QFETCH(QString, path);
        QFETCH(QString, mapped);
        const QList<TorrentsImport::PathRow> rows{
            {QStringLiteral("/data/torrents/"), QStringLiteral("/mnt/media/torrents"), {}},
            {QStringLiteral("/data/torrents/music"), QStringLiteral("/srv/music"), {}},
            {QStringLiteral("/home/d"), QString(), {}}, // half a row: off
        };
        QCOMPARE(TorrentsImport::mapPath(path, rows), mapped);
        QCOMPARE(TorrentsImport::covers(rows, path), mapped != path);
    }

    // Rows start from the path maps omaseed has: the exporting server's (remote → where the
    // target sees it) and the target's read backwards; only rows that change some path.
    void prefillsFromPathMaps()
    {
        TorrentsBackup::Contents contents;
        contents.records = {record(hashOf('a'), QStringLiteral("/data/torrents/films")),
                            record(hashOf('b'), QStringLiteral("/home/d/Torrents"))};
        PathMap source;
        source.addMapping(QStringLiteral("/data/torrents"), QStringLiteral("/mnt/media/torrents"));
        source.addMapping(QStringLiteral("/unused"), QStringLiteral("/mnt/unused"));
        // A target with no map is this machine: it sees /mnt/media/torrents as it is.
        auto rows = TorrentsImport::prefill(
            contents, source, QStringLiteral("home"), PathMap(), QStringLiteral("desk"));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.constFirst().from, QStringLiteral("/data/torrents"));
        QCOMPARE(rows.constFirst().to, QStringLiteral("/mnt/media/torrents"));
        QCOMPARE(rows.constFirst().source, QStringLiteral("home"));

        // A target in a container: this machine's /home/d is its /downloads.
        PathMap target;
        target.addMapping(QStringLiteral("/downloads"), QStringLiteral("/home/d"));
        rows = TorrentsImport::prefill(
            contents, std::nullopt, QString(), target, QStringLiteral("box"));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.constFirst().from, QStringLiteral("/home/d"));
        QCOMPARE(rows.constFirst().to, QStringLiteral("/downloads"));

        // The same server again: nothing to change.
        rows = TorrentsImport::prefill(
            contents, target, QStringLiteral("box"), target, QStringLiteral("box"));
        QCOMPARE(rows.size(), 1); // only the backwards row for /home/d
        QCOMPARE(rows.constFirst().from, QStringLiteral("/home/d"));
    }

    // ---- the plan (D8 a, §7) -----------------------------------------------------------

    void plansNewPresentMagnets()
    {
        TorrentsBackup::Contents contents;
        contents.manifest = manifest();
        TorrentRecord byV2 = record(hashOf('d'), QStringLiteral("/data/torrents"), 7);
        byV2.infohashV2 = QString(64, QLatin1Char('e'));
        TorrentRecord link =
            record(hashOf('f'), QStringLiteral("/data/torrents"), 0, QStringLiteral("films"));
        link.magnet = QStringLiteral("magnet:?xt=urn:btih:") + link.id;
        contents.records = {
            record(hashOf('a'),
                   QStringLiteral("/data/torrents"),
                   100,
                   QStringLiteral("films"),
                   {QStringLiteral("hd"), QStringLiteral("new")}),
            record(
                hashOf('b'), QStringLiteral("/data/torrents/films"), 200, QStringLiteral("music")),
            record(hashOf('c'), QStringLiteral("/elsewhere"), 300), // already there
            byV2,                                                   // there, by its v2 hash
            link,
        };
        TorrentsImport::Here here;
        here.ids = {hashOf('c'), QString(64, QLatin1Char('e'))};
        here.categories = {QStringLiteral("films")};
        here.tags = {QStringLiteral("hd")};
        const TorrentsImport::Plan plan = TorrentsImport::plan(contents, here);
        QCOMPARE(plan.add, (QList<int>{0, 1}));
        QCOMPARE(plan.magnets, QList<int>{4});
        QCOMPARE(plan.skipped, (QList<int>{2, 3}));
        QVERIFY(plan.finish.isEmpty());
        QCOMPARE(plan.addBytes, 300);
        QCOMPARE(plan.skippedBytes, 307);
        QCOMPARE(plan.categories, QStringList{QStringLiteral("music")});
        QCOMPARE(plan.tags, QStringList{QStringLiteral("new")});
        QCOMPARE(plan.count(), 3);

        const QList<TorrentsImport::PathRow> rows{
            {QStringLiteral("/data/torrents"), QStringLiteral("/mnt/t"), {}}};
        QCOMPARE(TorrentsImport::uncovered(contents, plan, rows), 0);
        QCOMPARE(TorrentsImport::uncovered(contents, plan, {}), 3);
    }

    // §7: a run that stopped halfway left torrents in the journal; the same file again
    // finishes those instead of skipping them, and adds the rest. Another file's journal is
    // not this one's.
    void resumesFromTheJournal()
    {
        QTemporaryDir dir;
        TorrentsBackup::Contents contents;
        contents.manifest = manifest();
        contents.records = {record(hashOf('a'), QStringLiteral("/d")),
                            record(hashOf('b'), QStringLiteral("/d")),
                            record(hashOf('c'), QStringLiteral("/d"))};
        const QString path = dir.filePath(QStringLiteral("imports/server.json"));
        TorrentsImport::Journal journal{TorrentsImport::Journal::keyFor(contents.manifest),
                                        {{hashOf('a'), QStringLiteral("check")}}};
        QVERIFY(journal.save(path));
        QVERIFY(QFile::exists(path));
        QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadOther | QFileDevice::ReadGroup),
                 QFileDevice::Permissions());
        const TorrentsImport::Journal loaded = TorrentsImport::Journal::load(path);
        QCOMPARE(loaded.key, journal.key);
        QCOMPARE(loaded.stages, journal.stages);

        TorrentsImport::Here here;
        here.ids = {hashOf('a'), hashOf('b')};
        TorrentsImport::Plan plan = TorrentsImport::plan(contents, here, loaded);
        QCOMPARE(plan.finish, QList<int>{0});
        QCOMPARE(plan.skipped, QList<int>{1});
        QCOMPARE(plan.add, QList<int>{2});

        TorrentsManifest other = contents.manifest;
        other.created = other.created.addSecs(60);
        TorrentsBackup::Contents second = contents;
        second.manifest = other;
        plan = TorrentsImport::plan(second, here, loaded);
        QVERIFY(plan.finish.isEmpty());
        QCOMPARE(plan.skipped, (QList<int>{0, 1}));

        // Emptied, the journal goes.
        const TorrentsImport::Journal empty{journal.key, {}};
        QVERIFY(empty.save(path));
        QVERIFY(!QFile::exists(path));
        QVERIFY(TorrentsImport::Journal::load(path).stages.isEmpty());
    }

    // ---- the add (#71) -----------------------------------------------------------------

    // A file exported from qBittorrent 5.2.3 holds a .torrent whose files share content: its
    // piece layer repeated per file, which qBittorrent refuses (415) and with it the whole
    // batch. The import puts the layer right before it sends it; a .torrent qBittorrent still
    // can't read is named, and the rest of its batch goes in on its own.
    void addsWhatTheServerCanRead()
    {
        QTemporaryDir dir;
        const QByteArray v1 = fixture(QStringLiteral("single_file_v1.torrent"));
        const QByteArray exported = duplicateFilesTorrent("dups", threeHashes().repeated(2));
        const QByteArray unreadable = duplicateFilesTorrent("short", threeHashes().left(64));
        QVERIFY(!loadsLikeLibtorrent(exported));
        QVERIFY(!loadsLikeLibtorrent(unreadable));

        const auto recordOf = [](const QByteArray &bytes, const QString &name) {
            const TorrentFile parsed = TorrentFile::parse(bytes);
            TorrentRecord r = record(parsed.torrentId(), QStringLiteral("/d"));
            r.infohashV1 = parsed.infoHashV2.isEmpty() ? parsed.infoHashV1 : QString();
            r.infohashV2 = parsed.infoHashV2;
            r.name = name;
            return r;
        };
        const QList<QPair<TorrentRecord, QByteArray>> torrents{
            {recordOf(v1, QStringLiteral("plain v1")), v1},
            {recordOf(exported, QStringLiteral("dups")), exported},
            {recordOf(unreadable, QStringLiteral("short")), unreadable}};
        const QString path = dir.filePath(QStringLiteral("t.omaseed-torrents"));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            TarWriter writer(&file);
            QVERIFY(writer.add(
                TorrentsBackup::kManifest, QJsonDocument(manifest().toJson()).toJson(), 0));
            for (const auto &[r, bytes] : torrents) {
                QVERIFY(writer.add(
                    TorrentsBackup::recordEntry(r.id), QJsonDocument(r.toJson()).toJson(), 0));
                QVERIFY(writer.add(TorrentsBackup::torrentEntry(r.id), bytes, 0));
            }
            QVERIFY(writer.finish());
        }
        QString error;
        const auto contents = TorrentsBackup::read(path, &error);
        QVERIFY2(contents.has_value(), qPrintable(error));
        const TorrentsImport::Plan plan = TorrentsImport::plan(*contents, {});
        QCOMPARE(plan.add, (QList<int>{0, 1, 2}));

        FakeQbt server;
        QbtClient client;
        client.setBaseUrl(server.url());
        TorrentsImport import(&client);
        TorrentsImport::Options options;
        options.check = false;
        options.journalPath = dir.filePath(QStringLiteral("imports/fake.json"));
        options.pollMs = 50;
        QSignalSpy finished(&import, &TorrentsImport::finished);
        import.start(*contents, plan, options);
        QVERIFY(finished.wait(20000));
        const auto outcome = finished.constFirst().constFirst().value<TorrentsImport::Outcome>();

        QVERIFY2(outcome.complete, qPrintable(outcome.errors.join(u"; ")));
        QCOMPARE(outcome.added, 2);
        QCOMPARE(outcome.errors,
                 QStringList{QStringLiteral("qBittorrent couldn't read the .torrent of short")});
        QCOMPARE(server.added.size(), 2);
        QVERIFY(server.added.contains(torrents.at(0).first.id));
        // The layer put right, the hashes untouched.
        const QByteArray sent = server.added.value(torrents.at(1).first.id);
        QVERIFY(loadsLikeLibtorrent(sent));
        QCOMPARE(sent, TorrentFile::repairPieceLayers(exported));
        // One batch, then each of its three on its own.
        QCOMPARE(server.adds, 4);
        // Nothing left for a next run: the refused one was never added.
        QVERIFY(!QFile::exists(options.journalPath));
    }

    // ---- the rename guard (#64, #67) ---------------------------------------------------

    void renameNeverReplacesAFileOnDisk()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("Album")));
        const auto touch = [&dir](const QString &name) {
            QFile f(dir.filePath(name));
            return f.open(QIODevice::WriteOnly) && f.write("x") == 1;
        };
        QVERIFY(touch(QStringLiteral("Album/real.flac")));
        // Only the new name is there: the rename only tells qBittorrent where its file is.
        QVERIFY(!TorrentsImport::renameWouldReplace(
            dir.path(), QStringLiteral("Album/old.flac"), QStringLiteral("Album/real.flac")));
        // Only the old one: a plain rename, nothing replaced.
        QVERIFY(!TorrentsImport::renameWouldReplace(
            dir.path(), QStringLiteral("Album/real.flac"), QStringLiteral("Album/new.flac")));
        // Both: libtorrent would move the stray file over the real one (upstream#85).
        QVERIFY(touch(QStringLiteral("Album/old.flac")));
        QVERIFY(TorrentsImport::renameWouldReplace(
            dir.path(), QStringLiteral("Album/old.flac"), QStringLiteral("Album/real.flac")));
        // A disk this machine can't see: nothing is known, nothing said (#64).
        QVERIFY(!TorrentsImport::renameWouldReplace(
            QString(), QStringLiteral("Album/old.flac"), QStringLiteral("Album/real.flac")));
    }
};

QTEST_GUILESS_MAIN(TstTorrentsBackup)
#include "tst_torrentsbackup.moc"
