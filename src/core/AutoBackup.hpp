#pragma once

// AutoBackup — omaseed's automatic copies (docs/24 §3, D3 a; #31).
//
// "weekly" (the default) or "daily" writes an omaseed backup into the backups folder when the
// newest automatic copy is that old, and keeps the last `kKeep`. The oldest goes only after a
// new one is written, so a failed copy never costs one that worked. A failure (a full disk,
// say) is reported once through `failed`, then not again until a copy succeeds. Safety copies
// made before a restore (`before-restore-…`) are never counted or removed here.
//
// #32 hook: everything a copy holds comes from `OmaseedBackup::Sources`, whose `extras` the
// app fills; each server's qBittorrent settings join the copies by adding them there.

#include "core/OmaseedBackup.hpp"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace omaseed::core {

class AutoBackup : public QObject
{
    Q_OBJECT

public:
    static constexpr int kKeep = 4;
    static inline const QString kPrefix = QStringLiteral("auto");

    explicit AutoBackup(QObject *parent = nullptr);

    void setSources(const OmaseedBackup::Sources &sources) { m_sources = sources; }
    const OmaseedBackup::Sources &sources() const { return m_sources; }

    /// "off", "weekly" or "daily"; anything else reads as "weekly".
    void setMode(const QString &mode);
    QString mode() const { return m_mode; }
    /// Seconds between copies, 0 when off.
    qint64 interval() const;

    /// Runs before each copy (App flushes the seed ledger so the copy has the last minutes).
    void setBeforeCopy(std::function<void()> hook) { m_beforeCopy = std::move(hook); }

    /// The automatic copies in the backups folder, newest first.
    QStringList copies() const;
    /// When the newest automatic copy was made; invalid when there is none.
    QDateTime lastCopyTime() const;

    /// Writes a copy when one is due at `now`. True when it wrote one.
    bool runIfDue(qint64 now);
    /// Writes a copy now, whatever the mode. True on success.
    bool runNow(qint64 now);

Q_SIGNALS:
    void copied(const QString &path);
    void failed(const QString &message);

private:
    void prune();

    OmaseedBackup::Sources m_sources;
    QString m_mode = QStringLiteral("weekly");
    std::function<void()> m_beforeCopy;
    bool m_failureReported = false;
};

} // namespace omaseed::core
