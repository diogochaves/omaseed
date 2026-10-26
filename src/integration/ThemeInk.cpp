#include "integration/ThemeInk.hpp"

#include "integration/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace omaseed::integration {

const std::array<const char *, ThemeInk::RoleCount> &ThemeInk::roleNames()
{
    static const std::array<const char *, RoleCount> names{"foreground",
                                                           "dimForeground",
                                                           "brightForeground",
                                                           "accent",
                                                           "red",
                                                           "yellow",
                                                           "orange",
                                                           "green",
                                                           "cyan",
                                                           "blue",
                                                           "magenta"};
    return names;
}

ThemeInk::ThemeInk(const QString &name, QObject *parent)
    : QObject(parent)
    , m_name(name)
{}

QColor ThemeInk::over(const QColor &top, const QColor &under, qreal alpha)
{
    const qreal a = std::clamp(alpha, qreal(0), qreal(1));
    const auto channel = [a](int t, int u) {
        return static_cast<int>(std::lround(t * a + u * (1 - a)));
    };
    return QColor::fromRgb(channel(top.red(), under.red()),
                           channel(top.green(), under.green()),
                           channel(top.blue(), under.blue()));
}

ThemeInk::Roles ThemeInk::resolveOn(const Roles &window, const QColor &ground)
{
    constexpr qreal floor = Theme::kTextContrastFloor;
    const QColor &foreground = window[Foreground];
    const QColor &bright = window[BrightForeground];
    // The text colour that reads best here: the end every lift may go to (C30 F6 a: never past
    // the palette, towards white or black).
    const QColor &best =
        Theme::contrastRatio(bright, ground) > Theme::contrastRatio(foreground, ground)
            ? bright
            : foreground;

    Roles roles;
    roles[Foreground] = Theme::liftToContrast(foreground, best, ground, floor);
    roles[BrightForeground] = Theme::liftToContrast(bright, best, ground, floor);
    for (int role = DimForeground; role < RoleCount; ++role) {
        if (role == BrightForeground)
            continue;
        // C30 F2, F3: the least lift towards the foreground, as Q19 does on the window, so a
        // caption keeps its distance from the main text wherever that is enough.
        QColor colour = Theme::liftToContrast(window[role], foreground, ground, floor);
        if (Theme::contrastRatio(colour, ground) < floor)
            colour = Theme::liftToContrast(colour, best, ground, floor);
        roles[role] = colour;
    }
    return roles;
}

void ThemeInk::resolve(const QColor &tint, qreal alpha, const QColor &under, const Roles &window)
{
    // C30 F6 on a tint: held where the theme's text still reads on it (the foreground's own
    // fills come in held already, Theme::fillCap).
    alpha = std::min(alpha,
                     Theme::fillCapFor(tint, window[Foreground], window[BrightForeground], under));
    QColor fill = tint;
    fill.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0), qreal(1))));
    const QColor ground = over(tint, under, alpha);
    const Roles roles = resolveOn(window, ground);
    if (qFuzzyCompare(1 + alpha, 1 + m_alpha) && fill == m_fill && ground == m_ground &&
        under == m_under && roles == m_roles)
        return;
    m_alpha = alpha;
    m_under = under;
    m_fill = fill;
    m_ground = ground;
    m_roles = roles;
    Q_EMIT changed();
}

ThemeGrounds::ThemeGrounds(QObject *parent)
    : QObject(parent)
    , m_window(new ThemeInk(QStringLiteral("window"), this))
    , m_idle(new ThemeInk(QStringLiteral("idle"), this))
    , m_hover(new ThemeInk(QStringLiteral("hover"), this))
    , m_cursor(new ThemeInk(QStringLiteral("cursor"), this))
    , m_selected(new ThemeInk(QStringLiteral("selected"), this))
    , m_selectedCursor(new ThemeInk(QStringLiteral("selectedCursor"), this))
    , m_pressed(new ThemeInk(QStringLiteral("pressed"), this))
    , m_warning(new ThemeInk(QStringLiteral("warning"), this))
    , m_attention(new ThemeInk(QStringLiteral("attention"), this))
    , m_danger(new ThemeInk(QStringLiteral("danger"), this))
    , m_ticked(new ThemeInk(QStringLiteral("ticked"), this))
    , m_accentIdle(new ThemeInk(QStringLiteral("accentIdle"), this))
    , m_blockIdle(new ThemeInk(QStringLiteral("blockIdle"), this))
    , m_blockHover(new ThemeInk(QStringLiteral("blockHover"), this))
    , m_blockSelected(new ThemeInk(QStringLiteral("blockSelected"), this))
    , m_blockSelectedCursor(new ThemeInk(QStringLiteral("blockSelectedCursor"), this))
    , m_blockPressed(new ThemeInk(QStringLiteral("blockPressed"), this))
    , m_highlight(new ThemeInk(QStringLiteral("highlight"), this))
    , m_hoverIdle(new ThemeInk(QStringLiteral("hoverIdle"), this))
    , m_hoverHover(new ThemeInk(QStringLiteral("hoverHover"), this))
    , m_hoverSelected(new ThemeInk(QStringLiteral("hoverSelected"), this))
    , m_hoverPressed(new ThemeInk(QStringLiteral("hoverPressed"), this))
    , m_panel(new ThemeInk(QStringLiteral("panel"), this))
    , m_picked(new ThemeInk(QStringLiteral("picked"), this))
    , m_turtle(new ThemeInk(QStringLiteral("turtle"), this))
    , m_burst(new ThemeInk(QStringLiteral("burst"), this))
{}

QList<ThemeInk *> ThemeGrounds::all() const
{
    return {m_window,        m_idle,           m_hover,         m_cursor,
            m_selected,      m_selectedCursor, m_pressed,       m_warning,
            m_attention,     m_danger,         m_ticked,        m_accentIdle,
            m_blockIdle,     m_blockHover,     m_blockSelected, m_blockSelectedCursor,
            m_blockPressed,  m_highlight,      m_hoverIdle,     m_hoverHover,
            m_hoverSelected, m_hoverPressed,   m_panel,         m_picked,
            m_turtle,        m_burst};
}

ThemeInk *ThemeGrounds::byName(const QString &name) const
{
    for (ThemeInk *ink : all())
        if (ink->name() == name)
            return ink;
    return nullptr;
}

} // namespace omaseed::integration
