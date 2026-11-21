#include "ui/app/AgentDo.hpp"

#include "core/ActionOutcome.hpp"
#include "core/AgentAnswers.hpp"
#include "core/AgentGate.hpp"
#include "core/AgentWords.hpp"
#include "core/AnnounceMask.hpp"
#include "core/Format.hpp"
#include "core/FreeSpace.hpp"
#include "core/LocateCheck.hpp"
#include "core/MoveHistory.hpp"
#include "core/MoveJob.hpp"
#include "core/PauseHold.hpp"
#include "core/ProblemLog.hpp"
#include "core/RemovedKeep.hpp"
#include "core/SharedData.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentProblem.hpp"
#include "core/Version.hpp"
#include "ui/app/AgentAsk.hpp"
#include "ui/app/AgentService.hpp"
#include "ui/app/App.hpp"
#include "ui/app/MoveController.hpp"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTime>
#include <QTimer>

#include <memory>

namespace omaseed::app {

namespace Answers = core::AgentAnswers;
namespace AgentWords = core::AgentWords;
using core::AgentGate;

namespace {

const QString kCommand = QStringLiteral("do");

/// What Do's options say (C45 V3: only what was given).
struct DoOptions
{
    bool list = false;
    bool dryRun = false;
    QString action;
    QStringList torrents;
    QString to;
    QString move;
    QString server;
    QString usage; ///< "" when the options are good
};

DoOptions parseOptions(const QString &json)
{
    DoOptions options;
    if (json.trimmed().isEmpty())
        return options;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        options.usage = AgentWords::doesNotTake(kCommand, json);
        return options;
    }
    const QJsonObject object = document.object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString key = it.key();
        const QJsonValue value = it.value();
        bool good = true;
        if (key == QLatin1String("list") || key == QLatin1String("dryRun")) {
            good = value.isBool();
            (key == QLatin1String("list") ? options.list : options.dryRun) = value.toBool();
        } else if (key == QLatin1String("torrents")) {
            good = value.isArray();
            for (const QJsonValue &hash : value.toArray()) {
                good = good && hash.isString() && !hash.toString().trimmed().isEmpty();
                options.torrents << hash.toString().toLower();
            }
        } else if (key == QLatin1String("action") || key == QLatin1String("to") ||
                   key == QLatin1String("move") || key == QLatin1String("server")) {
            good = value.isString();
            const QString text = value.toString();
            if (key == QLatin1String("action"))
                options.action = text;
            else if (key == QLatin1String("to"))
                options.to = text;
            else if (key == QLatin1String("move"))
                options.move = text;
            else
                options.server = text;
        } else if (key == QLatin1String("plan")) {
            // C50 GT4's plan comes with S11; the CLI says so before it calls.
            options.usage = AgentWords::notYet(QStringLiteral("do --plan"));
            return options;
        } else {
            good = false;
        }
        if (!good) {
            options.usage = AgentWords::doesNotTake(kCommand, key);
            return options;
        }
    }
    if (!options.list && options.action.isEmpty())
        options.usage = QStringLiteral("omaseed: do takes an action and its torrents: omaseed do "
                                       "recheck 3f9a1c0e. omaseed do --list says what it may do.");
    return options;
}

QString countOf(qsizetype n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
}

QJsonArray torrentsJson(const QList<Answers::LogTorrent> &torrents)
{
    QJsonArray out;
    for (const Answers::LogTorrent &torrent : torrents)
        out.append(QJsonObject{{QStringLiteral("hash"), torrent.hash},
                               {QStringLiteral("name"), torrent.name}});
    return out;
}

/// The envelope every do answer starts with (C45 V4).
QList<std::pair<QString, QJsonValue>> envelope(const QString &action)
{
    QList<std::pair<QString, QJsonValue>> fields{
        {QStringLiteral("v"), 1},
        {QStringLiteral("command"), kCommand},
        {QStringLiteral("at"), AgentWords::isoUtc(QDateTime::currentDateTimeUtc())},
        {QStringLiteral("omaseed"), core::version()},
    };
    if (!action.isEmpty())
        fields.append({QStringLiteral("action"), action});
    return fields;
}

/// The subject as the strip says it: the one torrent's name, else the count.
QString subjectOf(const QList<Answers::LogTorrent> &torrents)
{
    if (torrents.size() == 1)
        return torrents.constFirst().name;
    return countOf(torrents.size(), QStringLiteral("torrent"), QStringLiteral("torrents"));
}

/// An answer that ran nothing because of the ask: refused, unanswered, or not asked at all.
QString
notAskedAnswer(const QString &action, const QString &code, const QString &message, bool shown)
{
    auto fields = envelope(action);
    fields.append({QStringLiteral("asked"), shown});
    fields.append({QStringLiteral("answer"),
                   code == QLatin1String("refused") ? QStringLiteral("refused")
                                                    : QStringLiteral("no-answer")});
    fields.append(
        {QStringLiteral("error"),
         QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}});
    return QString::fromUtf8(AgentWords::orderedJson(fields));
}

/// The journal names what started the word (V17), else the word's own pid.
core::AgentOrigin originOf(const core::ProcParent::Caller &caller, bool asked)
{
    core::AgentOrigin origin;
    origin.caller = caller.parent;
    origin.pid = caller.parentPid != 0 ? caller.parentPid : caller.wordPid;
    origin.asked = asked;
    return origin;
}

/// How long an agent's move waits for its plan's file lists, listings and room (#293).
constexpr int kPlanWaitMs = 20000;

} // namespace

AgentDo::AgentDo(App *app, AgentService *service)
    : QObject(service)
    , m_app(app)
    , m_service(service)
{}

void AgentDo::run(const QString &json, uint callerPid, const Reply &reply)
{
    // V17: who asked, read once: the word's process as the bus named it, and its parent.
    proceed(json, core::ProcParent::callerOf(callerPid, m_procRoot), reply, false);
}

