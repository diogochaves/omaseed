#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>

#include <functional>

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace omaseed::integration {

class HyprlandEvents;
class ThemeReader;

/// The type scale, grouped under `Theme.font`. Every token is `base-size` times the shell's
/// multiplier, unless `[font]` in shell.toml pins that token to a pixel size.
class ThemeFont : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int baseSize READ baseSize NOTIFY changed FINAL)
    Q_PROPERTY(int caption READ caption NOTIFY changed FINAL)
    Q_PROPERTY(int small READ small NOTIFY changed FINAL)
    Q_PROPERTY(int body READ body NOTIFY changed FINAL)
    Q_PROPERTY(int subtitle READ subtitle NOTIFY changed FINAL)
    Q_PROPERTY(int title READ title NOTIFY changed FINAL)
    Q_PROPERTY(int heading READ heading NOTIFY changed FINAL)
    Q_PROPERTY(int display READ display NOTIFY changed FINAL)
    Q_PROPERTY(int iconSmall READ iconSmall NOTIFY changed FINAL)
    Q_PROPERTY(int icon READ icon NOTIFY changed FINAL)

public:
    using QObject::QObject;

    int baseSize() const { return m_baseSize; }
    int caption() const { return m_caption; }
    int small() const { return m_small; }
    int body() const { return m_body; }
    int subtitle() const { return m_subtitle; }
    int title() const { return m_title; }
    int heading() const { return m_heading; }
    int display() const { return m_display; }
    int iconSmall() const { return m_iconSmall; }
    int icon() const { return m_icon; }

    /// Recompute from `[font]` in the merged shell values. Emits changed() when anything moved.
    void apply(const QHash<QString, QString> &shell);

Q_SIGNALS:
    void changed();

private:
    int m_baseSize = 12;
    int m_caption = 10;
    int m_small = 11;
    int m_body = 12;
    int m_subtitle = 13;
    int m_title = 14;
    int m_heading = 16;
    int m_display = 24;
    int m_iconSmall = 11;
    int m_icon = 13;
};

/// The one source of every colour, size and alpha in the QML. Imported as `Omaseed.Kit`'s
/// `Theme` singleton; every value is a binding target, so a theme change repaints without a
/// restart. No QML file outside this class may hold a colour literal or a pixel size.
class Theme : public QObject
{
    Q_OBJECT

