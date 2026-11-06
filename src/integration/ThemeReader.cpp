#include "integration/ThemeReader.hpp"

#include "integration/AppIdentity.hpp"
#include "integration/DesktopSkin.hpp"

#include <QCryptographicHash>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>

namespace omaseed::integration {

namespace {

Q_LOGGING_CATEGORY(lcTheme, "omaseed.theme")

constexpr int kDebounceMs = 750;

/// bash's `[[ ${map[key]} ]]`: set and not empty.
bool hasValue(const ColorMap &map, const QString &key)
{
    const auto it = map.constFind(key);
    return it != map.constEnd() && !it->isEmpty();
}

/// bash's `${map[key]:-fallback}`.
QString valueOr(const ColorMap &map, const QString &key, const QString &fallback)
{
    return hasValue(map, key) ? map.value(key) : fallback;
}

/// bash's `alias_theme_color`: fill an empty or missing key from another one. The key is
/// created even when the source is missing, which is why the key sets match the shell's.
void aliasColor(ColorMap &map, const QString &key, const QString &fallback)
{
    if (!hasValue(map, key))
        map.insert(key, map.value(fallback));
}

/// One hex digit's value, or -1 when the character is not a hex digit — the same answer
/// `omarchy-theme-color`'s awk gives.
int hexDigit(QChar character)
{
    static const QString digits = QStringLiteral("0123456789abcdef");
    return static_cast<int>(digits.indexOf(character.toLower()));
}

/// One hex pair. Characters past the end of the text count as zero, which is what makes a
/// missing colour behave as #000000 in the shell's mixer and therefore here too.
int hexPair(QStringView text, qsizetype index)
{
    const int high = index < text.size() ? hexDigit(text.at(index)) : 0;
    const int low = index + 1 < text.size() ? hexDigit(text.at(index + 1)) : 0;
    return high * 16 + low;
}

/// Split `#rrggbb` into three channels the lenient way the shell's mixer does.
void channels(const QString &colour, int channel[3])
{
    QStringView text(colour);
    if (text.startsWith(QLatin1Char('#')))
        text = text.mid(1);
    for (int i = 0; i < 3; ++i)
        channel[i] = hexPair(text, i * 2);
}

/// Strict `#rrggbb` check, for the luminance branch of the mode cascade.
bool isHexColor(const QString &colour)
{
    static const QRegularExpression re(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    return re.match(colour).hasMatch();
}

QString readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream stream(&file);
    return stream.readAll();
}

} // namespace

// -------------------------------------------------------------------------------------
// The D-Bus face. The theme-set hook (T1.8) calls Reload() after omarchy-theme-set has
// moved the new theme into place, so we do not have to wait out the debounce.
// -------------------------------------------------------------------------------------
class ThemeDBusAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.diogochaves.omaseed.Theme")

public:
    explicit ThemeDBusAdaptor(ThemeReader *reader)
        : QDBusAbstractAdaptor(reader)
        , m_reader(reader)
    {}

public Q_SLOTS:
    void Reload() { m_reader->reload(); } // NOLINT: the D-Bus method name is capitalised

private:
    ThemeReader *m_reader;
};

// -------------------------------------------------------------------------------------
// Paths
// -------------------------------------------------------------------------------------

ThemeReader::Paths ThemeReader::Paths::fromEnvironment()
{
    Paths paths;
    const QString state = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    paths.stateCurrentDir = state + QStringLiteral("/omarchy/current");
    paths.configOmarchyDir = config + QStringLiteral("/omarchy");
    return paths;
}

// -------------------------------------------------------------------------------------
// Construction
// -------------------------------------------------------------------------------------

ThemeReader::ThemeReader(QObject *parent)
    : ThemeReader(Paths::fromEnvironment(), parent)
{}