void AgentDo::proceed(const QString &json,
                      const core::ProcParent::Caller &caller,
                      const Reply &reply,
                      bool asked)
{
    const auto fail = [&reply](const QString &code, const QString &message) {
        reply(AgentService::failure(kCommand, code, message));
    };
    const DoOptions options = parseOptions(json);
    if (!options.usage.isEmpty()) {
        fail(QStringLiteral("usage"), options.usage);
        return;
    }
    const AgentGate &gate = m_app->agentGate();

    // GT3: do --list says the lines as they stand. It reads no library, so it answers with
    // Reads off too: that is how an agent learns why it gets nothing (assumed, S3).
    if (options.list) {
        auto fields = envelope(QString());
        fields.append({QStringLiteral("list"), true});
        fields.append({QStringLiteral("lines"), gate.listLines()});
        fields.append(
            {QStringLiteral("never"), QJsonArray::fromStringList(AgentGate::neverActions())});
        reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
        return;
    }

    const QString action = options.action;
    if (m_app->isDemoMode()) {
        fail(QStringLiteral("not-allowed"), QStringLiteral("A demo acts on nothing.")); // Q15
        return;
    }
    const AgentGate::Decision decision = gate.decide(action);
    // C45 V18: a refusal by the gate is still an agent's try, for the journal and the row's
    // last line; a dry run tried nothing (GT5).
    const core::AgentOrigin origin = originOf(caller, asked);
    const auto noteRefused = [this, &options, &action, &origin](const QString &how,
                                                                const QString &subject = {}) {
        if (!options.dryRun)
            m_app->noteAgentRefusal(action, how, origin, subject);
    };
    if (decision == AgentGate::Decision::Never) {
        noteRefused(QStringLiteral("never"));
        // C45 V14, docs/62 V13's words.
        fail(QStringLiteral("never"),
             QStringLiteral("omaseed do doesn't delete, rename or change settings: those stay "
                            "with the person, in the window."));
        return;
    }
    if (decision == AgentGate::Decision::Off) {
        if (const QString off = m_service->readsOff(kCommand); !off.isEmpty()) {
            noteRefused(QStringLiteral("not allowed: Reads is off"));
            reply(off); // C50 GT8
            return;
        }
        const AgentGate::Kind kind = AgentGate::kindOf(action);
        const QString line = AgentGate::keyOf(kind);
        noteRefused(
            QStringLiteral("not allowed: %1 is off").arg(line.left(1).toUpper() + line.mid(1)));
        fail(QStringLiteral("not-allowed"),
             QStringLiteral("omaseed doesn't let agents %1: %2 is off in Settings › General › "
                            "Agents. Only the person can change that.")
                 .arg(action, line.left(1).toUpper() + line.mid(1)));
        return;
    }

    static const QStringList built{QStringLiteral("retry"),
                                   QStringLiteral("start-here"),
                                   QStringLiteral("recheck"),
                                   QStringLiteral("reannounce"),
                                   QStringLiteral("dismiss"),
                                   QStringLiteral("pause"),
                                   QStringLiteral("resume"),
                                   QStringLiteral("move"),
                                   QStringLiteral("move-back"),
                                   QStringLiteral("locate"),
                                   QStringLiteral("remove")};
    if (!built.contains(action)) {
        fail(QStringLiteral("usage"), AgentWords::notYet(QStringLiteral("do ") + action));
        return;
    }
    const bool serverWide = action == QLatin1String("pause") || action == QLatin1String("resume");
    const bool isMove = action == QLatin1String("move");
    const bool isBack = action == QLatin1String("move-back");
    const bool isLocate = action == QLatin1String("locate");
    if (isLocate &&
        (options.to.trimmed().isEmpty() || !options.move.isEmpty() || options.torrents.isEmpty())) {
        fail(QStringLiteral("usage"),
             QStringLiteral("omaseed: do locate takes the torrents' hashes and --to FOLDER, the "
                            "folder that holds their files: omaseed do locate 3f9a1c0e --to "
                            "/data/music."));
        return;
    }
    if (isMove &&
        (options.to.trimmed().isEmpty() || !options.move.isEmpty() || options.torrents.isEmpty())) {
        fail(QStringLiteral("usage"),
             QStringLiteral("omaseed: do move takes the torrents' hashes and --to FOLDER: "
                            "omaseed do move 3f9a1c0e --to /data/music."));
        return;
    }
    if (isBack && (options.move.trimmed().isEmpty() || !options.to.isEmpty() ||
                   !options.torrents.isEmpty())) {
        fail(QStringLiteral("usage"),
             QStringLiteral("omaseed: do move-back takes one move's id: omaseed moves --json "
                            "gives them."));
        return;
    }
    if (!isMove && !isBack && !isLocate &&
        (!options.to.isEmpty() || !options.move.isEmpty() ||
         serverWide != options.torrents.isEmpty())) {
        // The CLI never sends these (AgentWords::parse); busctl might.
        fail(QStringLiteral("usage"),
             serverWide ? QStringLiteral("omaseed: do %1 takes no torrents: it holds the whole "
                                         "server, as the window's %1 does.")
                              .arg(action)
                        : QStringLiteral("omaseed: do %1 takes the torrents' hashes, or 8 or more "
                                         "of their first characters: omaseed errors gives them.")
                              .arg(action));
        return;
    }

    // The server, as the reads check it (C45 V6): only the one omaseed is on can act.
    const Answers::Server current = m_service->currentServer();
    if (!options.server.isEmpty()) {
        QString id;
        if (const QString refused = m_service->serverFailure(kCommand, options.server, &id);
            !refused.isEmpty()) {
            reply(refused);
            return;
        }
        if (id != current.id) {
            reply(m_service->notCurrent(kCommand, current.name, id));
            return;
        }
    }
    if (m_service->firstSync(current)) {
        reply(m_service->firstSyncFailure(kCommand, current.name));
        return;
    }
    if (!current.reachable || !m_app->hasServer()) {
        fail(QStringLiteral("unreachable"),
             core::AnnounceMask::maskText(
                 QStringLiteral("%1 is unreachable, so it can't %2 now: %3")
                     .arg(current.name,
                          action,
                          current.problem ? current.problem->line : QString())));
        return;
    }

    // Every torrent named, before anything runs (V15).
    const QList<Answers::LogTorrent> library = m_service->logLibrary();
    QList<Answers::LogTorrent> torrents;
    QStringList hashes;
    for (const QString &query : options.torrents) {
        const Answers::TorrentMatch match = Answers::matchTorrent(library, query, current.name);
        if (!match.code.isEmpty()) {
            fail(match.code, match.message);
            return;
        }
        if (hashes.contains(match.torrent.hash))
            continue;
        hashes << match.torrent.hash;
        torrents << match.torrent;
    }

    // Moves plan first, as the window's sheet does (#293).
    if (isMove || isBack) {
        proceedMove(json,
                    caller,
                    reply,
                    asked,
                    options.dryRun,
                    action,
                    hashes,
                    options.to.trimmed(),
                    options.move.trimmed());
        return;
    }

    // Locate finds the files first (#294, C50 GT2).
    if (isLocate) {
        proceedLocate(json,
                      caller,
                      reply,
                      asked,
                      options.dryRun,
                      decision == AgentGate::Decision::Ask,
                      hashes,
                      options.to.trimmed());
        return;
    }

    // Remove looks at the disk first (#295, C50 GT1).
    if (action == QLatin1String("remove")) {
        proceedRemove(json, caller, reply, asked, options.dryRun, hashes);
        return;
    }

    // What each action needs of its torrents (refused: nothing runs).
    const core::ProblemLog *problems = m_app->engine()->problems();
    core::PauseHold *hold = m_app->pauseHold();
    if (action == QLatin1String("start-here")) {
        if (m_app->features().pausesOnError) {
            // C25 K24: the engine paused it, and only a recheck clears its error (the strip's
            // recheckFirst).
            fail(QStringLiteral("refused"),
                 QStringLiteral("Not started: %1 pauses a torrent whose move failed, and only a "
                                "recheck clears it. Recheck first.")
                     .arg(current.name));
            return;
        }
        for (const Answers::LogTorrent &torrent : std::as_const(torrents)) {
            if (problems == nullptr ||
                !problems->problem(torrent.hash).actions.contains(QStringLiteral("startHere"))) {
                fail(QStringLiteral("refused"),
                     QStringLiteral("Not started: %1 wasn't stopped by a failed move, and "
                                    "start-here is for one. retry starts it as it is.")
                         .arg(torrent.name));
                return;
            }
        }
    } else if (action == QLatin1String("dismiss")) {
        for (const Answers::LogTorrent &torrent : std::as_const(torrents)) {
            if (problems == nullptr || problems->failure(torrent.hash).isEmpty()) {
                fail(QStringLiteral("refused"),
                     QStringLiteral("Not dismissed: %1 shows no failure, so there is nothing to "
                                    "dismiss.")
                         .arg(torrent.name));
                return;
            }
        }
    } else if (serverWide) {
        if (hold == nullptr || hold->busy()) {
            fail(QStringLiteral("refused"),
                 QStringLiteral("A pause or a resume is already on its way on %1: ask again in a "
                                "moment.")
                     .arg(current.name));
            return;
        }
        if (action == QLatin1String("resume") && !hold->held()) {
            fail(QStringLiteral("refused"),
                 QStringLiteral("omaseed holds nothing paused on %1, so there is nothing to "
                                "resume.")
                     .arg(current.name));
            return;
        }
    }

    // GT5: every check made, nothing asked, nothing done.
    if (options.dryRun) {
        const bool asks = decision == AgentGate::Decision::Ask;
        auto fields = envelope(action);
        fields.append({QStringLiteral("dryRun"), true});
        fields.append(
            {QStringLiteral("would"), asks ? QStringLiteral("ask") : QStringLiteral("run")});
        fields.append({QStringLiteral("torrents"), torrentsJson(torrents)});
        fields.append(
            {QStringLiteral("outcome"),
             QStringLiteral("%1 would %2%3")
                 .arg(action,
                      asks ? QStringLiteral("ask in omaseed's window") : QStringLiteral("run"),
                      serverWide ? QStringLiteral(" on %1").arg(current.name)
                                 : QStringLiteral(" · ") + subjectOf(torrents))});
        reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
        return;
    }

    if (decision == AgentGate::Decision::Ask && !asked) {
        askFirst(json,
                 caller,
                 reply,
                 action,
                 serverWide ? current.name : subjectOf(torrents),
                 askRequest(action, hashes, current.name, caller));
        return;
    }

    // Allowed: run it as the window does, and answer once the server has.
    App::AgentAct act;
    act.word = action;
    act.origin = origin;
    const QString serverName = current.name;
    QPointer<AgentDo> self(this);
    const QString answered = asked ? QStringLiteral("allowed") : QStringLiteral("not-asked");
    act.answered = [self, reply, action, torrents, serverName, asked, answered](
                       const core::ActionOutcome &outcome) {
        if (self.isNull())
            return;
        if (action == QLatin1String("pause") || action == QLatin1String("resume")) {
            const int count = outcome.target.toInt();
            if (!outcome.cause.isEmpty()) {
                reply(AgentService::failure(
                    kCommand,
                    QStringLiteral("failed"),
                    core::AnnounceMask::maskText(QStringLiteral("%1 didn't %2: %3")
                                                     .arg(serverName, action, outcome.cause))));
                return;
            }
            const QString many =
                countOf(count, QStringLiteral("torrent"), QStringLiteral("torrents"));
            const QString said =
                action == QLatin1String("pause")
                    ? (count == 0 ? QStringLiteral("Nothing was running on %1: nothing paused.")
                                        .arg(serverName)
                                  : QStringLiteral("Paused %1 on %2: omaseed holds them until "
                                                   "resume.")
                                        .arg(many, serverName))
                    : QStringLiteral("Resumed %1 on %2.").arg(many, serverName);
            auto fields = envelope(action);
            fields.append({QStringLiteral("asked"), asked});
            fields.append({QStringLiteral("answer"), answered});
            fields.append({QStringLiteral("count"), count});
            fields.append({QStringLiteral("done"), QJsonArray()});
            fields.append({QStringLiteral("failed"), QJsonArray()});
            fields.append({QStringLiteral("outcome"), said});
            reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
            return;
        }
        QJsonArray done;
        QJsonArray failed;
        for (const Answers::LogTorrent &torrent : torrents) {
            if (!outcome.failedHashes.contains(torrent.hash)) {
                done.append(QJsonObject{{QStringLiteral("hash"), torrent.hash},
                                        {QStringLiteral("name"), torrent.name}});
                continue;
            }
            const QString cause = outcome.cause.isEmpty()
                                      ? QStringLiteral("%1 refused it").arg(serverName)
                                      : outcome.cause;
            failed.append(QJsonObject{{QStringLiteral("hash"), torrent.hash},
                                      {QStringLiteral("name"), torrent.name},
                                      {QStringLiteral("code"), QStringLiteral("failed")},
                                      {QStringLiteral("message"), cause}});
        }
        QString said;
        if (action == QLatin1String("dismiss"))
            said = torrents.size() == 1
                       ? QStringLiteral("Failure dismissed · %1").arg(torrents.constFirst().name)
                       : QStringLiteral("%1 dismissed")
                             .arg(countOf(torrents.size(),
                                          QStringLiteral("failure"),
                                          QStringLiteral("failures")));
        else
            said = outcome.words().value(QStringLiteral("headline")).toString();
        auto fields = envelope(action);
        fields.append({QStringLiteral("asked"), asked});
        fields.append({QStringLiteral("answer"), answered});
        fields.append({QStringLiteral("done"), done});
        fields.append({QStringLiteral("failed"), failed});
        fields.append({QStringLiteral("outcome"), core::AnnounceMask::maskText(said)});
        reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
    };

    bool sent = false;
    if (action == QLatin1String("retry") || action == QLatin1String("start-here"))
        sent = m_app->retryTorrents(hashes, act); // start-here is the strip's start (C25 K24)
    else if (action == QLatin1String("recheck"))
        sent = m_app->recheck(hashes, act);
    else if (action == QLatin1String("reannounce"))
        sent = m_app->reannounce(hashes, act);
    else if (action == QLatin1String("dismiss"))
        sent = m_app->dismissFailures(hashes, act);
    else if (action == QLatin1String("pause"))
        sent = m_app->pauseTorrents(act);
    else if (action == QLatin1String("resume"))
        sent = m_app->resumeTorrents(act);
    if (!sent)
        fail(QStringLiteral("failed"),
             QStringLiteral("omaseed couldn't send %1 to %2.").arg(action, serverName));
}

