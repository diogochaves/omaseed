#include "integration/AppIdentity.hpp"

#include <QTest>

namespace Identity = omaseed::integration::AppIdentity;

class TestAppIdentity : public QObject
{
    Q_OBJECT

private slots:
    void applicationIdIsTheReverseDnsName();
    void desktopFileAndBusNameFollowTheApplicationId();
    void objectPathIsTheApplicationIdAsAPath();
};

void TestAppIdentity::applicationIdIsTheReverseDnsName()
{
    QCOMPARE(Identity::applicationId(), QStringLiteral("io.github.diogochaves.omaseed"));
    QCOMPARE(Identity::applicationName(), QStringLiteral("omaseed"));
    QCOMPARE(Identity::displayName(), QStringLiteral("Omaseed"));
    QCOMPARE(Identity::organizationDomain(), QStringLiteral("io.github.diogochaves"));
}

void TestAppIdentity::desktopFileAndBusNameFollowTheApplicationId()
{
    QCOMPARE(Identity::desktopFileName(), Identity::applicationId());
    QCOMPARE(Identity::dbusServiceName(), Identity::applicationId());
}

void TestAppIdentity::objectPathIsTheApplicationIdAsAPath()
{
    QCOMPARE(Identity::dbusObjectPath(), QStringLiteral("/io/github/diogochaves/omaseed"));
}

QTEST_APPLESS_MAIN(TestAppIdentity)

#include "tst_appidentity.moc"
