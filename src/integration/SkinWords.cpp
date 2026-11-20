#include "integration/SkinWords.hpp"

#include "core/AgentAnswers.hpp"
#include "core/AgentWords.hpp"
#include "core/StateFile.hpp"
#include "integration/UserSkins.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace omaseed::integration::SkinWords {

namespace {

namespace AgentWords = core::AgentWords;
using Fields = QList<std::pair<QString, QJsonValue>>;

/// One JSON value as QJsonDocument writes it, compact.
QString scalar(const QJsonValue &value)
{
    const QByteArray array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}

QString pad(int indent)
{
    return QString(indent, QLatin1Char(' '));
}

/// `value` with two-space indent, an object's keys in QJsonObject's (sorted) order.
QString pretty(const QJsonValue &value, int indent)
{
    if (!value.isObject() || value.toObject().isEmpty())
        return scalar(value);
    const QJsonObject object = value.toObject();
    QStringList members;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        members << pad(indent + 2) + scalar(it.key()) + QStringLiteral(": ") +
                       pretty(it.value(), indent + 2);
    return QStringLiteral("{\n") + members.join(QStringLiteral(",\n")) + QLatin1Char('\n') +
           pad(indent) + QLatin1Char('}');
}

/// The grounds, in the order Theme resolves them, a recipe to a line as the built-ins write it.
QString groundsText(const QHash<QString, SkinGround> &grounds, int indent)
{
    QStringList members;
    for (const QString &name : SkinReader::groundNames()) {
        const SkinGround ground = grounds.value(name);
        const QJsonValue alpha =
            ground.shellAlpha ? QJsonValue(QStringLiteral("shell")) : QJsonValue(ground.alpha);
        members << pad(indent + 2) + scalar(name) +
                       QStringLiteral(": { \"tint\": %1, \"alpha\": %2, \"over\": %3 }")
                           .arg(scalar(ground.tint), scalar(alpha), scalar(ground.over));
    }
    return QStringLiteral("{\n") + members.join(QStringLiteral(",\n")) + QLatin1Char('\n') +
           pad(indent) + QLatin1Char('}');
}

QString shortPath(const QString &path, const QString &home)
{
    if (!home.isEmpty() && (path == home || path.startsWith(home + QLatin1Char('/'))))
        return QLatin1Char('~') + path.mid(home.size());
    return path;
}

QJsonValue orNull(const QString &text)
{
    return text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text);
}

QJsonObject listed(const QString &name, const QString &path, const Skin &skin)
{
    const bool builtIn = path.isEmpty();
    const QJsonValue from =
        skin.clonedFrom.skin.isEmpty()
            ? QJsonValue(QJsonValue::Null)
            : QJsonValue(QJsonObject{{QStringLiteral("skin"), skin.clonedFrom.skin},
                                     {QStringLiteral("version"), skin.clonedFrom.version}});
    return {{QStringLiteral("name"), name},
            {QStringLiteral("builtIn"), builtIn},
            {QStringLiteral("path"), builtIn ? QJsonValue(QJsonValue::Null) : QJsonValue(path)},
            {QStringLiteral("reads"), skin.drawn()},
            {QStringLiteral("reason"), orNull(skin.reason)},
            {QStringLiteral("version"), skin.version},
            {QStringLiteral("description"), orNull(skin.description)},
            {QStringLiteral("clonedFrom"), from},
            {QStringLiteral("dropped"), static_cast<int>(skin.drops.size())}};
}

QString listLines(const QJsonObject &answer, const QString &home)
{
    const QJsonArray skins = answer.value(QLatin1String("skins")).toArray();
    const QString choice = answer.value(QLatin1String("choice")).toString();
    qsizetype width = 0;
    for (const QJsonValue &skin : skins)
        width = std::max(width, skin.toObject().value(QLatin1String("name")).toString().size());
    QString out;
    for (const QJsonValue &value : skins) {
        const QJsonObject skin = value.toObject();
        const QString name = skin.value(QLatin1String("name")).toString();
        QStringList columns{name.leftJustified(width)};
        columns << (skin.value(QLatin1String("builtIn")).toBool()
                        ? QStringLiteral("built in")
                        : shortPath(skin.value(QLatin1String("path")).toString(), home));
        if (!skin.value(QLatin1String("reads")).toBool()) {
            columns << QStringLiteral("doesn't read: ") +
                           skin.value(QLatin1String("reason")).toString();
        } else {
            const QString description = skin.value(QLatin1String("description")).toString();
            if (!description.isEmpty())
                columns << description;
            if (const int dropped = skin.value(QLatin1String("dropped")).toInt(); dropped > 0)
                columns << QStringLiteral("%1 dropped").arg(dropped);
        }
        out += (name == choice ? QStringLiteral("* ") : QStringLiteral("  ")) +
               columns.join(QStringLiteral("   ")).trimmed() + QLatin1Char('\n');
    }
    const QJsonValue pinned = answer.value(QLatin1String("pinned"));
    if (pinned.isString())
        out +=
            QStringLiteral("OMASEED_SKIN=%1 draws %1 whatever is chosen.\n").arg(pinned.toString());
    return out;
}