QVariantMap AgentDo::askRequest(const QString &action,
                                const QStringList &hashes,
                                const QString &serverName,
                                const core::ProcParent::Caller &caller) const
{
    // docs/62 §5's ask: what, on which torrents (four, then "and N more"), who started the word.
    constexpr qsizetype kShown = 4;
    QVariantMap request;
    request.insert(QStringLiteral("action"), action);
    const bool serverWide = hashes.isEmpty();
    if (serverWide) {
        request.insert(
            QStringLiteral("head"),
            QStringLiteral("An agent asks to %1 everything on %2").arg(action, serverName));
        request.insert(QStringLiteral("server"),
                       QVariantMap{{QStringLiteral("name"), serverName},
                                   {QStringLiteral("line"),
                                    action == QLatin1String("pause")
                                        ? QStringLiteral("every running torrent, held until resume")
                                        : QStringLiteral("what omaseed holds paused there")}});
    } else {
        request.insert(
            QStringLiteral("head"),
            hashes.size() == 1
                ? QStringLiteral("An agent asks to %1 a torrent").arg(action)
                : QStringLiteral("An agent asks to %1 %2 torrents").arg(action).arg(hashes.size()));
        const core::TorrentModel *model = m_app->engine()->torrents();
        QVariantList torrents;
        for (const QString &hash : hashes) {
            if (torrents.size() == kShown)
                break;
            const int row = model->indexOfHash(hash);
            if (row < 0)
                continue;
            const core::Torrent &torrent = model->torrentAt(row);
            QStringList line;
            if (!torrent.savePath.isEmpty())
                line << QStringLiteral("in %1").arg(torrent.savePath);
            line << core::Format::size(torrent.size) << torrent.stateText().toLower();
            torrents.append(
                QVariantMap{{QStringLiteral("name"), torrent.name},
                            {QStringLiteral("line"), line.join(QStringLiteral(" · "))}});
        }
        request.insert(QStringLiteral("torrents"), torrents);
        request.insert(QStringLiteral("more"),
                       hashes.size() > kShown
                           ? QStringLiteral("and %1 more").arg(hashes.size() - kShown)
                           : QString());
    }
    addCaller(&request, caller);
    request.insert(QStringLiteral("allowText"), QStringLiteral("allow"));
    return request;
}

void AgentDo::addCaller(QVariantMap *request, const core::ProcParent::Caller &caller)
{
    // V17: the word as the person typed it when it is omaseed's, else the program's own name.
    const bool ours = caller.word.isEmpty() || caller.word == QLatin1String("omaseed");
    request->insert(QStringLiteral("word"), ours ? QStringLiteral("omaseed do") : caller.word);
    request->insert(QStringLiteral("wordPid"), caller.wordPid);
    request->insert(QStringLiteral("parent"), caller.parent);
    request->insert(QStringLiteral("parentPid"), caller.parentPid);
    request->insert(QStringLiteral("at"), QTime::currentTime().toString(QStringLiteral("HH:mm")));
}

void AgentDo::askFirst(const QString &json,
                       const core::ProcParent::Caller &caller,
                       const Reply &reply,
                       const QString &action,
                       const QString &subject,
                       const QVariantMap &request)
{
    // V17: the person answers in the `omaseed · agent` window. Refused or unanswered, the
    // answer says so and nothing runs; allowed, every check is made again (a setting turned
    // off meanwhile wins, a torrent gone is unknown) and it runs as asked.
    const core::AgentOrigin origin = originOf(caller, false);
    QPointer<AgentDo> self(this);
    const bool opened = m_app->agentAsk()->ask(
        request,
        [self, json, caller, reply, action, subject, origin](const QString &answer,
                                                             const QVariantList &) {
            if (answer == QLatin1String("allowed")) {
                if (!self.isNull()) {
                    self->proceed(json, caller, reply, true);
                    return;
                }
                reply(notAskedAnswer(action,
                                     QStringLiteral("no-answer"),
                                     QStringLiteral("omaseed went away before it could run it: "
                                                    "not done."),
                                     true));
                return;
            }
            const bool refused = answer == QLatin1String("refused");
            if (!self.isNull())
                self->m_app->noteAgentRefusal(action,
                                              refused ? QStringLiteral("refused in the window")
                                                      : QStringLiteral("no answer in the window"),
                                              origin,
                                              subject);
            // docs/62 V13's words.
            reply(refused ? notAskedAnswer(action,
                                           QStringLiteral("refused"),
                                           QStringLiteral("Refused in omaseed's window: not done."),
                                           true)
                          : notAskedAnswer(action,
                                           QStringLiteral("no-answer"),
                                           QStringLiteral("Nobody answered in omaseed's window for "
                                                          "2 minutes: not done."),
                                           true));
        });
    if (!opened) {
        // PLAN Q5: one ask at a time, no queue.
        m_app->noteAgentRefusal(
            action, QStringLiteral("not asked: another ask was open"), origin, subject);
        reply(notAskedAnswer(action,
                             QStringLiteral("no-answer"),
                             QStringLiteral("Another ask is open in omaseed's window, so this one "
                                            "wasn't asked: not done. Ask again once it is "
                                            "answered."),
                             false));
    }
}

// ---- moves (#293, C45 V14, V15, V17) ---------------------------------------------------------

