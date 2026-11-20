// DECISIONS C45 V19 (#163), C37 U4 (#225): the guide's library and skins parts, and the
// AGENTS.md, CLAUDE.md and skin.schema.json written into omaseed's config folder: each when
// missing, rewritten only when different, a file that isn't omaseed's left alone. The schema
// is checked against the reader: every token, Omarchy's defaults, the built-ins inside it and
// what the reader drops outside it. Every path is under a temporary directory.

#include "SchemaCheck.hpp"
#include "core/AgentWords.hpp"
#include "integration/AgentGuide.hpp"
#include "integration/SkinReader.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace Guide = omaseed::integration::AgentGuide;
namespace Words = omaseed::core::AgentWords;
using omaseed::integration::SkinReader;
using omaseed::tests::schemaProblems;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
}

QDateTime mtime(const QString &path)
{
    return QFileInfo(path).fileTime(QFileDevice::FileModificationTime);
}

void ageFile(const QString &path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addDays(-3),
                             QFileDevice::FileModificationTime));
}

QByteArray agentsBytes()
{
    return (Guide::header() + QStringLiteral("\n\n") + Guide::text()).toUtf8();
}

QJsonObject parsed(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

/// The schema of `group`.`key` (or of the group, with no key).
QJsonObject schemaOf(const QString &group, const QString &key = QString())
{
    const QJsonObject properties =
        SkinReader::schema().value(QLatin1String("properties")).toObject();
    const QJsonObject of = properties.value(group).toObject();
    if (key.isEmpty())
        return of;
    return of.value(QLatin1String("properties")).toObject().value(key).toObject();
}

/// The line count of `text`.
qsizetype linesOf(const QString &text)
{
    return text.count(QLatin1Char('\n'));
}

} // namespace

class TestAgentGuide : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void writesBothWhenMissing();
    void rewritesOnlyWhenDifferent();
    void aForeignAgentsMdIsNeverOverwritten();
    void leavesAForeignClaudeMd();
    void schemaWrittenOnlyWhenDifferent();
    void aForeignSchemaIsLeftAlone();

    void schemaNamesEveryToken();
    void schemaDefaultsAreOmarchys();
    void builtInSkinsAreInsideTheSchema();
    void aDroppedTokenIsOutsideTheSchema_data();
    void aDroppedTokenIsOutsideTheSchema();

    void guideHasBothParts();
    void guideOpensReadOnlyFirst();
    void guideSaysCheckDrawsBothLightnesses();
    void guideIsShort();
    void guideNamesEveryWordAndCode();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_config;
};

void TestAgentGuide::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_config = m_dir->filePath(QStringLiteral("omaseed"));
}

void TestAgentGuide::writesBothWhenMissing()
{
    // The folder itself may not exist yet on a first start.
    const Guide::WriteResult result = Guide::writeInto(m_config);
    QCOMPARE(result.agents, Guide::Wrote::Written);
    QCOMPARE(result.claude, Guide::Wrote::Written);
    const QByteArray agents = readFile(QDir(m_config).filePath(QStringLiteral("AGENTS.md")));
    QVERIFY(agents.startsWith(
        "Written by omaseed on every start. Edits here are overwritten.\n\n## Your library\n"));
    QCOMPARE(agents, agentsBytes());
    QCOMPARE(readFile(QDir(m_config).filePath(QStringLiteral("CLAUDE.md"))),
             QByteArray("@AGENTS.md\n"));
    // C37 U4: the schema a clone's $schema names, with omaseed's line as its first key.
    QCOMPARE(result.schema, Guide::Wrote::Written);
    const QByteArray schema = readFile(QDir(m_config).filePath(QStringLiteral("skin.schema.json")));
    QVERIFY(schema.startsWith("{\n    \"$comment\": \"Written by omaseed on every start. Edits "
                              "here are overwritten.\",\n"));
    QCOMPARE(schema, Guide::schemaBytes());
}

