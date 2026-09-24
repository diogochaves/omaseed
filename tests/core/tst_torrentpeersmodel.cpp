// TorrentPeersModel: the rid diff protocol of sync/torrentPeers (full update, partial merge,
// peers_removed) and the abbreviated speeds of spec §2 Fig. 4. The fixtures are hand-built
// from the WebUI API docs, because the :8081 playground never has a peer: nothing it holds
// is seeded by anyone.

#include "core/TorrentPeersModel.hpp"

#include <QAbstractItemModelTester>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using omaseed::core::TorrentPeersModel;

namespace {

QJsonObject fixture(const QString &name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/details/") + name);
    [&]() { QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName())); }();
    return QJsonDocument::fromJson(file.readAll()).object();
}

QVariant at(const TorrentPeersModel &model, int row, TorrentPeersModel::Role role)
{
    return model.data(model.index(row), role);
}

} // namespace

class TestTorrentPeersModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void the_figure_row_by_row();
    void a_partial_update_merges_and_removes();
    void clearing_forgets_the_rid();
    void an_ip_is_recovered_from_the_key();
    void a_poll_moves_rows_and_never_resets();
};

void TestTorrentPeersModel::the_figure_row_by_row()
{
    TorrentPeersModel model;
    QCOMPARE(model.rid(), 0);

    model.applyPeers(fixture(QStringLiteral("peers-full.json")));
    QCOMPARE(model.count(), 4);
    QCOMPARE(model.rid(), 1);

    // Fastest first, which is the order spec §2 Fig. 4 draws.
    const QStringList ips{QStringLiteral("185.21.216.140"),
                          QStringLiteral("2a01:4f8:c0c::1"),
                          QStringLiteral("91.121.58.7"),
                          QStringLiteral("77.54.113.202")};
    const QStringList codes{
        QStringLiteral("DE"), QStringLiteral("FI"), QStringLiteral("FR"), QStringLiteral("PT")};
    const QStringList clients{QStringLiteral("qBittorrent 5.2.3"),
                              QStringLiteral("Transmission 4.1.1"),
                              QStringLiteral("libtorrent 2.0.14"),
                              QStringLiteral("Deluge 2.2.0")};
    const QStringList down{QStringLiteral("1.02 MB/s"),
                           QStringLiteral("744 KB/s"),
                           QStringLiteral("402 KB/s"),
                           QStringLiteral("118 KB/s")};
    const QStringList up{QStringLiteral("0"),
                         QStringLiteral("12 KB/s"),
                         QStringLiteral("0"),
                         QStringLiteral("98 KB/s")};
    const QStringList done{QStringLiteral("100 %"),
                           QStringLiteral("100 %"),
                           QStringLiteral("88.4 %"),
                           QStringLiteral("64.0 %")};

    for (int row = 0; row < model.count(); ++row) {
        QCOMPARE(at(model, row, TorrentPeersModel::IpRole).toString(), ips.at(row));
        QCOMPARE(at(model, row, TorrentPeersModel::CountryCodeRole).toString(), codes.at(row));
        QCOMPARE(at(model, row, TorrentPeersModel::ClientRole).toString(), clients.at(row));
        QCOMPARE(at(model, row, TorrentPeersModel::DlSpeedTextRole).toString(), down.at(row));
        QCOMPARE(at(model, row, TorrentPeersModel::UpSpeedTextRole).toString(), up.at(row));
        QCOMPARE(at(model, row, TorrentPeersModel::ProgressTextRole).toString(), done.at(row));
    }

    // The key is what the ban and copy actions carry.
    QCOMPARE(model.addressAt(0), QStringLiteral("185.21.216.140:51413"));
    QCOMPARE(at(model, 0, TorrentPeersModel::PortRole).toInt(), 51413);
    QCOMPARE(at(model, 3, TorrentPeersModel::CountryRole).toString(), QStringLiteral("Portugal"));
    QCOMPARE(model.addressAt(99), QString());
}

