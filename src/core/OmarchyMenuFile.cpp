#include "core/OmarchyMenuFile.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSaveFile>

#include <utility>

namespace omaseed::core {

namespace {

// One top-level `"id": value` of the file: where its key's opening quote is, and where its
// value ends (one past its last byte).
struct Entry
{
    QByteArray key;
    qsizetype keyStart = 0;
    qsizetype valueEnd = 0;
};

// What a scan of the file found, without building anything from it.
struct Scan
{
    bool ok = false;
    QString error;
    qsizetype close = -1;      // the top-level object's closing brace
    qsizetype lastSigEnd = -1; // one past the last byte before it that isn't space or comment
    char lastSig = 0;          // that byte
    QList<Entry> entries;
};

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

Scan fail(Scan s, const char *why)
{
    s.ok = false;
    s.error = QString::fromLatin1(why);
    return s;
}

// A JSONC object, read only for where things are: strings (escapes included), `//` and
// `/* */` comments, nesting, the top-level ids. Values are not interpreted.
Scan scan(const QByteArray &t)
{
    Scan s;
    const qsizetype n = t.size();
    qsizetype i = 0;
    int depth = 0;
    bool opened = false;
    bool expectKey = false;
    bool inEntry = false;
    Entry cur;

    auto significant = [&s](qsizetype end, char c) {
        s.lastSigEnd = end;
        s.lastSig = c;
    };
    auto closeEntry = [&]() {
        if (inEntry) {
            cur.valueEnd = s.lastSigEnd;
            s.entries.append(cur);
            inEntry = false;
        }
    };

    while (i < n) {
        const char c = t.at(i);
        if (c == '/' && i + 1 < n && t.at(i + 1) == '/') {
            const qsizetype nl = t.indexOf('\n', i);
            i = nl < 0 ? n : nl;
            continue;
        }
        if (c == '/' && i + 1 < n && t.at(i + 1) == '*') {
            const qsizetype end = t.indexOf("*/", i + 2);
            if (end < 0)
                return fail(s, "a /* comment is never closed");
            i = end + 2;
            continue;
        }
        if (isSpace(c)) {
            ++i;
            continue;
        }
        if (depth == 0) {
            if (opened)
                return fail(s, "text after the closing brace");
            if (c != '{')
                return fail(s, "the file is not one JSONC object");
            opened = true;
            depth = 1;
            expectKey = true;
            significant(i + 1, c);
            ++i;
            continue;
        }
        if (c == '"') {
            qsizetype j = i + 1;
            while (j < n && t.at(j) != '"') {
                if (t.at(j) == '\n')
                    return fail(s, "a string runs past its line");
                j += t.at(j) == '\\' ? 2 : 1;
            }
            if (j >= n)
                return fail(s, "a string is never closed");
            if (depth == 1 && expectKey) {
                cur = Entry{t.mid(i + 1, j - i - 1), i, 0};
                inEntry = true;
                expectKey = false;
            }
            significant(j + 1, '"');
            i = j + 1;
            continue;
        }
        if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            if (depth == 1) {
                if (c != '}')
                    return fail(s, "a ] closes the top-level object");
                closeEntry();
                s.close = i;
                depth = 0;
                ++i;
                continue; // the closing brace is not "before" itself
            }
            --depth;
        } else if (c == ',' && depth == 1) {
            closeEntry();
            expectKey = true;
        }
        significant(i + 1, c);
        ++i;
    }
    if (!opened || s.close < 0)
        return fail(s, "the top-level object is never closed");
    s.ok = true;
    return s;
}

qsizetype lineStart(const QByteArray &t, qsizetype pos)
{
    if (pos <= 0)
        return 0;
    return t.lastIndexOf('\n', pos - 1) + 1;
}

// One past the line's newline (or the end of the text).
qsizetype lineEnd(const QByteArray &t, qsizetype pos)
{
    const qsizetype nl = t.indexOf('\n', pos);
    return nl < 0 ? t.size() : nl + 1;
}

int lineNumber(const QByteArray &t, qsizetype pos)
{
    return int(t.left(pos).count('\n')) + 1;
}

// The pre-C36 first-page row (T1.8): `"torrents": {...,"action":"... omaseed ..."},` alone on
// its line. Returns the range to remove, with the marker comment the old --menu wrote above it.
bool oldRowRange(const QByteArray &t, const Entry &e, qsizetype *from, qsizetype *to)
{
    if (e.key != "torrents")
        return false;
    const QByteArray entry = t.mid(e.keyStart, e.valueEnd - e.keyStart);
    if (entry.contains('\n') || !entry.contains("\"action\"") || !entry.contains("omaseed"))
        return false;
    const qsizetype ls = lineStart(t, e.keyStart);
    const qsizetype le = lineEnd(t, e.keyStart);
    const QByteArray line = t.mid(ls, le - ls).trimmed();
    if (line != entry && line != entry + ',')
        return false;
    *from = ls;
    *to = le;
    if (ls > 0) {
        const qsizetype prev = lineStart(t, ls - 1);
        const QByteArray above = t.mid(prev, ls - prev).trimmed();
        if (above.startsWith("//") && above.contains("omaseed T1.8"))
            *from = prev;
    }
    return true;
}

// Everything the file rule needs to know, from one read.
struct Analysis
{
    OmarchyMenuFile::State state = OmarchyMenuFile::State::NoSafePlace;
    QString detail;
    bool markersBroken = false;
    QByteArray text;
    // Block: the block's lines, and the marker lines' ends.
    qsizetype blockStart = -1;
    qsizetype blockEnd = -1;
    qsizetype openLineEnd = -1;
    qsizetype closeLineStart = -1;
    // NoBlock / OldRow: the text with the old row gone, and where the block goes in it.
    QByteArray working;
    qsizetype insertAt = -1;
    qsizetype commaAt = -1; // -1: no comma needed
    bool newlineBefore = false;
};

Analysis analyse(QByteArray text)
{
    using State = OmarchyMenuFile::State;
    Analysis a;
    a.text = std::move(text);
    const QByteArray &t = a.text;

    // The markers, line by line: they are comments, so never inside a (one-line) string.
    QList<qsizetype> opens;
    QList<qsizetype> closes;
    for (qsizetype ls = 0; ls < t.size();) {
        const qsizetype le = lineEnd(t, ls);
        const QByteArray line = t.mid(ls, le - ls).trimmed();
        if (line.startsWith(OmarchyMenuFile::openMarkerPrefix()))
            opens.append(ls);
        else if (line.startsWith(OmarchyMenuFile::closeMarker()))
            closes.append(ls);
        ls = le;
    }
    if (!opens.isEmpty() || !closes.isEmpty()) {
        if (opens.size() != 1 || closes.size() != 1 || closes.first() < opens.first()) {
            a.detail = QStringLiteral("omaseed's markers don't pair");
            a.markersBroken = true;
            return a;
        }
        a.state = State::Block;
        a.blockStart = opens.first();
        a.openLineEnd = lineEnd(t, opens.first());
        a.closeLineStart = closes.first();
        a.blockEnd = lineEnd(t, closes.first());
        return a;
    }

    // Marker to marker needs no scan; the rest does.
    const Scan s = scan(t);
    if (!s.ok) {
        a.detail = s.error;
        return a;
    }

    // C36 M14: a Torrents row that isn't omaseed's is the user's.
    QList<std::pair<qsizetype, qsizetype>> oldRows;
    for (const Entry &e : s.entries) {
        qsizetype from = 0;
        qsizetype to = 0;
        if (oldRowRange(t, e, &from, &to)) {
            oldRows.append({from, to});
        } else if (e.key == "trigger.torrents" || e.key == "torrents") {
            a.state = State::UsersRow;
            a.detail = QStringLiteral("line %1 uses %2")
                           .arg(lineNumber(t, e.keyStart))
                           .arg(QString::fromUtf8(e.key));
            return a;
        }
    }

    // C36 M13: the old first-page row goes when the block comes.
    a.working = t;
    for (qsizetype k = oldRows.size() - 1; k >= 0; --k)
        a.working.remove(oldRows.at(k).first, oldRows.at(k).second - oldRows.at(k).first);
    const QByteArray &w = a.working;
    const Scan ws = oldRows.isEmpty() ? s : scan(w);
    if (!ws.ok) {
        a.detail = ws.error;
        return a;
    }

    const qsizetype c = ws.close;
    if (ws.lastSig != ',' && ws.lastSig != '{') {
        // A comma goes right after the last entry, unless something (a comment) follows it
        // on its line: then there is no safe place for one (M13).
        const qsizetype restEnd = qMin(lineEnd(w, ws.lastSigEnd), c);
        if (!w.mid(ws.lastSigEnd, restEnd - ws.lastSigEnd).trimmed().isEmpty()) {
            a.detail = QStringLiteral("the entry on line %1 ends in a comment")
                           .arg(lineNumber(w, ws.lastSigEnd));
            return a;
        }
        a.commaAt = ws.lastSigEnd;
    }
    const qsizetype ls = lineStart(w, c);
    if (w.mid(ls, c - ls).trimmed().isEmpty()) {
        a.insertAt = ls;
    } else {
        a.insertAt = c;
        a.newlineBefore = true;
    }
    a.state = oldRows.isEmpty() ? State::NoBlock : State::OldRow;
    return a;
}

QByteArray readAll(const QString &path, bool *ok)
{
    QFile file(path);
    *ok = file.open(QIODevice::ReadOnly);
    return *ok ? file.readAll() : QByteArray();
}

// The block as given, with its last newline, or empty (and `error`) when it isn't a run of
// top-level entries each ending in a comma.
QByteArray blockBytes(const QString &block, QString *error)
{
    QByteArray b = block.toUtf8();
    if (!b.endsWith('\n'))
        b.append('\n');
    const Scan s = scan("{\n" + b + "}");
    QString why;
    if (!s.ok)
        why = s.error;
    else if (s.entries.isEmpty())
        why = QStringLiteral("it holds no entry");
    else if (s.lastSig != ',')
        why = QStringLiteral("its last entry has no comma");
    for (qsizetype ls = 0; why.isEmpty() && ls < b.size();) {
        const qsizetype le = lineEnd(b, ls);
        const QByteArray line = b.mid(ls, le - ls).trimmed();
        if (line.startsWith(OmarchyMenuFile::openMarkerPrefix()) ||
            line.startsWith(OmarchyMenuFile::closeMarker()))
            why = QStringLiteral("it holds a marker");
        ls = le;
    }
    if (why.isEmpty())
        return b;
    if (error)
        *error = QStringLiteral("not a block: %1").arg(why);
    return {};
}

QByteArray wrapped(const QByteArray &block)
{
    return "  " + OmarchyMenuFile::openMarker() + '\n' + block + "  " +
           OmarchyMenuFile::closeMarker() + '\n';
}

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

} // namespace

