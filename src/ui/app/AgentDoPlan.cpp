// AgentDo's plan (DECISIONS C50 GT4, GT5; board 67 §4, §6): `omaseed do --plan FILE|-`.
//
// Each line is a `do` of its own, run through AgentDo::proceed, so a line gets every check a
// single `do` gets, in the same words:
// 1. every line checked as a dry run (the gate, GT1's guard, GT2's find, the move's plan); one
//    off or refused line runs none, and the answer says which (or, with --dry-run, says what
//    each line would do and stops there);
// 2. each line that asks is checked once more up to its ask, whose map is kept instead of
//    opened (`probe`), for the plan's one window; a line Settings allows by then runs nothing
//    there (`runs`) and joins the dim lines, run after the person's allow;
// 3. one ask (AgentAsk) lists the plan grouped by action and folder: the lines Settings
//    allows dim, each torrent of a line that asks a box, ticked; the answer's ticks are
//    "LINE:HASH" (or "LINE" for a line with no torrents);
// 4. the lines run in the file's order, each checked again as it runs (a line that asks runs
//    as allowed in the window; one Settings allowed that asks now is refused, `inPlan`), and
//    the answer lists, per line, what was done, what failed and what was left unticked.

#include "core/AgentAnswers.hpp"
#include "core/AgentGate.hpp"
#include "core/AgentPlan.hpp"
#include "core/AgentWords.hpp"
#include "core/Format.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "core/Version.hpp"
#include "ui/app/AgentAsk.hpp"
#include "ui/app/AgentDo.hpp"
#include "ui/app/AgentService.hpp"
#include "ui/app/App.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

namespace omaseed::app {

namespace AgentPlan = core::AgentPlan;
namespace Answers = core::AgentAnswers;
namespace AgentWords = core::AgentWords;

namespace {

const QString kCommand = QStringLiteral("do");

QList<std::pair<QString, QJsonValue>> envelope()
{
    return {{QStringLiteral("v"), 1},
            {QStringLiteral("command"), kCommand},
            {QStringLiteral("at"), AgentWords::isoUtc(QDateTime::currentDateTimeUtc())},
            {QStringLiteral("omaseed"), core::version()}};
}

QString compact(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool isFailure(const QJsonObject &answer)
{
    return answer.contains(QStringLiteral("error"));
}

QString codeOf(const QJsonObject &answer)
{
    return answer.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("code"))
        .toString();
}

QString messageOf(const QJsonObject &answer)
{
    return answer.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("message"))
        .toString();
}

/// A line's torrents as its check matched them: [{hash, name, …}].
QJsonArray resolved(const QJsonObject &check)
{
    return check.value(QStringLiteral("torrents")).toArray();
}

bool serverWide(const QString &action)
{
    return action == QLatin1String("pause") || action == QLatin1String("resume");
}

/// A line counts its torrents; one with none (pause, resume, a move back) counts as one.
int weightOf(const AgentPlan::Line &line, const QJsonObject &check)
{
    if (serverWide(line.action) || line.action == QLatin1String("move-back"))
        return 1;
    return int(resolved(check).size());
}

QString countOf(qsizetype n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
}

/// An action counted: "remove", "removes"; "move back", "moves back".
std::pair<QString, QString> unitsOf(const QString &action)
{
    if (action == QLatin1String("move-back"))
        return {QStringLiteral("move back"), QStringLiteral("moves back")};
    if (action == QLatin1String("start-here"))
        return {QStringLiteral("start-here"), QStringLiteral("start-heres")};
    return {action, action + QLatin1Char('s')};
}

/// "6 removes", "1 move back": what an asking line asks for, as the plan's last line counts it.
QString asksFor(const QString &action, int n)
{
    const auto [one, many] = unitsOf(action);
    return countOf(n, one, many);
}

core::AgentOrigin originOf(const core::ProcParent::Caller &caller, bool asked)
{
    core::AgentOrigin origin;
    origin.caller = caller.parent;
    origin.pid = caller.parentPid != 0 ? caller.parentPid : caller.wordPid;
    origin.asked = asked;
    return origin;
}

} // namespace

