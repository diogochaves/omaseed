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
    Spec,     ///< a density: a number of spec px, 0 to 4000
    Alpha,    ///< 0 to 1
    Em,       ///< a tracking in em, 0 to 1
    Weight,   ///< a font weight, an integer from 100 to 900
    Bool,     ///< true or false
    LineRole, ///< a theme role, a skin role, or the skin's own `hair` or `faint` line
    Family,   ///< a font family; "" is fontconfig's monospace
    Case,     ///< "none" or "upper"
    Step,     ///< a step of the type scale: "caption", "small" or "body"
    Glyph,    ///< a glyph, `ms:<ligature>` or `icon:<name>`
    Word,     ///< a label's word
    Face,     ///< a QML file in the skin's faces/
};

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
          {QStringLiteral("faintAlpha"), Kind::Alpha}}},
        {QStringLiteral("cut"),
         {{QStringLiteral("panel"), Kind::Px}, {QStringLiteral("key"), Kind::Px}}},
        // A kind's own tint under its words (#171 step 5): a notice's ground is its kind's
        // colour at this alpha over the window (board 46 `.notice .pi`); 0 is the idle fill.
        {QStringLiteral("tint"), {{QStringLiteral("notice"), Kind::Alpha}}},
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
          {QStringLiteral("tags"), Kind::Bool}}},
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
                       text == QLatin1String("body")
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
        static const QRegularExpression file(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]*\\.qml$"));
        const QString face = value.toString();
        return value.isString() && file.match(face).hasMatch() ? QVariant(face) : QVariant();
    }
    }
    return {};
}

/// A ground's recipe from the file, or nothing when it is malformed.
std::optional<SkinGround> groundFrom(const QString &name, const QJsonValue &value)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject recipe = value.toObject();
    SkinGround ground;
    ground.tint = recipe.value(QLatin1String("tint")).toString();
    if (!isTint(ground.tint))
        return std::nullopt;
    const QJsonValue alpha = recipe.value(QLatin1String("alpha"));
    if (alpha.toString() == QLatin1String("shell"))
        ground.shellAlpha = true;
    else if (isNumberIn(alpha, 0, 1))
        ground.alpha = alpha.toDouble();
    else
        return std::nullopt;
    const QJsonValue over = recipe.value(QLatin1String("over"));
    if (!over.isUndefined()) {
        ground.over = over.toString();
        const bool known = ground.over == QLatin1String("window") ||
                           SkinReader::groundNames().contains(ground.over);
        if (!known || ground.over == name)
            return std::nullopt;
    }
    for (auto it = recipe.constBegin(); it != recipe.constEnd(); ++it)
        if (it.key() != QLatin1String("tint") && it.key() != QLatin1String("alpha") &&
            it.key() != QLatin1String("over"))
            return std::nullopt;
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

    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QString group = it.key();
        const QJsonValue value = it.value();
        if (group == QLatin1String("skin") || group == QLatin1String("version"))
            continue;

        if (group == QLatin1String("radius")) {
            if (value.toString() == QLatin1String("hyprland"))
                skin.tokens.insert(group, value.toString());
            else if (const QVariant px = check(value, Kind::Px); px.isValid())
                skin.tokens.insert(group, px);
            else
                skin.dropped << group;
            continue;
        }
        if (group == QLatin1String("numerals")) {
            const QString text = value.toString();
            if (text == QLatin1String("pad") || text == QLatin1String("plain"))
                skin.tokens.insert(group, text);
            else
                skin.dropped << group;
            continue;
        }
        if (group == QLatin1String("glow")) {
            // C24 H12: clean glow only. Any other value is held at "clean", never drawn.
            skin.tokens.insert(group, QStringLiteral("clean"));
            if (value.toString() != QLatin1String("clean"))
                skin.dropped << group;
            continue;
        }

        if (!value.isObject()) {
            skin.dropped << group;
            continue;
        }
        const QJsonObject members = value.toObject();

        if (group == QLatin1String("roles")) {
            for (auto m = members.constBegin(); m != members.constEnd(); ++m) {
                const QString role = m.value().toString();
                if (isSkinRole(m.key()) && themeRoles().contains(role))
                    skin.roles.insert(m.key(), role);
                else
                    skin.dropped << group + QLatin1Char('.') + m.key();
            }
            continue;
        }
        if (group == QLatin1String("grounds")) {
            for (auto m = members.constBegin(); m != members.constEnd(); ++m) {
                const auto ground =
                    groundNames().contains(m.key()) ? groundFrom(m.key(), m.value()) : std::nullopt;
                if (ground)
                    skin.grounds.insert(m.key(), *ground);
                else
                    skin.dropped << group + QLatin1Char('.') + m.key();
            }
            continue;
        }

        const auto fixed = fixedGroups().constFind(group);
        const auto open = openGroups().constFind(group);
        const bool faces = group == QLatin1String("faces");
        if (fixed == fixedGroups().constEnd() && open == openGroups().constEnd() && !faces) {
            skin.dropped << group;
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
            const QVariant token = kind ? check(m.value(), *kind) : QVariant();
            if (token.isValid())
                read.insert(m.key(), token);
            else
                skin.dropped << group + QLatin1Char('.') + m.key();
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
            {QStringLiteral("highlight"), shell("accent", "idle")},
            {QStringLiteral("panel"), shell("foreground")},
            {QStringLiteral("picked"), shell("accent")},
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
        QStringLiteral("highlight"),
        QStringLiteral("panel"),
        QStringLiteral("picked"),
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
