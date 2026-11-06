#include "integration/DesktopSkin.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <cmath>

namespace omaseed::integration {

namespace {

Q_LOGGING_CATEGORY(lcDesktopSkin, "omaseed.skin.desktop")

/// A burst of renames (a theme switch writes several files) becomes one read.
constexpr int kDebounceMs = 100;

QString themeSkinPath(const ThemeReader::Paths &paths)
{
    return paths.stateCurrentDir + QStringLiteral("/theme/console-skin.json");
}

QString configSkinPath(const ThemeReader::Paths &paths)
{
    return paths.configOmarchyDir + QStringLiteral("/console-skin.json");
}

QString consoleJsonPath(const ThemeReader::Paths &paths)
{
    return paths.configOmarchyDir + QStringLiteral("/console.json");
}

std::optional<QByteArray> readFile(const QString &path)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

/// omarchy-console's Skin.js `parseSkin`, as far as on/off goes: an object whose `version`
/// is a positive integer. "" when it reads, else the reason.
QString skinFileReason(const QByteArray &text)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(text, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("unparseable");
    const QJsonValue version = document.object().value(QStringLiteral("version"));
    if (!version.isDouble())
        return QStringLiteral("no version");
    const double number = version.toDouble();
    if (number < 1 || std::floor(number) != number)
        return QStringLiteral("no version");
    return QString();
}

} // namespace

DesktopSkin::State DesktopSkin::resolve(const std::optional<QByteArray> &themeSkin,
                                        const std::optional<QByteArray> &configSkin,
                                        const std::optional<QByteArray> &consoleJson)
{
    State state;
    QJsonObject fx;
    if (consoleJson) {
        const QJsonDocument document = QJsonDocument::fromJson(*consoleJson);
        fx = document.object().value(QStringLiteral("fx")).toObject();
    }
    state.systemFonts = fx.value(QStringLiteral("fonts")).toString() == QLatin1String("system");

    // The first file that exists is the one read, as the kit does: a theme's file that
    // doesn't read turns the skin off even with a good one in ~/.config/omarchy.
    const std::optional<QByteArray> &text = themeSkin ? themeSkin : configSkin;
    state.source = themeSkin    ? QStringLiteral("theme")
                   : configSkin ? QStringLiteral("config")
                                : QString();
    if (fx.value(QStringLiteral("skin")).toString() == QLatin1String("off"))
        state.reason = QStringLiteral("off");
    else if (!text)
        state.reason = QStringLiteral("missing");
    else
        state.reason = skinFileReason(*text);
    state.on = state.reason.isEmpty();
    return state;
}

DesktopSkin::State DesktopSkin::read(const ThemeReader::Paths &paths)
{
    return resolve(readFile(themeSkinPath(paths)),
                   readFile(configSkinPath(paths)),
                   readFile(consoleJsonPath(paths)));
}

DesktopSkin::DesktopSkin(ThemeReader::Paths paths, QObject *parent)
    : QObject(parent)
    , m_paths(std::move(paths))
    , m_state(read(m_paths))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &DesktopSkin::refresh);
    const auto touched = [this](const QString &) {
        rewatch();
        m_debounce.start();
    };
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, touched);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, touched);
    rewatch();
    qCInfo(lcDesktopSkin) << "desktop skin:" << (m_state.on ? "on" : "off") << m_state.reason
                          << m_state.source;
}

void DesktopSkin::refresh()
{
    rewatch();
    const State next = read(m_paths);
    if (next == m_state)
        return;
    m_state = next;
    qCInfo(lcDesktopSkin) << "desktop skin:" << (m_state.on ? "on" : "off") << m_state.reason
                          << m_state.source;
    Q_EMIT changed();
}

void DesktopSkin::rewatch()
{
    // Each file is replaced by rename (console.json by the console's panel, the theme folder
    // by omarchy-theme-set), so the folders are what is watched, the files too while they
    // exist, and everything again after each change.
    const QStringList watched = m_watcher.directories() + m_watcher.files();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    QStringList wanted{
        m_paths.stateCurrentDir,
        m_paths.stateCurrentDir + QStringLiteral("/theme"),
        themeSkinPath(m_paths),
        m_paths.configOmarchyDir,
        configSkinPath(m_paths),
        consoleJsonPath(m_paths),
    };
    wanted.removeIf([](const QString &path) { return !QFileInfo::exists(path); });
    if (!wanted.isEmpty())
        m_watcher.addPaths(wanted);
}

} // namespace omaseed::integration
