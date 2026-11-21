#pragma once

// AgentAsk — the one ask an agent's `do` may have open in omaseed's window (DECISIONS C45 V17;
// docs/62 §5, board 67 §4). AgentDo opens it with what to show; the `omaseed · agent` window
// (Views/AgentAskWindow.qml) draws `request` while `open` is true and answers with allow() or
// refuse(). Nothing else allows: no key does (the window's own rule), and esc, the window's
// close and two minutes without an answer refuse or give up.
//
// One ask at a time (PLAN Q5): a second while one is open is turned away at once (`ask`
// returns false) and its caller answers no-answer; there is no queue.
//
// `request` is a map the window reads as is:
//   head       "An agent asks to recheck a torrent"
//   action     the word ("recheck")
//   torrents   [{name, line}] up to four, `line` "in FOLDER · SIZE · state"
//   more       "and 3 more", or ""
//   server     for a server-wide act (pause, resume): {name, line}, else absent
//   word       the calling program as the ask says it ("omaseed do", or its comm)
//   wordPid    its pid (shown only when there is no parent to name)
//   parent     what started it ("claude"), "" when /proc couldn't say
//   parentPid  its pid
//   at         when it asked, "15:19"
//   allowText  the allow button's words ("allow")
// S6, S8, S10 and S11 add keys of their own (a move's folder, a plan's lines and ticks).

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace omaseed::app {

class AgentAsk : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(QVariantMap request READ request NOTIFY openChanged)
    /// How long an ask waits, in ms: two minutes (V17). OMASEED_ASK_TIMEOUT_MS shortens it, for
    /// the tests and the walks only.
    Q_PROPERTY(int timeoutMs READ timeoutMs CONSTANT)

public:
    /// What came of it: "allowed", "refused" or "no-answer"; `ticks` are the lines the person
    /// left ticked (a plan's, S11), empty otherwise.
    using Answer = std::function<void(const QString &answer, const QVariantList &ticks)>;

    /// An ask still open when omaseed quits is never answered: the word's call fails as the
    /// app leaves the bus, and nothing runs.
    explicit AgentAsk(QObject *parent = nullptr);

    bool isOpen() const { return static_cast<bool>(m_answer); }
    QVariantMap request() const { return m_request; }
    int timeoutMs() const { return m_timer.interval(); }
    /// The tests' own wait; the next ask uses it.
    void setTimeoutMs(int ms) { m_timer.setInterval(ms); }

    /// Open the ask. False, with `answer` never called, when one is already open.
    bool ask(const QVariantMap &request, Answer answer);

    /// The window's allow button; `ticks` as `Answer` takes them.
    Q_INVOKABLE void allow(const QVariantList &ticks = {});
    /// The window's refuse button, its esc and its close.
    Q_INVOKABLE void refuse();

Q_SIGNALS:
    void openChanged();

private:
    void finish(const QString &answer, const QVariantList &ticks);

    QVariantMap m_request;
    Answer m_answer;
    QTimer m_timer;
};

} // namespace omaseed::app
