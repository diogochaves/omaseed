#include "ui/Words.hpp"

#include "core/AgentText.hpp"
#include "core/AgentWords.hpp"
#include "core/Version.hpp"
#include "integration/AgentClient.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <cstdio>

#include <sys/ioctl.h>
#include <unistd.h>

namespace omaseed::ui {

namespace {

namespace AgentWords = core::AgentWords;
namespace AgentText = core::AgentText;

/// C45 V12: colour only on a terminal and without NO_COLOR; the terminal's width, else 80.
AgentText::TextStyle styleFor(int fd)
{
    AgentText::TextStyle style;
    const bool tty = ::isatty(fd) == 1;
    style.colour = tty && qEnvironmentVariableIsEmpty("NO_COLOR");
    winsize size{};
    if (tty && ::ioctl(fd, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
        style.width = size.ws_col;
    return style;
}

/// A failure's words for stderr: wrapped on a terminal, one line as they are when piped (an
/// agent reads them whole).
QByteArray failureWords(const QString &message)
{
    if (::isatty(STDERR_FILENO) != 1)
        return message.toUtf8();
    return AgentText::failure(message, styleFor(STDERR_FILENO)).toUtf8();
}

void write(std::FILE *stream, const QByteArray &bytes)
{
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stream);
    if (!bytes.endsWith('\n'))
        std::fputc('\n', stream);
    std::fflush(stream);
}

/// A failure the CLI made (C45 V4): one JSON object on stdout with --json, else its words on
/// stderr. The exit is V13's.
int fail(const QString &command, bool json, const QString &code, const QString &message)
{
    if (json)
        write(stdout, AgentWords::failure(command, code, message, QDateTime::currentDateTimeUtc()));
    else
        write(stderr, failureWords(message));
    return AgentWords::exitFor(code);
}

/// An answer the app made: its bytes with --json, else its lines. A failure the app made
/// (an "error" object) exits with its code's exit and, without --json, says it on stderr.
int answer(const QString &command, bool json, const QByteArray &bytes)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return fail(command,
                    json,
                    QStringLiteral("too-old"),
                    integration::AgentClient::tooOldWords(QString(), core::version()));
    const QJsonObject object = document.object();
    const QJsonObject failure = object.value(QStringLiteral("error")).toObject();
    if (!failure.isEmpty()) {
        const QString code = failure.value(QStringLiteral("code")).toString();
        if (json)
            write(stdout, bytes);
        else
            write(stderr, failureWords(failure.value(QStringLiteral("message")).toString()));
        return AgentWords::exitFor(code.isEmpty() ? QStringLiteral("failed") : code);
    }
    if (json) {
        write(stdout, bytes);
        return 0;
    }
    // C45 V12: the lines are made from the same JSON --json prints. A word whose lines aren't
    // in this omaseed yet (#163 steps C, D) prints its JSON.
    const QString lines = AgentText::lines(object, styleFor(STDOUT_FILENO));
    write(stdout, lines.isEmpty() ? bytes : lines.toUtf8());
    return 0;
}

} // namespace

int runWord(int argc, char *argv[], const std::function<QString()> &qtHelp)
{
    // No display: a word works over SSH as long as the session bus is reachable (C45 V3).
    QCoreApplication app(argc, argv);
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);

    const AgentWords::Request request = AgentWords::parse(arguments);
    switch (request.kind) {
    case AgentWords::Request::Kind::Page:
        write(stdout,
              (request.page.isEmpty() ? AgentWords::helpPage(qtHelp())
                                      : AgentWords::wordPage(request.page))
                  .toUtf8());
        return 0;
    case AgentWords::Request::Kind::Usage:
        return fail(request.word, request.json, QStringLiteral("usage"), request.message);
    case AgentWords::Request::Kind::Call:
        break;
    }

    const integration::AgentClient::Reply reply =
        integration::AgentClient::call(AgentWords::method(request.word), request.options);
    if (!reply.code.isEmpty())
        return fail(request.word, request.json, reply.code, reply.message);
    return answer(request.word, request.json, reply.answer);
}

} // namespace omaseed::ui