QString useLines(const QJsonObject &answer)
{
    const QJsonObject skin = answer.value(QLatin1String("skin")).toObject();
    const QString name = skin.value(QLatin1String("name")).toString();
    QString out =
        answer.value(QLatin1String("live")).toBool()
            ? QStringLiteral("%1 is drawn in the open window, and kept.").arg(name)
            : QStringLiteral("%1 is saved: omaseed draws it when it next starts.").arg(name);
    if (const int dropped = skin.value(QLatin1String("dropped")).toInt(); dropped > 0)
        out += (dropped == 1 ? QStringLiteral(" 1 token is left at Omarchy's")
                             : QStringLiteral(" %1 tokens are left at Omarchy's").arg(dropped)) +
               QStringLiteral(": omaseed check %1 names them.").arg(name);
    return out + QLatin1Char('\n');
}

/// The words a reason says, as the Skin row says them (docs/55 §4).
QString reasonWords(const QString &reason, int version)
{
    if (reason == QLatin1String("unparseable"))
        return QStringLiteral("its skin.json doesn't parse");
    if (reason == QLatin1String("no version"))
        return QStringLiteral("it has no version");
    if (reason == QLatin1String("newer"))
        return QStringLiteral("version %1 is newer than this omaseed").arg(version);
    if (reason == QLatin1String("missing"))
        return QStringLiteral("its folder has no skin.json");
    return reason;
}

QString checkLines(const QJsonObject &answer, const QString &home)
{
    const QString name = answer.value(QLatin1String("skin")).toString();
    const bool drawn = answer.value(QLatin1String("drawn")).toBool();
    QStringList head{name};
    head << (drawn ? QStringLiteral("drawn")
                   : QStringLiteral("drawn as Omarchy's: %1")
                         .arg(answer.value(QLatin1String("why")).toString()));
    head << QStringLiteral("%1 (%2)").arg(answer.value(QLatin1String("theme")).toString(),
                                          answer.value(QLatin1String("mode")).toString());
    head << QStringLiteral("base size %1").arg(answer.value(QLatin1String("baseSize")).toInt());
    QString out = head.join(QStringLiteral(" · ")) + QLatin1Char('\n');
    for (const QJsonValue &value : answer.value(QLatin1String("dropped")).toArray()) {
        const QJsonObject drop = value.toObject();
        const QJsonValue dropped = drop.value(QLatin1String("value"));
        out +=
            QStringLiteral("  dropped %1%2: %3\n")
                .arg(drop.value(QLatin1String("token")).toString(),
                     dropped.isNull() || dropped.isUndefined() ? QString()
                                                               : QLatin1Char(' ') + scalar(dropped),
                     drop.value(QLatin1String("why")).toString());
    }
    QStringList fonts;
    for (const QJsonValue &font : answer.value(QLatin1String("missingFonts")).toArray())
        fonts << font.toString();
    if (!fonts.isEmpty())
        out += QStringLiteral("  not installed: %1\n").arg(fonts.join(QStringLiteral(", ")));
    for (const QJsonValue &value : answer.value(QLatin1String("lifted")).toArray()) {
        const QJsonObject lift = value.toObject();
        out += QStringLiteral("  lifted %1 on %2 from %3 to %4: that ground is louder than "
                              "Omarchy's\n")
                   .arg(lift.value(QLatin1String("role")).toString(),
                        lift.value(QLatin1String("ground")).toString(),
                        QString::number(lift.value(QLatin1String("from")).toDouble(), 'f', 1),
                        QString::number(lift.value(QLatin1String("to")).toDouble(), 'f', 1));
    }
    out += QStringLiteral("  picture %1\n")
               .arg(shortPath(answer.value(QLatin1String("picture")).toString(), home));
    // #226: omabox checks a skin in every theme; said only where it isn't installed.
    if (!answer.value(QLatin1String("omabox")).toBool())
        out += QStringLiteral("omabox would check this skin in every theme, in a desktop nobody "
                              "sees: omabox.app\n");
    return out;
}

} // namespace