void TestAgentGuide::rewritesOnlyWhenDifferent()
{
    QVERIFY(QDir().mkpath(m_config));
    const QString agents = QDir(m_config).filePath(QStringLiteral("AGENTS.md"));
    const QString claude = QDir(m_config).filePath(QStringLiteral("CLAUDE.md"));
    QCOMPARE(Guide::writeInto(m_config).agents, Guide::Wrote::Written);
    ageFile(agents);
    ageFile(claude);
    const QDateTime agentsAt = mtime(agents);
    const QDateTime claudeAt = mtime(claude);

    // The same text: neither file is touched, so neither mtime moves.
    Guide::WriteResult result = Guide::writeInto(m_config);
    QCOMPARE(result.agents, Guide::Wrote::Same);
    QCOMPARE(result.claude, Guide::Wrote::Same);
    QCOMPARE(mtime(agents), agentsAt);
    QCOMPARE(mtime(claude), claudeAt);

    // An older omaseed's guide (its header, other text) is replaced; an edit too.
    writeFile(agents, (Guide::header() + QStringLiteral("\n\nan older guide\n")).toUtf8());
    ageFile(agents);
    result = Guide::writeInto(m_config);
    QCOMPARE(result.agents, Guide::Wrote::Written);
    QCOMPARE(readFile(agents), agentsBytes());
    QVERIFY(mtime(agents) > agentsAt);
    QCOMPARE(mtime(claude), claudeAt);
}

void TestAgentGuide::aForeignAgentsMdIsNeverOverwritten()
{
    QVERIFY(QDir().mkpath(m_config));
    const QString agents = QDir(m_config).filePath(QStringLiteral("AGENTS.md"));
    const QByteArray own = "# My notes\n\nWritten by omaseed on every start. Edits here are "
                           "overwritten.\n";
    writeFile(agents, own);
    ageFile(agents);
    const QDateTime at = mtime(agents);
    // Not on the first start, nor on any after it.
    for (int start = 0; start < 2; ++start) {
        const Guide::WriteResult result = Guide::writeInto(m_config);
        QCOMPARE(result.agents, Guide::Wrote::LeftAlone);
        QCOMPARE(readFile(agents), own);
        QCOMPARE(mtime(agents), at);
        // CLAUDE.md and the schema are still made when they are missing.
        QCOMPARE(result.claude, start == 0 ? Guide::Wrote::Written : Guide::Wrote::Same);
        QCOMPARE(result.schema, start == 0 ? Guide::Wrote::Written : Guide::Wrote::Same);
    }
}

void TestAgentGuide::leavesAForeignClaudeMd()
{
    QVERIFY(QDir().mkpath(m_config));
    const QString claude = QDir(m_config).filePath(QStringLiteral("CLAUDE.md"));
    const QByteArray own = "@AGENTS.md\n\nAlso: be brief.\n";
    writeFile(claude, own);
    const Guide::WriteResult result = Guide::writeInto(m_config);
    QCOMPARE(result.claude, Guide::Wrote::LeftAlone);
    QCOMPARE(readFile(claude), own);
    QCOMPARE(result.agents, Guide::Wrote::Written);
}

void TestAgentGuide::schemaWrittenOnlyWhenDifferent()
{
    const QString schema = QDir(m_config).filePath(QStringLiteral("skin.schema.json"));
    QCOMPARE(Guide::writeInto(m_config).schema, Guide::Wrote::Written);
    ageFile(schema);
    const QDateTime at = mtime(schema);

    // The same schema: not touched, so its mtime stays.
    QCOMPARE(Guide::writeInto(m_config).schema, Guide::Wrote::Same);
    QCOMPARE(mtime(schema), at);

    // An older omaseed's schema (its $comment first, other tokens), or an edit: replaced.
    for (const QByteArray &had :
         {QByteArray("{\n    \"$comment\": \"Written by omaseed on every start. Edits here are "
                     "overwritten.\",\n    \"title\": \"an older schema\"\n}\n"),
          QByteArray("{ \"$comment\" : \"Written by omaseed on every start. Edits here are "
                     "overwritten.\", \"title\": \"edited\"")}) {
        writeFile(schema, had);
        ageFile(schema);
        QCOMPARE(Guide::writeInto(m_config).schema, Guide::Wrote::Written);
        QCOMPARE(readFile(schema), Guide::schemaBytes());
        QVERIFY(mtime(schema) > at);
    }
}

