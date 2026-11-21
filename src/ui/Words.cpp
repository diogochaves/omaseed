#include "ui/Words.hpp"

#include "core/AgentDeny.hpp"
#include "core/AgentText.hpp"
#include "core/AgentWords.hpp"
#include "core/Paths.hpp"
#include "core/Version.hpp"
#include "integration/AgentClient.hpp"
#include "integration/AgentGuide.hpp"
#include "integration/AppIdentity.hpp"
#include "integration/SkillLinker.hpp"
#include "integration/SkillWords.hpp"
#include "integration/SkinReader.hpp"
#include "integration/SkinWords.hpp"
#include "integration/ThemeReader.hpp"
#include "ui/Check.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
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

/// `do --plan FILE|-` (C50 GT4): the CLI reads the plan, a file or stdin, and sends its text;
/// the app never opens a path an agent names (PLAN Q13). "" with `why` set when it can't.
QString readPlan(const QString &file, QString *why)
{
    constexpr qint64 kMost = 1 << 20; // a plan of thousands of lines is still far under it
    QFile in;
    const bool stdinPlan = file == QLatin1String("-");
    const bool opened = stdinPlan ? in.open(stdin, QIODevice::ReadOnly)
                                  : (in.setFileName(file), in.open(QIODevice::ReadOnly));
    if (!opened) {
        *why = QStringLiteral("omaseed: do --plan can't read %1: %2.").arg(file, in.errorString());
        return QString();
    }
    const QByteArray bytes = in.read(kMost + 1);
    if (bytes.size() > kMost) {
        *why = QStringLiteral("omaseed: do --plan reads plans up to 1 MB; %1 is bigger.")
                   .arg(stdinPlan ? QStringLiteral("stdin's") : file);
        return QString();
    }
    if (bytes.trimmed().isEmpty()) {
        *why = QStringLiteral("omaseed: do --plan found nothing in %1.")
                   .arg(stdinPlan ? QStringLiteral("stdin") : file);
        return QString();
    }
    return QString::fromUtf8(bytes);
}

/// True when any of a plan's lines has a failed entry (C45 V13: the server refused, exit 6).
bool planFailed(const QJsonObject &object)
{
    for (const QJsonValue &line : object.value(QStringLiteral("lines")).toArray())
        if (!line.toObject().value(QStringLiteral("failed")).toArray().isEmpty())
            return true;
    return false;
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
        if (json) {
            write(stdout, bytes);
        } else {
            // C50 GT4, GT5: a plan refused says its lines first, which one and why.
            if (object.value(QStringLiteral("plan")).toBool())
                if (const QString lines = AgentText::lines(object, styleFor(STDOUT_FILENO));
                    !lines.isEmpty())
                    write(stdout, lines.toUtf8());
            write(stderr, failureWords(failure.value(QStringLiteral("message")).toString()));
        }
        return AgentWords::exitFor(code.isEmpty() ? QStringLiteral("failed") : code);
    }
    // C45 V13: a do the server refused, for one torrent or more, exits 6 (its lines still say
    // what was done); a plan's per line.
    const bool failed =
        !object.value(QStringLiteral("failed")).toArray().isEmpty() || planFailed(object);
    const int exit = failed ? AgentWords::exitFor(QStringLiteral("failed")) : 0;
    if (json) {
        write(stdout, bytes);
        return exit;
    }
    // C45 V12: the lines are made from the same JSON --json prints. A word whose lines aren't
    // in this omaseed yet (#163 steps C, D) prints its JSON.
    QString lines = AgentText::lines(object, styleFor(STDOUT_FILENO));
    if (lines.isEmpty())
        lines = SkinWords::lines(object, QDir::homePath());
    write(stdout, lines.isEmpty() ? bytes : lines.toUtf8());
    return exit;
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

/// `skill link | unlink | status` (DECISIONS C48 AS6–AS8, docs/65 §5): the links into each
/// agent's skill folder, from the skill found now (the installed one, else a build's).
int skillLinks(const AgentWords::Request &request)
{
    namespace SkillLinker = integration::SkillLinker;
    const QString command = request.command();
#ifdef OMASEED_SOURCE_SKILL_DIR
    const QString source = SkillLinker::findSource(QStringLiteral(OMASEED_SOURCE_SKILL_DIR));
#else
    const QString source = SkillLinker::findSource();
#endif
    const SkillLinker::Report report =
        request.subword == QLatin1String("link")     ? SkillLinker::link(source)
        : request.subword == QLatin1String("unlink") ? SkillLinker::unlink(source)
                                                     : SkillLinker::status(source);
    if (!report.code.isEmpty())
        return fail(command, request.json, report.code, report.message);
    write(stdout,
          request.json
              ? integration::SkillWords::json(command, QDateTime::currentDateTimeUtc(), report)
              : integration::SkillWords::lines(command, report, QDir::homePath()).toUtf8());
    return 0;
}

