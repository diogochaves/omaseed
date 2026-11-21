// The agent skill's pages, read from the repo's data/agents/skill/ (OMASEED_SOURCE_SKILL_DIR):
// SKILL.md's frontmatter and map, its ref/ pages, and the budget that keeps the map the thing
// an agent loads first (DECISIONS C48 AS2–AS4, AS11; docs/65 §3–4).

#include <QDir>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTest>

namespace {

QString skillDir()
{
    return QStringLiteral(OMASEED_SOURCE_SKILL_DIR);
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString skillMd()
{
    return readText(skillDir() + QStringLiteral("/SKILL.md"));
}

/// SKILL.md's lines between the first two `---`, or empty when it has none.
QStringList frontmatterLines(const QString &skill)
{
    const QStringList lines = skill.split(QLatin1Char('\n'));
    if (lines.isEmpty() || lines.first() != QLatin1String("---"))
        return {};
    const qsizetype end = lines.indexOf(QStringLiteral("---"), 1);
    if (end < 0)
        return {};
    return lines.mid(1, end - 1);
}

/// SKILL.md from the line after its second `---`: what AGENTS.md is made from (C48).
QString body(const QString &skill)
{
    const QStringList lines = skill.split(QLatin1Char('\n'));
    const qsizetype end = lines.indexOf(QStringLiteral("---"), 1);
    return end < 0 ? skill : lines.mid(end + 1).join(QLatin1Char('\n'));
}

/// `key: value` per line, a double-quoted value unquoted. Each field is one line.
QHash<QString, QString> frontmatter(const QString &skill)
{
    QHash<QString, QString> fields;
    for (const QString &line : frontmatterLines(skill)) {
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        QString value = line.mid(colon + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) &&
            value.endsWith(QLatin1Char('"'))) {
            value = value.mid(1, value.size() - 2);
            value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
            value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        }
        fields.insert(line.left(colon).trimmed(), value);
    }
    return fields;
}

/// ref/'s pages, by file name.
QStringList pageNames()
{
    return QDir(skillDir() + QStringLiteral("/ref"))
        .entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
}

QString page(const QString &fileName)
{
    return readText(skillDir() + QStringLiteral("/ref/") + fileName);
}

/// The lines of 40 characters or more, trimmed: the ones that would be said twice.
QSet<QString> longLines(const QString &text)
{
    QSet<QString> lines;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.size() >= 40)
            lines.insert(trimmed);
    }
    return lines;
}

} // namespace

class TestSkillPages : public QObject
{
    Q_OBJECT

private slots:
    void theFolderIsThere();
    void frontmatterHasNameDescriptionTools();
    void allowedToolsAreTheSixReads();
    void descriptionCarriesTheTriggers();
    void budget();
    void everyRefIsAPage();
    void nothingSaidTwice();
    void mapAnswersTheTrial();
    void noBesideThisFile();
};

void TestSkillPages::theFolderIsThere()
{
    QVERIFY2(!skillMd().isEmpty(), qPrintable(skillDir() + QStringLiteral("/SKILL.md")));
    QCOMPARE(pageNames(),
             QStringList({QStringLiteral("files.md"),
                          QStringLiteral("library.md"),
                          QStringLiteral("skins.md")}));
}

void TestSkillPages::frontmatterHasNameDescriptionTools()
{
    const QString skill = skillMd();
    const QStringList lines = frontmatterLines(skill);
    QVERIFY2(!lines.isEmpty(), "SKILL.md opens with a --- frontmatter");
    const auto fields = frontmatter(skill);
    QCOMPARE(fields.size(), lines.size()); // one field per line, nothing else
    QCOMPARE(QSet<QString>(fields.keyBegin(), fields.keyEnd()),
             QSet<QString>({QStringLiteral("name"),
                            QStringLiteral("description"),
                            QStringLiteral("allowed-tools")}));
    QCOMPARE(fields.value(QStringLiteral("name")), QStringLiteral("omaseed"));
    QVERIFY(!fields.value(QStringLiteral("description")).isEmpty());
}