    // ---- colour roles ------------------------------------------------------------------
    Q_PROPERTY(QColor background READ background NOTIFY changed FINAL)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed FINAL)
    /// `dark_foreground`, lifted towards `foreground` until it reads at 3:1 against
    /// `background` (docs/DECISIONS.md Q19). A theme already at 3:1 keeps its colour.
    Q_PROPERTY(QColor dimForeground READ dimForeground NOTIFY changed FINAL)
    Q_PROPERTY(QColor brightForeground READ brightForeground NOTIFY changed FINAL)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed FINAL)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed FINAL)
    Q_PROPERTY(QColor selection READ selection NOTIFY changed FINAL)
    Q_PROPERTY(QColor red READ red NOTIFY changed FINAL)
    Q_PROPERTY(QColor yellow READ yellow NOTIFY changed FINAL)
    Q_PROPERTY(QColor orange READ orange NOTIFY changed FINAL)
    Q_PROPERTY(QColor green READ green NOTIFY changed FINAL)
    Q_PROPERTY(QColor cyan READ cyan NOTIFY changed FINAL)
    Q_PROPERTY(QColor blue READ blue NOTIFY changed FINAL)
    Q_PROPERTY(QColor magenta READ magenta NOTIFY changed FINAL)
    Q_PROPERTY(QColor brown READ brown NOTIFY changed FINAL)
    Q_PROPERTY(QColor darkBackground READ darkBackground NOTIFY changed FINAL)
    Q_PROPERTY(QColor darkerBackground READ darkerBackground NOTIFY changed FINAL)
    Q_PROPERTY(QColor lighterBackground READ lighterBackground NOTIFY changed FINAL)

    // ---- semantic state roles ----------------------------------------------------------
    Q_PROPERTY(QColor stateDownloading READ blue NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateSeeding READ green NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateStopped READ dimForeground NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateError READ red NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateChecking READ yellow NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateQueued READ magenta NOTIFY changed FINAL)
    Q_PROPERTY(QColor stateStalled READ orange NOTIFY changed FINAL)

    // ---- sizes and states --------------------------------------------------------------
    Q_PROPERTY(omaseed::integration::ThemeFont *font READ font CONSTANT FINAL)
    Q_PROPERTY(int rowHeight READ rowHeight NOTIFY changed FINAL)
    Q_PROPERTY(int controlHeight READ controlHeight NOTIFY changed FINAL)
    Q_PROPERTY(int railRowHeight READ railRowHeight NOTIFY changed FINAL)
    /// The keyboard-focus ring (review Q6): a 1 px accent stroke drawn `focusGap` outside the
    /// control's edge, so it never reads as the primary button's own accent outline.
    /// `focusExtent` is how far the ring reaches past the edge; a parent that clips leaves
    /// that much room around its focusable children.
    Q_PROPERTY(int focusGap READ focusGap NOTIFY changed FINAL)
    Q_PROPERTY(int focusExtent READ focusExtent NOTIFY changed FINAL)
    Q_PROPERTY(qreal fillIdle READ fillIdle NOTIFY changed FINAL)
    Q_PROPERTY(qreal fillHover READ fillHover NOTIFY changed FINAL)
    Q_PROPERTY(qreal fillSelected READ fillSelected NOTIFY changed FINAL)
    Q_PROPERTY(qreal fillPressed READ fillPressed NOTIFY changed FINAL)
    Q_PROPERTY(qreal borderIdle READ borderIdle NOTIFY changed FINAL)
    Q_PROPERTY(qreal borderHover READ borderHover NOTIFY changed FINAL)
    Q_PROPERTY(qreal hairlineAlpha READ hairlineAlpha NOTIFY changed FINAL)
    /// The spec's three alphas outside the shell's set, fixed by the spec rather than read
    /// from shell.toml: the rule between table and palette rows (`.tbl .r`, `.palette .it`,
    /// 5 %), a selected row under the cursor (`.tbl .r.sel.cur`, 24 %) and the rule under a
    /// key/value row (`.kv`, 6 %).
    Q_PROPERTY(qreal rowRuleAlpha READ rowRuleAlpha CONSTANT FINAL)
    Q_PROPERTY(qreal fillSelectedCursor READ fillSelectedCursor CONSTANT FINAL)
    Q_PROPERTY(qreal keyValueRuleAlpha READ keyValueRuleAlpha CONSTANT FINAL)

    // ---- identity and window -----------------------------------------------------------
    Q_PROPERTY(QString mode READ mode NOTIFY changed FINAL)
    Q_PROPERTY(QString themeName READ themeName NOTIFY changed FINAL)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed FINAL)
    /// Hyprland's `decoration:rounding` while it rounds corners; 0 while it cuts them
    /// (`rounding_power` below 2) or is not there. See windowCorners().
    Q_PROPERTY(int cornerRadius READ cornerRadius NOTIFY cornerRadiusChanged FINAL)
    /// Whether omaseed draws its own 1 px window edge: only without Hyprland. Under Hyprland
    /// the compositor's border is the edge, and its corners are Hyprland's.
    Q_PROPERTY(bool windowEdge READ windowEdge NOTIFY windowEdgeChanged FINAL)
    /// The ground's alpha as the shell wants it: `[launcher] background-alpha`, 0.95 when the
    /// key is missing (docs/DECISIONS.md Q1). `windowAlpha` starts here; the Appearance
    /// pane's override writes it, and a theme change only moves the default.
    Q_PROPERTY(qreal defaultWindowAlpha READ defaultWindowAlpha NOTIFY changed FINAL)
    Q_PROPERTY(
        qreal windowAlpha READ windowAlpha WRITE setWindowAlpha NOTIFY windowAlphaChanged FINAL)

