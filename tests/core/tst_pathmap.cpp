#include "core/PathMap.hpp"

#include <QJsonArray>
#include <QTest>

using omaseed::core::PathMap;

class TestPathMap : public QObject
{
    Q_OBJECT

private slots:
    void unmappedPathIsUnchanged();
    void rewritesExactPrefixMatch();
    void respectsComponentBoundary();
    void picksTheLongestMatchingPrefix();
    void toRemoteIsTheInverseOfToLocal();
    void normalisesSeparatorsAndTrailingSlashes();
    void jsonRoundTrips();
    void rootPrefixKeepsTheSeparator();
    void coversOnlyMappedPaths();
    void coversLocalOnlyMappedLocalPaths();
};

void TestPathMap::unmappedPathIsUnchanged()
{
    PathMap map;
    map.addMapping(QStringLiteral("/data"), QStringLiteral("/mnt/data"));
    QCOMPARE(map.toLocal(QStringLiteral("/other/path")), QStringLiteral("/other/path"));
}

void TestPathMap::rewritesExactPrefixMatch()
{
    PathMap map;
    map.addMapping(QStringLiteral("/data"), QStringLiteral("/mnt/data"));
    QCOMPARE(map.toLocal(QStringLiteral("/data")), QStringLiteral("/mnt/data"));
    QCOMPARE(map.toLocal(QStringLiteral("/data/movies/film.mkv")),
             QStringLiteral("/mnt/data/movies/film.mkv"));
}

void TestPathMap::respectsComponentBoundary()
{
    PathMap map;
    map.addMapping(QStringLiteral("/data"), QStringLiteral("/mnt/data"));
    // "/database" must NOT be treated as under "/data".
    QCOMPARE(map.toLocal(QStringLiteral("/database/file")), QStringLiteral("/database/file"));
}

void TestPathMap::picksTheLongestMatchingPrefix()
{
    PathMap map;
    map.addMapping(QStringLiteral("/data"), QStringLiteral("/mnt/data"));
    map.addMapping(QStringLiteral("/data/movies"), QStringLiteral("/mnt/films"));

    QCOMPARE(map.toLocal(QStringLiteral("/data/movies/film.mkv")),
             QStringLiteral("/mnt/films/film.mkv"));
    QCOMPARE(map.toLocal(QStringLiteral("/data/music/song.flac")),
             QStringLiteral("/mnt/data/music/song.flac"));
}

void TestPathMap::toRemoteIsTheInverseOfToLocal()
{
    PathMap map;
    map.addMapping(QStringLiteral("/data"), QStringLiteral("/mnt/data"));

    const QString remote = QStringLiteral("/data/movies/film.mkv");
    const QString local = map.toLocal(remote);
    QCOMPARE(map.toRemote(local), remote);
}

void TestPathMap::normalisesSeparatorsAndTrailingSlashes()
{
    PathMap map;
    map.addMapping(QStringLiteral("/data/"), QStringLiteral("C:\\downloads\\"));
    QCOMPARE(map.mappings().first().remote, QStringLiteral("/data"));
    QCOMPARE(map.mappings().first().local, QStringLiteral("C:/downloads"));

    QCOMPARE(map.toLocal(QStringLiteral("/data/file.txt")),
             QStringLiteral("C:/downloads/file.txt"));
}

void TestPathMap::jsonRoundTrips()
{
    PathMap map;
    map.addMapping(QStringLiteral("/data"), QStringLiteral("/mnt/data"));
    map.addMapping(QStringLiteral("/downloads"), QStringLiteral("/mnt/downloads"));

    const QJsonArray json = map.toJson();
    QCOMPARE(json.size(), 2);

    const PathMap restored = PathMap::fromJson(json);
    QCOMPARE(restored.mappings(), map.mappings());
}

void TestPathMap::rootPrefixKeepsTheSeparator()
{
    PathMap fromRoot;
    fromRoot.addMapping(QStringLiteral("/"), QStringLiteral("/mnt/nas"));
    QCOMPARE(fromRoot.toLocal(QStringLiteral("/data/file")), QStringLiteral("/mnt/nas/data/file"));
    QCOMPARE(fromRoot.toLocal(QStringLiteral("/")), QStringLiteral("/mnt/nas"));
    QCOMPARE(fromRoot.toRemote(QStringLiteral("/mnt/nas/data/file")), QStringLiteral("/data/file"));
    QCOMPARE(fromRoot.toRemote(QStringLiteral("/mnt/nas")), QStringLiteral("/"));

    PathMap toRoot;
    toRoot.addMapping(QStringLiteral("/downloads"), QStringLiteral("/"));
    QCOMPARE(toRoot.toLocal(QStringLiteral("/downloads/a")), QStringLiteral("/a"));
    QCOMPARE(toRoot.toRemote(QStringLiteral("/home/x")), QStringLiteral("/downloads/home/x"));
}

void TestPathMap::coversOnlyMappedPaths()
{
    PathMap map;
    QVERIFY(!map.covers(QStringLiteral("/data/films")));
    map.addMapping(QStringLiteral("/data"), QStringLiteral("/mnt/data"));
    QVERIFY(map.covers(QStringLiteral("/data")));
    QVERIFY(map.covers(QStringLiteral("/data/films/")));
    QVERIFY(!map.covers(QStringLiteral("/database")));
    QVERIFY(!map.covers(QStringLiteral("/other")));
}

void TestPathMap::coversLocalOnlyMappedLocalPaths()
{
    PathMap map;
    QVERIFY(!map.coversLocal(QStringLiteral("/mnt/data/films")));
    map.addMapping(QStringLiteral("/data"), QStringLiteral("/mnt/data"));
    QVERIFY(map.coversLocal(QStringLiteral("/mnt/data")));
    QVERIFY(map.coversLocal(QStringLiteral("/mnt/data/films/")));
    QVERIFY(!map.coversLocal(QStringLiteral("/mnt/database")));
    // The remote side is not the local one.
    QVERIFY(!map.coversLocal(QStringLiteral("/data/films")));
}

QTEST_APPLESS_MAIN(TestPathMap)

#include "tst_pathmap.moc"