ThemeReader::ThemeReader(Paths paths, QObject *parent)
    : QObject(parent)
    , m_paths(std::move(paths))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &ThemeReader::reload);
    connectWatcher();
    reload();
    // What a walk reads to know which skin is drawn (scripts/dev/settings-walk.sh).
    connect(this, &ThemeReader::skinChanged, this, [this]() {
        qCInfo(lcTheme).noquote() << QStringLiteral("skin: %1 (choice %2, fonts %3)")
                                         .arg(skinName(),
                                              skinChoiceInForce(),
                                              skinSystemFonts() ? QStringLiteral("system")
                                                                : QStringLiteral("the skin's"));
    });
    followDesktopSkin();
}

void ThemeReader::connectWatcher()
{
    const auto touched = [this](const QString &) {
        rewatch();
        m_debounce.start();
    };
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, touched);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, touched);
}

bool ThemeReader::exportToDBus(QDBusConnection connection)
{
    if (!connection.isConnected()) {
        qCInfo(lcTheme) << "not on the session bus; the theme-set hook cannot reach us";
        return false;
    }

    // One adaptor per reader, however many connections it is exported on.
    if (!findChild<ThemeDBusAdaptor *>(QString(), Qt::FindDirectChildrenOnly))
        new ThemeDBusAdaptor(this);
    if (!connection.registerObject(AppIdentity::dbusThemeObjectPath(), this)) {
        qCWarning(lcTheme) << "could not export" << AppIdentity::dbusThemeObjectPath();
        return false;
    }
    return true;
}

// -------------------------------------------------------------------------------------
// Reading
// -------------------------------------------------------------------------------------

QString ThemeReader::bundledColorsPath()
{
    return QStringLiteral(":/omaseed/theme/tokyo-night/colors.toml");
}

void ThemeReader::setFollowOmarchy(bool follow)
{
    if (m_followOmarchy == follow)
        return;
    m_followOmarchy = follow;
    reload();
}

void ThemeReader::setBaseSizeOverride(int pixels)
{
    const int clamped = pixels > 0 ? pixels : 0;
    if (m_baseSizeOverride == clamped)
        return;
    m_baseSizeOverride = clamped;
    reload();
}

void ThemeReader::setSkinChoice(const QString &choice)
{
    const QString wanted = choice == QLatin1String("console") || choice == QLatin1String("desktop")
                               ? choice
                               : QStringLiteral("omarchy");
    if (m_skinChoice == wanted)
        return;
    const QString name = skinName();
    const bool systemFonts = skinSystemFonts();
    m_skinChoice = wanted;
    followDesktopSkin();
    if (skinName() != name || skinSystemFonts() != systemFonts)
        Q_EMIT skinChanged();
}

QString ThemeReader::skinChoiceInForce() const
{
    const QString pinned = qEnvironmentVariable("OMASEED_SKIN").trimmed();
    return pinned.isEmpty() ? m_skinChoice : pinned;
}

QString ThemeReader::skinName() const
{
    const QString choice = skinChoiceInForce();
    if (choice != QLatin1String("desktop"))
        return choice;
    return desktopSkinOn() ? QStringLiteral("console") : QStringLiteral("omarchy");
}

bool ThemeReader::skinSystemFonts() const
{
    return m_desktopSkin && m_desktopSkin->on() && m_desktopSkin->systemFonts();
}

bool ThemeReader::desktopSkinOn() const
{
    return m_desktopSkin && m_desktopSkin->on();
}

void ThemeReader::followDesktopSkin()
{
    const bool following = skinChoiceInForce() == QLatin1String("desktop");
    if (following == (m_desktopSkin != nullptr))
        return;
    if (!following) {
        delete m_desktopSkin;
        m_desktopSkin = nullptr;
        return;
    }
    m_desktopSkin = new DesktopSkin(m_paths, this);
    connect(m_desktopSkin, &DesktopSkin::changed, this, &ThemeReader::skinChanged);
    // omarchy-theme-set renames the whole theme folder; the reader sees that first.
    connect(this, &ThemeReader::changed, m_desktopSkin, &DesktopSkin::refresh);
}

