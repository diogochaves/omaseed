#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace omaseed::integration {

/// One ground as a skin draws it (docs/DECISIONS.md C30's shape): `tint` at `alpha` over the
/// ground `over`. `tint` names a theme role (`foreground`, `lighterBackground`…) or one of the
/// skin's roles (`accent`, `signal`, `alert`, `warn`); `over` is `window` or another ground.
/// `shellAlpha` is the file's `"alpha": "shell"`: the alpha Omarchy's skin gives that ground
/// (shell.toml's fill for a fill, the spec's for a tint), which Theme knows and this doesn't.
struct SkinGround
{
    QString tint;
    qreal alpha = 0;
    bool shellAlpha = false;
    QString over = QStringLiteral("window");
    bool operator==(const SkinGround &) const = default;
};

/// A skin's tokens, read and checked (the console skin's design note, "The shape"). Only
/// what the file set, in the right shape, is here: a token left out or malformed is
/// Omarchy's, which every reader of a token passes as its default (`Theme.skin.n(name,
/// default)`). Omarchy is the skin with no overrides, so its file reads to an empty set.
struct Skin
{
    /// The skin's own name (`"skin"` in the file); "omarchy" when the file was not drawn.
    QString name = QStringLiteral("omarchy");
    /// The file's version, 0 when it has none that reads.
    int version = 0;
    /// Why the file is drawn as Omarchy, "" when it is drawn as written: "missing",
    /// "unparseable", "no version", "newer", "unknown skin".
    QString reason;
    /// The token paths that were in the file but malformed, and so are Omarchy's
    /// ("line.hair", "grounds.panel", "glow"…). For a log line and the tests.
    QStringList dropped;

    /// Which theme role plays each of the skin's roles: accent, signal, alert, warn. Always
    /// all four (Omarchy's when the file says nothing).
    QHash<QString, QString> roles;
    /// The grounds the file sets; every other ground is Omarchy's (SkinReader::omarchyGrounds).
    QHash<QString, SkinGround> grounds;
    /// Every other token the file set, by group, as QML reads it (`Theme.skin.tokens`):
    /// `line`, `cut`, `font`, `label`, `density`, `glyphs`, `labels`, `faces` (maps), and
    /// `radius`, `numerals`, `glow` (values). A group the file left out is absent.
    QVariantMap tokens;

    /// Whether the file is drawn (it read, and is not newer than omaseed).
    bool drawn() const { return reason.isEmpty(); }
    bool operator==(const Skin &) const = default;
};

/// Reads a skin file (`skin.json`) into a Skin: pure Qt, no QML (the design note's
/// "Loading"). A file is all or nothing: one that doesn't parse, has no integer `version`, or
/// is newer than omaseed is drawn as Omarchy with a reason; one that reads keeps every token
/// it carries in the right shape, and a token it leaves out or gets wrong is Omarchy's.
class SkinReader
{
public:
    /// The token set's version this omaseed reads: a file of this version or older is read,
    /// a newer one is drawn as Omarchy (omarchy-console's `kitReads` rule).
    static constexpr int kVersion = 1;

    /// The built-in skins, Omarchy first: the default, then the others in Appearance's order.
    static QStringList builtInNames();
    /// Where a built-in skin's file is (a Qt resource), "" for a name that isn't one.
    static QString builtInPath(const QString &name);
    /// A built-in skin, read. A name that isn't one is Omarchy with reason "unknown skin".
    static Skin builtIn(const QString &name);
    /// The skin `OMASEED_SKIN` asks for (tests and walks), "omarchy" when it is unset.
    static QString requestedName();

    /// A skin file's text, read and checked. `appVersion` is the newest version read.
    static Skin parse(const QByteArray &json, int appVersion = kVersion);

    /// Omarchy's recipe for every ground a skin may set, as Theme paints them without one
    /// (docs/DECISIONS.md C30 F7). Each takes its alpha from the shell (`shellAlpha`).
    static const QHash<QString, SkinGround> &omarchyGrounds();
    /// The grounds a skin may set, in the order Theme resolves them (each after its `over`).
    static const QStringList &groundNames();
    /// The theme roles a skin may name: Theme's colour roles.
    static const QStringList &themeRoles();
    /// The skin's own roles, and the theme role each is in Omarchy's skin.
    static const QHash<QString, QString> &omarchyRoles();
    /// The seams a skin may give a face (the design note's "Seams").
    static const QStringList &seams();