void AgentDo::whenPlanned(const std::function<QVariantMap()> &make,
                          const std::function<void(const QVariantMap &)> &done)
{
    // The sheet's plan fills in as the file lists, the listings and the server's room come in
    // (planChanged); an agent's move is checked on the whole of it, as a start waits for it.
    const QVariantMap first = make();
    if (!first.value(QStringLiteral("pending")).toBool()) {
        done(first);
        return;
    }
    struct Wait
    {
        QMetaObject::Connection changed;
        bool over = false;
    };
    auto wait = std::make_shared<Wait>();
    const auto finish = [wait, make, done](bool timedOut) {
        if (wait->over)
            return;
        const QVariantMap words = make();
        if (!timedOut && words.value(QStringLiteral("pending")).toBool())
            return;
        wait->over = true;
        QObject::disconnect(wait->changed);
        done(words);
    };
    wait->changed =
        connect(m_app->move(), &MoveController::planChanged, this, [finish]() { finish(false); });
    QTimer::singleShot(kPlanWaitMs, this, [finish]() { finish(true); });
}

void AgentDo::proceedMove(const QString &json,
                          const core::ProcParent::Caller &caller,
                          const Reply &reply,
                          bool asked,
                          bool dryRun,
                          const QString &action,
                          const QStringList &hashes,
                          const QString &to,
                          const QString &moveQuery)
{
    MoveController *move = m_app->move();
    const QString serverName = m_service->currentServer().name;
    const bool back = action == QLatin1String("move-back");
    const auto fail = [reply](const QString &code, const QString &message) {
        reply(AgentService::failure(kCommand, code, message));
    };
    if (move == nullptr || move->job() == nullptr) {
        fail(QStringLiteral("failed"),
             QStringLiteral("omaseed can't move on %1 now: it has no move job there.")
                 .arg(serverName));
        return;
    }

    // move-back: MOVE is a landed move's id as `omaseed moves --json` gives it, or 8 or more of
    // its first characters (as a hash is). PLAN Q7: one it doesn't know is refused.
    QString id;
    if (back) {
        QStringList found;
        for (const core::MoveRecord &record : move->history().records()) {
            if (record.id == moveQuery) {
                found = {record.id};
                break;
            }
            if (moveQuery.size() >= 8 && record.id.startsWith(moveQuery, Qt::CaseInsensitive))
                found << record.id;
        }
        if (found.size() > 1) {
            fail(QStringLiteral("ambiguous"),
                 QStringLiteral("%1 starts %2 of %3's landed moves: give more of its id.")
                     .arg(moveQuery)
                     .arg(found.size())
                     .arg(serverName));
            return;
        }
        if (found.isEmpty()) {
            fail(QStringLiteral("refused"),
                 QStringLiteral("Not moved back: %1 keeps no landed move %2. omaseed moves --json "
                                "gives the ids of the last moves.")
                     .arg(serverName, moveQuery));
            return;
        }
        id = found.constFirst();
    }

    QPointer<AgentDo> self(this);
    const auto make = [self, back, id, hashes, to]() -> QVariantMap {
        if (self.isNull() || self->m_app->move() == nullptr)
            return {};
        MoveController *controller = self->m_app->move();
        return back ? controller->planBackOf(id)
                    : controller->planSelection(hashes, to, QStringLiteral("oneFolder"));
    };
    whenPlanned(
        make,
        [self, json, caller, reply, asked, dryRun, action, back, id, hashes, to, serverName, fail](
            const QVariantMap &words) {
            if (self.isNull())
                return;
            if (words.isEmpty()) {
                fail(QStringLiteral("refused"),
                     QStringLiteral("Not moved back: nothing of that move is left to move back."));
                return;
            }
            if (words.value(QStringLiteral("pending")).toBool()) {
                fail(
                    QStringLiteral("failed"),
                    QStringLiteral("omaseed couldn't read what the move needs from %1 in time: not "
                                   "moved.")
                        .arg(serverName));
                return;
            }
            // C15, C16: a check that blocks the sheet's move refuses it here, in the sheet's words.
            if (!words.value(QStringLiteral("canMove")).toBool()) {
                QString why = words.value(QStringLiteral("blocker")).toString();
                for (const QVariant &value : words.value(QStringLiteral("checks")).toList()) {
                    const QVariantMap line = value.toMap();
                    const QString kind = line.value(QStringLiteral("kind")).toString();
                    if (why.isEmpty() &&
                        (kind == QLatin1String("bad") || kind == QLatin1String("warn")))
                        why = line.value(QStringLiteral("text")).toString();
                }
                if (why.isEmpty())
                    why = QStringLiteral("nothing to move.");
                if (!dryRun)
                    self->m_app->noteAgentRefusal(action,
                                                  QStringLiteral("refused: %1").arg(why),
                                                  originOf(caller, asked),
                                                  words.value(QStringLiteral("title")).toString());
                fail(QStringLiteral("refused"),
                     core::AnnounceMask::maskText((back ? QStringLiteral("Not moved back: %1")
                                                        : QStringLiteral("Not moved: %1"))
                                                      .arg(why)));
                return;
            }

            // What it moves: the plan's torrents (some may be left out, the ask shows why).
            const QStringList moving = words.value(QStringLiteral("moveHashes")).toStringList();
            const core::TorrentModel *model = self->m_app->engine()->torrents();
            QJsonArray done;
            QStringList names;
            for (const QString &hash : moving) {
                const int row = model->indexOfHash(hash);
                const QString name = row >= 0 ? model->torrentAt(row).name : QString();
                names << name;
                done.append(
                    QJsonObject{{QStringLiteral("hash"), hash}, {QStringLiteral("name"), name}});
            }
            const QString many =
                countOf(moving.size(), QStringLiteral("torrent"), QStringLiteral("torrents"));
            const QString subject = moving.size() == 1 ? names.constFirst() : many;
            const QString where = back ? QStringLiteral("back, each to where it was")
                                       : QStringLiteral("to %1").arg(to);

            if (dryRun) {
                // GT5: every check made, nothing asked; moves always ask.
                auto fields = envelope(action);
                fields.append({QStringLiteral("dryRun"), true});
                fields.append({QStringLiteral("would"), QStringLiteral("ask")});
                if (!back)
                    fields.append({QStringLiteral("to"), to});
                fields.append({QStringLiteral("torrents"), done});
                fields.append({QStringLiteral("outcome"),
                               core::AnnounceMask::maskText(
                                   QStringLiteral("%1 would ask in omaseed's window · %2 %3")
                                       .arg(action, many, where))});
                reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
                return;
            }
            if (!asked) {
                // C45 V14, C50 GT3: a move always asks, whatever its line says (never allow).
                self->askFirst(json,
                               caller,
                               reply,
                               action,
                               subject,
                               self->moveRequest(action, words, to, id, caller));
                return;
            }

            // Allowed in the window, checked again just now: the job starts, or waits behind the
            // running one (L12, PLAN Q6).
            MoveController *controller = self->m_app->move();
            const core::MoveJob *job = controller->job();
            const qsizetype queuedBefore = job->queuedTitles().size();
            const QDateTime startedBefore = job->startedAt();
            controller->startNextBy(QStringLiteral("agent")); // C45 V18
            const bool started =
                back ? controller->startBackOf(id)
                     : controller->startSelection(hashes, to, QStringLiteral("oneFolder"));
            if (!started) {
                fail(QStringLiteral("failed"),
                     QStringLiteral("omaseed couldn't start the move on %1: not moved.")
                         .arg(serverName));
                return;
            }
            const qsizetype queued = job->queuedTitles().size();
            QJsonValue jobId; // null: queued, or still reading what it needs
            if (queued <= queuedBefore && job->isActive() && job->startedAt().isValid() &&
                job->startedAt() != startedBefore)
                jobId = Answers::runningMoveId(job->startedAt());

            App::AgentAct act;
            act.word = action;
            act.origin = originOf(caller, true);
            core::ActionOutcome outcome;
            outcome.action = back ? QStringLiteral("moveBack") : QStringLiteral("move");
            outcome.hashes = moving;
            self->m_app->noteAgentAct(act, subject, outcome);

            auto fields = envelope(action);
            fields.append({QStringLiteral("asked"), true});
            fields.append({QStringLiteral("answer"), QStringLiteral("allowed")});
            if (!back)
                fields.append({QStringLiteral("to"), to});
            fields.append({QStringLiteral("job"), jobId});
            if (queued > queuedBefore)
                fields.append({QStringLiteral("queued"), int(queued)});
            fields.append({QStringLiteral("done"), done});
            fields.append({QStringLiteral("failed"), QJsonArray()});
            const QString said =
                queued > queuedBefore
                    ? QStringLiteral(
                          "Queued behind the move of %1: %2 %3, omaseed moves follows it")
                          .arg(job->title(), many, where)
                    : QStringLiteral("Moving %1 %2: omaseed moves follows it").arg(many, where);
            fields.append({QStringLiteral("outcome"), core::AnnounceMask::maskText(said)});
            reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
        });
}