QString ThemeReader::findColorsFile(bool *lightModeFile) const
{
    // "Follow the Omarchy theme" off: the bundled Tokyo Night is the only candidate, so the
    // app keeps its own look whatever `omarchy-theme-set` does.
    const QStringList candidates = m_followOmarchy
                                       ? QStringList{
                                             m_paths.stateCurrentDir
                                                 + QStringLiteral("/theme/colors.toml"),
                                             m_paths.configOmarchyDir
                                                 + QStringLiteral("/current/theme/colors.toml"),
                                             bundledColorsPath(),
                                         }
                                       : QStringList{bundledColorsPath()};
    for (const QString &candidate : candidates) {
        if (!QFileInfo::exists(candidate))
            continue;
        if (lightModeFile) {
            const QString beside =
                QFileInfo(candidate).absolutePath() + QStringLiteral("/light.mode");
            *lightModeFile = QFileInfo::exists(beside);
        }
        return candidate;
    }
    return {};
}

void ThemeReader::reload()
{
    bool lightModeFile = false;
    const QString colorsPath = findColorsFile(&lightModeFile);
    const ColorMap raw = parseToml(readTextFile(colorsPath));
    ColorMap palette = resolvePalette(raw, lightModeFile);

    // shell.toml: the theme's generated one first, the user's overlay on top.
    QHash<QString, QString> shell =
        parseToml(readTextFile(m_paths.stateCurrentDir + QStringLiteral("/theme/shell.toml")));
    const QHash<QString, QString> overlay =
        parseToml(readTextFile(m_paths.configOmarchyDir + QStringLiteral("/shell.toml")));
    for (auto it = overlay.constBegin(); it != overlay.constEnd(); ++it)
        shell.insert(it.key(), it.value());

    if (m_baseSizeOverride > 0)
        shell.insert(QStringLiteral("font.base-size"), QString::number(m_baseSizeOverride));

    QString name =
        m_followOmarchy
            ? readTextFile(m_paths.stateCurrentDir + QStringLiteral("/theme.name")).trimmed()
            : QString();
    if (name.isEmpty() && !colorsPath.isEmpty())
        name = QFileInfo(colorsPath).absolutePath().section(QLatin1Char('/'), -1);
    if (name.isEmpty())
        name = QStringLiteral("tokyo-night");

    // One fingerprint over everything we expose, so an identical rewrite stays quiet.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto feed = [&hash](const QHash<QString, QString> &map) {
        QStringList keys = map.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &key : std::as_const(keys)) {
            hash.addData(key.toUtf8());
            hash.addData(QByteArrayLiteral("="));
            hash.addData(map.value(key).toUtf8());
            hash.addData(QByteArrayLiteral("\n"));
        }
    };
    feed(palette);
    feed(shell);
    hash.addData(name.toUtf8());
    const QByteArray fingerprint = hash.result();

    m_source = colorsPath;
    rewatch();

    if (fingerprint == m_fingerprint) {
        // Reached from both the file watcher and the theme-set hook's D-Bus Reload() call
        // (T1.8); logging the skip is what lets a coalesced reload be told apart from one
        // that never ran at all.
        qCDebug(lcTheme) << "reload: fingerprint unchanged, not reapplying" << m_themeName;
        return;
    }

    m_fingerprint = fingerprint;
    m_palette = std::move(palette);
    m_shell = std::move(shell);
    m_themeName = name;
    qCInfo(lcTheme) << "theme changed:" << m_themeName;
    Q_EMIT changed();
}

void ThemeReader::rewatch()
{
    // Everything is replaced by rename at some point, so the watches are rebuilt from
    // scratch: the stable parent directory first, then whatever currently exists inside it.
    const QStringList watched = m_watcher.directories() + m_watcher.files();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);

    QStringList wanted{
        m_paths.stateCurrentDir,
        m_paths.stateCurrentDir + QStringLiteral("/theme"),
        m_paths.stateCurrentDir + QStringLiteral("/theme/colors.toml"),
        m_paths.stateCurrentDir + QStringLiteral("/theme/shell.toml"),
        m_paths.stateCurrentDir + QStringLiteral("/theme.name"),
        m_paths.configOmarchyDir,
        m_paths.configOmarchyDir + QStringLiteral("/shell.toml"),
        m_paths.configOmarchyDir + QStringLiteral("/current/theme"),
        m_paths.configOmarchyDir + QStringLiteral("/current/theme/colors.toml"),
    };
    wanted.removeIf([](const QString &path) { return !QFileInfo::exists(path); });
    if (!wanted.isEmpty())
        m_watcher.addPaths(wanted);
}

