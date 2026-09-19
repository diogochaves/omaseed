#include "core/Version.hpp"

#include <QRegularExpression>
#include <QTest>

class TestVersion : public QObject
{
    Q_OBJECT

private slots:
    void versionIsSemver();
    void versionLineNamesTheAppAndQt();
};

void TestVersion::versionIsSemver()
{
    const QString version = omaseed::core::version();
    QVERIFY(!version.isEmpty());
    const QRegularExpression semver(QStringLiteral("^\\d+\\.\\d+\\.\\d+"));
    QVERIFY2(semver.match(version).hasMatch(), qPrintable(version));
}

void TestVersion::versionLineNamesTheAppAndQt()
{
    const QString line = omaseed::core::versionLine();
    QVERIFY(line.startsWith(QStringLiteral("omaseed ")));
    QVERIFY(line.contains(omaseed::core::version()));
    QVERIFY(line.contains(QStringLiteral("Qt ")));
}

QTEST_APPLESS_MAIN(TestVersion)

#include "tst_version.moc"