QVariantMap AgentDo::moveRequest(const QString &action,
                                 const QVariantMap &words,
                                 const QString &to,
                                 const QString &moveId,
                                 const core::ProcParent::Caller &caller) const
{
    // docs/62 §5's move ask: the torrents by name, then where to, how much and the room there
    // ("to /data/music-new · 48.0 GB · 512 GB free there"), and that the move's own checks run.
    constexpr qsizetype kShown = 4;
    const bool back = action == QLatin1String("move-back");
    const QStringList moving = words.value(QStringLiteral("moveHashes")).toStringList();
    const core::TorrentModel *model = m_app->engine()->torrents();
    const core::MoveRecord *record = back ? m_app->move()->history().find(moveId) : nullptr;
    QVariantMap request;
    request.insert(QStringLiteral("action"), action);
    const QString count = moving.size() == 1 ? QStringLiteral("a torrent")
                                             : QStringLiteral("%1 torrents").arg(moving.size());
    request.insert(QStringLiteral("head"),
                   back ? QStringLiteral("An agent asks to move %1 back").arg(count)
                        : QStringLiteral("An agent asks to move %1").arg(count));
    QVariantList torrents;
    for (const QString &hash : moving) {
        if (torrents.size() == kShown)
            break;
        const int row = model->indexOfHash(hash);
        if (row < 0)
            continue;
        // A move back sends each to its own place: the line says which.
        const QString line = record != nullptr && record->from.contains(hash)
                                 ? QStringLiteral("back to %1").arg(record->from.value(hash))
                                 : QString();
        torrents.append(QVariantMap{{QStringLiteral("name"), model->torrentAt(row).name},
                                    {QStringLiteral("line"), line}});
    }
    request.insert(QStringLiteral("torrents"), torrents);
    request.insert(QStringLiteral("more"),
                   moving.size() > kShown
                       ? QStringLiteral("and %1 more").arg(moving.size() - kShown)
                       : QString());
    // "to FOLDER · SIZE · ROOM": the size on disk the sheet's header counts.
    QStringList where{back ? QStringLiteral("each back to where it was")
                           : QStringLiteral("to %1").arg(to)};
    where << core::Format::size(words.value(QStringLiteral("bytes")).toLongLong());
    if (const QString room = words.value(QStringLiteral("roomText")).toString(); !room.isEmpty())
        where << room;
    request.insert(QStringLiteral("where"), where.join(QStringLiteral(" · ")));
    // The sheet's lines that say something (names there, left out, room it can't tell, a move
    // running first), in its words; its green "all fine" line is the room above.
    QStringList checks;
    for (const QVariant &value : words.value(QStringLiteral("checks")).toList()) {
        const QVariantMap line = value.toMap();
        // A plain fact ("Order: … goes alone") says nothing to allow or refuse on; one the
        // sheet always shows (a move running first) does.
        const QString kind = line.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("ok") ||
            (kind == QLatin1String("fact") && !line.value(QStringLiteral("always")).toBool()))
            continue;
        checks << line.value(QStringLiteral("text")).toString();
    }
    request.insert(QStringLiteral("checks"), checks);
    request.insert(QStringLiteral("note"),
                   QStringLiteral("The move's own checks still run: room on the drive, names "
                                  "already there. Moves always ask, whatever Settings says."));
    addCaller(&request, caller);
    request.insert(QStringLiteral("allowText"),
                   back ? QStringLiteral("allow the move back") : QStringLiteral("allow the move"));
    return request;
}

// ---- locate (#294, C50 GT2) ------------------------------------------------------------------

namespace {

/// What the torrent's own state says of its files: gone from where it is (qBittorrent's
/// missing files, Transmission's "No data found", a file that vanished), or a failed move
/// Deluge forced into Error (K24, which a recheck clears).
bool saysMissing(const core::Torrent &torrent, const core::TorrentProblem &problem)
{
    return torrent.status.phase == core::TorrentPhase::Missing || problem.missingFiles ||
           problem.causeKey == QLatin1String("missingFiles") ||
           problem.causeKey == QLatin1String("missing") || problem.failedMove;
}

/// "Rain on Tin Roofs/" for a torrent in a folder of its own, else its one file's name: what
/// the folder to point at holds.
QString heldName(const QList<core::ContentFile> &files)
{
    if (files.isEmpty())
        return QString();
    const QString first = files.constFirst().path;
    const qsizetype slash = first.indexOf(QLatin1Char('/'));
    return slash > 0 ? first.left(slash + 1) : first;
}

/// The find as a failed line says it (docs/67 §2): "found 0 of 12 files in /data/shelf-b",
/// "found 7 of 12 files; 5 missing, the first "Lecture 08.mkv"".
QString findWords(const core::LocateCheck::Result &find,
                  const QList<core::ContentFile> &files,
                  const QString &to)
{
    const QString of = QStringLiteral("found %1 of %2 %3")
                           .arg(find.found)
                           .arg(find.files)
                           .arg(find.files == 1 ? QStringLiteral("file") : QStringLiteral("files"));
    if (find.found == 0)
        return QStringLiteral("%1 in %2").arg(of, to);
    return QStringLiteral("%1; %2 missing, the first “%3”")
        .arg(of)
        .arg(find.files - find.found)
        .arg(core::LocateCheck::shownName(files, find.firstMissing));
}

} // namespace

void AgentDo::proceedLocate(const QString &json,
                            const core::ProcParent::Caller &caller,
                            const Reply &reply,
                            bool asked,
                            bool dryRun,
                            bool asks,
                            const QStringList &hashes,
                            const QString &to)
{
    const Answers::Server current = m_service->currentServer();
    const auto refuse = [reply](const QString &message) {
        reply(AgentService::failure(
            kCommand, QStringLiteral("refused"), core::AnnounceMask::maskText(message)));
    };
    const core::TorrentModel *model = m_app->engine()->torrents();
    const core::ProblemLog *problems = m_app->engine()->problems();
    for (const QString &hash : hashes) {
        const core::Torrent &torrent = model->torrent(hash);
        const core::TorrentProblem problem =
            problems != nullptr ? problems->problem(hash) : core::TorrentProblem();
        // research/294-locate (5): a Deluge torrent whose check hit a file error sits in
        // Checking for good after a recheck, re-pointed or not. Its failed move (K24) is not
        // that: a recheck clears it.
        if (current.engine == QLatin1String("deluge") &&
            torrent.status.phase == core::TorrentPhase::Errored && !problem.failedMove) {
            refuse(QStringLiteral("Not located: Deluge can't recheck %1 in this error; remove it "
                                  "and add it again from the window.")
                       .arg(torrent.name));
            return;
        }
        // PLAN Q10: its files sit in qBittorrent's incomplete folder until it completes.
        if (current.engine == QLatin1String("qbittorrent") && !torrent.downloadPath.isEmpty() &&
            !torrent.isComplete()) {
            refuse(QStringLiteral("Not located: %1 keeps its files in qBittorrent's incomplete "
                                  "folder until it completes; locate it from the window.")
                       .arg(torrent.name));
            return;
        }
    }
    // C14 E11: the find reads this machine's disk, so FOLDER must be one it sees (the
    // calibration, then the path map when there is one).
    const QString localTo = m_app->freeSpaceProbe().localPathFor(to);
    if (localTo.isEmpty()) {
        refuse(QStringLiteral("Not located: omaseed can't see %1 from this machine (%2's disks "
                              "aren't this machine's, or no path map in its settings covers it), "
                              "so it can't check the files are there. Locate it from the window.")
                   .arg(to, current.name));
        return;
    }

    // Every torrent's file list, as the server has it.
    struct Gather
    {
        qsizetype left = 0;
        QHash<QString, QList<core::ContentFile>> files;
        QString error;
    };
    auto gather = std::make_shared<Gather>();
    gather->left = hashes.size();
    QPointer<AgentDo> self(this);
    const QString serverName = current.name;
    for (const QString &hash : hashes) {
        m_app->client()->files(hash)->then(
            this,
            [self,
             gather,
             hash,
             json,
             caller,
             reply,
             asked,
             dryRun,
             asks,
             hashes,
             to,
             localTo,
             serverName](const core::Result<QList<core::ContentFile>> &result) {
                if (result.isError()) {
                    if (gather->error.isEmpty())
                        gather->error = result.error().toString();
                } else {
                    gather->files.insert(hash, result.value());
                }
                if (--gather->left > 0 || self.isNull())
                    return;
                if (!gather->error.isEmpty()) {
                    reply(AgentService::failure(
                        kCommand,
                        QStringLiteral("failed"),
                        core::AnnounceMask::maskText(
                            QStringLiteral("omaseed couldn't read the torrents' files from %1: %2")
                                .arg(serverName, gather->error))));
                    return;
                }
                self->locateFound(
                    json, caller, reply, asked, dryRun, asks, hashes, to, localTo, gather->files);
            });
    }
}