struct AgentDo::PlanRun
{
    AgentPlan::Plan plan;
    QString server; ///< the --server or the plan's
    bool dryRun = false;
    core::ProcParent::Caller caller;
    Reply reply;
    QList<QJsonObject> checks;   ///< each line's dry run, or its failure
    QList<bool> asks;            ///< each line asks (its dry run said `would: ask`)
    QList<QVariantMap> requests; ///< each asking line's ask, kept (`probe`)
    QStringList ticks;           ///< what the person left ticked
    bool asked = false;
    QList<QJsonObject> answers; ///< each line's answer as it ran
    QList<QJsonArray> unticked; ///< each line's torrents left unticked
    int next = 0;
    QHash<int, QJsonArray> refused;       ///< a refused line's torrents, each with why (#303)
    QList<std::pair<int, QString>> alone; ///< a refused line's torrents still to check alone
    int nextAlone = 0;
    int planId = 0; ///< its lines' acts wait for the plan's one strip (#304, App::AgentAct::plan)
};

void AgentDo::runPlan(const QString &text,
                      const QString &server,
                      bool dryRun,
                      const core::ProcParent::Caller &caller,
                      const Reply &reply)
{
    AgentPlan::Plan plan = AgentPlan::parse(text);
    if (!plan.usage.isEmpty()) {
        reply(AgentService::failure(kCommand, QStringLiteral("usage"), plan.usage));
        return;
    }
    if (!server.isEmpty() && !plan.server.isEmpty() &&
        server.compare(plan.server, Qt::CaseInsensitive) != 0) {
        reply(AgentService::failure(
            kCommand,
            QStringLiteral("usage"),
            QStringLiteral("omaseed: the plan names %1 and --server %2: a plan acts on one "
                           "server, the one omaseed is on.")
                .arg(plan.server, server)));
        return;
    }
    auto run = std::make_shared<PlanRun>();
    run->server = server.isEmpty() ? plan.server : server;
    run->plan = std::move(plan);
    run->dryRun = dryRun;
    run->caller = caller;
    run->reply = reply;
    planCheckNext(run);
}

void AgentDo::planCheckNext(const std::shared_ptr<PlanRun> &run)
{
    // GT4: every line checked first, as its own dry run (GT5), nothing asked or done.
    const int i = int(run->checks.size());
    if (i == run->plan.lines.size()) {
        planChecked(run);
        return;
    }
    const QJsonObject options = AgentPlan::options(run->plan.lines.at(i), run->server, true);
    QPointer<AgentDo> self(this);
    proceed(
        compact(options),
        run->caller,
        [self, run](const QString &json) {
            run->checks << QJsonDocument::fromJson(json.toUtf8()).object();
            // Queued: a line answered at once doesn't nest the next inside it.
            QTimer::singleShot(0, self, [self, run]() {
                if (!self.isNull())
                    self->planCheckNext(run);
            });
        },
        false);
}

void AgentDo::planChecked(const std::shared_ptr<PlanRun> &run)
{
    run->asks.clear();
    bool failing = false;
    bool asking = false;
    for (const QJsonObject &check : std::as_const(run->checks)) {
        const bool asks = check.value(QStringLiteral("would")).toString() == QLatin1String("ask");
        run->asks << asks;
        failing = failing || isFailure(check);
        asking = asking || asks;
    }
    if (failing) {
        planRefusals(run);
        return;
    }
    if (run->dryRun) {
        run->reply(planChecks(run));
        return;
    }
    run->requests = QList<QVariantMap>(run->plan.lines.size());
    if (asking) {
        run->next = 0;
        planProbeNext(run);
        return;
    }
    run->next = 0;
    planRunNext(run);
}

namespace {

/// One torrent a refused line names, as its `torrents` lists it (#303): {hash, name, code,
/// message}, the hash and name as the library has them (the query when none matches).
QJsonObject refusalOf(const QList<Answers::LogTorrent> &library,
                      const QString &query,
                      const QString &serverName,
                      const QJsonObject &check)
{
    const Answers::TorrentMatch match = Answers::matchTorrent(library, query, serverName);
    return QJsonObject{
        {QStringLiteral("hash"), match.code.isEmpty() ? match.torrent.hash : query},
        {QStringLiteral("name"), match.code.isEmpty() ? match.torrent.name : QString()},
        {QStringLiteral("code"), codeOf(check)},
        {QStringLiteral("message"), messageOf(check)}};
}

} // namespace