// -------------------------------------------------------------------------------------
// Parsing
// -------------------------------------------------------------------------------------

QHash<QString, QString> ThemeReader::parseToml(const QString &text)
{
    static const QRegularExpression sectionRe(QStringLiteral("^\\[([A-Za-z0-9_.-]+)\\]"));
    static const QRegularExpression keyRe(QStringLiteral("^[A-Za-z0-9_-]+$"));
    // The value charset omarchy-theme-color accepts; anything else is dropped there too.
    static const QRegularExpression valueRe(QStringLiteral("^[A-Za-z0-9#(),._+/% -]*$"));

    QHash<QString, QString> values;
    QString section;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        const QRegularExpressionMatch sectionMatch = sectionRe.match(line);
        if (sectionMatch.hasMatch()) {
            section = sectionMatch.captured(1);
            continue;
        }

        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;

        QString key = line.left(equals);
        key.remove(QLatin1Char('"'));
        key.remove(QLatin1Char('\''));
        key.remove(QLatin1Char(' '));
        key.remove(QLatin1Char('\t'));
        if (key.isEmpty() || key.startsWith(QLatin1Char('#')))
            continue;
        if (!keyRe.match(key).hasMatch())
            continue;

        QString value = line.mid(equals + 1);
        const qsizetype quote = value.indexOf(QRegularExpression(QStringLiteral("[\"']")));
        if (quote >= 0) {
            value = value.mid(quote + 1);
            const qsizetype close = value.indexOf(QRegularExpression(QStringLiteral("[\"']")));
            value = close >= 0 ? value.left(close) : value;
        } else {
            value = value.trimmed();
            // A trailing comment after a bare value (`scale = 1.0  # roomier`). A value that
            // starts with '#' is a bare hex colour, so it is left whole.
            if (!value.startsWith(QLatin1Char('#'))) {
                static const QRegularExpression commentRe(QStringLiteral("\\s+#.*$"));
                value.remove(commentRe);
                value = value.trimmed();
            }
        }
        if (!valueRe.match(value).hasMatch())
            continue;

        values.insert(section.isEmpty() ? key : section + QLatin1Char('.') + key, value);
    }
    return values;
}

QString ThemeReader::mixColor(const QString &start, const QString &end, double amount)
{
    int from[3] = {0, 0, 0};
    int to[3] = {0, 0, 0};
    channels(start, from);
    channels(end, to);

    const double a = std::clamp(amount, 0.0, 1.0);
    QString out = QStringLiteral("#");
    for (int i = 0; i < 3; ++i) {
        const int mixed = static_cast<int>(from[i] * (1.0 - a) + to[i] * a + 0.5);
        out += QStringLiteral("%1").arg(mixed, 2, 16, QLatin1Char('0'));
    }
    return out;
}

QString ThemeReader::themeMode(const ColorMap &colors, bool lightModeFile)
{
    if (hasValue(colors, QStringLiteral("mode")))
        return colors.value(QStringLiteral("mode"));
    if (hasValue(colors, QStringLiteral("theme_type")))
        return colors.value(QStringLiteral("theme_type"));
    if (lightModeFile)
        return QStringLiteral("light");

    const QString background = colors.value(QStringLiteral("background"));
    if (isHexColor(background)) {
        int rgb[3] = {0, 0, 0};
        channels(background, rgb);
        return (rgb[0] + rgb[1] + rgb[2]) > 382 ? QStringLiteral("light") : QStringLiteral("dark");
    }
    return QStringLiteral("dark");
}

