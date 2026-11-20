#pragma once

#include "integration/SkinReader.hpp"

#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

namespace omaseed::integration {

/// One skin in the person's folder, as `skin list` and the Skin row show it.
struct UserSkin
{
    /// The folder's name, which is the skin's (DECISIONS C37 U1): the file's `"skin"` is not.
    QString name;
    /// The folder, `…/skins/<name>`.
    QString path;
    /// Its skin.json, read (reason "missing" when there is none).
    Skin skin;
    /// The skin's `description`, "" when it has none.
    QString description;
    bool operator==(const UserSkin &) const = default;
};

/// The person's own skins (DECISIONS C37 U1): one folder each under `skins/` in omaseed's
/// config folder, `skins/<name>/skin.json`. Pure file IO: it lists them, holds their names to
/// a rule, reads one, and watches the folder. Built-in skins are not in the folder; they come first
/// wherever skins are listed, and read() gives them by their name.
class UserSkins : public QObject
{
    Q_OBJECT

public:
    /// `dir` is the `skins/` folder (`paths::configDir() + "/skins"`; a test's own folder).
    explicit UserSkins(QString dir, QObject *parent = nullptr);

    QString dir() const { return m_dir; }

    /// Why `name` can't be a user skin's, "" when it can (C37 U1): letters, digits, `.`, `_`
    /// and `-`, 1 to 63 of them, starting with a letter or a digit, not ending in `.`, and not
    /// a built-in skin's name or `desktop` in any case.
    static QString nameProblem(const QString &name);

    /// The folders whose names pass, sorted without case, each read. A folder whose name
    /// fails is left out and logged under `omaseed.skins` with why.
    QList<UserSkin> list() const;

    /// The skin called `name`: a built-in's for a built-in name, the folder's for a user
    /// skin, Omarchy's with reason "missing" for one that isn't there.
    Skin read(const QString &name) const;

    /// The folder `path` read as a user skin called `name`: its skin.json parsed, its name the
    /// folder's (the file's `"skin"` is ignored), and a `faces/` beside it named as dropped
    /// (C37 U2: nothing in it is loaded).
    static Skin readFolder(const QString &path, const QString &name);

    // ---- the watch (C37 U1: a save repaints, live) ---------------------------------------

    /// The listing as last read: what changed() compares against.
    QList<UserSkin> skins() const { return m_skins; }
    /// The skin drawn, so its file is watched and its changes reach drawnChanged(): a user
    /// skin's name, or "" (a built-in, or a name that is none) for nothing to follow.
    void setDrawn(const QString &name);
    QString drawn() const { return m_drawn; }

    /// How long a burst of file events settles before the folder is read again.
    static constexpr int kDebounceMs = 250;

Q_SIGNALS:
    /// The listing moved: a skin added, removed, renamed, or one that reads differently.
    void changed();
    /// The drawn skin's file reads differently (a save, a break, a fix, its folder gone or
    /// back). Never for a rewrite that reads the same.
    void drawnChanged();

private:
    void rewatch();
    void settle();

    QString m_dir;
    QString m_drawn;
    Skin m_drawnSkin;
    QList<UserSkin> m_skins;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};

} // namespace omaseed::integration