QByteArray OmarchyMenuFile::openMarker()
{
    return openMarkerPrefix() +
           QByteArrayLiteral(" · Trigger › Torrents · written by omaseed (Settings › "
                             "Appearance); edits down to its end marker are replaced");
}

QByteArray OmarchyMenuFile::openMarkerPrefix()
{
    return QByteArrayLiteral("// >>> omaseed");
}

QByteArray OmarchyMenuFile::closeMarker()
{
    return QByteArrayLiteral("// <<< omaseed");
}

QString OmarchyMenuFile::defaultUserFile()
{
    // Omarchy's Menu.qml reads $HOME, not XDG_CONFIG_HOME.
    return QDir::homePath() + QStringLiteral("/.config/omarchy/extensions/omarchy-menu.jsonc");
}

QString OmarchyMenuFile::defaultOmarchyMenu()
{
    QString root = qEnvironmentVariable("OMARCHY_PATH");
    if (root.isEmpty())
        root = QStringLiteral("/usr/share/omarchy");
    return root + QStringLiteral("/default/omarchy/omarchy-menu.jsonc");
}

OmarchyMenuFile::OmarchyMenuFile(QString userFile, QString omarchyMenu)
    : m_userFile(std::move(userFile))
    , m_omarchyMenu(std::move(omarchyMenu))
{}