QByteArray check(const CheckFacts &facts, const QDateTime &at, const QString &omaseed)
{
    QJsonArray dropped;
    for (const SkinDrop &drop : facts.skin.drops)
        dropped.append(QJsonObject{{QStringLiteral("token"), drop.token},
                                   {QStringLiteral("value"), drop.value},
                                   {QStringLiteral("why"), drop.why}});
    QJsonArray lifted;
    for (const CheckLift &lift : facts.lifted)
        lifted.append(QJsonObject{{QStringLiteral("role"), lift.role},
                                  {QStringLiteral("ground"), lift.ground},
                                  {QStringLiteral("from"), lift.from},
                                  {QStringLiteral("to"), lift.to}});
    Fields fields = core::AgentAnswers::envelope(QStringLiteral("check"), at, omaseed);
    fields << std::pair{QStringLiteral("skin"), QJsonValue(facts.name)}
           << std::pair{QStringLiteral("version"), QJsonValue(facts.skin.version)}
           << std::pair{QStringLiteral("drawn"), QJsonValue(facts.skin.drawn())};
    if (!facts.skin.drawn())
        fields << std::pair{QStringLiteral("why"),
                            QJsonValue(reasonWords(facts.skin.reason, facts.skin.version))};
    fields << std::pair{QStringLiteral("dropped"), QJsonValue(dropped)}
           << std::pair{QStringLiteral("missingFonts"),
                        QJsonValue(QJsonArray::fromStringList(facts.missingFonts))}
           << std::pair{QStringLiteral("lifted"), QJsonValue(lifted)}
           << std::pair{QStringLiteral("theme"), QJsonValue(facts.theme)}
           << std::pair{QStringLiteral("mode"), QJsonValue(facts.mode)}
           << std::pair{QStringLiteral("baseSize"), QJsonValue(facts.baseSize)}
           << std::pair{QStringLiteral("picture"), QJsonValue(facts.picture)}
           << std::pair{QStringLiteral("omabox"), QJsonValue(facts.omabox)};
    return AgentWords::orderedJson(fields);
}

Pick pick(const QString &skinsDir, const QString &name)
{
    Pick picked;
    if (SkinReader::builtInNames().contains(name)) {
        picked.skin = SkinReader::builtIn(name);
        return picked;
    }
    if (const QString problem = UserSkins::nameProblem(name); !problem.isEmpty()) {
        picked.code = QStringLiteral("bad-name");
        picked.message = QStringLiteral("%1 can't be a skin's name: %2.").arg(name, problem);
        return picked;
    }
    const QString path = skinsDir + QLatin1Char('/') + name;
    if (skinsDir.isEmpty() || !QFileInfo(path).isDir()) {
        picked.code = QStringLiteral("unknown-skin");
        picked.message =
            QStringLiteral("No skin called %1. omaseed skin list lists them.").arg(name);
        return picked;
    }
    picked.path = path;
    picked.skin = UserSkins::readFolder(path, name);
    if (!picked.skin.drawn()) {
        picked.code = QStringLiteral("unreadable");
        picked.message = QStringLiteral("%1 doesn't read (%2): omaseed check %1 says why.")
                             .arg(name, picked.skin.reason);
    }
    return picked;
}

QString savedChoice()
{
    const QString choice = core::StateFile::readSection(QStringLiteral("appearance"))
                               .value(QStringLiteral("skin"))
                               .toString();
    return choice.isEmpty() ? QStringLiteral("omarchy") : choice;
}