void AgentDo::planRefusals(const std::shared_ptr<PlanRun> &run)
{
    // #303: a refused line names every torrent it refuses, each with why, so one dry run finds
    // them all. A line's check stops at its first refusal (or, for a short find, lists them in
    // `failed`); a line of more torrents is checked again one torrent at a time. A line that is
    // off is off for every torrent: its error says so once.
    const QList<Answers::LogTorrent> library = m_service->logLibrary();
    const QString serverName = m_service->currentServer().name;
    run->refused.clear();
    run->alone.clear();
    for (qsizetype i = 0; i < run->plan.lines.size(); ++i) {
        const QJsonObject check = run->checks.value(i);
        const AgentPlan::Line &line = run->plan.lines.at(i);
        if (!isFailure(check) || codeOf(check) == QLatin1String("not-allowed") ||
            line.torrents.isEmpty())
            continue;
        const QJsonArray failed = check.value(QStringLiteral("failed")).toArray();
        if (!failed.isEmpty()) {
            run->refused.insert(int(i), failed);
        } else if (line.torrents.size() == 1) {
            run->refused.insert(
                int(i),
                QJsonArray{refusalOf(library, line.torrents.constFirst(), serverName, check)});
        } else {
            for (const QString &query : line.torrents)
                run->alone.append({int(i), query});
        }
    }
    run->nextAlone = 0;
    planRefusalNext(run);
}

void AgentDo::planRefusalNext(const std::shared_ptr<PlanRun> &run)
{
    const QList<Answers::LogTorrent> library = m_service->logLibrary();
    const QString serverName = m_service->currentServer().name;
    if (run->nextAlone == run->alone.size()) {
        // Every torrent passing alone: the line refuses them together; each says the line's why.
        for (const auto &[i, query] : std::as_const(run->alone))
            if (run->refused.value(i).isEmpty())
                for (const QString &each : run->plan.lines.at(i).torrents)
                    run->refused[i].append(refusalOf(library, each, serverName, run->checks.at(i)));
        run->reply(planChecks(run));
        return;
    }
    const auto [i, query] = run->alone.at(run->nextAlone++);
    AgentPlan::Line line = run->plan.lines.at(i);
    line.torrents = QStringList{query};
    QPointer<AgentDo> self(this);
    proceed(
        compact(AgentPlan::options(line, run->server, true)),
        run->caller,
        [self, run, i, query, library, serverName](const QString &json) {
            const QJsonObject check = QJsonDocument::fromJson(json.toUtf8()).object();
            if (isFailure(check)) {
                const QJsonArray failed = check.value(QStringLiteral("failed")).toArray();
                if (!failed.isEmpty()) {
                    for (const QJsonValue &each : failed)
                        run->refused[i].append(each);
                } else {
                    run->refused[i].append(refusalOf(library, query, serverName, check));
                }
            }
            QTimer::singleShot(0, self, [self, run]() {
                if (!self.isNull())
                    self->planRefusalNext(run);
            });
        },
        false);
}

