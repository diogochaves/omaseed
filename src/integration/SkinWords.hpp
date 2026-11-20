#pragma once

// SkinWords — the answers of `omaseed skin list | clone | use` (DECISIONS C37 U3, U6; C45 V4,
// V12, V13): each one JSON object in the words' envelope, and the lines made from it.
//
// - `skin list` and `skin clone` need no display and no bus: the CLI answers them itself.
// - `skin use` is the running omaseed's (AgentService), and the CLI's when none is running:
//   both check the name here (pick) and answer with use().
// - Nothing here writes into a skin that is there (C37 U3: clone refuses a taken name), and
//   saveChoice never writes over a state.json that doesn't parse (C37 U6).

#include "integration/SkinReader.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace omaseed::integration::SkinWords {

/// What a word did: `json` its answer when `code` is "", else the failure's code and words.
struct Outcome
{
    QString code;
    QString message;
    QByteArray json;
};

/// A skin a word names, checked: `skin` read when `code` is "", else why not (bad-name,
/// unknown-skin, unreadable) and the words.
struct Pick
{
    QString code;
    QString message;
    Skin skin;
    QString path; ///< the user skin's folder, "" for a built-in
};

/// The skin called `name`, from the built-ins or `skinsDir`'s folders, as clone's source and
/// use's choice must be: one that reads.
Pick pick(const QString &skinsDir, const QString &name);

/// The skin choice state.json holds (`appearance.skin`), read only; "omarchy" when there is
/// none or the file doesn't read.
QString savedChoice();

/// `skin list`: the built-ins, then `skinsDir`'s skins (C37 U1's order); `choice` the saved
/// one, `pinned` OMASEED_SKIN ("" for none).
QByteArray list(const QString &skinsDir,
                const QString &choice,
                const QString &pinned,
                const QDateTime &at,
                const QString &omaseed);

/// The skin.json a clone of `source` (called `from`) writes (C37 U3): every token written
/// out, the source's as read and Omarchy's for the rest, those whose Omarchy value depends on
/// the place left out (SkinReader::contextualTokens). Two-space indent, the reader's order.
QByteArray cloneFile(const Skin &source, const QString &from);

/// `skin clone <from> <name>`: writes `skinsDir/<name>/skin.json`, atomically, never over a
/// folder (or file) already there, in any case.
Outcome clone(const QString &skinsDir,
              const QString &from,
              const QString &name,
              const QDateTime &at,
              const QString &omaseed);

/// `skin use`'s answer: `skin` {name, drawn, reason, dropped}, and whether the open window
/// draws it (`live`) or it waits for the next start.
QByteArray
use(const QString &name, const Skin &skin, bool live, const QDateTime &at, const QString &omaseed);

/// `skin use` with no omaseed running (C37 U6): `appearance.skin` saved in state.json, every
/// other key kept. A state.json that doesn't parse is never written (not-written).
Outcome saveChoice(const QString &name);

/// A role the C30 floor lifted on a ground under the checked skin and not under Omarchy's
/// (`check`'s `lifted`, DECISIONS C37 U5): its contrast there before and after.
struct CheckLift
{
    QString role;
    QString ground;
    double from = 0;
    double to = 0;
};

/// What `check <skin>` found: the skin as read (Omarchy's, with its reason, when it doesn't
/// read), under which theme, and where its picture is.
struct CheckFacts
{
    QString name;
    Skin skin;
    QStringList missingFonts;
    QList<CheckLift> lifted;
    QString theme;
    QString mode;
    int baseSize = 0;
    QString picture;
    bool omabox = false;
};

/// `check <skin>`'s answer (docs/55 §6): skin, version, drawn, why (only when not drawn),
/// dropped [{token, value, why}], missingFonts, lifted, theme, mode, baseSize, picture,
/// omabox.
QByteArray check(const CheckFacts &facts, const QDateTime &at, const QString &omaseed);

/// The lines of a skin answer (`command` "skin list", "skin clone", "skin use", "check"), "" for
/// another; `home` is shortened to "~" in paths.
QString lines(const QJsonObject &answer, const QString &home);

} // namespace omaseed::integration::SkinWords