void TestTorrentPeersModel::a_partial_update_merges_and_removes()
{
    TorrentPeersModel model;
    model.applyPeers(fixture(QStringLiteral("peers-full.json")));
    QSignalSpy counted(&model, &TorrentPeersModel::countChanged);

    model.applyPeers(fixture(QStringLiteral("peers-diff.json")));

    QCOMPARE(model.rid(), 2);
    QCOMPARE(model.count(), 4); // one gone, one new
    QCOMPARE(counted.count(), 0);

    // The Finnish peer was in peers_removed.
    for (int row = 0; row < model.count(); ++row)
        QVERIFY(at(model, row, TorrentPeersModel::IpRole).toString() !=
                QStringLiteral("2a01:4f8:c0c::1"));

    // 91.121.58.7 only sent dl_speed/up_speed/progress: its client and country survive.
    int french = -1;
    for (int row = 0; row < model.count(); ++row) {
        if (at(model, row, TorrentPeersModel::IpRole).toString() == QStringLiteral("91.121.58.7"))
            french = row;
    }
    QVERIFY(french >= 0);
    QCOMPARE(at(model, french, TorrentPeersModel::ClientRole).toString(),
             QStringLiteral("libtorrent 2.0.14"));
    QCOMPARE(at(model, french, TorrentPeersModel::CountryCodeRole).toString(),
             QStringLiteral("FR"));
    QCOMPARE(at(model, french, TorrentPeersModel::ProgressTextRole).toString(),
             QStringLiteral("100 %"));
    QCOMPARE(at(model, french, TorrentPeersModel::UpSpeedTextRole).toString(),
             QStringLiteral("4 KB/s"));
    QCOMPARE(at(model, french, TorrentPeersModel::DownloadedRole).toLongLong(), 71303168LL);

    // The newcomer is there in full, and lands where its speed puts it (2 K, third of four).
    QCOMPARE(at(model, 2, TorrentPeersModel::IpRole).toString(), QStringLiteral("203.0.113.9"));
    QCOMPARE(at(model, 2, TorrentPeersModel::CountryCodeRole).toString(), QStringLiteral("JP"));
    QCOMPARE(french, 3); // it stopped downloading, so it sorts last
}

void TestTorrentPeersModel::clearing_forgets_the_rid()
{
    TorrentPeersModel model;
    model.applyPeers(fixture(QStringLiteral("peers-full.json")));
    QCOMPARE(model.rid(), 1);

    model.clear();
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.rid(), 0); // the next poll asks for a full update

    // A full update after a clear starts from nothing, not from the old four.
    model.applyPeers(fixture(QStringLiteral("peers-full.json")));
    QCOMPARE(model.count(), 4);
}

void TestTorrentPeersModel::an_ip_is_recovered_from_the_key()
{
    // A partial update for a peer the model has never seen carries no `ip`; the key does.
    TorrentPeersModel model;
    QJsonObject answer;
    QJsonObject peers;
    QJsonObject peer;
    peer.insert(QStringLiteral("dl_speed"), 2048);
    peers.insert(QStringLiteral("198.51.100.4:6881"), peer);
    answer.insert(QStringLiteral("peers"), peers);
    answer.insert(QStringLiteral("rid"), 7);
    model.applyPeers(answer);

    QCOMPARE(model.count(), 1);
    QCOMPARE(at(model, 0, TorrentPeersModel::IpRole).toString(), QStringLiteral("198.51.100.4"));
    QCOMPARE(at(model, 0, TorrentPeersModel::DlSpeedTextRole).toString(), QStringLiteral("2 KB/s"));
}

void TestTorrentPeersModel::a_poll_moves_rows_and_never_resets()
{
    TorrentPeersModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.applyPeers(fixture(QStringLiteral("peers-full.json")));

    // The cursor sits on the peer in the last row; the diff re-sorts, drops one and adds one.
    const QString held = model.addressAt(model.count() - 1);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
    QSignalSpy applied(&model, &TorrentPeersModel::applied);

    model.applyPeers(fixture(QStringLiteral("peers-diff.json")));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(applied.count(), 1);

    // The held peer is still found by address, wherever its speed took it.
    const int row = model.rowOfAddress(held);
    QVERIFY(row >= 0);
    QCOMPARE(model.addressAt(row), held);
    QCOMPARE(model.rowOfAddress(QStringLiteral("2a01:4f8:c0c::1:6881")), -1);
    QCOMPARE(model.rowOfAddress(QString()), -1);

    // Speeds swap between two polls: the two peers trade rows by a move, same count.
    const QString first = model.addressAt(0);
    const QString second = model.addressAt(1);
    QJsonObject swap{{QStringLiteral("rid"), 3},
                     {QStringLiteral("peers"),
                      QJsonObject{{first, QJsonObject{{QStringLiteral("dl_speed"), 1}}},
                                  {second, QJsonObject{{QStringLiteral("dl_speed"), 99999999}}}}}};
    moved.clear();
    model.applyPeers(swap);
    QCOMPARE(reset.count(), 0);
    QVERIFY(moved.count() > 0);
    QCOMPARE(model.addressAt(0), second);
    QCOMPARE(model.count(), 4);

    // And the rows really are in speed order afterwards.
    for (int r = 1; r < model.count(); ++r)
        QVERIFY(model.data(model.index(r - 1), TorrentPeersModel::DlSpeedRole).toLongLong() >=
                model.data(model.index(r), TorrentPeersModel::DlSpeedRole).toLongLong());
}

QTEST_MAIN(TestTorrentPeersModel)
#include "tst_torrentpeersmodel.moc"
