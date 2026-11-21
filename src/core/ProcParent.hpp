#pragma once

// ProcParent — who started a process, as /proc says (DECISIONS C45 V17): the ask window names
// the program that ran `omaseed do` (its parent's `comm` and pid), read from
// `/proc/PID/stat`. Pure: the stat line is parsed here, and the reader takes the folder that
// stands for /proc, so a test hands it a tree of its own.
//
// omaseed can't tell who is behind a command (V17): a shell, an agent or a script all look
// alike here. This only names the program, and says nothing it couldn't read.

#include <QByteArray>
#include <QString>

#include <optional>

namespace omaseed::core::ProcParent {

/// One process's stat line, the parts the ask reads.
struct Stat
{
    uint pid = 0;
    uint ppid = 0;
    QString comm; ///< the program's name, at most 15 bytes (the kernel's TASK_COMM_LEN)
};

/// `/proc/PID/stat`'s line: "PID (COMM) STATE PPID …". COMM may hold spaces and parentheses,
/// so it ends at the line's last ')'. Nullopt for anything else.
std::optional<Stat> parseStat(const QByteArray &line);

/// The stat of `pid` under `root` ("/proc" on a real run); nullopt when there is no such
/// process, it can't be read, or `pid` is 0.
std::optional<Stat> read(uint pid, const QString &root = QStringLiteral("/proc"));

/// Who asked through `do` (V17): the process the bus named (the word's own, `omaseed`) and the
/// one that started it. A part is empty (0, "") when /proc couldn't say.
struct Caller
{
    uint wordPid = 0;
    QString word; ///< the calling program's comm ("omaseed", or "busctl")
    uint parentPid = 0;
    QString parent; ///< what started it ("claude", "bash")
};

/// `pid` and its parent, read under `root`.
Caller callerOf(uint pid, const QString &root = QStringLiteral("/proc"));

} // namespace omaseed::core::ProcParent
