#pragma once

#include <QDBusConnection>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace omaseed::integration {

/// What a dropped file, a pasted clipboard line, or a bit of dropped text resolves to. A
/// gadget (not a QObject) so QML can read `.kind` and `.value` straight off the QVariant that
/// `addablesFromDrop()` returns.
struct Addable
{
    Q_GADGET

public:
    enum class Kind {
        None,
        Magnet,
        HttpUrl,
        TorrentFile,
    };
    Q_ENUM(Kind)

    Q_PROPERTY(Kind kind MEMBER kind)
    Q_PROPERTY(QString value MEMBER value)

    Kind kind = Kind::None;
    QString value;

    bool operator==(const Addable &other) const
    {
        return kind == other.kind && value == other.value;
    }
};

/// Desktop glue that does not fit anywhere more specific: reveal-in-file-manager, magnet
/// handler registration, clipboard and drag-and-drop parsing. The charter's integration
/// table rows "Reveal in file manager", "Clipboard" and "Drag and drop", plus the `xdg-mime`
/// half of "magnet: and .torrent handler" (the .desktop file itself is T1.4's).
class Xdg : public QObject
{
    Q_OBJECT

public:
    explicit Xdg(QDBusConnection connection = QDBusConnection::sessionBus(),
                 QObject *parent = nullptr);

    /// The instance the QML `Xdg` singleton hands out. main() creates the one real `Xdg` and
    /// sets it here before the engine exists, the same way `Theme::setDefaultReader` works.
    static void setDefaultInstance(Xdg *instance);
    /// Factory for the QML singleton registration: returns the instance main() set, or (the
    /// QML tests, a tool) a fresh one owned by the engine.
    static Xdg *createForEngine(QQmlEngine *engine);

    /// Pure parser: what does this one line of text look like? No I/O except `QFile::exists`
    /// for a bare local path ending in `.torrent`.
    static Addable parseAddable(const QString &text);
    /// Splits `text` into lines and parses each one, dropping what parses to `Kind::None`. A
    /// line with whitespace in it that is not an existing `.torrent` path (which may contain
    /// spaces) is split on the whitespace and each token parsed: two magnets pasted side by
    /// side are two addables.
    static QList<Addable> parseAddableList(const QString &text);

    /// Reveals `path` in the file manager: a file is highlighted (`ShowItems`), a directory is
    /// opened (`ShowFolders`). Async; never blocks. Emits `revealFinished` with whether the
    /// D-Bus call is believed to have gone through (the fallback path returns true once
    /// `QDesktopServices::openUrl` accepts the URL; the D-Bus path reports via the callback).
    Q_INVOKABLE void showInFileManager(const QString &path);

    /// Combines dropped file URLs (kept only when they end in `.torrent`) and dropped/typed
    /// text (magnets, URLs, one per line) into one ordered list of addables.
    Q_INVOKABLE QList<Addable> addablesFromDrop(const QList<QUrl> &urls, const QString &text) const;

    /// Async: runs `xdg-mime query default x-scheme-handler/magnet` and compares the answer
    /// with our own desktop id. Emits `isDefaultMagnetHandlerResult`.
    Q_INVOKABLE void isDefaultMagnetHandler();
    /// Runs `xdg-mime default <us> x-scheme-handler/magnet application/x-bittorrent`. Never
    /// called automatically; the first-run prompt (T4.6) decides when to call it.
    Q_INVOKABLE void registerAsDefaultHandler();

    /// Whether the first-run magnet-handler prompt should still be shown, i.e. `state.json`
    /// does not yet have `magnetHandlerAsked: true`. Creates `state.json` with `{ "version": 1
    /// }` if it does not exist yet.
    Q_INVOKABLE bool firstRunPromptNeeded() const;
    /// Records that the prompt was shown (accepted or declined), so it is not shown again.
    Q_INVOKABLE void setFirstRunPromptAsked();

    /// The magnet/URL/`.torrent` addable found in the current clipboard text, or `Kind::None`.
    Q_INVOKABLE Addable detectClipboardMagnet() const;

signals:
    void revealFinished(bool dbusWorked);
    void isDefaultMagnetHandlerResult(bool isDefault, QString currentHandler);
    void registrationFinished(bool success);

private:
    QDBusConnection m_connection;

    static QString stateFilePath();
    static QJsonObject readState();
    static void writeState(const QJsonObject &state);
};

} // namespace omaseed::integration

Q_DECLARE_METATYPE(omaseed::integration::Addable)
