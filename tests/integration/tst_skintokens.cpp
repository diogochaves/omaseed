#include "integration/SkinReader.hpp"

#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

#include <optional>

using omaseed::integration::Skin;
using omaseed::integration::SkinReader;

namespace {

/// One `n("name", px)` or `token("group", "key", fallback)` call in the QML.
struct Call
{
    QString path;     ///< `density.name`, `group.key`
    QString fallback; ///< the fallback as written
    QString where;    ///< file:line, for a failure
};

/// Every such call in src/ui/qml (OMASEED_SOURCE_QML_DIR).
const QList<Call> &calls()
{
    static const QList<Call> found = [] {
        static const QRegularExpression density(
            QStringLiteral("(?<![A-Za-z0-9_])n\\(\"([A-Za-z0-9_]+)\",\\s*([^)]*)\\)"));
        static const QRegularExpression token(
            QStringLiteral("\\btoken\\(\"([a-z]+)\",\\s*\"([A-Za-z]+)\",\\s*([^)]*)\\)"));
        QList<Call> list;
        QDirIterator files(QStringLiteral(OMASEED_SOURCE_QML_DIR),
                           {QStringLiteral("*.qml")},
                           QDir::Files,
                           QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString path = files.next();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            int line = 0;
            while (!file.atEnd()) {
                ++line;
                const QString text = QString::fromUtf8(file.readLine());
                const QString where = path + QLatin1Char(':') + QString::number(line);
                for (auto m = density.globalMatch(text); m.hasNext();) {
                    const auto match = m.next();
                    list << Call{QStringLiteral("density.") + match.captured(1),
                                 match.captured(2).trimmed(),
                                 where};
                }
                for (auto m = token.globalMatch(text); m.hasNext();) {
                    const auto match = m.next();
                    list << Call{match.captured(1) + QLatin1Char('.') + match.captured(2),
                                 match.captured(3).trimmed(),
                                 where};
                }
            }
        }
        return list;
    }();
    return found;
}

/// A fallback as a value, when it is a literal: a number, a string, true or false, or one
/// of Qt's font weights. Anything else is an expression.
std::optional<QVariant> literal(const QString &fallback)
{
    static const QHash<QString, int> weights{
        {QStringLiteral("Font.Thin"), 100},
        {QStringLiteral("Font.ExtraLight"), 200},
        {QStringLiteral("Font.Light"), 300},
        {QStringLiteral("Font.Normal"), 400},
        {QStringLiteral("Font.Medium"), 500},
        {QStringLiteral("Font.DemiBold"), 600},
        {QStringLiteral("Font.Bold"), 700},
        {QStringLiteral("Font.ExtraBold"), 800},
        {QStringLiteral("Font.Black"), 900},
    };
    static const QRegularExpression number(QStringLiteral("^-?[0-9]+(\\.[0-9]+)?$"));
    static const QRegularExpression string(QStringLiteral("^\"([^\"]*)\"$"));
    if (number.match(fallback).hasMatch())
        return QVariant(fallback.toDouble());
    if (const auto m = string.match(fallback); m.hasMatch())
        return QVariant(m.captured(1));
    if (fallback == QLatin1String("true") || fallback == QLatin1String("false"))
        return QVariant(fallback == QLatin1String("true"));
    if (weights.contains(fallback))
        return QVariant(double(weights.value(fallback)));
    return std::nullopt;
}

/// The table's value at `path`, when it has one.
std::optional<QVariant> tableValue(const QString &path)
{
    const QString group = path.section(QLatin1Char('.'), 0, 0);
    const QString key = path.section(QLatin1Char('.'), 1);
    const QVariantMap values = SkinReader::omarchyTokens().value(group).toMap();
    if (!values.contains(key))
        return std::nullopt;
    return values.value(key);
}

bool same(const QVariant &table, const QVariant &qml)
{
    if (qml.typeId() == QMetaType::Double)
        return table.canConvert<double>() && table.typeId() != QMetaType::QString &&
               table.typeId() != QMetaType::Bool && table.toDouble() == qml.toDouble();
    return table == qml;
}

} // namespace

/// DECISIONS C37 U3: `skin clone` writes Omarchy's value for every token it can, so
/// SkinReader::omarchyTokens() must say what the QML's fallbacks say. This reads the QML and
/// holds the table to it.
class TestSkinTokens : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void everyDensityNameIsKnown();
    void everyTokenCallIsKnown();
    void literalFallbacksMatch();
    void contextualOnesAreLeftOut();
};

void TestSkinTokens::initTestCase()
{
    // About 160 density names and 46 token calls today: a few means the reading broke.
    QVERIFY2(calls().size() > 150, qPrintable(QString::number(calls().size())));
}

void TestSkinTokens::everyDensityNameIsKnown()
{
    for (const Call &call : calls()) {
        if (!call.path.startsWith(QLatin1String("density.")))
            continue;
        QVERIFY2(tableValue(call.path) || SkinReader::contextualTokens().contains(call.path),
                 qPrintable(call.path + QStringLiteral(" at ") + call.where));
    }
}

void TestSkinTokens::everyTokenCallIsKnown()
{
    for (const Call &call : calls()) {
        if (call.path.startsWith(QLatin1String("density.")))
            continue;
        QVERIFY2(tableValue(call.path) || SkinReader::contextualTokens().contains(call.path),
                 qPrintable(call.path + QStringLiteral(" at ") + call.where));
    }
    // And the table is tokens a skin may set: written as a file, it reads back whole.
    const QJsonObject file{{QStringLiteral("version"), SkinReader::kVersion}};
    QJsonObject tokens = QJsonObject::fromVariantMap(SkinReader::omarchyTokens());
    for (auto it = file.constBegin(); it != file.constEnd(); ++it)
        tokens.insert(it.key(), it.value());
    const Skin skin = SkinReader::parse(QJsonDocument(tokens).toJson());
    QVERIFY2(skin.drawn(), qPrintable(skin.reason));
    QVERIFY2(skin.dropped.isEmpty(), qPrintable(skin.dropped.join(QStringLiteral(", "))));
}

void TestSkinTokens::literalFallbacksMatch()
{
    for (const Call &call : calls()) {
        const auto table = tableValue(call.path);
        if (!table)
            continue;
        const QString at = call.path + QStringLiteral(" at ") + call.where;
        const auto qml = literal(call.fallback);
        QVERIFY2(qml, qPrintable(at + QStringLiteral(": an expression, so contextual")));
        QVERIFY2(same(*table, *qml),
                 qPrintable(at + QStringLiteral(": QML ") + call.fallback +
                            QStringLiteral(", table ") + table->toString()));
    }
}

void TestSkinTokens::contextualOnesAreLeftOut()
{
    for (const QString &path : SkinReader::contextualTokens()) {
        QVERIFY2(!tableValue(path), qPrintable(path + QStringLiteral(" is in both")));
        QList<QVariant> values;
        bool expression = false;
        for (const Call &call : calls()) {
            if (call.path != path)
                continue;
            if (const auto value = literal(call.fallback)) {
                if (!values.contains(*value))
                    values << *value;
            } else {
                expression = true;
            }
        }
        QVERIFY2(expression || values.size() > 1,
                 qPrintable(path + QStringLiteral(": one literal fallback, so not contextual")));
    }
}

QTEST_MAIN(TestSkinTokens)
#include "tst_skintokens.moc"
