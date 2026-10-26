#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>

#include <array>

namespace omaseed::integration {

/// Every colour omaseed paints text and glyphs with, as it reads on one ground: the window, or
/// a fill or tint over it (docs/DECISIONS.md C30 F1–F7). Theme resolves one of these for each
/// ground it knows, once per theme change, and QML reads `Theme.on.<ground>.<role>`: the text
/// on a selected row is `Theme.on.selected.dimForeground`, never `Theme.dimForeground`. Each
/// role is the window's colour (already at the floor there, Q19) given the least lift towards
/// the foreground that reads at 4.5:1 on this ground (C30 F2, F3), so a caption on a fill stays
/// a caption. `fill` is what the ground paints over the window, so a view paints the ground
/// and its text from the same token.
class ThemeInk : public QObject
{
    Q_OBJECT

    /// The ground's name in `Theme.on`: "window", "selected", "warning"…
    Q_PROPERTY(QString name READ name CONSTANT FINAL)
    /// The tint's alpha over the window (0 for the window itself), after C30 F6's cap.
    Q_PROPERTY(qreal alpha READ alpha NOTIFY changed FINAL)
    /// What to paint: the tint at `alpha`, transparent on the window.
    Q_PROPERTY(QColor fill READ fill NOTIFY changed FINAL)
    /// The opaque colour the text is measured against: `fill` over the window's background.
    Q_PROPERTY(QColor ground READ ground NOTIFY changed FINAL)

    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed FINAL)
    Q_PROPERTY(QColor dimForeground READ dimForeground NOTIFY changed FINAL)
    Q_PROPERTY(QColor brightForeground READ brightForeground NOTIFY changed FINAL)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed FINAL)
    Q_PROPERTY(QColor red READ red NOTIFY changed FINAL)
    Q_PROPERTY(QColor yellow READ yellow NOTIFY changed FINAL)
    Q_PROPERTY(QColor orange READ orange NOTIFY changed FINAL)
    Q_PROPERTY(QColor green READ green NOTIFY changed FINAL)
    Q_PROPERTY(QColor cyan READ cyan NOTIFY changed FINAL)
    Q_PROPERTY(QColor blue READ blue NOTIFY changed FINAL)
    Q_PROPERTY(QColor magenta READ magenta NOTIFY changed FINAL)

    // The state roles read the palette roles, as Theme's do.
    Q_PROPERTY(QColor stateDownloading READ blue NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateSeeding READ green NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateStopped READ dimForeground NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateError READ red NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateChecking READ yellow NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateQueued READ magenta NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateStalled READ orange NOTIFY changed FINAL)

public:
    /// The text roles, in the order `Roles` holds them.
    enum Role {
        Foreground,
        DimForeground,
        BrightForeground,
        Accent,
        Red,
        Yellow,
        Orange,
        Green,
        Cyan,
        Blue,
        Magenta,
        RoleCount
    };
    using Roles = std::array<QColor, RoleCount>;
    /// Each role's property name, as QML and `--theme-roles` spell it.
    static const std::array<const char *, RoleCount> &roleNames();

    ThemeInk(const QString &name, QObject *parent = nullptr);

    QString name() const { return m_name; }
    qreal alpha() const { return m_alpha; }
    QColor fill() const { return m_fill; }
    QColor ground() const { return m_ground; }
    /// The opaque ground `fill` is painted over: the window's background, or the ground of the
    /// one a skin paints it over (`over` in its recipe).
    QColor under() const { return m_under; }
    QColor role(Role role) const { return m_roles[role]; }
    const Roles &roles() const { return m_roles; }

    QColor foreground() const { return m_roles[Foreground]; }
    QColor dimForeground() const { return m_roles[DimForeground]; }
    QColor brightForeground() const { return m_roles[BrightForeground]; }
    QColor accent() const { return m_roles[Accent]; }
    QColor red() const { return m_roles[Red]; }
    QColor yellow() const { return m_roles[Yellow]; }
    QColor orange() const { return m_roles[Orange]; }
    QColor green() const { return m_roles[Green]; }
    QColor cyan() const { return m_roles[Cyan]; }
    QColor blue() const { return m_roles[Blue]; }
    QColor magenta() const { return m_roles[Magenta]; }

    /// `tint` at `alpha` over `under` (the window's background, or another ground's for a fill
    /// inside a filled block; `alpha` held by C30 F6's cap for that tint), and every role of
    /// `window` resolved on it. Emits
    /// changed() only when something moved, so a theme change that leaves this ground as it
    /// was repaints nothing on it.
    void resolve(const QColor &tint, qreal alpha, const QColor &under, const Roles &window);

    /// The roles of `window` (the theme's colours as they read on its background) on `ground`,
    /// an opaque colour (docs/DECISIONS.md C30 F1–F3, F5). The foreground and the bright one
    /// are lifted, when they fall short, towards whichever of the two reads better there; every
    /// other role gets the least lift towards the foreground, and only past it, towards that
    /// better one, when the foreground itself falls short there (C30 F6 caps the fills so it
    /// never does on a theme that reads on its own window). A role that reads is kept byte
    /// for byte.
    static Roles resolveOn(const Roles &window, const QColor &ground);

    /// `top` at `alpha` over the opaque `under`, as Qt paints it, rounded to bytes.
    static QColor over(const QColor &top, const QColor &under, qreal alpha);

