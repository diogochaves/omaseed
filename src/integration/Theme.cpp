#include "integration/Theme.hpp"

#include "integration/HyprlandEvents.hpp"
#include "integration/ThemeReader.hpp"

#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaProperty>
#include <QProcess>

#include <algorithm>
#include <cmath>

namespace omaseed::integration {

namespace {

ThemeReader *s_defaultReader = nullptr;

/// The reader setDefaultReader() set; when nobody wired one up (the QML tests, a tool), one
/// reading the desktop's theme directly. Deliberately not owned by anything; it lives as long
/// as the process does.
ThemeReader *defaultReader()
{
    if (!s_defaultReader)
        s_defaultReader = new ThemeReader;
    return s_defaultReader;
}

/// A shell.toml number, or the fallback when the key is absent or not a number.
qreal number(const QHash<QString, QString> &shell, const QString &key, qreal fallback)
{
    const auto it = shell.constFind(key);
    if (it == shell.constEnd())
        return fallback;
    bool ok = false;
    const double value = it->toDouble(&ok);
    return ok ? value : fallback;
}

bool boolean(const QHash<QString, QString> &shell, const QString &key, bool fallback)
{
    const auto it = shell.constFind(key);
    if (it == shell.constEnd())
        return fallback;
    if (*it == QLatin1String("true"))
        return true;
    if (*it == QLatin1String("false"))
        return false;
    return fallback;
}

/// A palette entry as a colour, or the fallback when the theme does not define it.
QColor colorOf(const ColorMap &palette, const QString &key, const QColor &fallback)
{
    const QColor colour = QColor::fromString(palette.value(key));
    return colour.isValid() ? colour : fallback;
}

int roundPositive(double value)
{
    return std::max(1, static_cast<int>(std::lround(value)));
}

/// WCAG relative luminance of an opaque colour: each sRGB channel linearised, then weighted.
qreal relativeLuminance(const QColor &colour)
{
    const auto linear = [](qreal channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(colour.redF()) + 0.7152 * linear(colour.greenF()) +
           0.0722 * linear(colour.blueF());
}

/// `from` moved the fraction `t` of the way to `to`, channel by channel, rounded to bytes.
QColor mixed(const QColor &from, const QColor &to, qreal t)
{
    const auto channel = [t](int a, int b) {
        return static_cast<int>(std::lround(a + (b - a) * t));
    };
    return QColor::fromRgb(channel(from.red(), to.red()),
                           channel(from.green(), to.green()),
                           channel(from.blue(), to.blue()));
}

} // namespace

// -------------------------------------------------------------------------------------
// ThemeFont
// -------------------------------------------------------------------------------------

void ThemeFont::apply(const QHash<QString, QString> &shell)
{
    const int base = roundPositive(number(shell, QStringLiteral("font.base-size"), 12));

    // A [font] key pins one token to a pixel size without disturbing the rest of the scale.
    const auto token = [&shell](const QString &key, int fallback) {
        const qreal pinned = number(shell, QStringLiteral("font.") + key, -1);
        return pinned > 0 ? roundPositive(pinned) : fallback;
    };
    const auto scaled = [base](double multiplier) { return roundPositive(base * multiplier); };

    const int before[] = {m_baseSize,
                          m_caption,
                          m_small,
                          m_body,
                          m_subtitle,
                          m_title,
                          m_heading,
                          m_display,
                          m_iconSmall,
                          m_icon};

    m_baseSize = base;
    m_caption = token(QStringLiteral("caption"), scaled(0.833));
    m_small = token(QStringLiteral("body-small"), scaled(0.917));
    m_body = token(QStringLiteral("body"), scaled(1.0));
    m_subtitle = token(QStringLiteral("subtitle"), scaled(1.083));
    m_title = token(QStringLiteral("title"), scaled(1.167));
    m_heading = token(QStringLiteral("heading"), scaled(1.333));
    m_display = token(QStringLiteral("display"), scaled(2.0));
    m_iconSmall = token(QStringLiteral("icon-small"), m_small);
    m_icon = token(QStringLiteral("icon"), m_subtitle);

    const int after[] = {m_baseSize,
                         m_caption,
                         m_small,
                         m_body,
                         m_subtitle,
                         m_title,
                         m_heading,
                         m_display,
                         m_iconSmall,
                         m_icon};
    if (!std::equal(std::begin(before), std::end(before), std::begin(after)))
        Q_EMIT changed();
}

// -------------------------------------------------------------------------------------
// Theme
// -------------------------------------------------------------------------------------

QString Theme::defaultEventSocket()
{
    return HyprlandEvents::defaultSocketPath();
}

Theme::Theme()
    : Theme(defaultReader())
{}

Theme::Theme(ThemeReader *reader, QObject *parent)
    : Theme(reader, Hyprland{}, parent)
{}

Theme::Theme(ThemeReader *reader, const Hyprland &hyprland, QObject *parent)
    : QObject(parent)
    , m_reader(reader)
    , m_font(new ThemeFont(this))
    , m_hyprland(hyprland)
    , m_on(new ThemeGrounds(this))
{
    resolveGrounds(); // the bundled palette's, until a reader gives one
    if (m_reader) {
        connect(m_reader, &ThemeReader::changed, this, &Theme::adopt);
        adopt();
    }
    // The ground starts at the shell's default; only setWindowAlpha() (the Appearance
    // override, through MainView's Binding) moves it after this, so a live theme change
    // never stomps an override the user has set.
    m_windowAlpha = m_defaultWindowAlpha;
    readCornerRadius();
    // #37: a theme or omarchy-console profile switch reloads Hyprland's config, maybe with
    // another rounding. Asking again on omaseed's own theme change would race that reload
    // (the theme files move first), so follow Hyprland's own event instead.
    if (!m_hyprland.eventSocket.isEmpty()) {
        m_hyprlandEvents = new HyprlandEvents(m_hyprland.eventSocket, this);
        connect(m_hyprlandEvents, &HyprlandEvents::configReloaded, this, &Theme::readCornerRadius);
    }
}

Theme::~Theme()
{
    // Leaving hyprctl running past our own destructor makes QProcess complain on exit. Two
    // reloads in a row can leave more than one asking.
    for (QProcess *hyprctl : findChildren<QProcess *>(Qt::FindDirectChildrenOnly)) {
        if (hyprctl->state() != QProcess::NotRunning) {
            hyprctl->kill();
            hyprctl->waitForFinished(100);
        }
    }
}

void Theme::setDefaultReader(ThemeReader *reader)
{
    s_defaultReader = reader;
}

void Theme::adopt()
{
    const ColorMap palette = m_reader->palette();
    const QHash<QString, QString> shell = m_reader->shellValues();
    const qreal spaceScaleBefore = spaceScale();

    m_background = colorOf(palette, QStringLiteral("background"), m_background);
    m_foreground = colorOf(palette, QStringLiteral("foreground"), m_foreground);
    m_dimForeground = colorOf(palette, QStringLiteral("dark_foreground"), m_dimForeground);
    m_brightForeground = colorOf(palette, QStringLiteral("bright_foreground"), m_brightForeground);
    m_accent = colorOf(palette, QStringLiteral("accent"), m_accent);
    m_muted = colorOf(palette, QStringLiteral("muted"), m_muted);
    m_selection = colorOf(palette, QStringLiteral("selection"), m_selection);
    m_red = colorOf(palette, QStringLiteral("red"), m_red);
    m_yellow = colorOf(palette, QStringLiteral("yellow"), m_yellow);
    m_orange = colorOf(palette, QStringLiteral("orange"), m_orange);
    m_green = colorOf(palette, QStringLiteral("green"), m_green);
    m_cyan = colorOf(palette, QStringLiteral("cyan"), m_cyan);
    m_blue = colorOf(palette, QStringLiteral("blue"), m_blue);
    m_magenta = colorOf(palette, QStringLiteral("magenta"), m_magenta);
    m_brown = colorOf(palette, QStringLiteral("brown"), m_brown);
    m_darkBackground = colorOf(palette, QStringLiteral("dark_background"), m_darkBackground);
    m_darkerBackground = colorOf(palette, QStringLiteral("darker_background"), m_darkerBackground);
    m_lighterBackground =
        colorOf(palette, QStringLiteral("lighter_background"), m_lighterBackground);
    // Most themes put dark_foreground under 4.5:1 against their own background, and a few
    // (the light ones, miasma) do the same with a palette colour; the spec paints captions,
    // states, links and glyphs in them. Each is lifted towards the foreground until it reads
    // (Q19, amended in #89); the opaque theme colour is the reference, not the alpha-blended
    // ground, which moves with the wallpaper. The state roles read these, so they follow.
    for (QColor *text : {&m_dimForeground,
                         &m_accent,
                         &m_red,
                         &m_yellow,
                         &m_orange,
                         &m_green,
                         &m_cyan,
                         &m_blue,
                         &m_magenta})
        *text = liftToContrast(*text, m_foreground, m_background, kTextContrastFloor);

    m_mode = palette.value(QStringLiteral("mode"), QStringLiteral("dark"));
    m_themeName = m_reader->themeName();

    m_spacingScale = number(shell, QStringLiteral("spacing.scale"), 1.0);
    m_spacingScaleWithFont = boolean(shell, QStringLiteral("spacing.scale-with-font"), true);
    m_fillIdle = number(shell, QStringLiteral("controls.normal-fill-alpha"), 0.04);
    m_fillHover = number(shell, QStringLiteral("controls.hover-cursor-fill-alpha"), 0.08);
    m_fillSelected = number(shell, QStringLiteral("controls.selected-fill-alpha"), 0.18);
    m_fillPressed = number(shell, QStringLiteral("controls.pressed-fill-alpha"), 0.22);
    m_borderIdle = number(shell, QStringLiteral("controls.normal-border-alpha"), 0.40);
    m_borderHover = number(shell, QStringLiteral("controls.hover-cursor-border-alpha"), 0.25);
    m_defaultWindowAlpha = std::clamp(
        number(shell, QStringLiteral("launcher.background-alpha"), 0.95), qreal(0), qreal(1));
    // C30 F6: a theme whose text reads on nothing at a fill's alpha (everforest: its foreground
    // and bright foreground are one colour) gets that fill held where the text still reads.
    m_fillCap = fillCapFor(m_foreground, m_foreground, m_brightForeground, m_background);
    m_fillSelectedCursor = kFillSelectedCursor;
    for (qreal *alpha :
         {&m_fillIdle, &m_fillHover, &m_fillSelected, &m_fillPressed, &m_fillSelectedCursor})
        *alpha = std::min(*alpha, m_fillCap);
    resolveGrounds();

    // The font family is fontconfig's answer for "monospace", which a theme change can move.
    if (QGuiApplication::instance())
        m_fontFamily = QFontInfo(QFont(QStringLiteral("monospace"))).family();

    m_font->apply(shell);
    Q_EMIT changed();
    if (!qFuzzyCompare(spaceScale(), spaceScaleBefore))
        Q_EMIT spaceScaleChanged();
}

void Theme::resolveGrounds()
{
    // Once per theme change, every role on every ground (C30 F7): a binding reads the
    // resolved colour, it never lifts one itself.
    ThemeInk::Roles window;
    window[ThemeInk::Foreground] = m_foreground;
    window[ThemeInk::DimForeground] = m_dimForeground;
    window[ThemeInk::BrightForeground] = m_brightForeground;
    window[ThemeInk::Accent] = m_accent;
    window[ThemeInk::Red] = m_red;
    window[ThemeInk::Yellow] = m_yellow;
    window[ThemeInk::Orange] = m_orange;
    window[ThemeInk::Green] = m_green;
    window[ThemeInk::Cyan] = m_cyan;
    window[ThemeInk::Blue] = m_blue;
    window[ThemeInk::Magenta] = m_magenta;

    m_on->window()->resolve(m_foreground, 0, m_background, window);
    m_on->idle()->resolve(m_foreground, m_fillIdle, m_background, window);
    m_on->hover()->resolve(m_foreground, m_fillHover, m_background, window);
    m_on->cursor()->resolve(m_foreground, m_fillHover, m_background, window);
    m_on->selected()->resolve(m_foreground, m_fillSelected, m_background, window);
    m_on->selectedCursor()->resolve(m_foreground, m_fillSelectedCursor, m_background, window);
    m_on->pressed()->resolve(m_foreground, m_fillPressed, m_background, window);
    m_on->warning()->resolve(m_yellow, warningTintAlpha(), m_background, window);
    m_on->attention()->resolve(m_red, attentionTintAlpha(), m_background, window);
    m_on->danger()->resolve(m_red, m_fillSelected, m_background, window);
    m_on->ticked()->resolve(m_accent, m_fillPressed, m_background, window);
    m_on->accentIdle()->resolve(m_accent, m_fillIdle, m_background, window);
    const QColor block = m_on->idle()->ground();
    m_on->blockIdle()->resolve(m_foreground, m_fillIdle, block, window);
    m_on->blockHover()->resolve(m_foreground, m_fillHover, block, window);
    m_on->highlight()->resolve(m_accent, highlightAlpha(), block, window);
}

qreal Theme::fillCapFor(const QColor &tint,
                        const QColor &foreground,
                        const QColor &bright,
                        const QColor &background)
{
    const QColor &best = contrastRatio(bright, background) > contrastRatio(foreground, background)
                             ? bright
                             : foreground;
    if (contrastRatio(best, background) < kTextContrastFloor)
        return 1.0;
    // Board 50's measure: 0.005 steps from the bare window up, the last one that still reads.
    int steps = 0;
    while (steps < 200 &&
           contrastRatio(best, ThemeInk::over(tint, background, (steps + 1) * 0.005)) >=
               kTextContrastFloor)
        ++steps;
    return steps >= 200 ? 1.0 : steps * 0.005;
}

Theme::WindowCorners Theme::windowCorners(const QByteArray &rounding,
                                          const QByteArray &roundingPower)
{
    // `getoption` answers {"option": …, "int": 24, "set": true}; an unknown option is the
    // plain text "no such option", which is not an object.
    const QJsonValue radius =
        QJsonDocument::fromJson(rounding).object().value(QStringLiteral("int"));
    if (!radius.isDouble())
        return {};
    const QJsonValue power =
        QJsonDocument::fromJson(roundingPower).object().value(QStringLiteral("float"));
    const bool cut = power.isDouble() && power.toDouble() < 2.0;
    return {cut ? 0 : std::max(0, radius.toInt()), false};
}

void Theme::readCornerRadius()
{
    // Hyprland's rounding and its power, asked for one after the other without blocking the
    // start-up; again on every `configreloaded` (#37).
    const int read = ++m_cornerRead;
    askHyprland(QStringLiteral("decoration:rounding"), [this, read](const QByteArray &rounding) {
        if (read != m_cornerRead)
            return;
        if (rounding.isEmpty()) {
            applyCorners({});
            return;
        }
        askHyprland(QStringLiteral("decoration:rounding_power"),
                    [this, rounding, read](const QByteArray &power) {
                        if (read == m_cornerRead)
                            applyCorners(windowCorners(rounding, power));
                    });
    });
}

void Theme::askHyprland(const QString &option, std::function<void(const QByteArray &)> done)
{
    auto *hyprctl = new QProcess(this);
    connect(hyprctl,
            &QProcess::finished,
            this,
            [hyprctl, done](int exitCode, QProcess::ExitStatus status) {
                hyprctl->deleteLater();
                done(exitCode == 0 && status == QProcess::NormalExit
                         ? hyprctl->readAllStandardOutput()
                         : QByteArray());
            });
    // No hyprctl at all: no Hyprland. (A crash also reports here, then finishes.)
    connect(hyprctl, &QProcess::errorOccurred, this, [hyprctl, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        hyprctl->deleteLater();
        done(QByteArray());
    });
    hyprctl->start(m_hyprland.hyprctl, {QStringLiteral("-j"), QStringLiteral("getoption"), option});
}

void Theme::applyCorners(const WindowCorners &corners)
{
    if (corners.radius != m_cornerRadius) {
        m_cornerRadius = corners.radius;
        Q_EMIT cornerRadiusChanged();
    }
    if (corners.edge != m_windowEdge) {
        m_windowEdge = corners.edge;
        Q_EMIT windowEdgeChanged();
    }
}

int Theme::rowHeight() const
{
    return static_cast<int>(std::lround(m_font->body() * 2.4));
}

int Theme::controlHeight() const
{
    return static_cast<int>(std::lround(m_font->body() * 2.333));
}

int Theme::railRowHeight() const
{
    return static_cast<int>(std::lround(m_font->body() * 2.1));
}

void Theme::setWindowAlpha(qreal alpha)
{
    const qreal clamped = std::clamp(alpha, qreal(0), qreal(1));
    if (qFuzzyCompare(clamped, m_windowAlpha))
        return;
    m_windowAlpha = clamped;
    Q_EMIT windowAlphaChanged();
}

int Theme::space(qreal px) const
{
    return spaceAt(px, spaceScale());
}

qreal Theme::spaceScale() const
{
    const qreal fontScale = std::max(1.0 / 12.0, m_font->baseSize() / 12.0);
    return m_spacingScale * (m_spacingScaleWithFont ? fontScale : 1.0);
}

int Theme::spaceAt(qreal px, qreal scale)
{
    if (px <= 0)
        return 0;
    return roundPositive(px * scale);
}

QColor Theme::fill(qreal alpha) const
{
    QColor colour = m_foreground;
    colour.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0), qreal(1))));
    return colour;
}

