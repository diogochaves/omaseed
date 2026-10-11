#include "integration/Autostart.hpp"

#include "integration/AppIdentity.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace omaseed::integration::Autostart {

namespace {

/// One `Key=value` line's value from a desktop entry's [Desktop Entry] group, "" when absent.
QString entryValue(const QByteArray &contents, const QByteArray &key)
{
    bool inMain = false;
    for (const QByteArray &raw : contents.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (line.startsWith('[')) {
            inMain = line == "[Desktop Entry]";
            continue;
        }
        if (!inMain || line.startsWith('#'))
            continue;
        const int eq = line.indexOf('=');
        if (eq > 0 && line.left(eq).trimmed() == key)
            return QString::fromUtf8(line.mid(eq + 1).trimmed());
    }
    return QString();
}

} // namespace

QString hiddenFlag()
{
    return QStringLiteral("--hidden");
}

QString directory()
{
    const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString base =
        config.isEmpty() ? QDir::home().filePath(QStringLiteral(".config")) : config;
    return base + QStringLiteral("/autostart");
}

QString entryPath()
{
    return directory() + QLatin1Char('/') + AppIdentity::desktopFileName() +
           QStringLiteral(".desktop");
}

QString mode(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("off");
    const QByteArray contents = file.readAll();
    if (entryValue(contents, "Hidden").compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("off");
    const QStringList exec =
        entryValue(contents, "Exec").split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return exec.contains(hiddenFlag()) ? QStringLiteral("tray") : QStringLiteral("window");
}

QString execProgram(const QString &binaryPath)
{
    const QString canonical = QFileInfo(binaryPath).canonicalFilePath();
    const QString path = canonical.isEmpty() ? binaryPath : canonical;
    if (path.isEmpty() || path.startsWith(QLatin1String("/usr/")))
        return AppIdentity::applicationName();
    // Desktop Entry spec, "The Exec key": a reserved character means the argument is quoted,
    // and inside quotes ", `, $ and \ are escaped with a backslash.
    static const QString reserved = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");
    bool needsQuotes = false;
    for (const QChar c : path)
        needsQuotes = needsQuotes || reserved.contains(c);
    if (!needsQuotes)
        return path;
    QString quoted;
    for (const QChar c : path) {
        if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$') ||
            c == QLatin1Char('\\'))
            quoted += QLatin1Char('\\');
        quoted += c;
    }
    // A backslash in a desktop entry's string value is itself escaped once more.
    quoted.replace(QLatin1String("\\"), QLatin1String("\\\\"));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QByteArray entryContents(const QString &mode, const QString &program)
{
    const QString exec =
        mode == QLatin1String("tray") ? program + QLatin1Char(' ') + hiddenFlag() : program;
    QByteArray out;
    out += "[Desktop Entry]\n";
    out += "Type=Application\n";
    out += "Name=" + AppIdentity::displayName().toUtf8() + "\n";
    out += "Comment=Starts omaseed at login (Settings > General > Start at login)\n";
    out += "Exec=" + exec.toUtf8() + "\n";
    out += "Icon=" + AppIdentity::applicationId().toUtf8() + "\n";
    out += "Terminal=false\n";
    out += "X-GNOME-Autostart-enabled=true\n";
    return out;
}

bool setMode(const QString &mode, const QString &program, const QString &path)
{
    if (mode != QLatin1String("tray") && mode != QLatin1String("window")) {
        QFile file(path);
        return !file.exists() || file.remove();
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(entryContents(mode, program));
    return file.commit();
}

} // namespace omaseed::integration::Autostart