void AgentDo::locateFound(const QString &json,
                          const core::ProcParent::Caller &caller,
                          const Reply &reply,
                          bool asked,
                          bool dryRun,
                          bool asks,
                          const QStringList &hashes,
                          const QString &to,
                          const QString &localTo,
                          const QHash<QString, QList<core::ContentFile>> &files)
{
    namespace LocateCheck = core::LocateCheck;
    const Answers::Server current = m_service->currentServer();
    const auto refuse = [reply](const QString &message) {
        reply(AgentService::failure(
            kCommand, QStringLiteral("refused"), core::AnnounceMask::maskText(message)));
    };
    const core::TorrentModel *model = m_app->engine()->torrents();
    const core::ProblemLog *problems = m_app->engine()->problems();
    const core::FreeSpaceProbe &probe = m_app->freeSpaceProbe();

    struct Found
    {
        QString hash;
        QString name;
        LocateCheck::Result find;
        bool autoTmm = false;
    };
    QList<Found> found;
    QJsonArray short_;
    QString firstShort;
    for (const QString &hash : hashes) {
        if (model->indexOfHash(hash) < 0) {
            reply(AgentService::failure(
                kCommand,
                QStringLiteral("unknown-torrent"),
                QStringLiteral("%1 has no torrent %2 any more.").arg(current.name, hash)));
            return;
        }
        const core::Torrent &torrent = model->torrent(hash);
        const QList<core::ContentFile> list = files.value(hash);
        const LocateCheck::Result find = LocateCheck::check(list, localTo, LocateCheck::statLocal);
        if (find.files == 0) {
            refuse(
                QStringLiteral("Not located: %1 lists no file to look for yet.").arg(torrent.name));
            return;
        }
        // GT2: locate is for files that are missing; files all where the torrent is need no
        // locate (a recheck checks them; elsewhere, that's a move).
        const core::TorrentProblem problem =
            problems != nullptr ? problems->problem(hash) : core::TorrentProblem();
        if (!saysMissing(torrent, problem)) {
            const QString here = probe.localPathFor(torrent.savePath);
            if (here.isEmpty() || LocateCheck::check(list, here, LocateCheck::statLocal).all()) {
                const bool same = core::SharedData::normalize(torrent.savePath) ==
                                  core::SharedData::normalize(to);
                refuse(same ? QStringLiteral("Not located: %1's files aren't missing: it is in %2 "
                                             "already. omaseed do recheck checks them.")
                                  .arg(torrent.name, torrent.savePath)
                            : QStringLiteral("Not located: %1's files aren't missing: they are in "
                                             "%2. Putting them in %3 is a move: omaseed do move "
                                             "%4 --to %3.")
                                  .arg(torrent.name, torrent.savePath, to, hash.left(8)));
                return;
            }
        }
        if (!find.all()) {
            const QString words = findWords(find, list, to);
            if (firstShort.isEmpty()) {
                firstShort = find.found == 0
                                 ? QStringLiteral("%1.\nPoint at the folder that holds “%2”.")
                                       .arg(words, heldName(list))
                                 : QStringLiteral("%1. A recheck there would download them again.")
                                       .arg(words);
                if (hashes.size() > 1)
                    firstShort = QStringLiteral("%1: %2").arg(torrent.name, firstShort);
            }
            short_.append(QJsonObject{{QStringLiteral("hash"), hash},
                                      {QStringLiteral("name"), torrent.name},
                                      {QStringLiteral("code"), QStringLiteral("not-found")},
                                      {QStringLiteral("found"), find.found},
                                      {QStringLiteral("files"), find.files},
                                      {QStringLiteral("message"), words}});
            continue;
        }
        // qBittorrent's set location turns automatic management off (research/294-locate (3)).
        found.append({hash,
                      torrent.name,
                      find,
                      current.engine == QLatin1String("qbittorrent") && torrent.autoTmm});
    }

    // docs/67 §2: a short find is refused, not run: re-pointing and downloading the rest is
    // the person's call, from the window. found and files on every line.
    if (!short_.isEmpty()) {
        const QString message =
            hashes.size() == 1 ? QStringLiteral("Not located: ") + firstShort
                               : QStringLiteral("Not located: %1 of %2 torrents aren't all in %3. ")
                                         .arg(short_.size())
                                         .arg(hashes.size())
                                         .arg(to) +
                                     firstShort;
        auto fields = envelope(QStringLiteral("locate"));
        fields.append({QStringLiteral("to"), to});
        fields.append({QStringLiteral("done"), QJsonArray()});
        fields.append({QStringLiteral("failed"), short_});
        fields.append(
            {QStringLiteral("error"),
             QJsonObject{{QStringLiteral("code"), QStringLiteral("not-found")},
                         {QStringLiteral("message"), core::AnnounceMask::maskText(message)}}});
        reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
        return;
    }

    const qsizetype offCount =
        std::count_if(found.cbegin(), found.cend(), [](const Found &f) { return f.autoTmm; });
    const QString subject =
        found.size() == 1
            ? found.constFirst().name
            : countOf(found.size(), QStringLiteral("torrent"), QStringLiteral("torrents"));
    const auto line = [](const Found &f, bool rechecking) {
        QJsonObject out{{QStringLiteral("hash"), f.hash},
                        {QStringLiteral("name"), f.name},
                        {QStringLiteral("found"), f.find.found},
                        {QStringLiteral("files"), f.find.files},
                        {QStringLiteral("bytes"), f.find.bytes}};
        if (rechecking)
            out.insert(QStringLiteral("rechecking"), true);
        if (f.autoTmm)
            out.insert(QStringLiteral("managementOff"), true);
        return out;
    };

    if (dryRun) {
        // GT5: every check made, the find too; nothing asked, nothing done.
        QJsonArray torrents;
        for (const Found &f : std::as_const(found))
            torrents.append(line(f, false));
        auto fields = envelope(QStringLiteral("locate"));
        fields.append({QStringLiteral("dryRun"), true});
        fields.append(
            {QStringLiteral("would"), asks ? QStringLiteral("ask") : QStringLiteral("run")});
        fields.append({QStringLiteral("to"), to});
        fields.append({QStringLiteral("torrents"), torrents});
        const QString every =
            found.size() == 1
                ? QStringLiteral("found %1 of %1 files").arg(found.constFirst().find.files)
                : QStringLiteral("every file found");
        fields.append(
            {QStringLiteral("outcome"),
             core::AnnounceMask::maskText(
                 QStringLiteral("locate would %1 · %2 · %3 in %4")
                     .arg(asks ? QStringLiteral("ask in omaseed's window") : QStringLiteral("run"),
                          subject,
                          every,
                          to))});
        reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
        return;
    }

    if (asks && !asked) {
        // C45 V17: the ask shows the folder and the find, and what the re-point costs.
        constexpr qsizetype kShown = 4;
        QVariantMap request;
        request.insert(QStringLiteral("action"), QStringLiteral("locate"));
        request.insert(
            QStringLiteral("head"),
            found.size() == 1
                ? QStringLiteral("An agent asks to locate a torrent")
                : QStringLiteral("An agent asks to locate %1 torrents").arg(found.size()));
        QVariantList torrents;
        for (const Found &f : std::as_const(found)) {
            if (torrents.size() == kShown)
                break;
            const core::Torrent &torrent = model->torrent(f.hash);
            QStringList words{QStringLiteral("found %1 of %1 files").arg(f.find.files),
                              core::Format::size(f.find.bytes)};
            if (!torrent.savePath.isEmpty())
                words << QStringLiteral("was in %1").arg(torrent.savePath);
            torrents.append(
                QVariantMap{{QStringLiteral("name"), f.name},
                            {QStringLiteral("line"), words.join(QStringLiteral(" · "))}});
        }
        request.insert(QStringLiteral("torrents"), torrents);
        request.insert(QStringLiteral("more"),
                       found.size() > kShown
                           ? QStringLiteral("and %1 more").arg(found.size() - kShown)
                           : QString());
        request.insert(QStringLiteral("where"),
                       QStringLiteral("in %1 · nothing moves, then a recheck").arg(to));
        QStringList checks;
        if (offCount > 0)
            checks << (found.size() == 1
                           ? QStringLiteral("Automatic management goes off for it: qBittorrent's "
                                            "set location does that.")
                           : QStringLiteral("Automatic management goes off for %1 of them: "
                                            "qBittorrent's set location does that.")
                                 .arg(offCount));
        request.insert(QStringLiteral("checks"), checks);
        request.insert(QStringLiteral("note"),
                       QStringLiteral("omaseed points the torrent at this folder without moving a "
                                      "file, then rechecks it: the recheck says whether the files "
                                      "are whole."));
        addCaller(&request, caller);
        request.insert(QStringLiteral("allowText"), QStringLiteral("allow"));
        askFirst(json, caller, reply, QStringLiteral("locate"), subject, request);
        return;
    }

    // Allowed: re-point, recheck, and answer once the server has.
    App::AgentAct act;
    act.word = QStringLiteral("locate");
    act.origin = originOf(caller, asked);
    QPointer<AgentDo> self(this);
    const QString answered = asked ? QStringLiteral("allowed") : QStringLiteral("not-asked");
    const QString serverName = current.name;
    act.answered = [self, reply, found, line, to, asked, answered, serverName, subject, offCount](
                       const core::ActionOutcome &outcome) {
        if (self.isNull())
            return;
        QJsonArray done;
        QJsonArray failed;
        for (const Found &f : found) {
            if (!outcome.failedHashes.contains(f.hash)) {
                done.append(line(f, true));
                continue;
            }
            failed.append(QJsonObject{{QStringLiteral("hash"), f.hash},
                                      {QStringLiteral("name"), f.name},
                                      {QStringLiteral("code"), QStringLiteral("failed")},
                                      {QStringLiteral("message"),
                                       outcome.cause.isEmpty()
                                           ? QStringLiteral("%1 refused it").arg(serverName)
                                           : outcome.cause}});
        }
        QString said =
            failed.isEmpty()
                ? (found.size() == 1
                       ? QStringLiteral("Located · %1 · rechecking").arg(subject)
                       : QStringLiteral("Located %1 in %2 · rechecking").arg(subject, to))
                : QStringLiteral("Not located · %1: %2")
                      .arg(subject,
                           outcome.cause.isEmpty() ? QStringLiteral("%1 refused it").arg(serverName)
                                                   : outcome.cause);
        if (failed.isEmpty() && offCount > 0)
            said += found.size() == 1
                        ? QStringLiteral(". Automatic management went off for it.")
                        : QStringLiteral(". Automatic management went off for %1 of them.")
                              .arg(offCount);
        auto fields = envelope(QStringLiteral("locate"));
        fields.append({QStringLiteral("to"), to});
        fields.append({QStringLiteral("asked"), asked});
        fields.append({QStringLiteral("answer"), answered});
        fields.append({QStringLiteral("done"), done});
        fields.append({QStringLiteral("failed"), failed});
        fields.append({QStringLiteral("outcome"), core::AnnounceMask::maskText(said)});
        reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
    };
    QStringList sending;
    for (const Found &f : std::as_const(found))
        sending << f.hash;
    if (!m_app->locate(sending, to, act))
        reply(AgentService::failure(
            kCommand,
            QStringLiteral("failed"),
            QStringLiteral("omaseed couldn't send locate to %1.").arg(current.name)));
}