QString AgentDo::planChecks(const std::shared_ptr<PlanRun> &run) const
{
    // GT5's table (board 67 §6): each line, what it would do; and, when one is off or refused,
    // the failure that runs none of it (GT4).
    QJsonArray lines;
    QList<int> failing;
    int torrents = 0;
    QStringList askWords;
    QHash<QString, int> asked;
    QStringList askedOrder;
    for (qsizetype i = 0; i < run->plan.lines.size(); ++i) {
        const AgentPlan::Line &line = run->plan.lines.at(i);
        const QJsonObject check = run->checks.value(i);
        QJsonObject out{{QStringLiteral("action"), line.action}};
        if (!line.to.isEmpty())
            out.insert(QStringLiteral("to"), line.to);
        if (!line.move.isEmpty())
            out.insert(QStringLiteral("move"), line.move);
        if (isFailure(check)) {
            failing << int(i);
            const QString code = codeOf(check);
            out.insert(QStringLiteral("would"),
                       code == QLatin1String("not-allowed") ? QStringLiteral("off")
                                                            : QStringLiteral("refused"));
            // #303: every torrent it refuses, each with why; `of` counts the line's torrents.
            out.insert(QStringLiteral("torrents"), run->refused.value(int(i)));
            if (!line.torrents.isEmpty())
                out.insert(QStringLiteral("of"), int(line.torrents.size()));
            out.insert(QStringLiteral("error"),
                       QJsonObject{{QStringLiteral("code"), code},
                                   {QStringLiteral("message"), messageOf(check)}});
            torrents += serverWide(line.action) || line.action == QLatin1String("move-back")
                            ? 1
                            : int(line.torrents.size());
        } else {
            out.insert(QStringLiteral("would"), check.value(QStringLiteral("would")));
            out.insert(QStringLiteral("torrents"), resolved(check));
            out.insert(QStringLiteral("outcome"), check.value(QStringLiteral("outcome")));
            torrents += weightOf(line, check);
            if (run->asks.value(i)) {
                if (!asked.contains(line.action))
                    askedOrder << line.action;
                asked[line.action] += weightOf(line, check);
            }
        }
        lines.append(out);
    }
    for (const QString &action : std::as_const(askedOrder))
        askWords << asksFor(action, asked.value(action));

    auto fields = envelope();
    fields.append({QStringLiteral("plan"), true});
    fields.append({QStringLiteral("server"), m_service->currentServer().name});
    if (run->dryRun)
        fields.append({QStringLiteral("dryRun"), true});
    fields.append({QStringLiteral("torrents"), torrents});
    fields.append({QStringLiteral("lines"), lines});
    if (failing.isEmpty()) {
        fields.append({QStringLiteral("outcome"),
                       askWords.isEmpty() ? QStringLiteral("Every line would run.")
                                          : QStringLiteral("Would ask once, for %1.")
                                                .arg(askWords.join(QStringLiteral(", ")))});
        return QString::fromUtf8(AgentWords::orderedJson(fields));
    }
    int off = 0;
    for (int i : std::as_const(failing))
        off += codeOf(run->checks.at(i)) == QLatin1String("not-allowed") ? 1 : 0;
    const QString how = off == failing.size() ? QStringLiteral("off")
                        : off == 0            ? QStringLiteral("refused")
                                              : QStringLiteral("off or refused");
    const QString said =
        QStringLiteral("%1 %2: nothing %3. Fix the plan and try again.")
            .arg(countOf(failing.size(), QStringLiteral("line"), QStringLiteral("lines")),
                 how,
                 run->dryRun ? QStringLiteral("would run") : QStringLiteral("ran"));
    fields.append({QStringLiteral("outcome"), said});
    // The first line's failure says why; its code is the exit (C45 V13).
    const int first = failing.constFirst();
    const AgentPlan::Line &line = run->plan.lines.at(first);
    fields.append({QStringLiteral("error"),
                   QJsonObject{{QStringLiteral("code"), codeOf(run->checks.at(first))},
                               {QStringLiteral("message"),
                                QStringLiteral("%1 Line %2 (%3): %4")
                                    .arg(said)
                                    .arg(first + 1)
                                    .arg(line.action, messageOf(run->checks.at(first)))}}});
    return QString::fromUtf8(AgentWords::orderedJson(fields));
}

void AgentDo::planProbeNext(const std::shared_ptr<PlanRun> &run)
{
    // Each line that asks, up to its ask: its map, for the plan's one window.
    while (run->next < run->plan.lines.size() && !run->asks.value(run->next))
        ++run->next;
    if (run->next == run->plan.lines.size()) {
        planAsk(run);
        return;
    }
    const int i = run->next++;
    QJsonObject options = AgentPlan::options(run->plan.lines.at(i), run->server, false);
    options.insert(QStringLiteral("probe"), true);
    QPointer<AgentDo> self(this);
    proceed(
        compact(options),
        run->caller,
        [self, run, i](const QString &json) {
            const QJsonObject answer = QJsonDocument::fromJson(json.toUtf8()).object();
            if (answer.contains(QStringLiteral("probe"))) {
                run->requests[i] = answer.value(QStringLiteral("probe")).toObject().toVariantMap();
            } else if (isFailure(answer)) {
                // Refused since its check a moment ago: the plan runs none (GT4).
                run->checks[i] = answer;
                run->reply(self.isNull() ? json : self->planChecks(run));
                return;
            } else {
                // Settings allow it now (its line turned to allow since the check): nothing
                // ran; it shows dim in the window and runs with the plan once allowed (GT4).
                run->asks[i] = false;
            }
            QTimer::singleShot(0, self, [self, run]() {
                if (!self.isNull())
                    self->planProbeNext(run);
            });
        },
        false);
}