public:
    /// How Theme reaches Hyprland: the `hyprctl` it asks for the rounding, and the event
    /// socket whose `configreloaded` makes it ask again (#37). Tests point both elsewhere.
    struct Hyprland
    {
        QString hyprctl = QStringLiteral("hyprctl");
        QString eventSocket = defaultEventSocket();
    };
    /// The session's event socket, "" when not under Hyprland.
    static QString defaultEventSocket();

    explicit Theme(ThemeReader *reader, QObject *parent = nullptr);
    Theme(ThemeReader *reader, const Hyprland &hyprland, QObject *parent = nullptr);
    ~Theme() override;

    /// The reader a QML-engine-created Theme binds to. main() sets this before the engine
    /// exists; anything else (the QML tests, a tool) gets a reader on the default paths.
    static void setDefaultReader(ThemeReader *reader);
    /// Factory for the QML singleton registration. The engine takes ownership.
    static Theme *createForEngine(QQmlEngine *engine);

    QColor background() const { return m_background; }
    QColor foreground() const { return m_foreground; }
    QColor dimForeground() const { return m_dimForeground; }
    QColor brightForeground() const { return m_brightForeground; }
    QColor accent() const { return m_accent; }
    QColor muted() const { return m_muted; }
    QColor selection() const { return m_selection; }
    QColor red() const { return m_red; }
    QColor yellow() const { return m_yellow; }
    QColor orange() const { return m_orange; }
    QColor green() const { return m_green; }
    QColor cyan() const { return m_cyan; }
    QColor blue() const { return m_blue; }
    QColor magenta() const { return m_magenta; }
    QColor brown() const { return m_brown; }
    QColor darkBackground() const { return m_darkBackground; }
    QColor darkerBackground() const { return m_darkerBackground; }
    QColor lighterBackground() const { return m_lighterBackground; }

    ThemeFont *font() const { return m_font; }
    int rowHeight() const;
    int controlHeight() const;
    int railRowHeight() const;
    int focusGap() const { return space(2); }
    int focusExtent() const { return focusGap() + 1; }
    qreal fillIdle() const { return m_fillIdle; }
    qreal fillHover() const { return m_fillHover; }
    qreal fillSelected() const { return m_fillSelected; }
    qreal fillPressed() const { return m_fillPressed; }
    qreal borderIdle() const { return m_borderIdle; }
    qreal borderHover() const { return m_borderHover; }
    qreal hairlineAlpha() const { return m_hairlineAlpha; }
    static constexpr qreal rowRuleAlpha() { return 0.05; }
    static constexpr qreal fillSelectedCursor() { return 0.24; }
    static constexpr qreal keyValueRuleAlpha() { return 0.06; }

    QString mode() const { return m_mode; }
    QString themeName() const { return m_themeName; }
    QString fontFamily() const { return m_fontFamily; }
    int cornerRadius() const { return m_cornerRadius; }
    bool windowEdge() const { return m_windowEdge; }
    qreal defaultWindowAlpha() const { return m_defaultWindowAlpha; }
    qreal windowAlpha() const { return m_windowAlpha; }
    void setWindowAlpha(qreal alpha);

    /// Spacing in spec pixels, scaled by `[spacing] scale` and the base size.
    Q_INVOKABLE int space(qreal px) const;
    /// The foreground at an alpha: 0.04 idle, 0.08 hover, 0.18 selected, 0.22 pressed.
    Q_INVOKABLE QColor fill(qreal alpha) const;

    /// WCAG 2 contrast ratio of two opaque colours, 1 to 21: relative luminance from the
    /// sRGB-linearised channels, then (lighter + 0.05) / (darker + 0.05).
    static qreal contrastRatio(const QColor &a, const QColor &b);
    /// `dim` itself when it already reads at `minimum` against `against`; otherwise the first
    /// colour on the line from `dim` towards `toward`, walked in 1/256 steps, that does, or
    /// `toward` when even that falls short (docs/DECISIONS.md Q19, the floor on dimForeground).
    static QColor
    liftToContrast(const QColor &dim, const QColor &toward, const QColor &against, qreal minimum);

    struct WindowCorners
    {
        int radius = 0;
        bool edge = true;
        bool operator==(const WindowCorners &) const = default;
    };
    /// What `hyprctl -j getoption` said for `decoration:rounding` and
    /// `decoration:rounding_power` (empty when hyprctl failed) → the radius omaseed's
    /// surfaces use and whether it draws its own window edge.
    ///  - No rounding answer: no Hyprland. Radius 0, and omaseed's edge is the only one.
    ///  - Power below 2: Hyprland cuts its corners instead of rounding them (it documents
    ///    2–10; 1 is a straight chamfer). A Qt radius can only round, so the surfaces stay
    ///    square and the compositor's border shows the cut (omarchy-console holds the
    ///    shell's corner radius at 0 by the same rule).
    ///  - Otherwise the rounding, as before. A Hyprland without the power option rounds.
    static WindowCorners windowCorners(const QByteArray &rounding, const QByteArray &roundingPower);