void TestAgentGuide::aForeignSchemaIsLeftAlone()
{
    QVERIFY(QDir().mkpath(m_config));
    const QString schema = QDir(m_config).filePath(QStringLiteral("skin.schema.json"));
    // Someone's own, and one whose first key isn't omaseed's line though it has it later.
    for (const QByteArray &own :
         {QByteArray("{\"title\": \"my schema\"}\n"),
          QByteArray("{\n  \"title\": \"mine\",\n  \"$comment\": \"Written by omaseed on every "
                     "start. Edits here are overwritten.\"\n}\n")}) {
        writeFile(schema, own);
        QCOMPARE(Guide::writeInto(m_config).schema, Guide::Wrote::LeftAlone);
        QCOMPARE(readFile(schema), own);
    }
}

void TestAgentGuide::schemaNamesEveryToken()
{
    const QJsonObject schema = SkinReader::schema();
    QCOMPARE(schema.value(QLatin1String("$schema")).toString(),
             QStringLiteral("https://json-schema.org/draft/2020-12/schema"));
    QCOMPARE(schema.value(QLatin1String("additionalProperties")), QJsonValue(false));
    const QJsonObject properties = schema.value(QLatin1String("properties")).toObject();

    // The top level: the reader's groups and fields, and nothing else.
    QStringList expected = SkinReader::omarchyTokens().keys();
    expected << QStringLiteral("$schema") << QStringLiteral("skin") << QStringLiteral("version")
             << QStringLiteral("description") << QStringLiteral("cloned_from")
             << QStringLiteral("roles") << QStringLiteral("grounds") << QStringLiteral("faces")
             << QStringLiteral("radius") << QStringLiteral("numerals") << QStringLiteral("glow")
             << QStringLiteral("glyphs") << QStringLiteral("labels");
    expected.sort();
    QStringList had = properties.keys();
    had.sort();
    QCOMPARE(had, expected);

    // Every fixed-group key and density name (Omarchy's table) and every contextual token.
    QStringList tokens;
    const QVariantMap &omarchy = SkinReader::omarchyTokens();
    for (auto group = omarchy.constBegin(); group != omarchy.constEnd(); ++group)
        for (const QString &key : group.value().toMap().keys())
            tokens << group.key() + QLatin1Char('.') + key;
    tokens << SkinReader::contextualTokens();
    for (const QString &path : tokens) {
        const QStringList parts = path.split(QLatin1Char('.'));
        QVERIFY2(!schemaOf(parts[0], parts[1]).isEmpty(), qPrintable(path));
    }
    // And no key the reader's tables don't have.
    for (auto group = properties.constBegin(); group != properties.constEnd(); ++group) {
        if (!omarchy.contains(group.key()))
            continue;
        for (const QString &key :
             group.value().toObject().value(QLatin1String("properties")).toObject().keys())
            QVERIFY2(tokens.contains(group.key() + QLatin1Char('.') + key),
                     qPrintable(group.key() + QLatin1Char('.') + key));
    }
    for (const QString &ground : SkinReader::groundNames())
        QVERIFY2(!schemaOf(QStringLiteral("grounds"), ground).isEmpty(), qPrintable(ground));
    for (const QString &seam : SkinReader::seams())
        QVERIFY2(!schemaOf(QStringLiteral("faces"), seam).isEmpty(), qPrintable(seam));
    for (const QString &role : SkinReader::omarchyRoles().keys())
        QVERIFY2(!schemaOf(QStringLiteral("roles"), role).isEmpty(), qPrintable(role));

    // A face is a built-in skin's name, never a file (C37 U2).
    QCOMPARE(schemaOf(QStringLiteral("faces"), QStringLiteral("Plate"))
                 .value(QLatin1String("$ref"))
                 .toString(),
             QStringLiteral("#/$defs/face"));
    QCOMPARE(schema.value(QLatin1String("$defs"))
                 .toObject()
                 .value(QLatin1String("face"))
                 .toObject()
                 .value(QLatin1String("enum"))
                 .toArray(),
             QJsonArray::fromStringList(SkinReader::builtInNames()));
}