QVariantMap AgentDo::planRequest(const std::shared_ptr<PlanRun> &run) const
{
    // Board 67 §4's window: the groups, the who, the yellow line.
    const core::TorrentModel *model = m_app->engine()->torrents();
    const QString serverName = m_service->currentServer().name;
    QVariantList groups;
    for (const AgentPlan::Group &group : AgentPlan::groups(run->plan.lines, run->asks)) {
        const QString title = AgentPlan::actionTitle(group.action);
        int count = 0;
        QStringList names;
        QStringList folders;
        QHash<QString, int> perFolder;
        QVariantList rows;
        QStringList wheres;
        QStringList checks;
        for (int i : group.lines) {
            const AgentPlan::Line &line = run->plan.lines.at(i);
            const QJsonArray torrents = resolved(run->checks.at(i));
            count += weightOf(line, run->checks.at(i));
            if (!line.to.isEmpty()) {
                if (!perFolder.contains(line.to))
                    folders << line.to;
                perFolder[line.to] += int(torrents.size());
            }
            const QVariantMap request = run->requests.value(i);
            const QString where = request.value(QStringLiteral("where")).toString();
            if (!where.isEmpty() && !wheres.contains(where))
                wheres << where;
            for (const QVariant &check : request.value(QStringLiteral("checks")).toList())
                if (!checks.contains(check.toString()))
                    checks << check.toString();
            if (torrents.isEmpty() || line.action == QLatin1String("move-back")) {
                // A line with no torrents of its own (a move back, pause, resume): one box.
                QStringList said;
                for (const QJsonValue &torrent : torrents)
                    said << torrent.toObject().value(QStringLiteral("name")).toString();
                rows.append(
                    QVariantMap{{QStringLiteral("key"), QString::number(i)},
                                {QStringLiteral("name"),
                                 said.isEmpty() ? request.value(QStringLiteral("head")).toString()
                                                : said.join(QStringLiteral(", "))},
                                {QStringLiteral("line"),
                                 line.action == QLatin1String("move-back")
                                     ? QStringLiteral("back, each to where it was")
                                     : serverName}});
                names << rows.constLast().toMap().value(QStringLiteral("name")).toString();
                continue;
            }
            for (const QJsonValue &value : torrents) {
                const QJsonObject torrent = value.toObject();
                const QString hash = torrent.value(QStringLiteral("hash")).toString();
                const QString name = torrent.value(QStringLiteral("name")).toString();
                names << name;
                QStringList said;
                if (const int row = model->indexOfHash(hash); row >= 0) {
                    const core::Torrent &t = model->torrentAt(row);
                    if (!t.savePath.isEmpty())
                        said << t.savePath;
                    said << core::Format::size(t.size);
                }
                rows.append(
                    QVariantMap{{QStringLiteral("key"), QStringLiteral("%1:%2").arg(i).arg(hash)},
                                {QStringLiteral("name"), name},
                                {QStringLiteral("line"), said.join(QStringLiteral(" · "))}});
            }
        }

        QString head;
        if (serverWide(group.action))
            head = QStringLiteral("%1 everything on %2").arg(title, serverName);
        else if (group.action == QLatin1String("move-back"))
            head = count == 1 ? title : QStringLiteral("%1 %2 moves").arg(title).arg(count);
        else if (group.action == QLatin1String("remove"))
            head = QStringLiteral("%1 %2 from %3").arg(title).arg(count).arg(serverName);
        else if (group.asks && !group.to.isEmpty())
            head = QStringLiteral("%1 %2 → %3").arg(title).arg(count).arg(group.to);
        else
            head = QStringLiteral("%1 %2").arg(title).arg(count);

        // #305: the window's foot counts the boxes ticked by action, the allowed lines apart.
        const auto [unit, units] = unitsOf(group.action);
        QVariantMap out{{QStringLiteral("action"), group.action},
                        {QStringLiteral("head"), head},
                        {QStringLiteral("asks"), group.asks},
                        {QStringLiteral("count"), count},
                        {QStringLiteral("unit"), unit},
                        {QStringLiteral("units"), units}};
        if (group.asks) {
            out.insert(QStringLiteral("rows"), rows);
            out.insert(QStringLiteral("more"), wheres.join(QStringLiteral(" · ")));
            out.insert(QStringLiteral("checks"), checks);
        } else {
            // Board 67: the dim group's line, a few names and where they go.
            QStringList more;
            if (!serverWide(group.action) && !names.isEmpty())
                more << names.mid(0, 2).join(QStringLiteral(", ")) +
                            (names.size() > 2 ? QStringLiteral("…") : QString());
            for (const QString &folder : std::as_const(folders))
                more << QStringLiteral("%1 → %2").arg(perFolder.value(folder)).arg(folder);
            if (group.action == QLatin1String("locate"))
                more << QStringLiteral("every file found");
            out.insert(QStringLiteral("more"), more.join(QStringLiteral(" · ")));
        }
        groups.append(out);
    }
    QVariantMap request{{QStringLiteral("action"), QStringLiteral("plan")},
                        {QStringLiteral("plan"), true},
                        {QStringLiteral("head"), QStringLiteral("An agent asks to run a plan")},
                        {QStringLiteral("groups"), groups}};
    addCaller(&request, run->caller);
    if (request.value(QStringLiteral("word")).toString() == QLatin1String("omaseed do"))
        request.insert(QStringLiteral("word"), QStringLiteral("omaseed do --plan"));
    request.insert(QStringLiteral("allowText"), QStringLiteral("allow these"));
    return request;
}