    /// Every ground's recipe under `skin`: Omarchy's, with the skin's laid over it. A skin
    /// recipe that would paint a ground over itself, through any chain of `over`, is
    /// dropped for Omarchy's, so the chain always ends on the window.
    static QHash<QString, SkinGround> groundsOf(const Skin &skin);
    /// `grounds`' names, each after the ground it is painted over: the order to resolve them.
    static QStringList groundOrder(const QHash<QString, SkinGround> &grounds);

    /// The families a skin names (`font.label`, `.value`, `.kana`, `.icons`, in that order),
    /// "" (fontconfig's monospace) left out.
    static QStringList fontFamilies(const Skin &skin);
    /// Those of `skin`'s families that aren't in `installed` (QFontDatabase::families()),
    /// compared without case. omaseed bundles no font (the console skin's note, question 1),
    /// so Appearance names what is missing and the Kit falls back per family.
    static QStringList missingFonts(const Skin &skin, const QStringList &installed);
};

/// `Theme.skin`: the skin Theme draws, for QML. Its tokens change only with the skin (the
/// Settings switch, step 2 of #171), and `changed()` says so; a binding that reads `tokens`
/// follows it, while one that only calls `n()` does not (an invokable reads no property).
class ThemeSkin : public QObject
{
    Q_OBJECT

    /// The skin's name ("omarchy", "console").
    Q_PROPERTY(QString name READ name NOTIFY changed FINAL)
    /// The skin that was asked for, which may not be the one drawn (see reason).
    Q_PROPERTY(QString requested READ requested NOTIFY changed FINAL)
    Q_PROPERTY(int version READ version NOTIFY changed FINAL)
    /// Why the asked-for skin is drawn as Omarchy, "" when it is drawn as written.
    Q_PROPERTY(QString reason READ reason NOTIFY changed FINAL)
    /// The skin's roles by name: accent, signal, alert, warn → a theme role's name.
    Q_PROPERTY(QVariantMap roles READ roles NOTIFY changed FINAL)
    /// Every token the skin set, by group (Skin::tokens), grounds and roles aside.
    Q_PROPERTY(QVariantMap tokens READ tokens NOTIFY changed FINAL)
    /// The desktop asks for the shell's font everywhere (omarchy-console's `fx.fonts =
    /// system`), while omaseed follows the desktop's skin: the skin's font tokens give way to
    /// Theme.fontFamily. Always false for a skin chosen by hand.
    Q_PROPERTY(bool systemFonts READ systemFonts NOTIFY changed FINAL)
    /// The skin's font families that are not installed (SkinReader::missingFonts); empty
    /// while systemFonts holds, since then none is used. Read when the skin changes.
    Q_PROPERTY(QStringList missingFonts READ missingFonts NOTIFY changed FINAL)

public:
    explicit ThemeSkin(QObject *parent = nullptr);

    QString name() const { return m_skin.name; }
    QString requested() const { return m_requested; }
    int version() const { return m_skin.version; }
    QString reason() const { return m_skin.reason; }
    QVariantMap roles() const;
    QVariantMap tokens() const { return m_skin.tokens; }
    bool systemFonts() const { return m_systemFonts; }
    QStringList missingFonts() const;
    const Skin &skin() const { return m_skin; }

    /// A built-in skin's font families that are not installed, whichever skin is drawn:
    /// Appearance's skin row names Console's before it is picked.
    Q_INVOKABLE QStringList missingFontsOf(const QString &name) const;

    /// A density token in spec px (`"titleBarHeight": 54`), or `fallback`: Theme.qml's own
    /// value, which is Omarchy's (the design note's `skin.n("titleBarHeight", space(46))`).
    Q_INVOKABLE QVariant n(const QString &name, const QVariant &fallback) const;
    /// Any token by its path, `"line.menuEdge"`, `"font.label"`, `"radius"`, or `fallback`.
    Q_INVOKABLE QVariant value(const QString &path, const QVariant &fallback) const;
    /// The skin's glyph for a Glyphs.qml name (`ms:<name>` for an icon-font ligature), or
    /// `fallback`.
    Q_INVOKABLE QString glyph(const QString &name, const QString &fallback) const;
    /// The skin's word for a label id (a kana word), or `fallback`.
    Q_INVOKABLE QString label(const QString &id, const QString &fallback) const;

    /// Draw `skin`, asked for as `requested`. Emits changed() when anything moved.
    void set(const Skin &skin, const QString &requested);
    /// Record the desktop's `fx.fonts = system` (see systemFonts). Emits changed() when it moved.
    void setSystemFonts(bool system);

Q_SIGNALS:
    void changed();

private:
    Skin m_skin;
    QString m_requested = QStringLiteral("omarchy");
    bool m_systemFonts = false;
};

} // namespace omaseed::integration