void TestAgentGuide::schemaDefaultsAreOmarchys()
{
    // Omarchy's table, key by key; a contextual token has no default, and says why.
    const QVariantMap &omarchy = SkinReader::omarchyTokens();
    QJsonObject everything{{QStringLiteral("version"), SkinReader::kVersion}};
    for (auto group = omarchy.constBegin(); group != omarchy.constEnd(); ++group) {
        const QVariantMap keys = group.value().toMap();
        QJsonObject written;
        for (auto key = keys.constBegin(); key != keys.constEnd(); ++key) {
            const QJsonValue value =
                schemaOf(group.key(), key.key()).value(QLatin1String("default"));
            QVERIFY2(value == QJsonValue::fromVariant(key.value()),
                     qPrintable(group.key() + QLatin1Char('.') + key.key()));
            written.insert(key.key(), value);
        }
        everything.insert(group.key(), written);
    }
    for (const QString &path : SkinReader::contextualTokens()) {
        const QStringList parts = path.split(QLatin1Char('.'));
        const QJsonObject token = schemaOf(parts[0], parts[1]);
        QVERIFY2(!token.contains(QLatin1String("default")), qPrintable(path));
        QVERIFY2(token.value(QLatin1String("description"))
                     .toString()
                     .contains(QLatin1String("depends on where it is drawn")),
                 qPrintable(path));
    }
    QJsonObject roles;
    for (auto it = SkinReader::omarchyRoles().constBegin();
         it != SkinReader::omarchyRoles().constEnd();
         ++it) {
        QCOMPARE(schemaOf(QStringLiteral("roles"), it.key()).value(QLatin1String("default")),
                 QJsonValue(it.value()));
        roles.insert(it.key(), it.value());
    }
    everything.insert(QStringLiteral("roles"), roles);
    QJsonObject grounds;
    for (const QString &name : SkinReader::groundNames()) {
        const QJsonObject recipe =
            schemaOf(QStringLiteral("grounds"), name).value(QLatin1String("default")).toObject();
        const auto ours = SkinReader::omarchyGrounds().value(name);
        QCOMPARE(recipe.value(QLatin1String("tint")).toString(), ours.tint);
        QCOMPARE(recipe.value(QLatin1String("alpha")).toString(), QStringLiteral("shell"));
        QCOMPARE(recipe.value(QLatin1String("over")).toString(), ours.over);
        grounds.insert(name, recipe);
    }
    everything.insert(QStringLiteral("grounds"), grounds);
    QJsonObject faces;
    for (const QString &seam : SkinReader::seams()) {
        QCOMPARE(schemaOf(QStringLiteral("faces"), seam).value(QLatin1String("default")),
                 QJsonValue(QStringLiteral("omarchy")));
        faces.insert(seam, QStringLiteral("omarchy"));
    }
    everything.insert(QStringLiteral("faces"), faces);
    QCOMPARE(schemaOf(QStringLiteral("glow")).value(QLatin1String("default")),
             QJsonValue(QStringLiteral("clean")));
    everything.insert(QStringLiteral("glow"), QStringLiteral("clean"));
    everything.insert(QStringLiteral("numerals"),
                      schemaOf(QStringLiteral("numerals")).value(QLatin1String("default")));

    // Every default at once: inside the schema, and the reader keeps every one.
    const QStringList problems = schemaProblems(everything, SkinReader::schema());
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    const auto skin = SkinReader::parse(QJsonDocument(everything).toJson());
    QVERIFY(skin.drawn());
    QVERIFY2(skin.dropped.isEmpty(), qPrintable(skin.dropped.join(QLatin1Char(' '))));
}

