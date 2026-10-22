#include "integration/QuitSignals.hpp"

#include <QSocketNotifier>
#include <QString>

#include <array>
#include <cerrno>
#include <csignal>

#include <sys/socket.h>
#include <unistd.h>

namespace omaseed::integration {

namespace {

constexpr std::array<int, 3> kSignals{SIGTERM, SIGINT, SIGHUP};

/// [0] is written by the handler, [1] read by the notifier; -1 while no QuitSignals is alive.
int s_pair[2] = {-1, -1};

void restoreDefaults()
{
    for (const int signal : kSignals)
        std::signal(signal, SIG_DFL);
}

extern "C" void onSignal(int signal)
{
    // Only async-signal-safe calls here: one byte down the pair, errno kept.
    const int saved = errno;
    const char byte = static_cast<char>(signal);
    if (s_pair[0] >= 0) {
        [[maybe_unused]] const ssize_t written = ::write(s_pair[0], &byte, 1);
    }
    errno = saved;
}

} // namespace

QuitSignals::QuitSignals(QObject *parent)
    : QObject(parent)
{
    if (s_pair[0] >= 0)
        return;
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, s_pair) != 0) {
        s_pair[0] = s_pair[1] = -1;
        return;
    }
    m_notifier = new QSocketNotifier(s_pair[1], QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &QuitSignals::readSignal);

    struct sigaction action = {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (const int signal : kSignals)
        ::sigaction(signal, &action, nullptr);
}

QuitSignals::~QuitSignals()
{
    if (m_notifier == nullptr)
        return;
    restoreDefaults();
    ::close(s_pair[0]);
    ::close(s_pair[1]);
    s_pair[0] = s_pair[1] = -1;
}

void QuitSignals::readSignal()
{
    char byte = 0;
    if (::read(s_pair[1], &byte, 1) != 1)
        return;
    // A second signal while the quit runs ends the process at once.
    restoreDefaults();
    Q_EMIT received(static_cast<unsigned char>(byte));
}

QString QuitSignals::nameOf(int signal)
{
    switch (signal) {
    case SIGTERM:
        return QStringLiteral("SIGTERM");
    case SIGINT:
        return QStringLiteral("SIGINT");
    case SIGHUP:
        return QStringLiteral("SIGHUP");
    default:
        return QString::number(signal);
    }
}

} // namespace omaseed::integration
