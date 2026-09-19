#include "integration/Theme.hpp"

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

Theme::Theme(ThemeReader *reader, QObject *parent)
    : QObject(parent)
    , m_reader(reader)
    , m_font(new ThemeFont(this))
{
    if (m_reader) {
        connect(m_reader, &ThemeReader::changed, this, &Theme::adopt);
        adopt();
    }
    readCornerRadius();
}

Theme::~Theme()
{
    // Leaving hyprctl running past our own destructor makes QProcess complain on exit.
    if (m_hyprctl && m_hyprctl->state() != QProcess::NotRunning) {
        m_hyprctl->kill();
        m_hyprctl->waitForFinished(100);
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

    // The font family is fontconfig's answer for "monospace", which a theme change can move.
    if (QGuiApplication::instance())
        m_fontFamily = QFontInfo(QFont(QStringLiteral("monospace"))).family();

    m_font->apply(shell);
    Q_EMIT changed();
}

void Theme::readCornerRadius()
{
    // Hyprland's rounding, asked for without blocking the start-up. No Hyprland, no radius.
    auto *hyprctl = new QProcess(this);
    m_hyprctl = hyprctl;
    connect(hyprctl,
            &QProcess::finished,
            this,
            [this, hyprctl](int exitCode, QProcess::ExitStatus status) {
                hyprctl->deleteLater();
                if (exitCode != 0 || status != QProcess::NormalExit)
                    return;
                const QJsonObject option =
                    QJsonDocument::fromJson(hyprctl->readAllStandardOutput()).object();
                const int radius = option.value(QStringLiteral("int")).toInt(0);
                if (radius == m_cornerRadius)
                    return;
                m_cornerRadius = radius;
                Q_EMIT cornerRadiusChanged();
            });
    connect(hyprctl, &QProcess::errorOccurred, this, [hyprctl]() { hyprctl->deleteLater(); });
    hyprctl->start(
        QStringLiteral("hyprctl"),
        {QStringLiteral("-j"), QStringLiteral("getoption"), QStringLiteral("decoration:rounding")});
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

} // namespace omaseed::integration