void TestAgentGuide::builtInSkinsAreInsideTheSchema()
{
    for (const QString &name : SkinReader::builtInNames()) {
        const QByteArray json = readFile(SkinReader::builtInPath(name));
        QVERIFY2(!json.isEmpty(), qPrintable(name));
        QVERIFY2(SkinReader::parse(json).dropped.isEmpty(), qPrintable(name));
        const QStringList problems = schemaProblems(parsed(json), SkinReader::schema());
        QVERIFY2(problems.isEmpty(),
                 qPrintable(name + QLatin1Char('\n') + problems.join(QLatin1Char('\n'))));
    }
    // And what the built-ins don't show, which the reader keeps too.
    const QByteArray kept =
        "{\"version\": 1, \"$schema\": \"../../skin.schema.json\", \"description\": \"Warm.\", "
        "\"cloned_from\": {\"skin\": \"console\", \"version\": 1}, \"radius\": \"hyprland\", "
        "\"glyphs\": {\"up\": \"icon:folder\", \"down\": \"\u25bc\"}, "
        "\"density\": {\"somethingNew\": 3}, \"grounds\": {\"hover\": {\"tint\": \"warn\", "
        "\"alpha\": \"shell\", \"over\": \"idle\"}}, \"line\": {\"menuEdgeRole\": \"faint\"}}";
    QVERIFY(SkinReader::parse(kept).dropped.isEmpty());
    const QStringList problems = schemaProblems(parsed(kept), SkinReader::schema());
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
}

void TestAgentGuide::aDroppedTokenIsOutsideTheSchema_data()
{
    QTest::addColumn<QByteArray>("json");
    const auto row = [](const char *name, const char *tokens) {
        QTest::newRow(name) << (QByteArray("{\"version\": 1, ") + tokens + '}');
    };
    QTest::newRow("no version") << QByteArray("{\"line\": {\"hair\": 1}}");
    QTest::newRow("newer") << QByteArray("{\"version\": 2}");
    row("unknown group", "\"colour\": {\"x\": 1}");
    row("group not an object", "\"line\": 1");
    row("unknown key", "\"line\": {\"bold\": 1}");
    row("width too wide", "\"line\": {\"hair\": 9}");
    row("px not whole", "\"cut\": {\"panel\": 1.5}");
    row("alpha over 1", "\"tint\": {\"notice\": 1.2}");
    row("line role a hex", "\"line\": {\"hairRole\": \"#ff0000\"}");
    row("role a hex", "\"roles\": {\"accent\": \"#fff\"}");
    row("unknown role", "\"roles\": {\"loud\": \"red\"}");
    row("ground alpha", "\"grounds\": {\"hover\": {\"tint\": \"foreground\", \"alpha\": 1.4}}");
    row("ground over itself",
        "\"grounds\": {\"hover\": {\"tint\": \"foreground\", \"alpha\": 0.1, \"over\": "
        "\"hover\"}}");
    row("ground extra key",
        "\"grounds\": {\"hover\": {\"tint\": \"foreground\", \"alpha\": 0.1, \"blur\": 2}}");
    row("ground tint", "\"grounds\": {\"hover\": {\"tint\": \"#000\", \"alpha\": 0.1}}");
    row("unknown ground", "\"grounds\": {\"nope\": {\"tint\": \"foreground\", \"alpha\": 0.1}}");
    row("face a file", "\"faces\": {\"Plate\": \"Plate.qml\"}");
    row("face a path", "\"faces\": {\"Plate\": \"../x\"}");
    row("unknown seam", "\"faces\": {\"Brackets\": \"console\"}");
    row("family with a control", "\"font\": {\"label\": \"a\\u0001b\"}");
    row("case", "\"label\": {\"case\": \"lower\"}");
    row("weight", "\"label\": {\"weight\": 950}");
    row("step", "\"label\": {\"controlSize\": \"huge\"}");
    row("count", "\"meter\": {\"piecesColumns\": 300}");
    row("bool", "\"select\": {\"cursorMark\": \"yes\"}");
    row("density too big", "\"density\": {\"railWidth\": 5000}");
    row("density name", "\"density\": {\"9x\": 1}");
    row("ligature", "\"glyphs\": {\"up\": \"ms:Bad\"}");
    row("bare icon", "\"glyphs\": {\"up\": \"icon:\"}");
    row("empty glyph", "\"glyphs\": {\"up\": \"\"}");
    row("empty word", "\"labels\": {\"seed\": \"\"}");
    row("long word", "\"labels\": {\"seed\": \"abcdefghijklmnopqrstuvwxyzabcdefg\"}");
    row("radius", "\"radius\": 70");
    row("radius word", "\"radius\": \"round\"");
    row("numerals", "\"numerals\": \"wide\"");
    row("glow", "\"glow\": \"soft\"");
    row("empty description", "\"description\": \"\"");
    row("cloned_from", "\"cloned_from\": {\"skin\": \"console\"}");
}

