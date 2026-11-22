#include "core/ActionOutcome.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <iterator>

namespace omaseed::core {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ActionOutcome", text);
}

/// A path's last part, for a row's mark: "/mnt/fast/keep" reads "keep".
QString leaf(const QString &path)
{
    QString trimmed = path;
    while (trimmed.size() > 1 && trimmed.endsWith(QLatin1Char('/')))
        trimmed.chop(1);
    const qsizetype slash = trimmed.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 && slash + 1 < trimmed.size() ? trimmed.mid(slash + 1) : trimmed;
}

/// An action's words: what follows the subject when it landed ("moved to %1", %1 the
/// target), when it didn't ("didn't move"), and the row's mark when it landed ("moved · %1",
/// %1 the target's last part). Participles and "didn't", so one count or many read the same
/// ("Arcade Set 01 stopped", "3 stopped").
struct Phrases
{
    QString done;
    QString failed;
    QString row;
};

Phrases phrases(const QString &action, const QString &target)
{
    const bool on = target != QLatin1String("off");
    if (action == QLatin1String("move"))
        return {
            tr("moved to %1").arg(target), tr("didn't move"), tr("moved · %1").arg(leaf(target))};
    if (action == QLatin1String("moveBack"))
        return {tr("moved back"), tr("didn't move back"), tr("moved back")};
    if (action == QLatin1String("start"))
        return {tr("started"), tr("didn't start"), tr("started")};
    if (action == QLatin1String("stop"))
        return {tr("stopped"), tr("didn't stop"), tr("stopped")};
    if (action == QLatin1String("retry"))
        return {tr("started again"), tr("didn't start"), tr("retrying")};
    if (action == QLatin1String("forceStart"))
        return {tr("force started"), tr("didn't start"), tr("forced")};
    if (action == QLatin1String("recheck"))
        return {tr("sent to recheck"), tr("didn't recheck"), tr("rechecking")};
    if (action == QLatin1String("reannounce"))
        return {tr("reannounced"), tr("didn't reannounce"), tr("reannounced")};
    // #294 (C50 GT2): an agent's locate, re-pointed at %1 and sent to recheck.
    if (action == QLatin1String("locate"))
        return {tr("located in %1 · rechecking").arg(target),
                tr("didn't locate"),
                tr("located · %1").arg(leaf(target))};
    // #292: an agent's dismiss through `do` (the window's own dismiss puts no strip up).
    if (action == QLatin1String("dismiss"))
        return {tr("cleared"), tr("didn't clear"), tr("cleared")};
    if (action == QLatin1String("category")) {
        if (target.isEmpty())
            return {
                tr("taken out of its category"), tr("didn't change category"), tr("no category")};
        return {tr("put in %1").arg(target),
                tr("didn't change category"),
                tr("category · %1").arg(target)};
    }
    // C25 K13–K15: Deluge's labels, one per torrent, in the categories place.
    if (action == QLatin1String("label")) {
        if (target.isEmpty())
            return {tr("taken out of its label"), tr("didn't change label"), tr("no label")};
        return {
            tr("labelled %1").arg(target), tr("didn't change label"), tr("label · %1").arg(target)};
    }
    if (action == QLatin1String("addTags"))
        return {
            tr("tagged %1").arg(target), tr("didn't take the tag"), tr("tagged · %1").arg(target)};
    if (action == QLatin1String("removeTags"))
        return {tr("untagged %1").arg(target), tr("kept the tag"), tr("untagged")};
    // C25 K14: Transmission's labels.
    if (action == QLatin1String("addLabels"))
        return {tr("labelled %1").arg(target),
                tr("didn't take the label"),
                tr("labelled · %1").arg(target)};
    if (action == QLatin1String("removeLabels"))
        return {tr("unlabelled %1").arg(target), tr("kept the label"), tr("unlabelled")};
    if (action == QLatin1String("limits"))
        return {tr("given new limits"), tr("didn't take the limits"), tr("limits set")};
    if (action == QLatin1String("rename"))
        return {tr("renamed to %1").arg(target), tr("didn't rename"), tr("renamed")};
    if (action == QLatin1String("queueUp"))
        return {tr("moved up the queue"), tr("didn't move in the queue"), tr("queue ↑")};
    if (action == QLatin1String("queueDown"))
        return {tr("moved down the queue"), tr("didn't move in the queue"), tr("queue ↓")};
    if (action == QLatin1String("delete"))
        return {tr("deleted"), tr("didn't delete"), QString()};
    // #295 (C50 GT1): an agent's remove, the torrent only, kept 30 days to add back (GT6).
    if (action == QLatin1String("remove"))
        return {tr("removed · kept 30 days"), tr("didn't remove"), QString()};
    if (action == QLatin1String("deleteWithFiles"))
        return {tr("deleted with its files"), tr("didn't delete"), QString()};
    if (action == QLatin1String("sequential"))
        return {on ? tr("set to download in order") : tr("set to download in any order"),
                tr("didn't change order"),
                on ? tr("in order") : tr("any order")};
    if (action == QLatin1String("firstLast"))
        return {on ? tr("set to fetch first and last pieces first")
                   : tr("set to fetch pieces in any order"),
                tr("didn't change order"),
                on ? tr("first, last first") : tr("any order")};
    if (action == QLatin1String("superSeeding"))
        return {on ? tr("set to super seed") : tr("set not to super seed"),
                tr("didn't change super seeding"),
                on ? tr("super seeding") : tr("seeding")};
    if (action == QLatin1String("autoManagement"))
        return {on ? tr("set to automatic management") : tr("set to manual management"),
                tr("didn't change management"),
                on ? tr("automatic") : tr("manual")};
    return {tr("done"), tr("didn't happen"), tr("done")};
}