QByteArray list(const QString &skinsDir,
                const QString &choice,
                const QString &pinned,
                const QDateTime &at,
                const QString &omaseed)
{
    QJsonArray skins;
    for (const QString &name : SkinReader::builtInNames())
        skins << listed(name, QString(), SkinReader::builtIn(name));
    if (!skinsDir.isEmpty()) {
        const UserSkins folder(skinsDir);
        for (const UserSkin &skin : folder.list())
            skins << listed(skin.name, skin.path, skin.skin);
    }
    Fields fields = core::AgentAnswers::envelope(QStringLiteral("skin list"), at, omaseed);
    fields << std::pair{QStringLiteral("choice"), QJsonValue(choice)}
           << std::pair{QStringLiteral("pinned"), orNull(pinned)}
           << std::pair{QStringLiteral("skins"), QJsonValue(skins)};
    return AgentWords::orderedJson(fields);
}

QByteArray cloneFile(const Skin &source, const QString &from)
{
    // C37 U3: every token written out, so the person (or their agent) sees what each one is.
    const QVariantMap &omarchy = SkinReader::omarchyTokens();
    const auto group = [&](const QString &name) {
        QVariantMap merged = omarchy.value(name).toMap();
        const QVariantMap own = source.tokens.value(name).toMap();
        for (auto it = own.constBegin(); it != own.constEnd(); ++it)
            merged.insert(it.key(), it.value());
        return QJsonValue(QJsonObject::fromVariantMap(merged));
    };

    QList<std::pair<QString, QString>> members;
    const auto add = [&members](const QString &key, const QString &text) {
        members.append({key, text});
    };
    const auto value = [](const QJsonValue &v) { return pretty(v, 2); };

    add(QStringLiteral("$schema"), scalar(QStringLiteral("../../skin.schema.json")));
    add(QStringLiteral("version"), scalar(SkinReader::kVersion));
    add(QStringLiteral("cloned_from"),
        QStringLiteral("{ \"skin\": %1, \"version\": %2 }")
            .arg(scalar(from), scalar(source.version)));
    if (!source.description.isEmpty())
        add(QStringLiteral("description"), scalar(source.description));

    QJsonObject roles;
    for (auto it = source.roles.constBegin(); it != source.roles.constEnd(); ++it)
        roles.insert(it.key(), it.value());
    for (auto it = SkinReader::omarchyRoles().constBegin();
         it != SkinReader::omarchyRoles().constEnd();
         ++it)
        if (!roles.contains(it.key()))
            roles.insert(it.key(), it.value());
    add(QStringLiteral("roles"), value(roles));
    add(QStringLiteral("grounds"), groundsText(SkinReader::groundsOf(source), 2));
    for (const char *name : {"line", "cut", "tint"})
        add(QLatin1String(name), value(group(QLatin1String(name))));
    if (source.tokens.contains(QStringLiteral("radius")))
        add(QStringLiteral("radius"),
            scalar(QJsonValue::fromVariant(source.tokens.value(QStringLiteral("radius")))));
    for (const char *name : {"font", "label", "select", "plates", "meter"})
        add(QLatin1String(name), value(group(QLatin1String(name))));
    add(QStringLiteral("numerals"),
        scalar(
            source.tokens.value(QStringLiteral("numerals"), QStringLiteral("plain")).toString()));
    add(QStringLiteral("density"), value(group(QStringLiteral("density"))));
    // Glyphs and labels have no Omarchy value: they are names a skin adds (design 5).
    for (const char *name : {"glyphs", "labels"})
        if (source.tokens.contains(QLatin1String(name)))
            add(QLatin1String(name),
                value(
                    QJsonObject::fromVariantMap(source.tokens.value(QLatin1String(name)).toMap())));
    add(QStringLiteral("glow"), scalar(QStringLiteral("clean")));
    // C37 U2: a face names a built-in skin; Omarchy's for every seam the source leaves.
    QJsonObject faces =
        QJsonObject::fromVariantMap(source.tokens.value(QStringLiteral("faces")).toMap());
    for (const QString &seam : SkinReader::seams())
        if (!faces.contains(seam))
            faces.insert(seam, QStringLiteral("omarchy"));
    add(QStringLiteral("faces"), value(faces));

    QStringList lines;
    for (const auto &[key, text] : members)
        lines << pad(2) + scalar(key) + QStringLiteral(": ") + text;
    return (QStringLiteral("{\n") + lines.join(QStringLiteral(",\n")) + QStringLiteral("\n}\n"))
        .toUtf8();
}