OmarchyMenuFile::State OmarchyMenuFile::state(QString *detail) const
{
    if (!QFileInfo::exists(m_omarchyMenu)) {
        setError(detail, QStringLiteral("needs Omarchy's menu"));
        return State::NoMenu;
    }
    if (!QFileInfo::exists(m_userFile))
        return State::NoFile;
    bool ok = false;
    const QByteArray text = readAll(m_userFile, &ok);
    if (!ok) {
        setError(detail, QStringLiteral("can't read %1").arg(m_userFile));
        return State::NoSafePlace;
    }
    const Analysis a = analyse(text);
    setError(detail, a.detail);
    return a.state;
}

bool OmarchyMenuFile::add(const QString &block, QString *error)
{
    const QByteArray b = blockBytes(block, error);
    if (b.isEmpty())
        return false;
    if (!QFileInfo::exists(m_omarchyMenu)) {
        setError(error, QStringLiteral("needs Omarchy's menu"));
        return false;
    }
    if (!QFileInfo::exists(m_userFile))
        return write("{\n" + wrapped(b) + "}\n", error);

    bool ok = false;
    const Analysis a = analyse(readAll(m_userFile, &ok));
    if (!ok) {
        setError(error, QStringLiteral("can't read %1").arg(m_userFile));
        return false;
    }
    switch (a.state) {
    case State::Block:
        return write(a.text.left(a.openLineEnd) + b + a.text.mid(a.closeLineStart), error);
    case State::NoBlock:
    case State::OldRow: {
        QByteArray out = a.working.left(a.insertAt);
        if (a.commaAt >= 0)
            out.insert(a.commaAt, ',');
        if (a.newlineBefore)
            out.append('\n');
        out += wrapped(b) + a.working.mid(a.insertAt);
        return write(out, error);
    }
    default:
        setError(error, QStringLiteral("%1: %2").arg(m_userFile, a.detail));
        return false;
    }
}