QString kindName(ActionOutcome::Kind kind)
{
    switch (kind) {
    case ActionOutcome::Kind::Done:
        return QStringLiteral("done");
    case ActionOutcome::Kind::Failed:
        return QStringLiteral("failed");
    case ActionOutcome::Kind::Mixed:
        return QStringLiteral("mixed");
    }
    return QStringLiteral("done");
}

} // namespace

QString ActionOutcome::byAnAgent()
{
    return tr("by an agent");
}

ActionOutcome::Kind ActionOutcome::kind() const
{
    if (failedHashes.isEmpty())
        return Kind::Done;
    for (const QString &hash : hashes) {
        if (!failedHashes.contains(hash))
            return Kind::Mixed;
    }
    return Kind::Failed;
}

QVariantMap ActionOutcome::words() const
{
    const Phrases words = phrases(action, target);
    const Kind k = kind();
    QStringList done;
    for (const QString &hash : hashes) {
        if (!failedHashes.contains(hash))
            done << hash;
    }
    // The subject: the torrent by name when the action was about one, else the count. Never
    // a list (D3).
    const auto subject = [this](qsizetype count) {
        return count == 1 && !name.isEmpty() ? name : QString::number(count);
    };
    QString headline;
    QString line = cause;
    switch (k) {
    case Kind::Done:
        headline = subject(hashes.size()) + QLatin1Char(' ') + words.done;
        line.clear();
        break;
    case Kind::Failed: {
        const QString who = failedHashes.size() == 1 && !failedName.isEmpty()
                                ? failedName
                                : subject(failedHashes.size());
        headline = who + QLatin1Char(' ') + words.failed;
        break;
    }
    case Kind::Mixed:
        // Board 45 O3: "2 moved to /mnt/fast/keep, 1 didn't" / "Short Film 02: no room".
        headline = tr("%1 %2, %3 didn't")
                       .arg(QString::number(done.size()),
                            words.done,
                            QString::number(failedHashes.size()));
        if (!failedName.isEmpty() && !cause.isEmpty())
            line = tr("%1: %2").arg(failedName, cause);
        break;
    }
    return {
        {QStringLiteral("kind"), kindName(k)},
        {QStringLiteral("mark"),
         k == Kind::Done     ? QStringLiteral("✓")
         : k == Kind::Failed ? QStringLiteral("✕")
                             : QStringLiteral("!")},
        {QStringLiteral("headline"), headline},
        {QStringLiteral("line"), line},
        {QStringLiteral("details"), details},
        {QStringLiteral("action"), action},
        {QStringLiteral("target"), target},
        {QStringLiteral("hashes"), hashes},
        {QStringLiteral("doneHashes"), done},
        {QStringLiteral("failedHashes"), failedHashes},
        {QStringLiteral("doneRow"), words.row},
        {QStringLiteral("failedRow"), words.failed},
        {QStringLiteral("startHere"), startHere && k != Kind::Done},
        {QStringLiteral("recheckFirst"), recheckFirst && k != Kind::Done},
        {QStringLiteral("agent"), byAgent ? byAnAgent() : QString()},
        {QStringLiteral("keptIds"), keptIds},
    };
}

