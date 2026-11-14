/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "config-konsole.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <algorithm>

namespace
{
constexpr qint64 ClaudeTranscriptTailBytes = 1024 * 1024;
constexpr auto ClaudeAgentName = "claude";
constexpr auto RateLimitedStatus = "rateLimited";

QJsonObject hookPayload()
{
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(input.readAll()).object();
}

bool hasClaudeUsageLimitWrapUp(const QJsonObject &payload)
{
    if (!payload.value(QStringLiteral("agent_id")).toString().trimmed().isEmpty()) {
        return false;
    }
    QFile transcript(payload.value(QStringLiteral("transcript_path")).toString());
    if (!QFileInfo(transcript).isFile() || !transcript.open(QIODevice::ReadOnly)) {
        return false;
    }
    const qint64 offset = std::max<qint64>(0, transcript.size() - ClaudeTranscriptTailBytes);
    if (!transcript.seek(offset)) {
        return false;
    }
    const QList<QByteArray> lines = transcript.read(ClaudeTranscriptTailBytes).split('\n');
    const QString promptId = payload.value(QStringLiteral("prompt_id")).toString();
    const QString sessionId = payload.value(QStringLiteral("session_id")).toString();
    // Graceful usage-limit shutdowns finish with Stop, not StopFailure. Claude
    // records the reason as metadata on a user message before its final reply.
    for (auto line = lines.crbegin(); line != lines.crend(); ++line) {
        const QJsonObject record = QJsonDocument::fromJson(*line).object();
        if (record.value(QStringLiteral("type")) != QLatin1String("user") || record.value(QStringLiteral("isSidechain")).toBool()) {
            continue;
        }
        const QString recordPromptId = record.value(QStringLiteral("promptId")).toString();
        const QString recordSessionId = record.value(QStringLiteral("sessionId")).toString();
        if ((!promptId.isEmpty() && !recordPromptId.isEmpty() && recordPromptId != promptId)
            || (!sessionId.isEmpty() && !recordSessionId.isEmpty() && recordSessionId != sessionId)) {
            return false;
        }
        const bool isMeta = record.value(QStringLiteral("isMeta")).toBool();
        const bool isTurnCompanion = record.value(QStringLiteral("turnCompanion")).toBool();
        const QString usageLimitNote = record.value(QStringLiteral("usageLimitNote")).toString();
        if (isMeta && isTurnCompanion && !usageLimitNote.isEmpty()) {
            return usageLimitNote == QLatin1String("wrap_up");
        }
        const QJsonValue content = record.value(QStringLiteral("message")).toObject().value(QStringLiteral("content"));
        const QJsonArray blocks = content.toArray();
        const bool hasText = content.isString() || std::any_of(blocks.cbegin(), blocks.cend(), [](const QJsonValue &block) {
                                 return block.toObject().value(QStringLiteral("type")) == QLatin1String("text");
                             });
        if (!isMeta && !isTurnCompanion && hasText) {
            return false;
        }
    }
    return false;
}

// Agent hooks must never fail: a failing hook can stop the agent's action.
int finishForHook(bool hookMode, int code)
{
    if (hookMode) {
        QTextStream(stdout) << "{}\n";
        return 0;
    }

    return code;
}

void printError(bool hookMode, const QString &message)
{
    if (!hookMode) {
        QTextStream(stderr) << message << '\n';
    }
}

void appendHookTrace(const QString &phase,
                     const QString &agent,
                     const QString &event,
                     const QString &status,
                     const QString &sessionPath,
                     const QJsonObject &payload,
                     const QString &error = {})
{
    const QString tracePath = qEnvironmentVariable("KMUX_AGENT_HOOK_LOG");
    if (tracePath.isEmpty()) {
        return;
    }

    QJsonObject record{
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("phase"), phase},
        {QStringLiteral("agent"), agent},
        {QStringLiteral("event"), event},
        {QStringLiteral("status"), status},
        {QStringLiteral("helper_pid"), QString::number(QCoreApplication::applicationPid())},
        {QStringLiteral("terminal_session"), sessionPath},
        {QStringLiteral("session_id"), payload.value(QStringLiteral("session_id")).toString()},
        {QStringLiteral("prompt_id"), payload.value(QStringLiteral("prompt_id")).toString()},
    };
    if (!error.isEmpty()) {
        record.insert(QStringLiteral("error"), error);
    }