// ---- remove (#295, C50 GT1, GT6) -------------------------------------------------------------

namespace {

/// GT1: the engine's own word that a torrent's files are gone (its missing state, a missing
/// files cause). A failed move is not that: its files are somewhere (assumed, S10).
bool goneByState(const core::Torrent &torrent, const core::TorrentProblem &problem)
{
    return torrent.status.phase == core::TorrentPhase::Missing || problem.missingFiles ||
           problem.causeKey == QLatin1String("missingFiles") ||
           problem.causeKey == QLatin1String("missing");
}

/// docs/67 §2's second line under a not-removable.
const QString kOnlyGone =
    QStringLiteral("\nomaseed do removes only a torrent whose files are gone. Try recheck.");

/// The day a kept torrent goes (GT6), as the answer's `kept` says it.
QString keptDay()
{
    return QDate::currentDate().addDays(core::RemovedKeep::kDays).toString(Qt::ISODate);
}

} // namespace

void AgentDo::proceedRemove(const QString &json,
                            const core::ProcParent::Caller &caller,
                            const Reply &reply,
                            bool asked,
                            bool dryRun,
                            const QStringList &hashes)
{
    const Answers::Server current = m_service->currentServer();
    const core::TorrentModel *model = m_app->engine()->torrents();
    const core::ProblemLog *problems = m_app->engine()->problems();
    for (const QString &hash : hashes) {
        const core::Torrent &torrent = model->torrent(hash);
        const core::TorrentProblem problem =
            problems != nullptr ? problems->problem(hash) : core::TorrentProblem();
        if (!goneByState(torrent, problem)) {
            reply(AgentService::failure(
                kCommand,
                QStringLiteral("not-removable"),
                core::AnnounceMask::maskText(
                    QStringLiteral("Not removed: %1 doesn't say %2's files are gone (it is %3).")
                        .arg(current.name, torrent.name, torrent.stateText().toLower()) +
                    kOnlyGone)));
            return;
        }
    }

    // Every torrent's file list, as the server has it, to look for them on the disk.
    struct Gather
    {
        qsizetype left = 0;
        QHash<QString, QList<core::ContentFile>> files;
        QString error;
    };
    auto gather = std::make_shared<Gather>();
    gather->left = hashes.size();
    QPointer<AgentDo> self(this);
    const QString serverName = current.name;
    for (const QString &hash : hashes) {
        m_app->client()->files(hash)->then(
            this,
            [self, gather, hash, json, caller, reply, asked, dryRun, hashes, serverName](
                const core::Result<QList<core::ContentFile>> &result) {
                if (result.isError()) {
                    if (gather->error.isEmpty())
                        gather->error = result.error().toString();
                } else {
                    gather->files.insert(hash, result.value());
                }
                if (--gather->left > 0 || self.isNull())
                    return;
                if (!gather->error.isEmpty()) {
                    reply(AgentService::failure(
                        kCommand,
                        QStringLiteral("failed"),
                        core::AnnounceMask::maskText(
                            QStringLiteral("omaseed couldn't read the torrents' files from %1: %2")
                                .arg(serverName, gather->error))));
                    return;
                }
                self->removeChecked(json, caller, reply, asked, dryRun, hashes, gather->files);
            });
    }
}

