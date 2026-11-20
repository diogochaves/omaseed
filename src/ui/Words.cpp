#include "ui/Words.hpp"

#include "core/AgentText.hpp"
#include "core/AgentWords.hpp"
#include "core/Version.hpp"
#include "integration/AgentClient.hpp"
#include "integration/SkinReader.hpp"
#include "integration/SkinWords.hpp"
#include "integration/ThemeReader.hpp"
#include "ui/Check.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
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
namespace SkinWords = integration::SkinWords;

/// The person's skins folder, as the app reads it (C37 U1).
QString skinsDir()
{
    return integration::ThemeReader::Paths::fromEnvironment().skinsDir;
}

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
    QString lines = AgentText::lines(object, styleFor(STDOUT_FILENO));
    if (lines.isEmpty())
        lines = SkinWords::lines(object, QDir::homePath());
    write(stdout, lines.isEmpty() ? bytes : lines.toUtf8());
    return 0;
}

/// `check <skin>` (C37 U5): the skin named is checked first (bad-name, unknown-skin: exit 2);
/// one that doesn't read is still checked, drawn as Omarchy's, and exits 1 with its why.
/// runCheck makes its own application, so this runs before any other exists.
int check(int argc, char *argv[], const AgentWords::Request &request)
{
    const QString command = request.command();
    const QString name = request.positionals.first();
    const SkinWords::Pick picked = SkinWords::pick(skinsDir(), name);
    if (!picked.code.isEmpty() && picked.code != QLatin1String("unreadable"))
        return fail(command, request.json, picked.code, picked.message);
    const CheckResult result =
        runCheck(argc, argv, name, request.options.value(QStringLiteral("theme")).toString());
    if (!result.code.isEmpty())
        return fail(command, request.json, result.code, result.message);
    if (const int code = answer(command, request.json, result.json); code != 0)
        return code;
    return result.exit;
}

/// The words the CLI answers itself (C37 U3): skin list and skin clone.
int local(const AgentWords::Request &request)
{
    const QString command = request.command();
    const QDateTime at = QDateTime::currentDateTimeUtc();
    if (request.subword == QLatin1String("list"))
        return answer(command,
                      request.json,
                      SkinWords::list(skinsDir(),
                                      SkinWords::savedChoice(),
                                      qEnvironmentVariable("OMASEED_SKIN"),
                                      at,
                                      core::version()));
    const SkinWords::Outcome outcome = SkinWords::clone(
        skinsDir(), request.positionals.at(0), request.positionals.at(1), at, core::version());
    if (!outcome.code.isEmpty())
        return fail(command, request.json, outcome.code, outcome.message);
    return answer(command, request.json, outcome.json);
}

/// `skin use` with no omaseed to draw it (C37 U6, #283): the choice saved for the next start,
/// never over a state.json that doesn't parse.
int saved(const AgentWords::Request &request, const integration::Skin &skin)
{
    const QString command = request.command();
    const QString name = request.positionals.first();
    const SkinWords::Outcome outcome = SkinWords::saveChoice(name);
    if (!outcome.code.isEmpty())
        return fail(command, request.json, outcome.code, outcome.message);
    return answer(
        command,
        request.json,
        SkinWords::use(name, skin, false, QDateTime::currentDateTimeUtc(), core::version()));
}

} // namespace

int runWord(int argc, char *argv[], const std::function<QString()> &qtHelp)
{
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);
    const AgentWords::Request request = AgentWords::parse(arguments);
    // check draws offscreen on an application of its own (C37 U5).
    if (request.kind == AgentWords::Request::Kind::Local && request.word == QLatin1String("check"))
        return check(argc, argv, request);

    // No display: a word works over SSH as long as the session bus is reachable (C45 V3).
    QCoreApplication app(argc, argv);
    switch (request.kind) {
    case AgentWords::Request::Kind::Page:
        write(stdout,
              (request.page.isEmpty() ? AgentWords::helpPage(qtHelp())
                                      : AgentWords::wordPage(request.page))
                  .toUtf8());
        return 0;
    case AgentWords::Request::Kind::Usage:
        return fail(request.word, request.json, QStringLiteral("usage"), request.message);
    case AgentWords::Request::Kind::Local:
        return local(request);
    case AgentWords::Request::Kind::Call:
        break;
    }

    const QString command = request.command();
    // C37 U6: `skin use` names a skin that reads, checked before anything is asked or saved.
    integration::SkinWords::Pick picked;
    if (request.subword == QLatin1String("use")) {
        picked = SkinWords::pick(skinsDir(), request.positionals.first());
        if (!picked.code.isEmpty())
            return fail(command, request.json, picked.code, picked.message);
    }
    const integration::AgentClient::Reply reply =
        integration::AgentClient::call(AgentWords::method(command), request.options);
    if (request.subword == QLatin1String("use") &&
        (reply.code == QLatin1String("not-running") || reply.code == QLatin1String("no-bus")))
        return saved(request, picked.skin);
    if (!reply.code.isEmpty())
        return fail(command, request.json, reply.code, reply.message);
    return answer(command, request.json, reply.answer);
}

} // namespace omaseed::ui