void AgentDo::planAsk(const std::shared_ptr<PlanRun> &run)
{
    if (!run->asks.contains(true)) {
        // Settings allow every line that asked now: the plan runs as an allowed one.
        planRunNext(run);
        return;
    }
    const QString subject =
        QStringLiteral("a plan of %1")
            .arg(countOf(run->plan.lines.size(), QStringLiteral("line"), QStringLiteral("lines")));
    const core::AgentOrigin origin = originOf(run->caller, false);
    const auto notRun = [run](const QString &code, const QString &message, bool shown) {
        auto fields = envelope();
        fields.append({QStringLiteral("plan"), true});
        fields.append({QStringLiteral("asked"), shown});
        fields.append({QStringLiteral("answer"),
                       code == QLatin1String("refused") ? QStringLiteral("refused")
                                                        : QStringLiteral("no-answer")});
        fields.append(
            {QStringLiteral("error"),
             QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}});
        run->reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
    };
    QPointer<AgentDo> self(this);
    const bool opened = m_app->agentAsk()->ask(
        planRequest(run),
        [self, run, subject, origin, notRun](const QString &answer, const QVariantList &ticks) {
            if (answer == QLatin1String("allowed") && !self.isNull()) {
                run->asked = true;
                for (const QVariant &tick : ticks)
                    run->ticks << tick.toString();
                run->next = 0;
                self->planRunNext(run);
                return;
            }
            const bool refused = answer == QLatin1String("refused");
            if (!self.isNull())
                self->m_app->noteAgentRefusal(QStringLiteral("plan"),
                                              refused ? QStringLiteral("refused in the window")
                                                      : QStringLiteral("no answer in the window"),
                                              origin,
                                              subject);
            notRun(refused ? QStringLiteral("refused") : QStringLiteral("no-answer"),
                   refused ? QStringLiteral("Refused in omaseed's window: none of the plan ran.")
                   : answer == QLatin1String("allowed")
                       ? QStringLiteral("omaseed went away before it could run the plan: none "
                                        "of it ran.")
                       : QStringLiteral("Nobody answered in omaseed's window for 2 minutes: "
                                        "none of the plan ran."),
                   true);
        });
    if (opened) {
        // docs/67 §6: "Asking in omaseed's window… 6 removes ask." on the CLI's stderr.
        QStringList order;
        QHash<QString, int> asked;
        for (qsizetype i = 0; i < run->plan.lines.size(); ++i) {
            if (!run->asks.value(i))
                continue;
            const AgentPlan::Line &line = run->plan.lines.at(i);
            if (!asked.contains(line.action))
                order << line.action;
            asked[line.action] += weightOf(line, run->checks.value(i));
        }
        QStringList words;
        int total = 0;
        for (const QString &action : std::as_const(order)) {
            words << asksFor(action, asked.value(action));
            total += asked.value(action);
        }
        m_service->sayAsking(run->caller.wordPid,
                             QStringLiteral("Asking in omaseed's window… %1 %2.")
                                 .arg(words.join(QStringLiteral(", ")),
                                      total == 1 ? QStringLiteral("asks") : QStringLiteral("ask")));
    } else {
        m_app->noteAgentRefusal(QStringLiteral("plan"),
                                QStringLiteral("not asked: another ask was open"),
                                origin,
                                subject);
        notRun(QStringLiteral("no-answer"),
               QStringLiteral("Another ask is open in omaseed's window, so the plan wasn't asked: "
                              "none of it ran. Ask again once it is answered."),
               false);
    }
}