ActionOutcome ActionOutcome::fromMap(const QVariantMap &map)
{
    ActionOutcome outcome;
    outcome.action = map.value(QStringLiteral("action")).toString();
    outcome.hashes = map.value(QStringLiteral("hashes")).toStringList();
    outcome.failedHashes = map.value(QStringLiteral("failedHashes")).toStringList();
    outcome.name = map.value(QStringLiteral("name")).toString();
    outcome.failedName = map.value(QStringLiteral("failedName")).toString();
    outcome.target = map.value(QStringLiteral("target")).toString();
    outcome.cause = map.value(QStringLiteral("cause")).toString();
    outcome.details = map.value(QStringLiteral("details")).toString();
    outcome.startHere = map.value(QStringLiteral("startHere")).toBool();
    outcome.recheckFirst = map.value(QStringLiteral("recheckFirst")).toBool();
    outcome.byAgent = map.value(QStringLiteral("byAgent")).toBool();
    outcome.keptIds = map.value(QStringLiteral("keptIds")).toStringList();
    return outcome;
}

QVariantMap ActionOutcome::planWords(const QList<ActionOutcome> &parts, const QString &line)
{
    // By action, in the order the plan ran them: how many landed, and where.
    struct Group
    {
        QString action;
        int done = 0;
        QStringList targets; ///< the distinct targets of the parts that landed
    };
    QList<Group> groups;
    QStringList hashes;
    QStringList done;
    QStringList failed;
    QStringList keptIds;
    QString failedName;
    QString cause;
    QString details;
    int serverWideFailed = 0;
    for (const ActionOutcome &part : parts) {
        auto group = std::find_if(groups.begin(), groups.end(), [&part](const Group &g) {
            return g.action == part.action;
        });
        if (group == groups.end()) {
            groups.append(Group{part.action, 0, {}});
            group = std::prev(groups.end());
        }
        int landed = 0;
        if (part.hashes.isEmpty()) {
            // Pause and resume: the server's count, or its cause.
            if (part.cause.isEmpty())
                landed = part.target.toInt();
            else
                ++serverWideFailed;
        }
        for (const QString &hash : part.hashes) {
            hashes << hash;
            if (part.failedHashes.contains(hash)) {
                failed << hash;
            } else {
                done << hash;
                ++landed;
            }
        }
        if ((!part.failedHashes.isEmpty() || (part.hashes.isEmpty() && !part.cause.isEmpty())) &&
            cause.isEmpty()) {
            failedName = part.failedName;
            if (failedName.isEmpty() && part.failedHashes.size() == 1)
                failedName = part.name;
            cause = part.cause;
            details = part.details;
        }
        keptIds << part.keptIds;
        group->done += landed;
        if (landed > 0 && !part.target.isEmpty() && !group->targets.contains(part.target))
            group->targets << part.target;
    }

    QStringList said;
    for (const Group &group : std::as_const(groups)) {
        if (group.done == 0)
            continue;
        const QString n = QString::number(group.done);
        const QString where = group.targets.size() == 1
                                  ? group.targets.constFirst()
                                  : tr("%1 folders").arg(group.targets.size());
        if (group.action == QLatin1String("locate"))
            said << tr("%1 located in %2").arg(n, where);
        else if (group.action == QLatin1String("move"))
            said << tr("%1 moving to %2").arg(n, where);
        else if (group.action == QLatin1String("moveBack"))
            said << tr("%1 moving back").arg(n);
        else if (group.action == QLatin1String("remove"))
            said << tr("%1 removed").arg(n);
        else if (group.action == QLatin1String("pause"))
            said << tr("%1 paused").arg(n);
        else if (group.action == QLatin1String("resume"))
            said << tr("%1 resumed").arg(n);
        else
            said << n + QLatin1Char(' ') + phrases(group.action, group.targets.value(0)).done;
    }
    const int failures = int(failed.size()) + serverWideFailed;
    const Kind k = failures == 0 ? Kind::Done : said.isEmpty() ? Kind::Failed : Kind::Mixed;
    QString headline = tr("Plan: %1").arg(said.join(QStringLiteral(" · ")));
    if (said.isEmpty())
        headline = tr("Plan: none of it ran");
    else if (failures > 0)
        headline = tr("%1, %2 didn't").arg(headline).arg(failures);
    QStringList lines;
    if (!line.isEmpty())
        lines << line;
    if (!cause.isEmpty())
        lines << (failedName.isEmpty() ? cause : tr("%1: %2").arg(failedName, cause));
    return {
        {QStringLiteral("kind"), kindName(k)},
        {QStringLiteral("mark"),
         k == Kind::Done     ? QStringLiteral("✓")
         : k == Kind::Failed ? QStringLiteral("✕")
                             : QStringLiteral("!")},
        {QStringLiteral("headline"), headline},
        {QStringLiteral("line"), lines.join(QStringLiteral(" · "))},
        {QStringLiteral("details"), details},
        {QStringLiteral("action"), QStringLiteral("plan")},
        {QStringLiteral("target"), QString()},
        {QStringLiteral("hashes"), hashes},
        {QStringLiteral("doneHashes"), done},
        {QStringLiteral("failedHashes"), failed},
        {QStringLiteral("doneRow"), done.isEmpty() ? QString() : tr("done")},
        {QStringLiteral("failedRow"), tr("didn't happen")},
        {QStringLiteral("startHere"), false},
        {QStringLiteral("recheckFirst"), false},
        {QStringLiteral("agent"), byAnAgent()},
        {QStringLiteral("keptIds"), keptIds},
    };
}