bool OmarchyMenuFile::rewrite(const QString &block, QString *error)
{
    const QByteArray b = blockBytes(block, error);
    if (b.isEmpty())
        return false;
    bool ok = false;
    const Analysis a = analyse(readAll(m_userFile, &ok));
    if (!ok || a.state != State::Block) {
        setError(error, QStringLiteral("%1 holds no omaseed block").arg(m_userFile));
        return false;
    }
    return write(a.text.left(a.openLineEnd) + b + a.text.mid(a.closeLineStart), error);
}

bool OmarchyMenuFile::remove(QString *error)
{
    if (!QFileInfo::exists(m_userFile))
        return true;
    bool ok = false;
    const Analysis a = analyse(readAll(m_userFile, &ok));
    if (!ok) {
        setError(error, QStringLiteral("can't read %1").arg(m_userFile));
        return false;
    }
    if (a.state == State::Block)
        return write(a.text.left(a.blockStart) + a.text.mid(a.blockEnd), error);
    if (a.markersBroken) {
        setError(error, QStringLiteral("%1: %2").arg(m_userFile, a.detail));
        return false;
    }
    return true;
}

bool OmarchyMenuFile::write(const QByteArray &bytes, QString *error) const
{
    QDir().mkpath(QFileInfo(m_userFile).absolutePath());
    // Written aside and renamed into place (C36 M13): Omarchy's menu hot-reloads the file and
    // must never read half of it.
    QSaveFile file(m_userFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(error, QStringLiteral("can't write %1: %2").arg(m_userFile, file.errorString()));
        return false;
    }
    return true;
}

} // namespace omaseed::core
