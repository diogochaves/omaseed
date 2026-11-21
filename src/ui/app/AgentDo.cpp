#include "ui/app/AgentDo.hpp"

#include "core/ActionOutcome.hpp"
#include "core/AgentAnswers.hpp"
#include "core/AgentGate.hpp"
#include "core/AgentWords.hpp"
#include "core/AnnounceMask.hpp"
#include "core/Format.hpp"
#include "core/PauseHold.hpp"
#include "core/ProblemLog.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentProblem.hpp"
#include "core/Version.hpp"
#include "ui/app/AgentService.hpp"
#include "ui/app/App.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTime>

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
    // The journal names what started the word (V17), else the word's own pid.
    core::AgentOrigin origin;
    origin.caller = caller.parent;
    origin.pid = caller.parentPid != 0 ? caller.parentPid : caller.wordPid;
    origin.asked = asked;
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
                                   QStringLiteral("resume")};
    if (!built.contains(action)) {
        // move, move-back (S6), locate (S8), remove (S10).
        fail(QStringLiteral("usage"), AgentWords::notYet(QStringLiteral("do ") + action));
        return;
    }
    const bool serverWide = action == QLatin1String("pause") || action == QLatin1String("resume");
    if (!options.to.isEmpty() || !options.move.isEmpty() ||
        serverWide != options.torrents.isEmpty()) {
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
        // V17: the person answers in the `omaseed · agent` window. Refused or unanswered, the
        // answer says so and nothing runs; allowed, every check is made again (a setting turned
        // off meanwhile wins, a torrent gone is unknown) and it runs as asked.
        const QString subject = serverWide ? current.name : subjectOf(torrents);
        const auto notAsked = [action](const QString &code, const QString &message, bool shown) {
            auto fields = envelope(action);
            fields.append({QStringLiteral("asked"), shown});
            fields.append({QStringLiteral("answer"),
                           code == QLatin1String("refused") ? QStringLiteral("refused")
                                                            : QStringLiteral("no-answer")});
            fields.append({QStringLiteral("error"),
                           QJsonObject{{QStringLiteral("code"), code},
                                       {QStringLiteral("message"), message}}});
            return QString::fromUtf8(AgentWords::orderedJson(fields));
        };
        QPointer<AgentDo> self(this);
        const bool opened = m_app->agentAsk()->ask(
            askRequest(action, hashes, current.name, caller),
            [self, json, caller, reply, action, subject, origin, notAsked](const QString &answer,
                                                                           const QVariantList &) {
                if (answer == QLatin1String("allowed")) {
                    if (!self.isNull()) {
                        self->proceed(json, caller, reply, true);
                        return;
                    }
                    reply(notAsked(QStringLiteral("no-answer"),
                                   QStringLiteral("omaseed went away before it could run it: "
                                                  "not done."),
                                   true));
                    return;
                }
                const bool refused = answer == QLatin1String("refused");
                if (!self.isNull())
                    self->m_app->noteAgentRefusal(action,
                                                  refused
                                                      ? QStringLiteral("refused in the window")
                                                      : QStringLiteral("no answer in the window"),
                                                  origin,
                                                  subject);
                // docs/62 V13's words.
                reply(refused ? notAsked(QStringLiteral("refused"),
                                         QStringLiteral("Refused in omaseed's window: not done."),
                                         true)
                              : notAsked(QStringLiteral("no-answer"),
                                         QStringLiteral("Nobody answered in omaseed's window for "
                                                        "2 minutes: not done."),
                                         true));
            });
        if (!opened) {
            // PLAN Q5: one ask at a time, no queue.
            noteRefused(QStringLiteral("not asked: another ask was open"), subject);
            reply(notAsked(QStringLiteral("no-answer"),
                           QStringLiteral("Another ask is open in omaseed's window, so this one "
                                          "wasn't asked: not done. Ask again once it is answered."),
                           false));
        }
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
    // V17: the word as the person typed it when it is omaseed's, else the program's own name.
    const bool ours = caller.word.isEmpty() || caller.word == QLatin1String("omaseed");
    request.insert(QStringLiteral("word"), ours ? QStringLiteral("omaseed do") : caller.word);
    request.insert(QStringLiteral("wordPid"), caller.wordPid);
    request.insert(QStringLiteral("parent"), caller.parent);
    request.insert(QStringLiteral("parentPid"), caller.parentPid);
    request.insert(QStringLiteral("at"), QTime::currentTime().toString(QStringLiteral("HH:mm")));
    request.insert(QStringLiteral("allowText"), QStringLiteral("allow"));
    return request;
}

} // namespace omaseed::app
