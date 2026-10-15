// #63: which files of a torrent are extras (NFO and friends, samples, adverts, stray images).
// A table: a torrent's files in (path, and a size where it matters), its extras out.

#include "core/TorrentExtras.hpp"

#include <QTest>

using omaseed::core::TorrentFileEntry;
namespace TorrentExtras = omaseed::core::TorrentExtras;

namespace {

constexpr qint64 kMiB = 1024 * 1024;

/// "path" or "path=bytes" (1 MiB when no size is given).
QList<TorrentFileEntry> filesOf(const QStringList &specs)
{
    QList<TorrentFileEntry> files;
    for (const QString &spec : specs) {
        const qsizetype equals = spec.lastIndexOf(QLatin1Char('='));
        if (equals > 0)
            files.append({spec.left(equals), spec.mid(equals + 1).toLongLong()});
        else
            files.append({spec, kMiB});
    }
    return files;
}

QString kindName(TorrentExtras::Kind kind)
{
    switch (kind) {
    case TorrentExtras::Kind::Info:
        return QStringLiteral("info");
    case TorrentExtras::Kind::Sample:
        return QStringLiteral("sample");
    case TorrentExtras::Kind::Advert:
        return QStringLiteral("advert");
    case TorrentExtras::Kind::Image:
        return QStringLiteral("image");
    }
    return {};
}

} // namespace

class TestTorrentExtras : public QObject
{
    Q_OBJECT

private slots:
    void find_data();
    void find();
};

void TestTorrentExtras::find_data()
{
    QTest::addColumn<QStringList>("files");
    /// Each extra as "path kind", in file-id order.
    QTest::addColumn<QStringList>("extras");

    const QString movie = QStringLiteral("Movie.2026.1080p.mkv=4000000000");

    QTest::newRow("a film alone") << QStringList{movie, QStringLiteral("Subs/en.srt")}
                                  << QStringList();
    QTest::newRow("NFO, checksums and text at any depth")
        << QStringList{movie,
                       QStringLiteral("movie.nfo"),
                       QStringLiteral("Proof/release.SFV"),
                       QStringLiteral("deep/er/hashes.md5"),
                       QStringLiteral("README.txt"),
                       QStringLiteral("scene.srr")}
        << QStringList{QStringLiteral("movie.nfo info"),
                       QStringLiteral("Proof/release.SFV info"),
                       QStringLiteral("deep/er/hashes.md5 info"),
                       QStringLiteral("README.txt info"),
                       QStringLiteral("scene.srr info")};
    QTest::newRow("samples: a folder, a name, a -sample. file")
        << QStringList{movie,
                       QStringLiteral("Sample/movie.mkv"),
                       QStringLiteral("samples/clip.mkv"),
                       QStringLiteral("sample.mkv"),
                       QStringLiteral("movie-SAMPLE.mkv"),
                       QStringLiteral("Extras/making-of.mkv")}
        << QStringList{QStringLiteral("Sample/movie.mkv sample"),
                       QStringLiteral("samples/clip.mkv sample"),
                       QStringLiteral("sample.mkv sample"),
                       QStringLiteral("movie-SAMPLE.mkv sample")};
    QTest::newRow("not samples: a word that only contains it")
        << QStringList{movie,
                       QStringLiteral("Sampler/track.flac"),
                       QStringLiteral("examples/a.mkv"),
                       QStringLiteral("sampled.mkv"),
                       QStringLiteral("movie.sample.mkv")}
        << QStringList();
    QTest::newRow("adverts at the top level only")
        << QStringList{movie,
                       QStringLiteral("Visit Our Site.url"),
                       QStringLiteral("site.website"),
                       QStringLiteral("index.HTML"),
                       QStringLiteral("shortcut.lnk"),
                       QStringLiteral("setup.exe"),
                       QStringLiteral("Docs/manual.html"),
                       QStringLiteral("Tools/setup.exe")}
        << QStringList{QStringLiteral("Visit Our Site.url advert"),
                       QStringLiteral("site.website advert"),
                       QStringLiteral("index.HTML advert"),
                       QStringLiteral("shortcut.lnk advert"),
                       QStringLiteral("setup.exe advert")};
    QTest::newRow("a cover and a screenshot beside a film")
        << QStringList{movie,
                       QStringLiteral("cover.jpg=200000"),
                       QStringLiteral("Screens/s1.PNG=300000")}
        << QStringList{QStringLiteral("cover.jpg image"), QStringLiteral("Screens/s1.PNG image")};
    QTest::newRow("an image set: five images") << QStringList{movie,
                                                              QStringLiteral("a.jpg=1000"),
                                                              QStringLiteral("b.jpg=1000"),
                                                              QStringLiteral("c.jpg=1000"),
                                                              QStringLiteral("d.png=1000"),
                                                              QStringLiteral("e.png=1000")}
                                               << QStringList();
    QTest::newRow("an image set: the images are the size")
        << QStringList{QStringLiteral("album.flac=100000000"),
                       QStringLiteral("booklet.jpg=2000000")}
        << QStringList();
    QTest::newRow("one file of each, counted once")
        << QStringList{QStringLiteral("Album/01 Track.flac=30000000"),
                       QStringLiteral("Album/02 Track.flac=30000000"),
                       QStringLiteral("Album/folder.jpg=100000"),
                       QStringLiteral("Album/album.nfo=2000"),
                       QStringLiteral("Album/Sample/notes.txt=10"),
                       QStringLiteral("Tracker.url=100")}
        << QStringList{QStringLiteral("Album/folder.jpg image"),
                       QStringLiteral("Album/album.nfo info"),
                       QStringLiteral("Album/Sample/notes.txt info"),
                       QStringLiteral("Tracker.url advert")};
    QTest::newRow("a single file is the torrent")
        << QStringList{QStringLiteral("notes.txt")} << QStringList();
    QTest::newRow("only text files: their own content")
        << QStringList{QStringLiteral("Books/a.txt"), QStringLiteral("Books/b.txt")}
        << QStringList();
    QTest::newRow("the Public Domain Sampler (docs/29 F2)")
        << QStringList{QStringLiteral("README.txt=812"),
                       QStringLiteral("sampler.mkv=25165824"),
                       QStringLiteral("sampler.srt=43008")}
        << QStringList{QStringLiteral("README.txt info")};
    QTest::newRow("no files") << QStringList() << QStringList();
}

void TestTorrentExtras::find()
{
    QFETCH(QStringList, files);
    QFETCH(QStringList, extras);

    const QList<TorrentFileEntry> entries = filesOf(files);
    QStringList found;
    for (const TorrentExtras::Extra &extra : TorrentExtras::find(entries))
        found.append(entries.at(extra.index).path + QLatin1Char(' ') + kindName(extra.kind));
    QCOMPARE(found, extras);
}

QTEST_GUILESS_MAIN(TestTorrentExtras)
#include "tst_torrentextras.moc"