ColorMap ThemeReader::resolvePalette(const ColorMap &raw, bool lightModeFile)
{
    ColorMap c = raw;
    const QString black = QStringLiteral("#000000");
    const QString white = QStringLiteral("#ffffff");

    // The complete legacy short-name palette, before ANSI fallbacks or derived shades.
    // Canonical names win when a theme defines both.
    const QList<QPair<QString, QString>> shortNames{
        {QStringLiteral("background"), QStringLiteral("bg")},
        {QStringLiteral("dark_background"), QStringLiteral("dark_bg")},
        {QStringLiteral("darker_background"), QStringLiteral("darker_bg")},
        {QStringLiteral("lighter_background"), QStringLiteral("lighter_bg")},
        {QStringLiteral("foreground"), QStringLiteral("fg")},
        {QStringLiteral("dark_foreground"), QStringLiteral("dark_fg")},
        {QStringLiteral("light_foreground"), QStringLiteral("light_fg")},
        {QStringLiteral("bright_foreground"), QStringLiteral("bright_fg")},
    };
    for (const auto &[canonical, shortName] : shortNames)
        aliasColor(c, canonical, shortName);

    // Themes written before the semantic palette only define ANSI names.
    aliasColor(c, QStringLiteral("background"), QStringLiteral("color0"));
    aliasColor(c, QStringLiteral("foreground"), QStringLiteral("color7"));
    if (hasValue(c, QStringLiteral("background")))
        c.insert(QStringLiteral("color0"), c.value(QStringLiteral("background")));
    if (hasValue(c, QStringLiteral("foreground")))
        c.insert(QStringLiteral("color7"), c.value(QStringLiteral("foreground")));

    // ANSI color1..color14 under their semantic names.
    const QList<QPair<QString, QString>> ansiToSemantic{
        {QStringLiteral("red"), QStringLiteral("color1")},
        {QStringLiteral("green"), QStringLiteral("color2")},
        {QStringLiteral("yellow"), QStringLiteral("color3")},
        {QStringLiteral("blue"), QStringLiteral("color4")},
        {QStringLiteral("magenta"), QStringLiteral("color5")},
        {QStringLiteral("cyan"), QStringLiteral("color6")},
        {QStringLiteral("bright_red"), QStringLiteral("color9")},
        {QStringLiteral("bright_green"), QStringLiteral("color10")},
        {QStringLiteral("bright_yellow"), QStringLiteral("color11")},
        {QStringLiteral("bright_blue"), QStringLiteral("color12")},
        {QStringLiteral("bright_magenta"), QStringLiteral("color13")},
        {QStringLiteral("bright_cyan"), QStringLiteral("color14")},
    };
    for (const auto &[semantic, ansi] : ansiToSemantic)
        aliasColor(c, semantic, ansi);
    aliasColor(c, QStringLiteral("magenta"), QStringLiteral("purple"));
    aliasColor(c, QStringLiteral("bright_magenta"), QStringLiteral("bright_purple"));

    const QString foreground = c.value(QStringLiteral("foreground"));
    const QString background = c.value(QStringLiteral("background"));
    if (!hasValue(c, QStringLiteral("light_foreground")))
        c.insert(QStringLiteral("light_foreground"),
                 valueOr(c, QStringLiteral("color7"), foreground));
    if (!hasValue(c, QStringLiteral("bright_foreground")))
        c.insert(QStringLiteral("bright_foreground"),
                 valueOr(c, QStringLiteral("color15"), foreground));
    c.insert(QStringLiteral("cursor"), c.value(QStringLiteral("bright_foreground")));
    if (!hasValue(c, QStringLiteral("lighter_background")))
        c.insert(QStringLiteral("lighter_background"),
                 valueOr(c, QStringLiteral("color0"), background));
    if (!hasValue(c, QStringLiteral("dark_foreground")))
        c.insert(QStringLiteral("dark_foreground"),
                 valueOr(c, QStringLiteral("color8"), foreground));
    if (!hasValue(c, QStringLiteral("muted")))
        c.insert(QStringLiteral("muted"),
                 valueOr(c, QStringLiteral("color8"), c.value(QStringLiteral("dark_foreground"))));
    if (!hasValue(c, QStringLiteral("selection"))) {
        const QString cascade = valueOr(
            c,
            QStringLiteral("selection_background"),
            valueOr(c, QStringLiteral("color8"), valueOr(c, QStringLiteral("color0"), background)));
        c.insert(QStringLiteral("selection"), cascade);
    }
    aliasColor(c, QStringLiteral("selection_background"), QStringLiteral("selection"));
    aliasColor(c, QStringLiteral("selection_foreground"), QStringLiteral("bright_foreground"));
    aliasColor(c, QStringLiteral("orange"), QStringLiteral("yellow"));
    if (!hasValue(c, QStringLiteral("brown")))
        c.insert(QStringLiteral("brown"), mixColor(c.value(QStringLiteral("orange")), black, 0.5));

    // Shades derived from the base accents when a theme leaves them out.
    if (!hasValue(c, QStringLiteral("dark_background")))
        c.insert(QStringLiteral("dark_background"),
                 mixColor(c.value(QStringLiteral("background")), black, 0.25));
    if (!hasValue(c, QStringLiteral("darker_background")))
        c.insert(QStringLiteral("darker_background"),
                 mixColor(c.value(QStringLiteral("background")), black, 0.5));
    const QStringList brightened{
        QStringLiteral("red"),
        QStringLiteral("yellow"),
        QStringLiteral("green"),
        QStringLiteral("cyan"),
        QStringLiteral("blue"),
        QStringLiteral("magenta"),
    };
    for (const QString &base : brightened) {
        const QString bright = QStringLiteral("bright_") + base;
        if (!hasValue(c, bright))
            c.insert(bright, mixColor(c.value(base), white, 0.2));
    }
    aliasColor(c, QStringLiteral("purple"), QStringLiteral("magenta"));
    aliasColor(c, QStringLiteral("bright_purple"), QStringLiteral("bright_magenta"));

    // Semantic themes stay readable to consumers that still ask for ANSI names.
    const QList<QPair<QString, QString>> semanticToAnsi{
        {QStringLiteral("color0"), QStringLiteral("background")},
        {QStringLiteral("color1"), QStringLiteral("red")},
        {QStringLiteral("color2"), QStringLiteral("green")},
        {QStringLiteral("color3"), QStringLiteral("yellow")},
        {QStringLiteral("color4"), QStringLiteral("blue")},
        {QStringLiteral("color5"), QStringLiteral("magenta")},
        {QStringLiteral("color6"), QStringLiteral("cyan")},
        {QStringLiteral("color7"), QStringLiteral("foreground")},
        {QStringLiteral("color8"), QStringLiteral("muted")},
        {QStringLiteral("color9"), QStringLiteral("bright_red")},
        {QStringLiteral("color10"), QStringLiteral("bright_green")},
        {QStringLiteral("color11"), QStringLiteral("bright_yellow")},
        {QStringLiteral("color12"), QStringLiteral("bright_blue")},
        {QStringLiteral("color13"), QStringLiteral("bright_magenta")},
        {QStringLiteral("color14"), QStringLiteral("bright_cyan")},
        {QStringLiteral("color15"), QStringLiteral("bright_foreground")},
    };
    for (const auto &[ansi, semantic] : semanticToAnsi)
        aliasColor(c, ansi, semantic);

    // And canonical themes stay readable to templates written against the short names.
    for (const auto &[canonical, shortName] : shortNames) {
        if (hasValue(c, canonical))
            c.insert(shortName, c.value(canonical));
    }

    const QString mode = themeMode(c, lightModeFile);
    c.insert(QStringLiteral("mode"), mode);
    c.insert(QStringLiteral("theme_type"), mode);
    return c;
}

} // namespace omaseed::integration

#include "ThemeReader.moc"