ActionOutcomes::ActionOutcomes(QObject *parent)
    : QObject(parent)
{}

void ActionOutcomes::setDoneDuration(std::chrono::milliseconds duration)
{
    m_doneDuration = duration;
}

void ActionOutcomes::report(const ActionOutcome &outcome)
{
    show(outcome.words(), {outcome});
}

void ActionOutcomes::reportPlan(const QList<ActionOutcome> &parts, const QString &line)
{
    show(ActionOutcome::planWords(parts, line), parts);
}

void ActionOutcomes::show(QVariantMap words, const QList<ActionOutcome> &parts)
{
    const int serial = ++m_serial;
    // D3: the next action ends what the last one left on the rows.
    m_marks.clear();
    words.insert(QStringLiteral("serial"), serial);
    for (const ActionOutcome &outcome : parts) {
        const QVariantMap own = outcome.words();
        const QString doneRow = own.value(QStringLiteral("doneRow")).toString();
        const QString failedRow = own.value(QStringLiteral("failedRow")).toString();
        for (const QString &hash : outcome.hashes) {
            const bool failed = outcome.failedHashes.contains(hash);
            if (!failed && doneRow.isEmpty())
                continue; // a deleted row has nothing to show
            m_marks.insert(hash, Mark{failed ? failedRow : doneRow, failed, serial});
        }
    }
    m_current = words;
    Q_EMIT currentChanged();
    bumpRevision();
    QTimer::singleShot(m_doneDuration, this, [this, serial]() { dropDoneMarks(serial); });
}

void ActionOutcomes::reportMap(const QVariantMap &map)
{
    report(ActionOutcome::fromMap(map));
}

void ActionOutcomes::reportWords(const QVariantMap &words)
{
    const int serial = ++m_serial;
    m_marks.clear();
    m_current = words;
    m_current.insert(QStringLiteral("serial"), serial);
    Q_EMIT currentChanged();
    bumpRevision();
}

void ActionOutcomes::dismiss()
{
    if (m_current.isEmpty())
        return;
    m_current.clear();
    bool changed = false;
    for (auto it = m_marks.begin(); it != m_marks.end();) {
        if (it->failed) {
            it = m_marks.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    Q_EMIT currentChanged();
    if (changed)
        bumpRevision();
}

void ActionOutcomes::expire(int serial)
{
    if (m_current.value(QStringLiteral("serial")).toInt() == serial)
        dismiss();
}

void ActionOutcomes::clear()
{
    ++m_serial; // a done strip's time running out names an older serial now
    const bool hadMarks = !m_marks.isEmpty();
    m_marks.clear();
    if (!m_current.isEmpty()) {
        m_current.clear();
        Q_EMIT currentChanged();
    }
    if (hadMarks)
        bumpRevision();
}

QVariantMap ActionOutcomes::rowMark(const QString &hash) const
{
    const auto it = m_marks.constFind(hash);
    if (it == m_marks.constEnd())
        return {};
    return {
        {QStringLiteral("text"), it->text},
        {QStringLiteral("kind"), it->failed ? QStringLiteral("failed") : QStringLiteral("done")}};
}

void ActionOutcomes::dropDoneMarks(int serial)
{
    bool changed = false;
    for (auto it = m_marks.begin(); it != m_marks.end();) {
        if (!it->failed && it->serial == serial) {
            it = m_marks.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        bumpRevision();
}

void ActionOutcomes::bumpRevision()
{
    ++m_revision;
    Q_EMIT revisionChanged();
}

} // namespace omaseed::core
