#include "integration/Theme.hpp"

#include "integration/HyprlandEvents.hpp"
#include "integration/ThemeReader.hpp"

#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <algorithm>
#include <cmath>

namespace omaseed::integration {

namespace {

ThemeReader *s_defaultReader = nullptr;

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

/// The contrast every tracked caption and column header must reach against the window
/// background (docs/QUESTIONS.md Q19, Diogo's ruling of 2026-09-19).
constexpr qreal kMinimumDimContrast = 3.0;

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

Theme::Theme(ThemeReader *reader, QObject *parent)
    : Theme(reader, Hyprland{}, parent)
{}

Theme::Theme(ThemeReader *reader, const Hyprland &hyprland, QObject *parent)
    : QObject(parent)
    , m_reader(reader)
    , m_font(new ThemeFont(this))
    , m_hyprland(hyprland)
{
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

Theme *Theme::createForEngine(QQmlEngine *engine)
{
    Q_UNUSED(engine);
    if (!s_defaultReader) {
        // Nobody wired one up (the QML tests, a tool): read the desktop's theme directly.
        // Deliberately not owned by anything; it lives as long as the process does.
        s_defaultReader = new ThemeReader;
    }
    return new Theme(s_defaultReader); // the QML engine takes ownership
}

void Theme::adopt()
{
    const ColorMap palette = m_reader->palette();
    const QHash<QString, QString> shell = m_reader->shellValues();

    m_background = colorOf(palette, QStringLiteral("background"), m_background);
    m_foreground = colorOf(palette, QStringLiteral("foreground"), m_foreground);
    m_dimForeground = colorOf(palette, QStringLiteral("dark_foreground"), m_dimForeground);
    // Twelve of the bundled themes put dark_foreground under 3:1 against their own
    // background, and the spec draws every caption in it. Lift it towards the foreground
    // until it reads (Q19); the opaque theme colour is the reference, not the alpha-blended
    // ground, which moves with the wallpaper.
    m_dimForeground =
        liftToContrast(m_dimForeground, m_foreground, m_background, kMinimumDimContrast);
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

    // The font family is fontconfig's answer for "monospace", which a theme change can move.
    if (QGuiApplication::instance())
        m_fontFamily = QFontInfo(QFont(QStringLiteral("monospace"))).family();

    m_font->apply(shell);
    Q_EMIT changed();
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
    if (px <= 0)
        return 0;
    const qreal fontScale = std::max(1.0 / 12.0, m_font->baseSize() / 12.0);
    const qreal scale = m_spacingScale * (m_spacingScaleWithFont ? fontScale : 1.0);
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

} // namespace omaseed::integration
