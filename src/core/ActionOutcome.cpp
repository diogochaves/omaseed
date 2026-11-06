#include "core/ActionOutcome.hpp"

#include <QCoreApplication>

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
    if (action == QLatin1String("category")) {
        if (target.isEmpty())
            return {
                tr("taken out of its category"), tr("didn't change category"), tr("no category")};
        return {tr("put in %1").arg(target),
                tr("didn't change category"),
                tr("category · %1").arg(target)};
    }
    if (action == QLatin1String("addTags"))
        return {
            tr("tagged %1").arg(target), tr("didn't take the tag"), tr("tagged · %1").arg(target)};
    if (action == QLatin1String("removeTags"))
        return {tr("untagged %1").arg(target), tr("kept the tag"), tr("untagged")};
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
    return outcome;
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
    const int serial = ++m_serial;
    // D3: the next action ends what the last one left on the rows.
    m_marks.clear();
    QVariantMap words = outcome.words();
    words.insert(QStringLiteral("serial"), serial);
    const QString doneRow = words.value(QStringLiteral("doneRow")).toString();
    const QString failedRow = words.value(QStringLiteral("failedRow")).toString();
    for (const QString &hash : outcome.hashes) {
        const bool failed = outcome.failedHashes.contains(hash);
        if (!failed && doneRow.isEmpty())
            continue; // a deleted row has nothing to show
        m_marks.insert(hash, Mark{failed ? failedRow : doneRow, failed, serial});
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