Q_SIGNALS:
    void changed();

private:
    QString m_name;
    qreal m_alpha = 0;
    QColor m_fill;
    QColor m_ground;
    QColor m_under;
    Roles m_roles;
};

/// `Theme.on`: the grounds text is painted on, each a ThemeInk (docs/DECISIONS.md C30 F7). The
/// objects live as long as the Theme and only their colours change, so a binding to
/// `Theme.on.selected.dimForeground` follows a theme change like one to `Theme.dimForeground`.
///
/// - `window`: the window's own ground, no fill.
/// - The foreground's fills (ARCHITECTURE rule 2): `idle` (fillIdle, a bordered control, a
///   field), `hover` and `cursor` (fillHover: the pointer's row, the keyboard's row),
///   `selected` (fillSelected), `selectedCursor` (fillSelectedCursor), `pressed` (fillPressed).
/// - Tints of a palette colour (C30 F5): `warning` (yellow, warningTintAlpha: the yellow
///   bands), `attention` (the error colour, attentionTintAlpha: the console's attention block),
///   `danger` (red at fillSelected: a selected danger button), `ticked` (accent at fillPressed:
///   a ticked box, a switched-on toggle), `accentIdle` (accent at fillIdle: the drop
///   target), and `turtle` and `burst` (yellow and green at fillSelected: the alternative
///   speeds' chip, DECISIONS C32 D6 a).
/// - A fill inside a filled block, over that block's ground: `blockIdle` (fillIdle over
///   `idle`: a notice's details, a chip in a block), `blockHover` (fillHover over `idle`: a
///   picker's row under the pointer or the keyboard), `blockSelected`, `blockSelectedCursor`
///   and `blockPressed` (fillSelected, fillSelectedCursor and fillPressed over `idle`: the
///   add sheet's selected rows, the rail's selected server, a button pressed in a notice),
///   `highlight` (accent at highlightAlpha over `idle`: the selected text of a rename in
///   place).
/// - A fill over the hover fill (#188): `hoverIdle`, `hoverHover`, `hoverSelected` and
///   `hoverPressed` (fillIdle, fillHover, fillSelected and fillPressed over `hover`): a button
///   on the action bar, whose own ground is the hover fill, and the controls on a hovered
///   settings row. `Theme.within(block, plain)` (Kit/Theme.qml) picks the nested ground.
/// - The skin's own (#171, the console skin's design note): `panel` (a plate's ground: a
///   sheet, a menu; the window's own in Omarchy's skin) and `picked` (a torrent picked among
///   several). A skin may paint any ground but the window its own way, as a recipe of tint,
///   alpha and the ground it lies over (SkinReader), and C30's floor holds on each.
class ThemeGrounds : public QObject
{
    Q_OBJECT