Outcome clone(const QString &skinsDir,
              const QString &from,
              const QString &name,
              const QDateTime &at,
              const QString &omaseed)
{
    Outcome outcome;
    const Pick source = pick(skinsDir, from);
    if (!source.code.isEmpty()) {
        outcome.code = source.code;
        outcome.message = source.message;
        return outcome;
    }
    if (const QString problem = UserSkins::nameProblem(name); !problem.isEmpty()) {
        outcome.code = QStringLiteral("bad-name");
        outcome.message = QStringLiteral("%1 can't be a skin's name: %2.").arg(name, problem);
        return outcome;
    }
    // Never over what is there, nor beside a name that differs only in case (C37 U3).
    const QDir dir(skinsDir);
    QString taken;
    if (QFileInfo::exists(dir.filePath(name)))
        taken = dir.filePath(name);
    for (const QString &entry :
         dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        if (taken.isEmpty() && entry.compare(name, Qt::CaseInsensitive) == 0)
            taken = dir.filePath(entry);
    if (!taken.isEmpty()) {
        outcome.code = QStringLiteral("skin-exists");
        outcome.message = QStringLiteral("There's already a skin called %1, in %2. Pick another "
                                         "name, or remove that folder first.")
                              .arg(name, taken);
        return outcome;
    }

    const QString folder = dir.filePath(name);
    const QString path = folder + QStringLiteral("/skin.json");
    QSaveFile file(path);
    if (!QDir().mkpath(folder) || !file.open(QIODevice::WriteOnly) ||
        file.write(cloneFile(source.skin, from)) < 0 || !file.commit()) {
        outcome.code = QStringLiteral("not-written");
        outcome.message = QStringLiteral("%1 can't be written: %2.").arg(path, file.errorString());
        return outcome;
    }
    Fields fields = core::AgentAnswers::envelope(QStringLiteral("skin clone"), at, omaseed);
    fields << std::pair{QStringLiteral("skin"), QJsonValue(name)}
           << std::pair{QStringLiteral("path"), QJsonValue(path)}
           << std::pair{QStringLiteral("from"), QJsonValue(from)};
    outcome.json = AgentWords::orderedJson(fields);
    return outcome;
}

QByteArray
use(const QString &name, const Skin &skin, bool live, const QDateTime &at, const QString &omaseed)
{
    Fields fields = core::AgentAnswers::envelope(QStringLiteral("skin use"), at, omaseed);
    fields << std::pair{QStringLiteral("skin"),
                        QJsonValue(QJsonObject{
                            {QStringLiteral("name"), name},
                            {QStringLiteral("drawn"), skin.drawn()},
                            {QStringLiteral("reason"), orNull(skin.reason)},
                            {QStringLiteral("dropped"), static_cast<int>(skin.drops.size())}})}
           << std::pair{QStringLiteral("live"), QJsonValue(live)};
    return AgentWords::orderedJson(fields);
}

Outcome saveChoice(const QString &name)
{
    Outcome outcome;
    std::optional<QJsonObject> state = core::StateFile::readStrict();
    if (!state) {
        outcome.code = QStringLiteral("not-written");
        outcome.message = QStringLiteral(
            "state.json doesn't parse, so the choice isn't saved: omaseed would write over it. "
            "Open omaseed and pick the skin in Settings.");
        return outcome;
    }
    QJsonObject appearance = state->value(QStringLiteral("appearance")).toObject();
    appearance.insert(QStringLiteral("skin"), name);
    state->insert(QStringLiteral("appearance"), appearance);
    if (!core::StateFile::write(*state)) {
        outcome.code = QStringLiteral("not-written");
        outcome.message = QStringLiteral("%1 can't be written, so the choice isn't saved.")
                              .arg(core::StateFile::path());
    }
    return outcome;
}

QString lines(const QJsonObject &answer, const QString &home)
{
    const QString command = answer.value(QLatin1String("command")).toString();
    if (command == QLatin1String("skin list"))
        return listLines(answer, home);
    if (command == QLatin1String("skin clone")) {
        const QString name = answer.value(QLatin1String("skin")).toString();
        return QStringLiteral("%1: a copy of %2 in %3. omaseed check %1 draws it.\n")
            .arg(name,
                 answer.value(QLatin1String("from")).toString(),
                 shortPath(answer.value(QLatin1String("path")).toString(), home));
    }
    if (command == QLatin1String("skin use"))
        return useLines(answer);
    if (command == QLatin1String("check"))
        return checkLines(answer, home);
    return QString();
}

} // namespace omaseed::integration::SkinWords
