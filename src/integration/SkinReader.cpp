#include "integration/SkinReader.hpp"

#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <functional>
#include <limits>
#include <optional>

namespace omaseed::integration {

namespace {

/// What a token's value must be. Sizes are small integers in spec px (they go through
/// `Theme.space()`, so a skin never sets a size outside `base-size`), alphas and em
/// trackings fractions; the rest are names and words.
enum class Kind {
    Width,    ///< a line's width: an integer, 0 to 8 px
    Px,       ///< a cut or a radius: an integer, 0 to 64 px
    Count,    ///< how many of something (a grid's columns): an integer, 0 to 256
    Spec,     ///< a density: a number of spec px, 0 to 4000
    Alpha,    ///< 0 to 1
    Em,       ///< a tracking in em, 0 to 1
    Weight,   ///< a font weight, an integer from 100 to 900
    Bool,     ///< true or false
    LineRole, ///< a theme role, a skin role, or the skin's own `hair` or `faint` line
    Family,   ///< a font family; "" is fontconfig's monospace
    Case,     ///< "none" or "upper"
    Step,     ///< a step of the type scale: "caption", "small", "body" or "heading"
    Glyph,    ///< a glyph, `ms:<ligature>` or `icon:<name>`
    Word,     ///< a label's word
    Face,     ///< a built-in skin's name, whose face the seam draws (DECISIONS C37 U2)
};

/// Why a value isn't a token of `kind` (DECISIONS C37 U5): short English, one per kind.
QString whyNot(Kind kind)
{
    switch (kind) {
    case Kind::Width:
        return QStringLiteral("not a whole number from 0 to 8");
    case Kind::Px:
        return QStringLiteral("not a whole number from 0 to 64");
    case Kind::Count:
        return QStringLiteral("not a whole number from 0 to 256");
    case Kind::Spec:
        return QStringLiteral("not a number from 0 to 4000");
    case Kind::Alpha:
    case Kind::Em:
        return QStringLiteral("not between 0 and 1");
    case Kind::Weight:
        return QStringLiteral("not a weight from 100 to 900");
    case Kind::Bool:
        return QStringLiteral("not true or false");
    case Kind::LineRole:
        return QStringLiteral("not a theme role, a skin role, hair or faint");
    case Kind::Family:
        return QStringLiteral("not a font family");
    case Kind::Case:
        return QStringLiteral("not none or upper");
    case Kind::Step:
        return QStringLiteral("not caption, small, body or heading");
    case Kind::Glyph:
        return QStringLiteral("not a glyph (ms:name or icon:name)");
    case Kind::Word:
        return QStringLiteral("not a word of 1 to 32 characters");
    case Kind::Face:
        return QStringLiteral("names a built-in skin (omarchy, console), never a file");
    }
    return {};
}

QString unknownToken()
{
    return QStringLiteral("not a token omaseed knows");
}

/// The groups whose keys are fixed, and each key's kind (the design note's "The shape").
const QHash<QString, QHash<QString, Kind>> &fixedGroups()
{
    static const QHash<QString, QHash<QString, Kind>> groups{
        {QStringLiteral("line"),
         {{QStringLiteral("hair"), Kind::Width},
          {QStringLiteral("lit"), Kind::Width},
          {QStringLiteral("edge"), Kind::Width},
          {QStringLiteral("menuEdge"), Kind::Width},
          {QStringLiteral("hairRole"), Kind::LineRole},
          {QStringLiteral("litRole"), Kind::LineRole},
          {QStringLiteral("edgeRole"), Kind::LineRole},
          {QStringLiteral("menuEdgeRole"), Kind::LineRole},
          {QStringLiteral("hairAlpha"), Kind::Alpha},
          {QStringLiteral("faintAlpha"), Kind::Alpha},
          // A cover's edge while it is selected or needs you (#171 step 6; board 46
          // `.card.sel`, `.card.needs`: the plate's own 1 px edge in the accent).
          {QStringLiteral("card"), Kind::Width},
          // The current tab's underline is the lit line (board 46 `.tab.on`: 2 px of the
          // accent) rather than a 1 px bright one; a cover that needs you takes the card's
          // edge in the accent and its fact in the accent (`.card.needs`).
          {QStringLiteral("tabLit"), Kind::Bool},
          {QStringLiteral("needsEdge"), Kind::Bool}}},
        {QStringLiteral("cut"),
         {{QStringLiteral("panel"), Kind::Px}, {QStringLiteral("key"), Kind::Px}}},
        // A kind's own tint under its words (#171 step 5): a notice's ground is its kind's
        // colour at this alpha over the window (board 46 `.notice .pi`); 0 is the idle fill.
        // A block's ground inside a view (`.blk .pi`): its kind's colour at this alpha over
        // the panel, when the blocks are plated.
        {QStringLiteral("tint"),
         {{QStringLiteral("notice"), Kind::Alpha}, {QStringLiteral("block"), Kind::Alpha}}},
        {QStringLiteral("font"),
         {{QStringLiteral("label"), Kind::Family},
          {QStringLiteral("value"), Kind::Family},
          {QStringLiteral("kana"), Kind::Family},
          {QStringLiteral("icons"), Kind::Family}}},
        {QStringLiteral("label"),
         {{QStringLiteral("case"), Kind::Case},
          {QStringLiteral("track"), Kind::Em},
          {QStringLiteral("trackTitle"), Kind::Em},
          {QStringLiteral("weight"), Kind::Weight},
          {QStringLiteral("kana"), Kind::Bool},
          // The other voices (#171 step 5): a heading's (the title bar's title, a sheet's and
          // a menu's head), a control's words (a button, a tool), the status line's; and
          // whether a strip or band names its outcome by a word instead of a sign.
          {QStringLiteral("titleCase"), Kind::Case},
          {QStringLiteral("titleWeight"), Kind::Weight},
          {QStringLiteral("controlCase"), Kind::Case},
          {QStringLiteral("controlTrack"), Kind::Em},
          {QStringLiteral("controlWeight"), Kind::Weight},
          {QStringLiteral("controlSize"), Kind::Step},
          {QStringLiteral("statusCase"), Kind::Case},
          {QStringLiteral("statusTrack"), Kind::Em},
          {QStringLiteral("tags"), Kind::Bool},
          // The Views' voices (#171 step 6): a thing's name (a rail row, a table's name
          // cell, a strip's words: `.ri`, `.tbl td.nm2`), the rows' names' case (`.row .nm`),
          // and the small captions over or beside a value (`.row .fact span`, `.tbl th`,
          // `.kv dt`, `.tab`).
          {QStringLiteral("nameTrack"), Kind::Em},
          {QStringLiteral("nameWeight"), Kind::Weight},
          {QStringLiteral("rowCase"), Kind::Case},
          {QStringLiteral("captionCase"), Kind::Case},
          {QStringLiteral("captionTrack"), Kind::Em},
          {QStringLiteral("captionWeight"), Kind::Weight},
          // The table's status word in the caption voice (`.verb`), the rows' fact at a step
          // of the type scale (`.row .fact b`), and the rows' fix as a filled accent tag
          // (`.row .fix`) rather than an outlined one.
          {QStringLiteral("verb"), Kind::Bool},
          {QStringLiteral("rowFactSize"), Kind::Step},
          {QStringLiteral("fixTag"), Kind::Bool}}},
        // The details' pieces bar as a grid of this many columns and at most this many rows
        // (board 46 `.pmap`: 34 × 7); 0 columns draws it as a line.
        {QStringLiteral("meter"),
         {{QStringLiteral("piecesColumns"), Kind::Count},
          {QStringLiteral("piecesRows"), Kind::Count}}},
        // What marks the cursor's row and a picked one (#171 step 6; board 46 `.row.sel`,
        // `.row.picked`): the SelectionMark on the cursor instead of a hairline round it,
        // and the `picked` ground instead of the selected ground with a rule.
        // Whether the blocks inside a view stand on their own plates (#171 step 6; board 46
        // `.dhero.pn`, `.blk`, board 49 `.rtnote`): Omarchy's sit on the view's ground.
        {QStringLiteral("plates"), {{QStringLiteral("blocks"), Kind::Bool}}},
        {QStringLiteral("select"),
         {{QStringLiteral("cursorMark"), Kind::Bool},
          {QStringLiteral("pickedGround"), Kind::Bool}}},
    };
    return groups;
}

/// The groups keyed by any name (a Theme.qml token, a Glyphs.qml name, a label id), and the
/// kind of every value in them.
const QHash<QString, Kind> &openGroups()
{
    static const QHash<QString, Kind> groups{
        {QStringLiteral("density"), Kind::Spec},
        {QStringLiteral("glyphs"), Kind::Glyph},
        {QStringLiteral("labels"), Kind::Word},
    };
    return groups;
}

bool isName(const QString &key)
{
    static const QRegularExpression name(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{0,63}$"));
    return name.match(key).hasMatch();
}

bool hasControl(const QString &text)
{
    for (const QChar c : text)
        if (c.category() == QChar::Other_Control)
            return true;
    return false;
}

bool isInteger(const QJsonValue &value, int low, int high)
{
    if (!value.isDouble())
        return false;
    const double v = value.toDouble();
    return std::isfinite(v) && std::floor(v) == v && v >= low && v <= high;
}

bool isNumberIn(const QJsonValue &value, double low, double high)
{
    if (!value.isDouble())
        return false;
    const double v = value.toDouble();
    return std::isfinite(v) && v >= low && v <= high;
}

bool isSkinRole(const QString &name)
{
    return SkinReader::omarchyRoles().contains(name);
}

/// A colour a ground or a line may name: a theme role, or one of the skin's roles.
bool isTint(const QString &name)
{
    return SkinReader::themeRoles().contains(name) || isSkinRole(name);
}

/// `value` as a token of `kind`, or an invalid QVariant when it isn't one.
QVariant check(const QJsonValue &value, Kind kind)
{
    switch (kind) {
    case Kind::Width:
        return isInteger(value, 0, 8) ? QVariant(value.toInt()) : QVariant();
    case Kind::Px:
        return isInteger(value, 0, 64) ? QVariant(value.toInt()) : QVariant();
    case Kind::Count:
        return isInteger(value, 0, 256) ? QVariant(value.toInt()) : QVariant();
    case Kind::Spec:
        return isNumberIn(value, 0, 4000) ? QVariant(value.toDouble()) : QVariant();
    case Kind::Alpha:
    case Kind::Em:
        return isNumberIn(value, 0, 1) ? QVariant(value.toDouble()) : QVariant();
    case Kind::Weight:
        return isInteger(value, 100, 900) ? QVariant(value.toInt()) : QVariant();
    case Kind::Bool:
        return value.isBool() ? QVariant(value.toBool()) : QVariant();
    case Kind::LineRole: {
        const QString role = value.toString();
        const bool ok = value.isString() && (isTint(role) || role == QLatin1String("hair") ||
                                             role == QLatin1String("faint"));
        return ok ? QVariant(role) : QVariant();
    }
    case Kind::Family: {
        const QString family = value.toString();
        return value.isString() && family.size() <= 128 && !hasControl(family) ? QVariant(family)
                                                                               : QVariant();
    }
    case Kind::Case: {
        const QString text = value.toString();
        return text == QLatin1String("none") || text == QLatin1String("upper") ? QVariant(text)
                                                                               : QVariant();
    }
    case Kind::Step: {
        const QString text = value.toString();
        return text == QLatin1String("caption") || text == QLatin1String("small") ||
                       text == QLatin1String("body") || text == QLatin1String("heading")
                   ? QVariant(text)
                   : QVariant();
    }
    case Kind::Glyph: {
        static const QRegularExpression ligature(QStringLiteral("^ms:[a-z0-9_]+$"));
        const QString glyph = value.toString();
        if (!value.isString() || glyph.isEmpty() || glyph.size() > 64 || hasControl(glyph))
            return {};
        if (glyph.startsWith(QLatin1String("ms:")) && !ligature.match(glyph).hasMatch())
            return {};
        if (glyph == QLatin1String("icon:"))
            return {};
        return glyph;
    }
    case Kind::Word: {
        const QString word = value.toString();
        return value.isString() && !word.isEmpty() && word.size() <= 32 && !hasControl(word)
                   ? QVariant(word)
                   : QVariant();
    }
    case Kind::Face: {
        // C37 U2: a face names a built-in skin, so no file a skin carries is ever loaded.
        const QString face = value.toString();
        return value.isString() && SkinReader::builtInNames().contains(face) ? QVariant(face)
                                                                             : QVariant();
    }
    }
    return {};
}

/// A ground's recipe from the file, or nothing when it is malformed, with `why` said.
std::optional<SkinGround> groundFrom(const QString &name, const QJsonValue &value, QString *why)
{
    const auto fail = [why](const char *text) {
        *why = QLatin1String(text);
        return std::nullopt;
    };
    if (!value.isObject())
        return fail("not a recipe: {tint, alpha, over}");
    const QJsonObject recipe = value.toObject();
    for (auto it = recipe.constBegin(); it != recipe.constEnd(); ++it)
        if (it.key() != QLatin1String("tint") && it.key() != QLatin1String("alpha") &&
            it.key() != QLatin1String("over"))
            return fail("a recipe takes only tint, alpha and over");
    SkinGround ground;
    ground.tint = recipe.value(QLatin1String("tint")).toString();
    if (!isTint(ground.tint))
        return fail("its tint is not a theme role or a skin role");
    const QJsonValue alpha = recipe.value(QLatin1String("alpha"));
    if (alpha.toString() == QLatin1String("shell"))
        ground.shellAlpha = true;
    else if (isNumberIn(alpha, 0, 1))
        ground.alpha = alpha.toDouble();
    else
        return fail("its alpha is not between 0 and 1, nor shell");
    const QJsonValue over = recipe.value(QLatin1String("over"));
    if (!over.isUndefined()) {
        ground.over = over.toString();
        if (ground.over == name)
            return fail("a ground painted over itself");
        const bool known = ground.over == QLatin1String("window") ||
                           SkinReader::groundNames().contains(ground.over);
        if (!known)
            return fail("it is painted over a ground omaseed doesn't know");
    }
    return ground;
}

/// Omarchy's skin: no overrides, with the reason it stands in for another.
Skin omarchy(const QString &reason, int version = 0)
{
    Skin skin;
    skin.reason = reason;
    skin.version = version;
    skin.roles = SkinReader::omarchyRoles();
    return skin;
}

} // namespace

QStringList SkinReader::builtInNames()
{
    return {QStringLiteral("omarchy"), QStringLiteral("console")};
}

QString SkinReader::builtInPath(const QString &name)
{
    if (!builtInNames().contains(name))
        return {};
    return QStringLiteral(":/omaseed/skins/%1/skin.json").arg(name);
}

Skin SkinReader::builtIn(const QString &name)
{
    const QString path = builtInPath(name);
    if (path.isEmpty())
        return omarchy(QStringLiteral("unknown skin"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return omarchy(QStringLiteral("missing"));
    return parse(file.readAll());
}

QString SkinReader::requestedName()
{
    const QString name = qEnvironmentVariable("OMASEED_SKIN").trimmed();
    return name.isEmpty() ? QStringLiteral("omarchy") : name;
}

Skin SkinReader::parse(const QByteArray &json, int appVersion)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return omarchy(QStringLiteral("unparseable"));
    const QJsonObject root = document.object();

    // The versioning rule (omarchy-console's kitReads): one positive integer, and a file newer
    // than this omaseed is drawn as Omarchy, not half-read.
    const QJsonValue version = root.value(QLatin1String("version"));
    if (!isInteger(version, 1, std::numeric_limits<int>::max()))
        return omarchy(QStringLiteral("no version"));
    if (version.toInt() > appVersion)
        return omarchy(QStringLiteral("newer"), version.toInt());

    Skin skin = omarchy(QString(), version.toInt());
    const QJsonValue name = root.value(QLatin1String("skin"));
    skin.name =
        name.isString() && isName(name.toString()) ? name.toString() : QStringLiteral("unnamed");
    // C37 U5: what is dropped is named with its value and why, in the order met.
    const auto drop = [&skin](const QString &token, const QJsonValue &value, const QString &why) {
        skin.dropped << token;
        skin.drops << SkinDrop{token, value, why};
    };

    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QString group = it.key();
        const QJsonValue value = it.value();
        if (group == QLatin1String("skin") || group == QLatin1String("version"))
            continue;

        // C37 U1, U3: a user skin's caption, where it was cloned from, and its schema's path
        // for an editor (read by nobody here).
        if (group == QLatin1String("$schema"))
            continue;
        if (group == QLatin1String("description")) {
            const QString text = value.toString();
            if (value.isString() && !text.isEmpty() && text.size() <= 120 && !hasControl(text))
                skin.description = text;
            else
                drop(group, value, QStringLiteral("not a description of 1 to 120 characters"));
            continue;
        }
        if (group == QLatin1String("cloned_from")) {
            const QJsonObject from = value.toObject();
            const QJsonValue fromSkin = from.value(QLatin1String("skin"));
            const QJsonValue fromVersion = from.value(QLatin1String("version"));
            if (value.isObject() && from.size() == 2 && fromSkin.isString() &&
                !fromSkin.toString().isEmpty() &&
                isInteger(fromVersion, 1, std::numeric_limits<int>::max()))
                skin.clonedFrom = SkinOrigin{fromSkin.toString(), fromVersion.toInt()};
            else
                drop(group, value, QStringLiteral("not {\"skin\": a name, \"version\": a number}"));
            continue;
        }

        if (group == QLatin1String("radius")) {
            if (value.toString() == QLatin1String("hyprland"))
                skin.tokens.insert(group, value.toString());
            else if (const QVariant px = check(value, Kind::Px); px.isValid())
                skin.tokens.insert(group, px);
            else
                drop(group, value, QStringLiteral("not a whole number from 0 to 64, nor hyprland"));
            continue;
        }
        if (group == QLatin1String("numerals")) {
            const QString text = value.toString();
            if (text == QLatin1String("pad") || text == QLatin1String("plain"))
                skin.tokens.insert(group, text);
            else
                drop(group, value, QStringLiteral("not pad or plain"));
            continue;
        }
        if (group == QLatin1String("glow")) {
            // C24 H12: clean glow only. Any other value is held at "clean", never drawn.
            skin.tokens.insert(group, QStringLiteral("clean"));
            if (value.toString() != QLatin1String("clean"))
                drop(group, value, QStringLiteral("held at clean (C24 H12)"));
            continue;
        }

        const auto fixed = fixedGroups().constFind(group);
        const auto open = openGroups().constFind(group);
        const bool faces = group == QLatin1String("faces");
        const bool known = fixed != fixedGroups().constEnd() || open != openGroups().constEnd() ||
                           faces || group == QLatin1String("roles") ||
                           group == QLatin1String("grounds");
        if (!known) {
            drop(group, value, unknownToken());
            continue;
        }
        if (!value.isObject()) {
            drop(group, value, QStringLiteral("not a group: an object of its tokens"));
            continue;
        }
        const QJsonObject members = value.toObject();

        if (group == QLatin1String("roles")) {
            for (auto m = members.constBegin(); m != members.constEnd(); ++m) {
                const QString path = group + QLatin1Char('.') + m.key();
                const QString role = m.value().toString();
                if (!isSkinRole(m.key()))
                    drop(path, m.value(), unknownToken());
                else if (!m.value().isString() || !themeRoles().contains(role))
                    drop(path, m.value(), QStringLiteral("not a theme role"));
                else
                    skin.roles.insert(m.key(), role);
            }
            continue;
        }
        if (group == QLatin1String("grounds")) {
            for (auto m = members.constBegin(); m != members.constEnd(); ++m) {
                const QString path = group + QLatin1Char('.') + m.key();
                if (!groundNames().contains(m.key())) {
                    drop(path, m.value(), unknownToken());
                    continue;
                }
                QString why;
                if (const auto ground = groundFrom(m.key(), m.value(), &why))
                    skin.grounds.insert(m.key(), *ground);
                else
                    drop(path, m.value(), why);
            }
            continue;
        }

        QVariantMap read;
        for (auto m = members.constBegin(); m != members.constEnd(); ++m) {
            std::optional<Kind> kind;
            if (fixed != fixedGroups().constEnd()) {
                if (const auto k = fixed->constFind(m.key()); k != fixed->constEnd())
                    kind = *k;
            } else if (faces) {
                if (seams().contains(m.key()))
                    kind = Kind::Face;
            } else if (isName(m.key())) {
                kind = *open;
            }
            const QString path = group + QLatin1Char('.') + m.key();
            if (!kind) {
                drop(path, m.value(), unknownToken());
                continue;
            }
            if (const QVariant token = check(m.value(), *kind); token.isValid())
                read.insert(m.key(), token);
            else
                drop(path, m.value(), whyNot(*kind));
        }
        if (!read.isEmpty())
            skin.tokens.insert(group, read);
    }
    return skin;
}

const QHash<QString, SkinGround> &SkinReader::omarchyGrounds()
{
    // Each as Theme paints it without a skin, its alpha the shell's (C30 F5, F7). `panel` is
    // a plate's ground (a sheet, a menu): the window's own in Omarchy's skin. `picked` is a
    // torrent picked among several: board 46's `.sk-oma .row.picked`, the accent at 9 %.
    static const QHash<QString, SkinGround> grounds = [] {
        const auto shell = [](const char *tint, const char *over = "window") {
            return SkinGround{QLatin1String(tint), 0, true, QLatin1String(over)};
        };
        return QHash<QString, SkinGround>{
            {QStringLiteral("idle"), shell("foreground")},
            {QStringLiteral("hover"), shell("foreground")},
            {QStringLiteral("cursor"), shell("foreground")},
            {QStringLiteral("selected"), shell("foreground")},
            {QStringLiteral("selectedCursor"), shell("foreground")},
            {QStringLiteral("pressed"), shell("foreground")},
            {QStringLiteral("warning"), shell("yellow")},
            {QStringLiteral("attention"), shell("red")},
            {QStringLiteral("danger"), shell("red")},
            {QStringLiteral("ticked"), shell("accent")},
            {QStringLiteral("accentIdle"), shell("accent")},
            {QStringLiteral("blockIdle"), shell("foreground", "idle")},
            {QStringLiteral("blockHover"), shell("foreground", "idle")},
            {QStringLiteral("blockSelected"), shell("foreground", "idle")},
            {QStringLiteral("blockSelectedCursor"), shell("foreground", "idle")},
            {QStringLiteral("blockPressed"), shell("foreground", "idle")},
            {QStringLiteral("highlight"), shell("accent", "idle")},
            {QStringLiteral("hoverIdle"), shell("foreground", "hover")},
            {QStringLiteral("hoverHover"), shell("foreground", "hover")},
            {QStringLiteral("hoverSelected"), shell("foreground", "hover")},
            {QStringLiteral("hoverPressed"), shell("foreground", "hover")},
            {QStringLiteral("panel"), shell("foreground")},
            {QStringLiteral("picked"), shell("accent")},
            {QStringLiteral("turtle"), shell("yellow")},
            {QStringLiteral("burst"), shell("green")},
        };
    }();
    return grounds;
}

const QStringList &SkinReader::groundNames()
{
    static const QStringList names{
        QStringLiteral("idle"),
        QStringLiteral("hover"),
        QStringLiteral("cursor"),
        QStringLiteral("selected"),
        QStringLiteral("selectedCursor"),
        QStringLiteral("pressed"),
        QStringLiteral("warning"),
        QStringLiteral("attention"),
        QStringLiteral("danger"),
        QStringLiteral("ticked"),
        QStringLiteral("accentIdle"),
        QStringLiteral("blockIdle"),
        QStringLiteral("blockHover"),
        QStringLiteral("blockSelected"),
        QStringLiteral("blockSelectedCursor"),
        QStringLiteral("blockPressed"),
        QStringLiteral("highlight"),
        QStringLiteral("hoverIdle"),
        QStringLiteral("hoverHover"),
        QStringLiteral("hoverSelected"),
        QStringLiteral("hoverPressed"),
        QStringLiteral("panel"),
        QStringLiteral("picked"),
        QStringLiteral("turtle"),
        QStringLiteral("burst"),
    };
    return names;
}

const QStringList &SkinReader::themeRoles()
{
    static const QStringList roles{
        QStringLiteral("background"),
        QStringLiteral("foreground"),
        QStringLiteral("dimForeground"),
        QStringLiteral("brightForeground"),
        QStringLiteral("accent"),
        QStringLiteral("muted"),
        QStringLiteral("selection"),
        QStringLiteral("red"),
        QStringLiteral("yellow"),
        QStringLiteral("orange"),
        QStringLiteral("green"),
        QStringLiteral("cyan"),
        QStringLiteral("blue"),
        QStringLiteral("magenta"),
        QStringLiteral("brown"),
        QStringLiteral("darkBackground"),
        QStringLiteral("darkerBackground"),
        QStringLiteral("lighterBackground"),
    };
    return roles;
}

const QHash<QString, QString> &SkinReader::omarchyRoles()
{
    // omarchy-console's BASELINE roles, which are also what omaseed paints today: the accent
    // for what is selected or needs you, cyan for data, red for failure, yellow for a warning.
    static const QHash<QString, QString> roles{
        {QStringLiteral("accent"), QStringLiteral("accent")},
        {QStringLiteral("signal"), QStringLiteral("cyan")},
        {QStringLiteral("alert"), QStringLiteral("red")},
        {QStringLiteral("warn"), QStringLiteral("yellow")},
    };
    return roles;
}

const QStringList &SkinReader::seams()
{
    static const QStringList seams{QStringLiteral("Plate"),
                                   QStringLiteral("SelectionMark"),
                                   QStringLiteral("Meter"),
                                   QStringLiteral("Lamp")};
    return seams;
}

const QVariantMap &SkinReader::omarchyTokens()
{
    // The QML's fallbacks (Theme.qml's token() and n() calls, Glyphs.qml aside), written out
    // once (DECISIONS C37 U3). A weight is Qt's (Font.Normal 400, Font.DemiBold 600); a family
    // "" is Theme.fontFamily. tst_skintokens fails when one drifts from the QML.
    static const QVariantMap tokens = [] {
        const auto group = [](std::initializer_list<std::pair<const char *, QVariant>> keys) {
            QVariantMap map;
            for (const auto &[key, value] : keys)
                map.insert(QLatin1String(key), value);
            return map;
        };
        const QString none = QStringLiteral("none");
        const QString body = QStringLiteral("body");
        const QString accent = QStringLiteral("accent");
        return QVariantMap{
            {QStringLiteral("line"),
             group({{"hair", 1},
                    {"lit", 2},
                    {"edge", 1},
                    {"menuEdge", 1},
                    {"hairRole", QStringLiteral("foreground")},
                    {"litRole", accent},
                    {"edgeRole", accent},
                    {"menuEdgeRole", accent},
                    {"tabLit", false},
                    {"needsEdge", false}})},
            {QStringLiteral("cut"), group({{"panel", 0}, {"key", 0}})},
            {QStringLiteral("tint"), group({{"notice", 0.0}, {"block", 0.0}})},
            {QStringLiteral("font"),
             group({{"label", QString()},
                    {"value", QString()},
                    {"kana", QString()},
                    {"icons", QString()}})},
            {QStringLiteral("label"),
             group({{"case", QStringLiteral("upper")},
                    {"trackTitle", 0.0},
                    {"weight", 400},
                    {"kana", false},
                    {"titleCase", none},
                    {"titleWeight", 600},
                    {"controlCase", none},
                    {"controlTrack", 0.0},
                    {"controlWeight", 400},
                    {"controlSize", body},
                    {"statusCase", none},
                    {"statusTrack", 0.0},
                    {"tags", false},
                    {"nameTrack", 0.0},
                    {"nameWeight", 400},
                    {"rowCase", none},
                    {"captionCase", none},
                    {"verb", false},
                    {"rowFactSize", body},
                    {"fixTag", false}})},
            {QStringLiteral("meter"), group({{"piecesColumns", 0}, {"piecesRows", 1}})},
            {QStringLiteral("plates"), group({{"blocks", false}})},
            {QStringLiteral("select"), group({{"cursorMark", false}, {"pickedGround", false}})},
            {QStringLiteral("density"),
             [] {
                 static const std::pair<QString, double> names[] = {
                     {QStringLiteral("actionBarButtonSize"), 30},
                     {QStringLiteral("actionBarCountEnd"), 12},
                     {QStringLiteral("actionBarCountStart"), 8},
                     {QStringLiteral("actionBarGap"), 1},
                     {QStringLiteral("actionBarLift"), 12},
                     {QStringLiteral("actionBarPadding"), 4},
                     {QStringLiteral("actionBarRuleInset"), 4},
                     {QStringLiteral("actionBarStripGap"), 8},
                     {QStringLiteral("actionBarWordPadding"), 9},
                     {QStringLiteral("addMenuWidth"), 330},
                     {QStringLiteral("appMenuWidth"), 292},
                     {QStringLiteral("createChipGap"), 8},
                     {QStringLiteral("createChipPaddingX"), 10},
                     {QStringLiteral("createChipPaddingY"), 3},
                     {QStringLiteral("createChipsGap"), 6},
                     {QStringLiteral("createChipTop"), 4},
                     {QStringLiteral("createFactGap"), 8},
                     {QStringLiteral("createFactMark"), 16},
                     {QStringLiteral("createFactsGap"), 6},
                     {QStringLiteral("createLinkReach"), 4},
                     {QStringLiteral("createNoteGap"), 10},
                     {QStringLiteral("createNotePadding"), 9},
                     {QStringLiteral("createPairGap"), 16},
                     {QStringLiteral("createSheetWidth"), 640},
                     {QStringLiteral("detailsBodyGap"), 12},
                     {QStringLiteral("detailsBodyInset"), 16},
                     {QStringLiteral("detailsBodyPaddingTop"), 12},
                     {QStringLiteral("detailsCaptionGap"), 8},
                     {QStringLiteral("detailsEventGap"), 8},
                     {QStringLiteral("detailsEventMarkSize"), 7},
                     {QStringLiteral("detailsEventMarkTop"), 5},
                     {QStringLiteral("detailsEventMarkWidth"), 10},
                     {QStringLiteral("detailsEventSpacing"), 7},
                     {QStringLiteral("detailsEventTimeWidth"), 70},
                     {QStringLiteral("detailsHeadingGap"), 6},
                     {QStringLiteral("detailsHeroGap"), 3},
                     {QStringLiteral("detailsHeroInset"), 16},
                     {QStringLiteral("detailsHeroMarginBottom"), 0},
                     {QStringLiteral("detailsHeroMarginSide"), 0},
                     {QStringLiteral("detailsHeroMarginTop"), 0},
                     {QStringLiteral("detailsHeroPaddingBottom"), 12},
                     {QStringLiteral("detailsHeroPaddingTop"), 8},
                     {QStringLiteral("displayMenuWidth"), 240},
                     {QStringLiteral("keyBadgeEdge"), 3},
                     {QStringLiteral("keyBadgeHeight"), 14},
                     {QStringLiteral("keyBadgeOverhangX"), 6},
                     {QStringLiteral("keyBadgeOverhangY"), 9},
                     {QStringLiteral("keyBadgePadding"), 4},
                     {QStringLiteral("keyBadgeRowGap"), 6},
                     {QStringLiteral("libraryGroupHeadBar"), 0},
                     {QStringLiteral("libraryGroupHeadBarHeight"), 11},
                     {QStringLiteral("libraryGroupHeadBarInset"), 8},
                     {QStringLiteral("libraryGroupHeadHeight"), 30},
                     {QStringLiteral("libraryGroupHeadInset"), 16},
                     {QStringLiteral("libraryGroupHeadPaddingBottom"), 4},
                     {QStringLiteral("libraryMenuInset"), 24},
                     {QStringLiteral("libraryRenamePadding"), 6},
                     {QStringLiteral("libraryRenameRise"), 3},
                     {QStringLiteral("libraryRowFactWidth"), 85},
                     {QStringLiteral("libraryRowFixPadding"), 6},
                     {QStringLiteral("libraryRowGap"), 9},
                     {QStringLiteral("libraryRowGlyphWidth"), 18},
                     {QStringLiteral("libraryRowHeight"), 46},
                     {QStringLiteral("libraryRowInsetEnd"), 16},
                     {QStringLiteral("libraryRowInsetStart"), 13},
                     {QStringLiteral("libraryRowLineGap"), 1},
                     {QStringLiteral("libraryRowLineThickness"), 4},
                     {QStringLiteral("libraryRowLineWidth"), 155},
                     {QStringLiteral("libraryRowLineWidthNarrow"), 90},
                     {QStringLiteral("libraryRowNameFloor"), 80},
                     {QStringLiteral("libraryRowSelectedEdge"), 2},
                     {QStringLiteral("libraryRowWordsFloor"), 24},
                     {QStringLiteral("menuHeadInset"), 14},
                     {QStringLiteral("menuHeadLineGap"), 1},
                     {QStringLiteral("menuHeadPadding"), 4},
                     {QStringLiteral("meterCellWidth"), 3},
                     {QStringLiteral("noticeBandInsetEnd"), 0},
                     {QStringLiteral("noticeBandInsetStart"), 0},
                     {QStringLiteral("noticeBandInsetTop"), 0},
                     {QStringLiteral("outcomeStripLift"), 12},
                     {QStringLiteral("outcomeStripReasonGap"), 1},
                     {QStringLiteral("piecesCellMax"), 10},
                     {QStringLiteral("progressLineThickness"), 2},
                     {QStringLiteral("railCurrentEdge"), 2},
                     {QStringLiteral("railFoldedWidth"), 46},
                     {QStringLiteral("railHeadHeight"), 46},
                     {QStringLiteral("railMarkGap"), 9},
                     {QStringLiteral("railMarkInset"), 14},
                     {QStringLiteral("railMarkSize"), 20},
                     {QStringLiteral("railMenuGap"), 4},
                     {QStringLiteral("railTipGap"), 2},
                     {QStringLiteral("railViewsPadding"), 8},
                     {QStringLiteral("railWidth"), 200},
                     {QStringLiteral("searchAnswersWidth"), 392},
                     {QStringLiteral("searchBarGap"), 9},
                     {QStringLiteral("searchBarInset"), 16},
                     {QStringLiteral("searchBarInsetBottom"), 7},
                     {QStringLiteral("searchBarInsetTop"), 11},
                     {QStringLiteral("searchEmptyGap"), 7},
                     {QStringLiteral("searchEmptyTop"), 37},
                     {QStringLiteral("searchEmptyWidth"), 410},
                     {QStringLiteral("searchFieldMinWidth"), 185},
                     {QStringLiteral("searchFieldNarrowWidth"), 110},
                     {QStringLiteral("searchFoldWidth"), 720},
                     {QStringLiteral("searchLineGap"), 11},
                     {QStringLiteral("searchLineInsetBottom"), 6},
                     {QStringLiteral("searchMenuWidth"), 240},
                     {QStringLiteral("searchNoteInset"), 4},
                     {QStringLiteral("searchPaneGap"), 9},
                     {QStringLiteral("searchPaneInset"), 16},
                     {QStringLiteral("searchPaneWidth"), 380},
                     {QStringLiteral("searchPillDot"), 6},
                     {QStringLiteral("searchPillGap"), 6},
                     {QStringLiteral("searchPillMinWidth"), 84},
                     {QStringLiteral("searchPluginGap"), 9},
                     {QStringLiteral("searchPluginInset"), 10},
                     {QStringLiteral("searchPluginNameWidth"), 110},
                     {QStringLiteral("searchPluginRowHeight"), 36},
                     {QStringLiteral("searchPluginVersionWidth"), 40},
                     {QStringLiteral("selectionMarkArm"), 8},
                     {QStringLiteral("sheetBodyPadding"), 12},
                     {QStringLiteral("sheetButtonGap"), 8},
                     {QStringLiteral("sheetButtonMinWidth"), 64},
                     {QStringLiteral("sheetButtonPadding"), 12},
                     {QStringLiteral("sheetFootPadding"), 10},
                     {QStringLiteral("sheetHeadGap"), 10},
                     {QStringLiteral("sheetHeadPadding"), 12},
                     {QStringLiteral("sheetInset"), 16},
                     {QStringLiteral("sheetMargin"), 16},
                     {QStringLiteral("sheetRowGap"), 12},
                     {QStringLiteral("spaceChipGap"), 6},
                     {QStringLiteral("spaceChipMaxWidth"), 240},
                     {QStringLiteral("spaceChipPadX"), 7},
                     {QStringLiteral("spaceChipPadY"), 1},
                     {QStringLiteral("spaceFolderGap"), 6},
                     {QStringLiteral("spaceFolderHead"), 17},
                     {QStringLiteral("spaceGap"), 8},
                     {QStringLiteral("spaceInset"), 18},
                     {QStringLiteral("spaceInsetBottom"), 14},
                     {QStringLiteral("spaceInsetTop"), 10},
                     {QStringLiteral("spaceLineGap"), 12},
                     {QStringLiteral("spaceTileFactHeight"), 50},
                     {QStringLiteral("spaceTilePadX"), 8},
                     {QStringLiteral("spaceTilePadY"), 6},
                     {QStringLiteral("spaceTileWordsWidth"), 70},
                     {QStringLiteral("statusLineDotSize"), 7},
                     {QStringLiteral("statusLineGap"), 11},
                     {QStringLiteral("statusLineHoverInset"), 3},
                     {QStringLiteral("statusLineInsetEnd"), 13},
                     {QStringLiteral("statusLineInsetStart"), 16},
                     {QStringLiteral("statusLineLaneGap"), 6},
                     {QStringLiteral("tableNameFloor"), 80},
                     {QStringLiteral("titleBarFindWidth"), 164},
                     {QStringLiteral("titleBarGap"), 11},
                     {QStringLiteral("titleBarHeight"), 46},
                     {QStringLiteral("titleBarInsetEnd"), 13},
                     {QStringLiteral("titleBarInsetStart"), 16},
                     {QStringLiteral("titleBarLineGap"), 1},
                     {QStringLiteral("titleBarSparkHeight"), 12},
                     {QStringLiteral("titleBarSparkWidth"), 72},
                 };
                 QVariantMap map;
                 for (const auto &[name, px] : names)
                     map.insert(name, px);
                 return map;
             }()},
        };
    }();
    return tokens;
}

const QStringList &SkinReader::contextualTokens()
{
    // Each is drawn from a value of the place that reads it: the lit line's width, the
    // hairline's and the row rule's alpha, a caption's own tracking and weight, and two
    // trackings for a label (0.08 for a caption, 0.12 for a library group's head).
    static const QStringList paths{
        QStringLiteral("line.card"),
        QStringLiteral("line.hairAlpha"),
        QStringLiteral("line.faintAlpha"),
        QStringLiteral("label.track"),
        QStringLiteral("label.captionTrack"),
        QStringLiteral("label.captionWeight"),
    };
    return paths;
}

QHash<QString, SkinGround> SkinReader::groundsOf(const Skin &skin)
{
    QHash<QString, SkinGround> grounds = omarchyGrounds();
    for (auto it = skin.grounds.constBegin(); it != skin.grounds.constEnd(); ++it)
        grounds.insert(it.key(), it.value());

    // Walk each chain of `over`; a skin recipe on a loop goes back to Omarchy's, whose own
    // chains all end on the window, until none is left.
    for (bool looped = true; looped;) {
        looped = false;
        for (const QString &start : groundNames()) {
            QStringList chain{start};
            QString at = grounds.value(start).over;
            while (at != QLatin1String("window") && !chain.contains(at)) {
                chain << at;
                at = grounds.value(at).over;
            }
            if (at == QLatin1String("window"))
                continue;
            for (const QString &ground : chain.mid(chain.indexOf(at)))
                if (skin.grounds.contains(ground) &&
                    grounds.value(ground) != omarchyGrounds().value(ground)) {
                    grounds.insert(ground, omarchyGrounds().value(ground));
                    looped = true;
                    break;
                }
        }
    }
    return grounds;
}

QStringList SkinReader::groundOrder(const QHash<QString, SkinGround> &grounds)
{
    QStringList order;
    const std::function<void(const QString &)> visit = [&](const QString &name) {
        if (order.contains(name) || !grounds.contains(name))
            return;
        const QString over = grounds.value(name).over;
        if (over != QLatin1String("window") && over != name)
            visit(over);
        order << name;
    };
    for (const QString &name : groundNames())
        visit(name);
    return order;
}

ThemeSkin::ThemeSkin(QObject *parent)
    : QObject(parent)
{
    m_skin.roles = SkinReader::omarchyRoles();
    m_skin.version = SkinReader::kVersion;
}

QVariantMap ThemeSkin::roles() const
{
    QVariantMap roles;
    for (auto it = m_skin.roles.constBegin(); it != m_skin.roles.constEnd(); ++it)
        roles.insert(it.key(), it.value());
    return roles;
}

QVariant ThemeSkin::n(const QString &name, const QVariant &fallback) const
{
    return m_skin.tokens.value(QStringLiteral("density")).toMap().value(name, fallback);
}

QVariant ThemeSkin::value(const QString &path, const QVariant &fallback) const
{
    const QStringList parts = path.split(QLatin1Char('.'));
    QVariant at = m_skin.tokens;
    for (const QString &part : parts) {
        const QVariantMap map = at.toMap();
        const auto it = map.constFind(part);
        if (it == map.constEnd())
            return fallback;
        at = *it;
    }
    return at;
}

QString ThemeSkin::glyph(const QString &name, const QString &fallback) const
{
    return m_skin.tokens.value(QStringLiteral("glyphs")).toMap().value(name, fallback).toString();
}

QString ThemeSkin::label(const QString &id, const QString &fallback) const
{
    return m_skin.tokens.value(QStringLiteral("labels")).toMap().value(id, fallback).toString();
}

QStringList SkinReader::fontFamilies(const Skin &skin)
{
    const QVariantMap font = skin.tokens.value(QStringLiteral("font")).toMap();
    QStringList families;
    for (const char *role : {"label", "value", "kana", "icons"}) {
        const QString family = font.value(QLatin1String(role)).toString().trimmed();
        if (!family.isEmpty() && !families.contains(family))
            families.append(family);
    }
    return families;
}

QStringList SkinReader::missingFonts(const Skin &skin, const QStringList &installed)
{
    QStringList missing;
    for (const QString &family : fontFamilies(skin)) {
        if (!installed.contains(family, Qt::CaseInsensitive))
            missing.append(family);
    }
    return missing;
}

QStringList ThemeSkin::missingFonts() const
{
    if (m_systemFonts)
        return {};
    return SkinReader::missingFonts(m_skin, QFontDatabase::families());
}

QStringList ThemeSkin::missingFontsOf(const QString &name) const
{
    return SkinReader::missingFonts(SkinReader::builtIn(name), QFontDatabase::families());
}

void ThemeSkin::setSystemFonts(bool system)
{
    if (m_systemFonts == system)
        return;
    m_systemFonts = system;
    Q_EMIT changed();
}

void ThemeSkin::set(const Skin &skin, const QString &requested)
{
    if (skin == m_skin && requested == m_requested)
        return;
    m_skin = skin;
    m_requested = requested;
    Q_EMIT changed();
}

} // namespace omaseed::integration
