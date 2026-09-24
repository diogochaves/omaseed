// T4.4 — the category/tag palettes' create actions against the real :8081 playground.
// `App::createCategory()`/`createTag()` are thin wrappers over `QbtClient::torrentsCreate*()`
// (App itself is exercised in demo mode by tst_app_tablestate.cpp, which never reaches a
// server); this file checks the underlying calls actually create and can be cleaned up.
//
// Nothing here touches the real library server: the URL comes from QBT_TEST_URL and falls
// back to the playground's fixed address. The whole case skips when :8081 is down.

#include "core/QbtClient.hpp"

#include "LiveServer.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

using namespace omaseed::core;

namespace {

const char *kCategory = "omaseed-test-cat";
const char *kTag = "omaseed-test-tag";

QUrl playgroundUrl()
{
    return LiveServer::playground();
}

bool answers(const QUrl &url)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), quint16(url.port(80)));
    return socket.waitForConnected(2000);
}

template<class T>
QbtResult<T> await(QObject *context, QbtReply<T> *reply, int timeoutMs = 20000)
{
    QbtResult<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const QbtResult<T> &value) {
        result = value;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return result;
}

} // namespace

class TestAppPalettesLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();

    void createCategoryMakesItVisible();
    void createTagMakesItVisible();

private:
    QUrl m_url;
};

void TestAppPalettesLive::initTestCase()
{
    m_url = playgroundUrl();
    if (!answers(m_url))
        QSKIP("the :8081 playground is not running (scripts/dev/test-server.sh start)");
}

void TestAppPalettesLive::cleanup()
{
    if (m_url.isEmpty() || !answers(m_url))
        return;
    QbtClient client;
    client.setBaseUrl(m_url);
    await(this, client.torrentsRemoveCategories({QString::fromLatin1(kCategory)}));
    await(this, client.torrentsDeleteTags({QString::fromLatin1(kTag)}));
}

void TestAppPalettesLive::createCategoryMakesItVisible()
{
    QbtClient client;
    client.setBaseUrl(m_url);

    const QbtResult<QByteArray> created =
        await(this, client.torrentsCreateCategory(QString::fromLatin1(kCategory)));
    QVERIFY2(!created.isError(), qPrintable(created.error().toString()));

    const QbtResult<QJsonObject> categories = await(this, client.torrentsCategories());
    QVERIFY2(!categories.isError(), qPrintable(categories.error().toString()));
    QVERIFY(categories.value().contains(QString::fromLatin1(kCategory)));
}

void TestAppPalettesLive::createTagMakesItVisible()
{
    QbtClient client;
    client.setBaseUrl(m_url);

    const QbtResult<QByteArray> created =
        await(this, client.torrentsCreateTags({QString::fromLatin1(kTag)}));
    QVERIFY2(!created.isError(), qPrintable(created.error().toString()));

    const QbtResult<QJsonArray> tags = await(this, client.torrentsTags());
    QVERIFY2(!tags.isError(), qPrintable(tags.error().toString()));
    bool found = false;
    for (const QJsonValue &value : tags.value()) {
        if (value.toString() == QString::fromLatin1(kTag))
            found = true;
    }
    QVERIFY(found);
}

QTEST_MAIN(TestAppPalettesLive)
#include "tst_app_palettes_live.moc"