/// `skill` and `skill PAGE` (DECISIONS C48, docs/65 §4): the map as --skill prints it, or a
/// page; with --json, the envelope with page ("map" or its name) and text.
int skill(const AgentWords::Request &request)
{
    namespace AgentGuide = integration::AgentGuide;
    const QString command = request.command();
    if (!AgentWords::isSkillPrint(request))
        return skillLinks(request);
    const QString text = request.subword.isEmpty() ? integration::SkillLinker::printedGuide()
                                                   : AgentGuide::page(request.subword);
    if (request.json) {
        write(stdout,
              AgentWords::orderedJson(
                  {{QStringLiteral("v"), 1},
                   {QStringLiteral("command"), command},
                   {QStringLiteral("at"), AgentWords::isoUtc(QDateTime::currentDateTimeUtc())},
                   {QStringLiteral("page"),
                    request.subword.isEmpty() ? QStringLiteral("map") : request.subword},
                   {QStringLiteral("text"), text}}));
        return 0;
    }
    write(stdout, text.toUtf8());
    return 0;
}

/// `help agents [--for AGENT]` (C50 GT7): the deny rules, filled with the address of the
/// server the running omaseed is on, asked through Status; with none running (or one that
/// won't say), printed with placeholders and a line saying so. Never a file read, never a
/// file written: exit 0 either way.
int helpAgents(const AgentWords::Request &request)
{
    namespace AgentDeny = core::AgentDeny;
    AgentDeny::Target target;
    target.busName = integration::AppIdentity::dbusServiceName();
    target.configDir = core::paths::configDir();
    target.home = QDir::homePath();
    const integration::AgentClient::Reply reply =
        integration::AgentClient::call(QStringLiteral("Status"), QJsonObject{}, 5000);
    const QJsonObject object = QJsonDocument::fromJson(reply.answer).object();
    const QString failed =
        object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
    const QJsonObject server = object.value(QStringLiteral("server")).toObject();
    const QString address = server.value(QStringLiteral("address")).toString();
    if (!reply.code.isEmpty() || !failed.isEmpty() || address.isEmpty()) {
        target.unknownWhy = !reply.code.isEmpty() ? reply.code
                            : !failed.isEmpty()   ? failed
                                                  : QStringLiteral("no-server");
    } else {
        const qsizetype colon = address.lastIndexOf(QLatin1Char(':'));
        const bool hasPort = colon > 0 && !address.mid(colon + 1).contains(QLatin1Char(']'));
        target.host = hasPort ? address.left(colon) : address;
        target.port = hasPort ? address.mid(colon + 1) : QString();
        target.serverName = server.value(QStringLiteral("name")).toString();
    }
    const AgentDeny::Rules rules =
        AgentDeny::rulesFor(request.options.value(QStringLiteral("for")).toString(), target);
    write(stdout,
          request.json ? AgentDeny::json(rules, target, QDateTime::currentDateTimeUtc())
                       : rules.text.toUtf8());
    return 0;
}

} // namespace

int runWord(int argc, char *argv[], const std::function<QString()> &qtHelp)
{
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);
    const AgentWords::Request request =
        AgentWords::parse(arguments, integration::AgentGuide::pages());
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
        if (request.word == QLatin1String("skill"))
            return skill(request);
        if (request.word == QLatin1String("help"))
            return helpAgents(request);
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
    QJsonObject options = request.options;
    if (options.contains(QStringLiteral("planFile"))) {
        QString why;
        const QString plan = readPlan(options.take(QStringLiteral("planFile")).toString(), &why);
        if (plan.isEmpty())
            return fail(command, request.json, QStringLiteral("usage"), why);
        options.insert(QStringLiteral("plan"), plan);
    }
    // `do` waits for the person's answer, then the server's (C45 V15, V17).
    const integration::AgentClient::Reply reply = integration::AgentClient::call(
        AgentWords::method(command),
        options,
        options.contains(QStringLiteral("plan")) ? AgentWords::kPlanTimeoutMs
        : request.word == QLatin1String("do")    ? AgentWords::kAskTimeoutMs
                                                 : AgentWords::kReplyTimeoutMs);
    if (request.subword == QLatin1String("use") &&
        (reply.code == QLatin1String("not-running") || reply.code == QLatin1String("no-bus")))
        return saved(request, picked.skin);
    if (!reply.code.isEmpty())
        return fail(command, request.json, reply.code, reply.message);
    return answer(command, request.json, reply.answer);
}

} // namespace omaseed::ui
