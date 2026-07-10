/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace
{
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
                     qlonglong agentPid,
                     const QString &sessionPath,
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
        {QStringLiteral("agent_pid"), agentPid > 0 ? QString::number(agentPid) : QString()},
        {QStringLiteral("helper_pid"), QString::number(QCoreApplication::applicationPid())},
        {QStringLiteral("terminal_session"), sessionPath},
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
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Set the project status for the current Kmux terminal session."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption hookModeOption(QStringLiteral("hook-output"),
                                            QStringLiteral("Print an empty JSON object for agent hook protocols after updating the status."));
    const QCommandLineOption agentPidOption(QStringLiteral("agent-pid"),
                                            QStringLiteral("Associate the status with an agent process so it can be cleared when that process exits."),
                                            QStringLiteral("pid"));
    const QCommandLineOption agentOption(QStringLiteral("agent"), QStringLiteral("Agent name used by the hook trace."), QStringLiteral("name"));
    const QCommandLineOption eventOption(QStringLiteral("event"), QStringLiteral("Agent event name used by the hook trace."), QStringLiteral("name"));
    parser.addOption(hookModeOption);
    parser.addOption(agentPidOption);
    parser.addOption(agentOption);
    parser.addOption(eventOption);
    parser.addPositionalArgument(QStringLiteral("status"), QStringLiteral("Project status: running, idle, needsInput, unknown, or none."));
    parser.process(app);

    const bool hookMode = parser.isSet(hookModeOption);
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        printError(hookMode, QStringLiteral("Missing status."));
        if (hookMode) {
            return finishForHook(hookMode, 2);
        }
        parser.showHelp(2);
    }

    const QString service = qEnvironmentVariable("KMUX_DBUS_SERVICE");
    const QString objectPath = qEnvironmentVariable("KMUX_DBUS_SESSION");
    bool validAgentPid = false;
    const qlonglong agentPid = parser.value(agentPidOption).toLongLong(&validAgentPid);
    const QString status = args.first();
    const QString agent = parser.value(agentOption);
    const QString event = parser.value(eventOption);
    appendHookTrace(QStringLiteral("received"), agent, event, status, validAgentPid ? agentPid : 0, objectPath);
    if (service.isEmpty() || objectPath.isEmpty()) {
        const QString error = QStringLiteral("KMUX_DBUS_SERVICE and KMUX_DBUS_SESSION must be set.");
        appendHookTrace(QStringLiteral("failed"), agent, event, status, validAgentPid ? agentPid : 0, objectPath, error);
        printError(hookMode, error);
        return finishForHook(hookMode, 2);
    }

    QDBusInterface session(service, objectPath, QStringLiteral("io.github.kmux_project.kmux.Session"), QDBusConnection::sessionBus());
    if (!session.isValid()) {
        appendHookTrace(QStringLiteral("failed"), agent, event, status, validAgentPid ? agentPid : 0, objectPath, session.lastError().message());
        printError(hookMode, session.lastError().message());
        return finishForHook(hookMode, 3);
    }

    const QDBusReply<void> reply = !agent.isEmpty() && !event.isEmpty()
        ? session.call(QStringLiteral("setProjectStatusForAgentEvent"), status, validAgentPid ? agentPid : 0, agent, event)
        : validAgentPid && agentPid > 0 ? session.call(QStringLiteral("setProjectStatusWithProcess"), status, agentPid)
                                        : session.call(QStringLiteral("setProjectStatus"), status);
    if (!reply.isValid()) {
        appendHookTrace(QStringLiteral("failed"), agent, event, status, validAgentPid ? agentPid : 0, objectPath, reply.error().message());
        printError(hookMode, reply.error().message());
        return finishForHook(hookMode, 3);
    }

    appendHookTrace(QStringLiteral("applied"), agent, event, status, validAgentPid ? agentPid : 0, objectPath);
    return finishForHook(hookMode, 0);
}