    Q_PROPERTY(omaseed::integration::ThemeInk *window READ window CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *idle READ idle CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *hover READ hover CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *cursor READ cursor CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *selected READ selected CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *selectedCursor READ selectedCursor CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *pressed READ pressed CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *warning READ warning CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *attention READ attention CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *danger READ danger CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *ticked READ ticked CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *accentIdle READ accentIdle CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *blockIdle READ blockIdle CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *blockHover READ blockHover CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *blockSelected READ blockSelected CONSTANT FINAL)
    Q_PROPERTY(
        omaseed::integration::ThemeInk *blockSelectedCursor READ blockSelectedCursor CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *blockPressed READ blockPressed CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *highlight READ highlight CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *hoverIdle READ hoverIdle CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *hoverHover READ hoverHover CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *hoverSelected READ hoverSelected CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *hoverPressed READ hoverPressed CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *panel READ panel CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *picked READ picked CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *turtle READ turtle CONSTANT FINAL)
    Q_PROPERTY(omaseed::integration::ThemeInk *burst READ burst CONSTANT FINAL)

public:
    explicit ThemeGrounds(QObject *parent = nullptr);

    ThemeInk *window() const { return m_window; }
    ThemeInk *idle() const { return m_idle; }
    ThemeInk *hover() const { return m_hover; }
    ThemeInk *cursor() const { return m_cursor; }
    ThemeInk *selected() const { return m_selected; }
    ThemeInk *selectedCursor() const { return m_selectedCursor; }
    ThemeInk *pressed() const { return m_pressed; }
    ThemeInk *warning() const { return m_warning; }
    ThemeInk *attention() const { return m_attention; }
    ThemeInk *danger() const { return m_danger; }
    ThemeInk *ticked() const { return m_ticked; }
    ThemeInk *accentIdle() const { return m_accentIdle; }
    ThemeInk *blockIdle() const { return m_blockIdle; }
    ThemeInk *blockHover() const { return m_blockHover; }
    ThemeInk *blockSelected() const { return m_blockSelected; }
    ThemeInk *blockSelectedCursor() const { return m_blockSelectedCursor; }
    ThemeInk *blockPressed() const { return m_blockPressed; }
    ThemeInk *highlight() const { return m_highlight; }
    ThemeInk *hoverIdle() const { return m_hoverIdle; }
    ThemeInk *hoverHover() const { return m_hoverHover; }
    ThemeInk *hoverSelected() const { return m_hoverSelected; }
    ThemeInk *hoverPressed() const { return m_hoverPressed; }
    ThemeInk *panel() const { return m_panel; }
    ThemeInk *picked() const { return m_picked; }
    ThemeInk *turtle() const { return m_turtle; }
    ThemeInk *burst() const { return m_burst; }

    /// Every ground, window first: what `--theme-roles` prints and the contrast tests walk.
    QList<ThemeInk *> all() const;
    /// The ground of that name, nullptr for none.
    ThemeInk *byName(const QString &name) const;

private:
    ThemeInk *m_window;
    ThemeInk *m_idle;
    ThemeInk *m_hover;
    ThemeInk *m_cursor;
    ThemeInk *m_selected;
    ThemeInk *m_selectedCursor;
    ThemeInk *m_pressed;
    ThemeInk *m_warning;
    ThemeInk *m_attention;
    ThemeInk *m_danger;
    ThemeInk *m_ticked;
    ThemeInk *m_accentIdle;
    ThemeInk *m_blockIdle;
    ThemeInk *m_blockHover;
    ThemeInk *m_blockSelected;
    ThemeInk *m_blockSelectedCursor;
    ThemeInk *m_blockPressed;
    ThemeInk *m_highlight;
    ThemeInk *m_hoverIdle;
    ThemeInk *m_hoverHover;
    ThemeInk *m_hoverSelected;
    ThemeInk *m_hoverPressed;
    ThemeInk *m_panel;
    ThemeInk *m_picked;
    ThemeInk *m_turtle;
    ThemeInk *m_burst;
};

} // namespace omaseed::integration