void TestSkillPages::allowedToolsAreTheSixReads()
{
    // AS4 a: the words that only read. clone and use change the person's files and window,
    // skill link their agents' folders, and do has its own gate (C45 V16): never here.
    const QString tools = frontmatter(skillMd()).value(QStringLiteral("allowed-tools"));
    static const QRegularExpression entry(QStringLiteral("Bash\\(([^)]*)\\)"));
    QStringList commands;
    QString rest = tools;
    for (auto it = entry.globalMatch(tools); it.hasNext();) {
        const auto match = it.next();
        QString command = match.captured(1);
        if (command.endsWith(QLatin1String(" *")))
            command.chop(2);
        commands.append(command);
        rest.replace(match.captured(0), QString());
    }
    QVERIFY2(rest.trimmed().isEmpty(), qPrintable(QStringLiteral("not a Bash(…): ") + rest));
    commands.sort();
    QCOMPARE(commands,
             QStringList({QStringLiteral("omaseed check"),
                          QStringLiteral("omaseed errors"),
                          QStringLiteral("omaseed log"),
                          QStringLiteral("omaseed moves"),
                          QStringLiteral("omaseed skin list"),
                          QStringLiteral("omaseed status")}));
    for (const QString &never : {QStringLiteral("clone"),
                                 QStringLiteral("skin use"),
                                 QStringLiteral("skill"),
                                 QStringLiteral("omaseed do")})
        QVERIFY2(!tools.contains(never), qPrintable(never));
}

void TestSkillPages::descriptionCarriesTheTriggers()
{
    // AS3 a: the person's own words from #227's two requests, the engines and omaseed.
    const QString description = frontmatter(skillMd()).value(QStringLiteral("description"));
    for (const QString &word : {QStringLiteral("omaseed"),
                                QStringLiteral("torrent"),
                                QStringLiteral("didn't finish"),
                                QStringLiteral("stuck"),
                                QStringLiteral("slow"),
                                QStringLiteral("warmer"),
                                QStringLiteral("rounder"),
                                QStringLiteral("qBittorrent"),
                                QStringLiteral("Transmission"),
                                QStringLiteral("Deluge")})
        QVERIFY2(description.contains(word), qPrintable(word));
    // "change my theme" is the desktop's (Omarchy's skill): theme only as omaseed's.
    static const QRegularExpression theme(QStringLiteral("theme"),
                                          QRegularExpression::CaseInsensitiveOption);
    for (auto it = theme.globalMatch(description); it.hasNext();) {
        const qsizetype at = it.next().capturedStart();
        QVERIFY2(description.left(at).endsWith(QLatin1String("omaseed's ")),
                 qPrintable(description.mid(qMax<qsizetype>(0, at - 20), 30)));
    }
}

void TestSkillPages::budget()
{
    // AS11 a: tokens counted as bytes / 4.
    const QString skill = skillMd();
    const QByteArray bytes = skill.toUtf8();
    QVERIFY2(skill.count(QLatin1Char('\n')) < 70, "SKILL.md: under 70 lines");
    QVERIFY2(bytes.size() / 4 < 1000, "SKILL.md: under 1,000 tokens");
    QVERIFY2(frontmatter(skill).value(QStringLiteral("description")).size() < 700,
             "the description: under 700 characters");
    for (const QString &name : pageNames())
        QVERIFY2(page(name).toUtf8().size() / 4 < 1200, qPrintable(name + u": under 1,200 tokens"));
}

void TestSkillPages::everyRefIsAPage()
{
    static const QRegularExpression ref(QStringLiteral("ref: ([A-Za-z0-9_-]+\\.md)"));
    QSet<QString> named;
    const QString map = body(skillMd());
    for (auto it = ref.globalMatch(map); it.hasNext();) {
        const QString name = it.next().captured(1);
        named.insert(name);
        QVERIFY2(pageNames().contains(name), qPrintable(u"ref: " + name + u" is not in ref/"));
    }
    for (const QString &name : pageNames())
        QVERIFY2(named.contains(name), qPrintable(name + u": no ref: in SKILL.md names it"));
}