qreal Theme::contrastRatio(const QColor &a, const QColor &b)
{
    const qreal la = relativeLuminance(a);
    const qreal lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor
Theme::liftToContrast(const QColor &dim, const QColor &toward, const QColor &against, qreal minimum)
{
    if (contrastRatio(dim, against) >= minimum)
        return dim; // byte for byte: a theme that reads already is not touched
    if (contrastRatio(toward, against) < minimum)
        return toward; // a degenerate palette: the best it has
    // A linear walk rather than a bisection: the contrast need not be monotone in t when
    // dim sits on the other side of the background from toward, and 257 steps are nothing.
    for (int step = 1; step < 256; ++step) {
        const QColor candidate = mixed(dim, toward, step / 256.0);
        if (contrastRatio(candidate, against) >= minimum)
            return candidate;
    }
    return toward;
}

QString Theme::hexColor(const QColor &colour)
{
    QString hex = colour.name(QColor::HexRgb);
    if (colour.alpha() < 255)
        hex += QStringLiteral("%1").arg(colour.alpha(), 2, 16, QLatin1Char('0'));
    return hex;
}

QJsonObject Theme::rolesJson(const Theme &theme)
{
    QJsonObject colors;
    QJsonObject alphas;
    const QMetaObject *meta = theme.metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        const int type = property.metaType().id();
        if (type == QMetaType::QColor)
            colors.insert(QString::fromLatin1(property.name()),
                          hexColor(property.read(&theme).value<QColor>()));
        // spaceScale is a size factor, not an alpha (#74).
        else if ((type == QMetaType::Double || type == QMetaType::Float) &&
                 qstrcmp(property.name(), "spaceScale") != 0)
            alphas.insert(QString::fromLatin1(property.name()), property.read(&theme).toDouble());
    }

    QJsonObject roles;
    roles.insert(QStringLiteral("theme"), theme.themeName());
    roles.insert(QStringLiteral("mode"), theme.mode());
    roles.insert(QStringLiteral("source"), theme.m_reader ? theme.m_reader->source() : QString());
    roles.insert(QStringLiteral("colors"), colors);
    roles.insert(QStringLiteral("alphas"), alphas);
    roles.insert(QStringLiteral("windowAlpha"), theme.windowAlpha());

    // C30 F7: every role on every ground, as `Theme.on` holds them, for the contrast checks.
    QJsonObject grounds;
    for (const ThemeInk *ink : theme.on()->all()) {
        QJsonObject inkColors;
        for (int role = 0; role < ThemeInk::RoleCount; ++role)
            inkColors.insert(QString::fromLatin1(ThemeInk::roleNames()[role]),
                             hexColor(ink->role(static_cast<ThemeInk::Role>(role))));
        QJsonObject ground;
        ground.insert(QStringLiteral("alpha"), ink->alpha());
        ground.insert(QStringLiteral("fill"), hexColor(ink->fill()));
        ground.insert(QStringLiteral("ground"), hexColor(ink->ground()));
        ground.insert(QStringLiteral("colors"), inkColors);
        grounds.insert(ink->name(), ground);
    }
    roles.insert(QStringLiteral("grounds"), grounds);
    return roles;
}

} // namespace omaseed::integration
