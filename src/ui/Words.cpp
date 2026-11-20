#include "ui/Words.hpp"

#include "core/AgentWords.hpp"
#include "core/Version.hpp"
#include "integration/AgentClient.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <cstdio>

namespace omaseed::ui {

namespace {

namespace AgentWords = core::AgentWords;

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
        write(stderr, message.toUtf8());
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
            write(stderr, failure.value(QStringLiteral("message")).toString().toUtf8());
        return AgentWords::exitFor(code.isEmpty() ? QStringLiteral("failed") : code);
    }
    // C45 V12's human lines are AgentText's, which the reads bring with them (steps B–D of
    // #163); until a word has its lines, its JSON stands in.
    write(stdout, bytes);
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