void TestSkillPages::nothingSaidTwice()
{
    QHash<QString, QSet<QString>> texts;
    texts.insert(QStringLiteral("SKILL.md"), longLines(body(skillMd())));
    for (const QString &name : pageNames())
        texts.insert(name, longLines(page(name)));
    const QStringList names = texts.keys();
    for (qsizetype i = 0; i < names.size(); ++i) {
        for (qsizetype j = i + 1; j < names.size(); ++j) {
            const QSet<QString> both = texts.value(names[i]) & texts.value(names[j]);
            QVERIFY2(both.isEmpty(),
                     qPrintable(names[i] + u" and " + names[j] + u" both say: " +
                                QStringList(both.values()).join(u" | ")));
        }
    }
}

void TestSkillPages::mapAnswersTheTrial()
{
    // AS2 a: the map alone answers #227's two requests.
    const QString map = body(skillMd());
    for (const QString &line : {
             // the never list
             QStringLiteral("## Never"),
             QStringLiteral("no qBittorrent, Transmission or Deluge API or files"),
             QStringLiteral("state.json, servers.json, moves/, secrets/"),
             QStringLiteral("The bar widget's D-Bus interface"),
             QStringLiteral("fix.words"),
             QStringLiteral("Exit 3"),
             // the four reads, and where to start
             QStringLiteral("omaseed status --json"),
             QStringLiteral("omaseed errors --json"),
             QStringLiteral("omaseed moves --json"),
             QStringLiteral("omaseed log --json"),
             QStringLiteral("errors first, then moves"),
             QStringLiteral("needsYou"),
             // the wish map, and the schema's rule
             QStringLiteral("warmer, cooler"),
             QStringLiteral("rounder, sharper"),
             QStringLiteral("denser, airier"),
             QStringLiteral("a font"),
             QStringLiteral("grep ~/.config/omaseed/skin.schema.json"),
             QStringLiteral("never read it whole"),
             QStringLiteral("look at both pictures"),
             // the way back
             QStringLiteral("The way back: omaseed skin use omarchy"),
         })
        QVERIFY2(map.contains(line), qPrintable(line));

    // The loop, in order: list, check, clone the drawn skin, edit, check, use.
    qsizetype at = 0;
    for (const QString &step : {QStringLiteral("omaseed skin list --json"),
                                QStringLiteral("omaseed check NAME --json"),
                                QStringLiteral("omaseed skin clone DRAWN NAME"),
                                QStringLiteral("edit skins/NAME/skin.json"),
                                QStringLiteral("omaseed skin use NAME")}) {
        const qsizetype next = map.indexOf(step, at);
        QVERIFY2(next >= 0, qPrintable(u"out of order or missing: " + step));
        at = next + step.size();
    }
}

void TestSkillPages::noBesideThisFile()
{
    // In a skill folder nothing sits beside the pages but the pages: the schema is named by
    // its one place (C48; skins.md said "beside this file" when it lived in ~/.config/omaseed/).
    const QString schema = QStringLiteral("skin.schema.json");
    const QString placed = QStringLiteral("~/.config/omaseed/skin.schema.json");
    const QString map = body(skillMd());
    QVERIFY(map.contains(placed));
    QVERIFY(page(QStringLiteral("skins.md")).contains(placed));
    for (const QString &text : {map, page(QStringLiteral("skins.md"))}) {
        for (qsizetype at = text.indexOf(schema); at >= 0; at = text.indexOf(schema, at + 1))
            QVERIFY2(text.left(at + schema.size()).endsWith(placed),
                     qPrintable(text.mid(qMax<qsizetype>(0, at - 30), 50)));
    }
    for (const QString &name : pageNames())
        QVERIFY2(!page(name).contains(QLatin1String("beside this file")), qPrintable(name));
    // SKILL.md's one: the sentence about ref/, which AGENTS.md and omaseed skill rewrite.
    for (const QString &line : map.split(QLatin1Char('\n'))) {
        if (line.contains(QLatin1String("beside this file")))
            QCOMPARE(line,
                     QStringLiteral("\"ref: FILE\" is a page in ref/ beside this file: read it "
                                    "when the task needs it."));
    }
}

QTEST_GUILESS_MAIN(TestSkillPages)
#include "tst_skillpages.moc"