Q_SIGNALS:
    void changed();
    void windowAlphaChanged();
    void cornerRadiusChanged();
    void windowEdgeChanged();

private:
    void adopt();
    void readCornerRadius();
    void askHyprland(const QString &option, std::function<void(const QByteArray &)> done);
    void applyCorners(const WindowCorners &corners);

    ThemeReader *m_reader = nullptr;
    ThemeFont *m_font = nullptr;
    Hyprland m_hyprland;
    HyprlandEvents *m_hyprlandEvents = nullptr;
    /// Bumped by each readCornerRadius(): an answer to an older read is dropped, so two
    /// reloads in a row can't end on the first one's values.
    int m_cornerRead = 0;

    QColor m_background{QStringLiteral("#1a1b26")};
    QColor m_foreground{QStringLiteral("#a9b1d6")};
    QColor m_dimForeground{QStringLiteral("#565f89")};
    QColor m_brightForeground{QStringLiteral("#c0caf5")};
    QColor m_accent{QStringLiteral("#7aa2f7")};
    QColor m_muted{QStringLiteral("#414868")};
    QColor m_selection{QStringLiteral("#292e42")};
    QColor m_red{QStringLiteral("#f7768e")};
    QColor m_yellow{QStringLiteral("#e0af68")};
    QColor m_orange{QStringLiteral("#eb927b")};
    QColor m_green{QStringLiteral("#9ece6a")};
    QColor m_cyan{QStringLiteral("#449dab")};
    QColor m_blue{QStringLiteral("#7aa2f7")};
    QColor m_magenta{QStringLiteral("#ad8ee6")};
    QColor m_brown{QStringLiteral("#75493d")};
    QColor m_darkBackground{QStringLiteral("#13141c")};
    QColor m_darkerBackground{QStringLiteral("#0e0e14")};
    QColor m_lighterBackground{QStringLiteral("#24283b")};

    qreal m_spacingScale = 1.0;
    bool m_spacingScaleWithFont = true;
    qreal m_fillIdle = 0.04;
    qreal m_fillHover = 0.08;
    qreal m_fillSelected = 0.18;
    qreal m_fillPressed = 0.22;
    qreal m_borderIdle = 0.40;
    qreal m_borderHover = 0.25;
    qreal m_hairlineAlpha = 0.12;

    QString m_mode = QStringLiteral("dark");
    QString m_themeName = QStringLiteral("tokyo-night");
    QString m_fontFamily = QStringLiteral("monospace");
    int m_cornerRadius = 0;
    /// Until hyprctl answers, guessed from the environment, so a Hyprland session never
    /// flashes a second border while the first answer is on its way.
    bool m_windowEdge = !qEnvironmentVariableIsSet("HYPRLAND_INSTANCE_SIGNATURE");
    qreal m_defaultWindowAlpha = 0.95;
    qreal m_windowAlpha = 0.95;
};

} // namespace omaseed::integration