void AgentDo::planRunNext(const std::shared_ptr<PlanRun> &run)
{
    if (run->answers.size() < run->plan.lines.size()) {
        run->answers = QList<QJsonObject>(run->plan.lines.size());
        run->unticked = QList<QJsonArray>(run->plan.lines.size());
        run->planId = ++m_lastPlan;
    }
    if (run->next >= run->plan.lines.size()) {
        planDone(run);
        return;
    }
    const int i = run->next++;
    const QPointer<AgentDo> self(this);
    const auto onward = [self, run]() {
        QTimer::singleShot(0, self, [self, run]() {
            if (!self.isNull())
                self->planRunNext(run);
        });
    };
    const AgentPlan::Line &given = run->plan.lines.at(i);
    const QJsonArray torrents = resolved(run->checks.at(i));
    AgentPlan::Line line = given;
    const bool asks = run->asks.at(i);
    if (asks) {
        // Only what the person left ticked (GT4); the rest is said as unticked.
        if (torrents.isEmpty() || given.action == QLatin1String("move-back")) {
            if (!run->ticks.contains(QString::number(i))) {
                run->unticked[i] =
                    torrents.isEmpty()
                        ? QJsonArray{QJsonObject{{QStringLiteral("name"), given.action}}}
                        : torrents;
                onward();
                return;
            }
        } else {
            line.torrents.clear();
            QJsonArray left;
            for (const QJsonValue &value : torrents) {
                const QJsonObject torrent = value.toObject();
                const QString hash = torrent.value(QStringLiteral("hash")).toString();
                if (run->ticks.contains(QStringLiteral("%1:%2").arg(i).arg(hash)))
                    line.torrents << hash;
                else
                    left.append(QJsonObject{
                        {QStringLiteral("hash"), hash},
                        {QStringLiteral("name"), torrent.value(QStringLiteral("name"))}});
            }
            run->unticked[i] = left;
            if (line.torrents.isEmpty()) {
                onward();
                return;
            }
        }
    } else if (!torrents.isEmpty() && given.action != QLatin1String("move-back")) {
        // The hashes its check matched, so a prefix can't match another torrent since.
        line.torrents.clear();
        for (const QJsonValue &value : torrents)
            line.torrents << value.toObject().value(QStringLiteral("hash")).toString();
    }
    QJsonObject options = AgentPlan::options(line, run->server, false);
    options.insert(QStringLiteral("inPlan"), true);
    options.insert(QStringLiteral("planId"), run->planId);
    proceed(
        compact(options),
        run->caller,
        [run, i, onward](const QString &json) {
            run->answers[i] = QJsonDocument::fromJson(json.toUtf8()).object();
            onward();
        },
        asks);
}