    QByteArray line = QJsonDocument(record).toJson(QJsonDocument::Compact);
    line.append('\n');
    QFile traceFile(tracePath);
    if (traceFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        traceFile.write(line);
    }
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kmux-project-status"));
    QCoreApplication::setApplicationVersion(QStringLiteral(KMUX_DISPLAY_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Set the project status for the current Kmux terminal session."));
    const QCommandLineOption helpOption = parser.addHelpOption();
    const QCommandLineOption versionOption = parser.addVersionOption();

    const QCommandLineOption hookModeOption(QStringLiteral("hook-output"),
                                            QStringLiteral("Answer an agent hook: print an empty JSON object and always succeed."));
    const QCommandLineOption processOption(QStringLiteral("agent-pid"), QStringLiteral("Clear the status when this process exits."), QStringLiteral("pid"));
    const QCommandLineOption agentOption(QStringLiteral("agent"), QStringLiteral("Agent that runs the hook."), QStringLiteral("name"));
    const QCommandLineOption eventOption(QStringLiteral("event"), QStringLiteral("Agent event name used by the hook trace."), QStringLiteral("name"));
    const QCommandLineOption claudeStopOption(QStringLiteral("claude-stop"),
                                              QStringLiteral("Report a Claude Code Stop hook that ends a turn at a usage limit."));
    const QCommandLineOption claudeStopFailureOption(QStringLiteral("claude-stop-failure"),
                                                     QStringLiteral("Report a Claude Code StopFailure hook caused by a rate limit."));
    parser.addOption(hookModeOption);
    parser.addOption(processOption);
    parser.addOption(agentOption);
    parser.addOption(eventOption);
    parser.addOption(claudeStopOption);
    parser.addOption(claudeStopFailureOption);
    parser.addPositionalArgument(QStringLiteral("status"), QStringLiteral("Project status: running, idle, needsInput, rateLimited, unknown, or none."));

    // Hooks that earlier Kmux versions installed may still pass options that
    // this version no longer knows; hooks must succeed regardless.
    const bool parsed = parser.parse(app.arguments());
    const bool hookMode = parser.isSet(hookModeOption);
    if (parser.isSet(helpOption)) {
        parser.showHelp();
    }
    if (parser.isSet(versionOption)) {
        parser.showVersion();
    }
    if (!parsed && !hookMode) {
        QTextStream(stderr) << parser.errorText() << '\n';
        return 2;
    }

    const QString service = qEnvironmentVariable("KMUX_DBUS_SERVICE");
    const QString objectPath = qEnvironmentVariable("KMUX_DBUS_SESSION");
    const QString agent = parser.value(agentOption);
    const QString event = parser.value(eventOption);

    QString status;
    QJsonObject payload;
    if (hookMode) {
        // Agents report their own state with OSC 7501. A hook reports only a
        // stop at a usage limit, which Claude Code reports as an ordinary result.
        const bool claudeStop = parser.isSet(claudeStopOption);
        const bool claudeStopFailure = parser.isSet(claudeStopFailureOption);
        if (claudeStop || claudeStopFailure) {
            payload = hookPayload();
        }
        const bool rateLimited =
            (claudeStopFailure && payload.value(QStringLiteral("error")).toString().compare(QLatin1String("rate_limit"), Qt::CaseInsensitive) == 0)
            || (claudeStop && hasClaudeUsageLimitWrapUp(payload));
        status = rateLimited ? QString::fromLatin1(RateLimitedStatus) : QStringLiteral("none");
        appendHookTrace(QStringLiteral("received"), agent, event, status, objectPath, payload);
        if (!rateLimited) {
            appendHookTrace(QStringLiteral("ignored"), agent, event, status, objectPath, payload);
            return finishForHook(hookMode, 0);
        }
    } else {
        const QStringList args = parser.positionalArguments();
        if (args.isEmpty()) {
            QTextStream(stderr) << "Missing status.\n";
            parser.showHelp(2);
        }
        status = args.first();
    }

    if (service.isEmpty() || objectPath.isEmpty()) {
        const QString error = QStringLiteral("KMUX_DBUS_SERVICE and KMUX_DBUS_SESSION must be set.");
        appendHookTrace(QStringLiteral("failed"), agent, event, status, objectPath, payload, error);
        printError(hookMode, error);
        return finishForHook(hookMode, 2);
    }

    QDBusInterface session(service, objectPath, QStringLiteral("io.github.vityas_off.kmux.Session"), QDBusConnection::sessionBus());
    if (!session.isValid()) {
        appendHookTrace(QStringLiteral("failed"), agent, event, status, objectPath, payload, session.lastError().message());
        printError(hookMode, session.lastError().message());
        return finishForHook(hookMode, 3);
    }

    bool validProcessId = false;
    const qlonglong processId = parser.value(processOption).toLongLong(&validProcessId);
    const QDBusReply<void> reply = hookMode ? session.call(QStringLiteral("setProjectStatusForAgent"), status, QString::fromLatin1(ClaudeAgentName))
        : validProcessId && processId > 0   ? session.call(QStringLiteral("setProjectStatusWithProcess"), status, processId)
                                            : session.call(QStringLiteral("setProjectStatus"), status);
    if (!reply.isValid()) {
        appendHookTrace(QStringLiteral("failed"), agent, event, status, objectPath, payload, reply.error().message());
        printError(hookMode, reply.error().message());
        return finishForHook(hookMode, 3);
    }

    appendHookTrace(QStringLiteral("applied"), agent, event, status, objectPath, payload);
    return finishForHook(hookMode, 0);
}
