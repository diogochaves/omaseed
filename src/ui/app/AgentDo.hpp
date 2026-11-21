#pragma once

// AgentDo — the app's side of `omaseed do` (DECISIONS C45 V14, V15; C50 GT3, GT5, GT8): the
// options read, every check made before anything runs, then the act through App as the window
// runs it, answered once the server has.
//
// The order (each a whole-call failure, nothing run):
// 1. the options (usage); `list` answers the gate's lines as they stand, whatever it says;
// 2. a demo acts on nothing (not-allowed; PLAN Q15);
// 3. the gate (AgentGate::decide): never, not-allowed (reads off, or the kind's line off);
// 4. an action not in this build yet (move, move-back, locate, remove: usage);
// 5. the server, as the reads check it: --server, not-current, first-sync, unreachable;
// 6. every torrent matched (unknown-torrent, ambiguous) and checked for its action
//    (start-here, dismiss, resume: refused, with why);
// 7. --dry-run answers what would happen; ask answers no-answer until the ask window exists
//    (S5); allow runs it.
//
// The fixes here: retry, start-here (a start, for a torrent the engine stopped after a failed
// move; on an engine that pauses on error, recheck first), recheck, reannounce, dismiss, and
// pause and resume (C36 M10's hold, the whole server).

#include <QObject>
#include <QString>

#include <functional>

namespace omaseed::app {

class App;
class AgentService;

class AgentDo : public QObject
{
    Q_OBJECT

public:
    using Reply = std::function<void(const QString &json)>;

    AgentDo(App *app, AgentService *service);

    /// `options` as the Agent interface's Do got them; `reply` is called exactly once.
    void run(const QString &options, uint callerPid, const Reply &reply);

private:
    App *m_app;
    AgentService *m_service;
};

} // namespace omaseed::app