void AgentDo::removeChecked(const QString &json,
                            const core::ProcParent::Caller &caller,
                            const Reply &reply,
                            bool asked,
                            bool dryRun,
                            const QStringList &hashes,
                            const QHash<QString, QList<core::ContentFile>> &files)
{
    const Answers::Server current = m_service->currentServer();
    const core::TorrentModel *model = m_app->engine()->torrents();
    const core::FreeSpaceProbe &probe = m_app->freeSpaceProbe();

    struct Look
    {
        QString hash;
        QString name;
        bool seen = false; ///< omaseed looked on the disk (else the server's word alone)
    };
    QList<Look> looks;
    for (const QString &hash : hashes) {
        if (model->indexOfHash(hash) < 0) {
            reply(AgentService::failure(
                kCommand,
                QStringLiteral("unknown-torrent"),
                QStringLiteral("%1 has no torrent %2 any more.").arg(current.name, hash)));
            return;
        }
        const core::Torrent &torrent = model->torrent(hash);
        const QList<core::ContentFile> list = files.value(hash);
        // GT1: none of its files at its save path (nor in qBittorrent's incomplete folder,
        // while it isn't complete), wherever this machine sees them (C14 E11).
        QStringList folders{torrent.savePath};
        if (!torrent.downloadPath.isEmpty() && !torrent.isComplete())
            folders << torrent.downloadPath;
        Look look{hash, torrent.name, false};
        for (const QString &folder : std::as_const(folders)) {
            const QString local = probe.localPathFor(folder);
            if (local.isEmpty())
                continue;
            look.seen = true;
            const core::LocateCheck::Result there =
                core::LocateCheck::present(list, local, core::LocateCheck::statLocal);
            if (there.found > 0) {
                reply(AgentService::failure(
                    kCommand,
                    QStringLiteral("not-removable"),
                    core::AnnounceMask::maskText(
                        QStringLiteral("Not removed: %1 has its files (%2 of %3 at %4).")
                            .arg(torrent.name)
                            .arg(there.found)
                            .arg(there.files)
                            .arg(folder) +
                        kOnlyGone)));
                return;
            }
        }
        looks.append(look);
    }

    const qsizetype unseen =
        std::count_if(looks.cbegin(), looks.cend(), [](const Look &l) { return !l.seen; });
    const QString subject =
        looks.size() == 1
            ? looks.constFirst().name
            : countOf(looks.size(), QStringLiteral("torrent"), QStringLiteral("torrents"));
    // The ask's and the dry run's word on how omaseed knows (GT1).
    const QString how =
        unseen == 0
            ? (looks.size() == 1 ? QStringLiteral("none of its files is on disk (checked)")
                                 : QStringLiteral("none of their files is on disk (checked)"))
        : unseen == looks.size()
            ? QStringLiteral("omaseed can't see %1's disks: the server's word alone")
                  .arg(current.name)
            : QStringLiteral("none on disk where omaseed sees it; the server's word alone for "
                             "%1")
                  .arg(unseen);

    if (dryRun) {
        // GT5: every check made, the disk's too; nothing asked, kept or removed. A remove
        // always asks (GT1).
        QJsonArray torrents;
        for (const Look &l : std::as_const(looks))
            torrents.append(QJsonObject{{QStringLiteral("hash"), l.hash},
                                        {QStringLiteral("name"), l.name},
                                        {QStringLiteral("checked"), l.seen}});
        auto fields = envelope(QStringLiteral("remove"));
        fields.append({QStringLiteral("dryRun"), true});
        fields.append({QStringLiteral("would"), QStringLiteral("ask")});
        fields.append({QStringLiteral("torrents"), torrents});
        fields.append({QStringLiteral("outcome"),
                       core::AnnounceMask::maskText(
                           QStringLiteral("remove would ask in omaseed's window · %1 · %2")
                               .arg(subject, how))});
        reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
        return;
    }

    if (!asked) {
        // C45 V17, C50 GT1: a remove always asks, whatever the line says.
        constexpr qsizetype kShown = 4;
        QVariantMap request;
        request.insert(QStringLiteral("action"), QStringLiteral("remove"));
        request.insert(
            QStringLiteral("head"),
            looks.size() == 1
                ? QStringLiteral("An agent asks to remove a torrent")
                : QStringLiteral("An agent asks to remove %1 torrents").arg(looks.size()));
        QVariantList torrents;
        for (const Look &l : std::as_const(looks)) {
            if (torrents.size() == kShown)
                break;
            const core::Torrent &torrent = model->torrent(l.hash);
            QStringList line;
            if (!torrent.savePath.isEmpty())
                line << QStringLiteral("was in %1").arg(torrent.savePath);
            line << core::Format::size(torrent.size)
                 << (l.seen ? QStringLiteral("files gone (checked)")
                            : QStringLiteral("files gone, the server says"));
            torrents.append(
                QVariantMap{{QStringLiteral("name"), l.name},
                            {QStringLiteral("line"), line.join(QStringLiteral(" · "))}});
        }
        request.insert(QStringLiteral("torrents"), torrents);
        request.insert(QStringLiteral("more"),
                       looks.size() > kShown
                           ? QStringLiteral("and %1 more").arg(looks.size() - kShown)
                           : QString());
        request.insert(
            QStringLiteral("where"),
            QStringLiteral("%1 · %2 30 days to add back")
                .arg(how,
                     looks.size() == 1 ? QStringLiteral("kept") : QStringLiteral("each kept")));
        request.insert(QStringLiteral("note"),
                       QStringLiteral("omaseed takes the torrent off %1's list, never a file. Add "
                                      "it back, paused where it was, from the outcome strip or "
                                      "Settings › General › Agents.")
                           .arg(current.name));
        addCaller(&request, caller);
        request.insert(QStringLiteral("allowText"), QStringLiteral("allow the remove"));
        askFirst(json, caller, reply, QStringLiteral("remove"), subject, request);
        return;
    }

    QStringList sending;
    for (const Look &l : std::as_const(looks))
        sending << l.hash;
    keepThenRemove(caller, reply, asked, sending);
}

/// One `do remove` on its way: what is left to keep, and what was kept (GT6).
struct AgentDo::KeepRun
{
    core::ProcParent::Caller caller;
    Reply reply;
    bool asked = false;
    QStringList hashes;
    QString serverName;
    qsizetype next = 0;
    QHash<QString, QString> kept; ///< hash → RemovedKeep id
};

void AgentDo::keepThenRemove(const core::ProcParent::Caller &caller,
                             const Reply &reply,
                             bool asked,
                             const QStringList &hashes)
{
    auto run = std::make_shared<KeepRun>();
    run->caller = caller;
    run->reply = reply;
    run->asked = asked;
    run->hashes = hashes;
    run->serverName = m_service->currentServer().name;
    keepNext(run);
}

void AgentDo::keepNext(const std::shared_ptr<KeepRun> &run)
{
    // GT6: the way back first, one torrent at a time (each its own export); then the remove.
    if (run->next < run->hashes.size()) {
        const QString hash = run->hashes.at(run->next++);
        QPointer<AgentDo> self(this);
        m_app->keepRemoved(hash, [self, run, hash](const QString &id, const QString &error) {
            if (id.isEmpty()) {
                // Q11: no way back, no remove; what was kept for it goes.
                for (const QString &kept : std::as_const(run->kept))
                    core::RemovedKeep::forget(kept);
                if (!self.isNull())
                    self->m_app->refreshRemovedKept();
                run->reply(AgentService::failure(
                    kCommand,
                    QStringLiteral("not-removable"),
                    core::AnnounceMask::maskText(
                        QStringLiteral("Not removed: %1%2 omaseed keeps a way back before "
                                       "every remove.")
                            .arg(error,
                                 error.endsWith(QLatin1Char('.')) ? QString()
                                                                  : QStringLiteral(".")))));
                return;
            }
            run->kept.insert(hash, id);
            if (self.isNull()) {
                core::RemovedKeep::forget(id);
                return;
            }
            // Out of the keep's own callback before the next export starts.
            QTimer::singleShot(0, self, [self, run]() {
                if (!self.isNull())
                    self->keepNext(run);
            });
        });
        return;
    }

    App::AgentAct act;
    act.word = QStringLiteral("remove");
    act.origin = originOf(run->caller, run->asked);
    act.kept = run->kept;
    const QString day = keptDay();
    QHash<QString, QString> names;
    const core::TorrentModel *model = m_app->engine()->torrents();
    for (const QString &hash : std::as_const(run->hashes))
        names.insert(hash, model->torrent(hash).name);
    const QString answered = run->asked ? QStringLiteral("allowed") : QStringLiteral("not-asked");
    QPointer<AgentDo> self(this);
    act.answered = [self, run, names, answered, day](const core::ActionOutcome &outcome) {
        if (self.isNull())
            return;
        const QString &serverName = run->serverName;
        const QString refusedBy = outcome.cause.isEmpty()
                                      ? QStringLiteral("%1 refused it").arg(serverName)
                                      : outcome.cause;
        QJsonArray done;
        QJsonArray failed;
        for (const QString &hash : std::as_const(run->hashes)) {
            if (!outcome.failedHashes.contains(hash)) {
                done.append(QJsonObject{{QStringLiteral("hash"), hash},
                                        {QStringLiteral("name"), names.value(hash)},
                                        {QStringLiteral("kept"), day}});
                continue;
            }
            failed.append(QJsonObject{{QStringLiteral("hash"), hash},
                                      {QStringLiteral("name"), names.value(hash)},
                                      {QStringLiteral("code"), QStringLiteral("failed")},
                                      {QStringLiteral("message"), refusedBy}});
        }
        // docs/67 §2: "Removed from attic · Night Market Field Recordings · its files were
        // already gone".
        QString said;
        if (done.isEmpty())
            said = QStringLiteral("Not removed: %1").arg(refusedBy);
        else if (done.size() == 1)
            said = QStringLiteral("Removed from %1 · %2 · its files were already gone. Kept until "
                                  "%3 to add back in omaseed's window.")
                       .arg(serverName,
                            done.first().toObject().value(QLatin1String("name")).toString(),
                            day);
        else
            said = QStringLiteral("Removed %1 torrents from %2 · their files were already gone. "
                                  "Each kept until %3 to add back in omaseed's window.")
                       .arg(done.size())
                       .arg(serverName, day);
        auto fields = envelope(QStringLiteral("remove"));
        fields.append({QStringLiteral("asked"), run->asked});
        fields.append({QStringLiteral("answer"), answered});
        fields.append({QStringLiteral("done"), done});
        fields.append({QStringLiteral("failed"), failed});
        fields.append({QStringLiteral("outcome"), core::AnnounceMask::maskText(said)});
        run->reply(QString::fromUtf8(AgentWords::orderedJson(fields)));
    };
    if (!m_app->removeForAgent(run->hashes, act)) {
        for (const QString &kept : std::as_const(run->kept))
            core::RemovedKeep::forget(kept);
        m_app->refreshRemovedKept();
        run->reply(AgentService::failure(
            kCommand,
            QStringLiteral("failed"),
            QStringLiteral("omaseed couldn't send remove to %1.").arg(run->serverName)));
    }
}

} // namespace omaseed::app
