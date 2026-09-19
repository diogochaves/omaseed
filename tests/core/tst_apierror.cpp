#include "core/ApiError.hpp"

#include <QTest>

using omaseed::core::ApiError;

class TestApiError : public QObject
{
    Q_OBJECT

private slots:
    void defaultIsNotAnError();
    void mapsHttpStatus_data();
    void mapsHttpStatus();
    void transientErrorsAreWorthRetrying();
    void describesItselfInOneLine();
    void comparesByValue();
};

void TestApiError::defaultIsNotAnError()
{
    const ApiError error;
    QCOMPARE(error.kind(), ApiError::Kind::None);
    QVERIFY(!error.isError());
    QVERIFY(!error.isTransient());
    QCOMPARE(error.httpStatus(), 0);
    QVERIFY(error.message().isEmpty());
}

void TestApiError::mapsHttpStatus_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<ApiError::Kind>("kind");

    QTest::newRow("no request") << 0 << ApiError::Kind::None;
    QTest::newRow("ok") << 200 << ApiError::Kind::None;
    QTest::newRow("no content") << 204 << ApiError::Kind::None;
    QTest::newRow("unauthorized") << 401 << ApiError::Kind::Unauthorized;
    QTest::newRow("forbidden") << 403 << ApiError::Kind::Forbidden;
    QTest::newRow("not found") << 404 << ApiError::Kind::NotFound;
    QTest::newRow("conflict") << 409 << ApiError::Kind::Conflict;
    QTest::newRow("teapot") << 418 << ApiError::Kind::Unknown;
    QTest::newRow("server") << 500 << ApiError::Kind::Server;
    QTest::newRow("gateway") << 502 << ApiError::Kind::Server;
}

void TestApiError::mapsHttpStatus()
{
    QFETCH(int, status);
    QFETCH(ApiError::Kind, kind);

    const ApiError error = ApiError::fromHttpStatus(status);
    QCOMPARE(error.kind(), kind);
    QCOMPARE(error.isError(), kind != ApiError::Kind::None);
    QCOMPARE(error.httpStatus(), error.isError() ? status : 0);
}

void TestApiError::transientErrorsAreWorthRetrying()
{
    QVERIFY(ApiError(ApiError::Kind::Network, QStringLiteral("refused")).isTransient());
    QVERIFY(ApiError(ApiError::Kind::Timeout, QString()).isTransient());
    QVERIFY(ApiError::fromHttpStatus(503).isTransient());
    QVERIFY(!ApiError::fromHttpStatus(401).isTransient());
    QVERIFY(!ApiError(ApiError::Kind::Parse, QString()).isTransient());
}

void TestApiError::describesItselfInOneLine()
{
    const ApiError error = ApiError::fromHttpStatus(401, QStringLiteral("bad credential"));
    QCOMPARE(error.toString(), QStringLiteral("unauthorized (401): bad credential"));
    QCOMPARE(ApiError().toString(), QStringLiteral("none"));
    QCOMPARE(ApiError(ApiError::Kind::Timeout, QStringLiteral("5s")).toString(),
             QStringLiteral("timeout: 5s"));
}

void TestApiError::comparesByValue()
{
    QCOMPARE(ApiError::fromHttpStatus(404, QStringLiteral("gone")),
             ApiError(ApiError::Kind::NotFound, QStringLiteral("gone"), 404));
    QVERIFY(ApiError::fromHttpStatus(404) != ApiError::fromHttpStatus(409));
}

QTEST_APPLESS_MAIN(TestApiError)

#include "tst_apierror.moc"