void TestAgentGuide::aDroppedTokenIsOutsideTheSchema()
{
    // What the reader drops (or draws whole as Omarchy's), the schema refuses. A ground loop
    // longer than one ground is the one drop a schema can't say.
    QFETCH(QByteArray, json);
    const auto skin = SkinReader::parse(json);
    QVERIFY2(!skin.drawn() || !skin.drops.isEmpty(), "the reader kept it");
    QVERIFY2(!schemaProblems(parsed(json), SkinReader::schema()).isEmpty(), "the schema takes it");
}

void TestAgentGuide::guideHasBothParts()
{
    const QString library = Guide::libraryPart();
    const QString skins = Guide::skinsPart();
    QVERIFY(library.startsWith(QStringLiteral("## Your library\n")));
    QVERIFY(skins.startsWith(QStringLiteral("## Your skins\n")));
    QCOMPARE(Guide::text(), library + QLatin1Char('\n') + skins);
    QVERIFY(library.endsWith(QStringLiteral(
        "Every answer is one JSON object with \"v\": 1; fields are only added within it.\n")));

    // The skins part names the schema beside it, and the folder no agent reads (#283: with no
    // keyring, passwords are in secrets/).
    QVERIFY(skins.contains(QStringLiteral("skin.schema.json beside this file")));
    QVERIFY(skins.contains(QStringLiteral("in secrets/ on a machine without one")));
    QVERIFY(skins.contains(QStringLiteral("never read that folder")));
    QVERIFY(skins.contains(QStringLiteral("skin-check.sh")));

    QVERIFY(QDir().mkpath(m_config));
    Guide::writeInto(m_config);
    const QString agents =
        QString::fromUtf8(readFile(QDir(m_config).filePath(QStringLiteral("AGENTS.md"))));
    QVERIFY(agents.contains(QStringLiteral("\n## Your library\n")));
    QVERIFY(agents.contains(QStringLiteral("\n## Your skins\n")));
}

void TestAgentGuide::guideOpensReadOnlyFirst()
{
    // herdr's lesson: inspect before acting. The reads come before clone and use.
    const QString skins = Guide::skinsPart();
    const qsizetype list = skins.indexOf(QStringLiteral("omaseed skin list --json"));
    const qsizetype check = skins.indexOf(QStringLiteral("omaseed check NAME --json"));
    const qsizetype clone = skins.indexOf(QStringLiteral("omaseed skin clone "));
    const qsizetype use = skins.indexOf(QStringLiteral("omaseed skin use "));
    QVERIFY(list > 0);
    QVERIFY(check > list);
    QVERIFY(clone > check);
    QVERIFY(use > clone);
}