void AgentDo::planDone(const std::shared_ptr<PlanRun> &run)
{
    // Board 67 §6's answer: per line done, failed and unticked; the plan's last line.
    QJsonArray lines;
    int of = 0;
    int done = 0;
    // #304: what the plan's one strip, journal and last line say.
    int rechecking = 0;
    int acted = 0;
    int didnt = 0;
    QStringList actedOrder;
    QHash<QString, int> actedBy;
    QStringList torrentLines;
    for (qsizetype i = 0; i < run->plan.lines.size(); ++i) {
        const AgentPlan::Line &line = run->plan.lines.at(i);
        const QJsonObject &check = run->checks.at(i);
        const QJsonObject &answer = run->answers.at(i);
        of += weightOf(line, check);
        QJsonObject out{{QStringLiteral("action"), line.action}};
        if (!line.to.isEmpty())
            out.insert(QStringLiteral("to"), line.to);
        if (!line.move.isEmpty())
            out.insert(QStringLiteral("move"), line.move);
        QJsonArray lineDone;
        QJsonArray failed;
        const bool ran = !answer.isEmpty();
        if (ran && isFailure(answer)) {
            // Refused or failed as it ran (a setting changed, a torrent gone): this line alone.
            const QJsonObject failure{{QStringLiteral("code"), codeOf(answer)},
                                      {QStringLiteral("message"), messageOf(answer)}};
            const QJsonArray torrents = resolved(check);
            if (torrents.isEmpty()) {
                failed.append(failure);
            } else {
                for (const QJsonValue &value : torrents) {
                    const QJsonObject torrent = value.toObject();
                    bool unticked = false;
                    for (const QJsonValue &left : run->unticked.at(i))
                        unticked = unticked || left.toObject().value(QStringLiteral("hash")) ==
                                                   torrent.value(QStringLiteral("hash"));
                    if (unticked)
                        continue;
                    QJsonObject entry = failure;
                    entry.insert(QStringLiteral("hash"), torrent.value(QStringLiteral("hash")));
                    entry.insert(QStringLiteral("name"), torrent.value(QStringLiteral("name")));
                    failed.append(entry);
                }
            }
        } else if (ran) {
            lineDone = answer.value(QStringLiteral("done")).toArray();
            failed = answer.value(QStringLiteral("failed")).toArray();
            const bool none = serverWide(line.action) ||
                              (line.action == QLatin1String("move-back") && lineDone.isEmpty());
            done += none && failed.isEmpty() ? 1 : int(lineDone.size());
            for (const QString &key :
                 {QStringLiteral("job"), QStringLiteral("queued"), QStringLiteral("count")})
                if (answer.contains(key))
                    out.insert(key, answer.value(key));
            out.insert(QStringLiteral("outcome"), answer.value(QStringLiteral("outcome")));
        }
        out.insert(QStringLiteral("done"), lineDone);
        out.insert(QStringLiteral("failed"), failed);
        if (line.action == QLatin1String("locate") || line.action == QLatin1String("recheck"))
            rechecking += int(lineDone.size());
        if (ran) {
            const bool wide = serverWide(line.action);
            const int count = wide ? 1 : int(lineDone.size() + failed.size());
            if (count > 0 && !actedBy.contains(line.action))
                actedOrder << line.action;
            actedBy[line.action] += count;
            acted += count;
            didnt += int(failed.size());
            const QString where = line.to.isEmpty() ? QString()
                                  : line.action == QLatin1String("locate")
                                      ? QStringLiteral(" in %1").arg(line.to)
                                      : QStringLiteral(" to %1").arg(line.to);
            if (wide && failed.isEmpty())
                torrentLines << QStringLiteral("agent: %1 on %2, in the plan")
                                    .arg(line.action, m_service->currentServer().name);
            for (const QJsonValue &value : std::as_const(lineDone))
                torrentLines << QStringLiteral("agent: %1 on %2%3, in the plan")
                                    .arg(line.action,
                                         value.toObject().value(QStringLiteral("name")).toString(),
                                         where);
            for (const QJsonValue &value : std::as_const(failed)) {
                const QJsonObject each = value.toObject();
                const QString name = each.value(QStringLiteral("name")).toString();
                torrentLines << QStringLiteral("agent: %1%2%3, in the plan, didn't: %4")
                                    .arg(line.action,
                                         name.isEmpty() ? QString() : QStringLiteral(" on ") + name,
                                         where,
                                         each.value(QStringLiteral("message")).toString());
            }
        }
        if (run->asks.at(i))
            out.insert(QStringLiteral("unticked"), run->unticked.at(i));
        lines.append(out);
    }
    auto fields = envelope();
    fields.append({QStringLiteral("plan"), true});
    fields.append({QStringLiteral("server"), m_service->currentServer().name});
    fields.append({QStringLiteral("asked"), run->asked});
    fields.append({QStringLiteral("answer"),
                   run->asked ? QStringLiteral("allowed") : QStringLiteral("not-asked")});
    fields.append({QStringLiteral("torrents"), of});
    fields.append({QStringLiteral("rechecking"), rechecking});
    fields.append({QStringLiteral("lines"), lines});
    fields.append({QStringLiteral("outcome"), AgentPlan::outcome(done, of, rechecking)});
    if (acted > 0) {
        QStringList by;
        for (const QString &action : std::as_const(actedOrder))
            by << QStringLiteral("%1 %2").arg(actedBy.value(action)).arg(action);
        const QString subject =
            countOf(acted, QStringLiteral("torrent"), QStringLiteral("torrents"));
        m_app->noteAgentPlan(run->planId,
                             originOf(run->caller, run->asked),
                             subject,
                             QStringLiteral("%1 (%2)").arg(subject, by.join(QStringLiteral(", "))),
                             didnt > 0 ? QStringLiteral("%1 didn't").arg(didnt) : QString(),
                             torrentLines,
                             AgentPlan::doneLine(done, of, rechecking));
    }
    run->reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
}

} // namespace omaseed::app