void TestAgentGuide::guideSaysCheckDrawsBothLightnesses()
{
    // A skin takes its colours from the theme, and check draws a light and a dark picture
    // itself (C37 U5, amended 2026-11-20): the guide says so, and no longer asks for a second
    // check under a light theme by hand.
    const QString skins = Guide::skinsPart();
    QVERIFY(skins.contains(QStringLiteral("a light and a dark picture")));
    static const QRegularExpression byHand(QStringLiteral("omaseed check \\S+ --theme \\S"));
    QVERIFY(!byHand.match(skins).hasMatch());
}

void TestAgentGuide::guideIsShort()
{
    // The schema carries the reference, so the prose stays short (C37 U4).
    QVERIFY2(linesOf(Guide::skinsPart()) <= 90,
             qPrintable(QString::number(linesOf(Guide::skinsPart()))));
    QVERIFY2(linesOf(Guide::text()) <= 160, qPrintable(QString::number(linesOf(Guide::text()))));
}

void TestAgentGuide::guideNamesEveryWordAndCode()
{
    const QString text = Guide::text();

    // Every read of the table, as its command line (C45 V19), and every skin word (C37 U3).
    for (const QString &word : {QStringLiteral("status"),
                                QStringLiteral("errors"),
                                QStringLiteral("log"),
                                QStringLiteral("moves")}) {
        QVERIFY2(!Words::method(word).isEmpty(), qPrintable(word));
        QVERIFY2(text.contains(QStringLiteral("  omaseed %1 --json ").arg(word)), qPrintable(word));
    }
    for (const QString &line : {QStringLiteral("omaseed skin list --json"),
                                QStringLiteral("omaseed skin clone "),
                                QStringLiteral("omaseed skin use "),
                                QStringLiteral("omaseed check ")})
        QVERIFY2(text.contains(line), qPrintable(line));
    for (const QString &word : {QStringLiteral("status"),
                                QStringLiteral("errors"),
                                QStringLiteral("log"),
                                QStringLiteral("moves"),
                                QStringLiteral("check"),
                                QStringLiteral("skin")}) {
        QVERIFY2(Words::isWord(word) && !Words::isReserved(word), qPrintable(word));
        QVERIFY2(text.contains(QStringLiteral("omaseed %1 ").arg(word)), qPrintable(word));
    }

    // Every failure code, in the rows of the exit the CLI gives it.
    const QString library = Guide::libraryPart();
    for (const QString &code : {QStringLiteral("unreadable"),
                                QStringLiteral("usage"),
                                QStringLiteral("unknown-server"),
                                QStringLiteral("ambiguous"),
                                QStringLiteral("unknown-skin"),
                                QStringLiteral("bad-name"),
                                QStringLiteral("skin-exists"),
                                QStringLiteral("no-theme"),
                                QStringLiteral("not-running"),
                                QStringLiteral("too-old"),
                                QStringLiteral("no-bus"),
                                QStringLiteral("no-reply"),
                                QStringLiteral("not-current"),
                                QStringLiteral("unreachable"),
                                QStringLiteral("no-log"),
                                QStringLiteral("unknown-torrent"),
                                QStringLiteral("first-sync"),
                                QStringLiteral("not-written")}) {
        const int exit = Words::exitFor(code);
        const qsizetype row = library.indexOf(QStringLiteral("\n  %1  ").arg(exit));
        QVERIFY2(row >= 0, qPrintable(code));
        // The row runs to the next exit's row, or the blank line after the list.
        static const QRegularExpression next(QStringLiteral("\n(  \\d  |\n)"));
        const qsizetype end = library.indexOf(next, row + 1);
        const QString rows = library.mid(row, end - row);
        QVERIFY2(rows.contains(code), qPrintable(code + QStringLiteral(" not on: ") + rows));
    }
    QVERIFY(library.contains(QStringLiteral("\n  0  answered")));

    // No action is offered: they aren't built.
    QVERIFY(!text.contains(QStringLiteral("### Acting")));
    QVERIFY(!text.contains(QStringLiteral("omaseed do")));
}

QTEST_GUILESS_MAIN(TestAgentGuide)
#include "tst_agentguide.moc"
