/*
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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

namespace
{
QString stripTomlComment(const QString &line)
{
    QChar quote;
    bool escaped = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (!quote.isNull()) {
            if (quote == QLatin1Char('"') && ch == QLatin1Char('\\') && !escaped) {
                escaped = true;
                continue;
            }
            if (ch == quote && !escaped) {
                quote = QChar();
            }
            escaped = false;
        } else if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
        } else if (ch == QLatin1Char('#')) {
            return line.left(i);
        }
    }
    return line;
}

int unquotedEquals(const QString &line)
{
    QChar quote;
    bool escaped = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (!quote.isNull()) {
            if (quote == QLatin1Char('"') && ch == QLatin1Char('\\') && !escaped) {
                escaped = true;
                continue;
            }
            if (ch == quote && !escaped) {
                quote = QChar();
            }
            escaped = false;
        } else if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
        } else if (ch == QLatin1Char('=')) {
            return i;
        }
    }
    return -1;
}

QString tomlStringValue(QString value)
{
    value = stripTomlComment(value).trimmed();
    if (value.size() >= 2
        && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
            || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\'')))) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

QString topLevelTomlValue(const QString &path, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stripTomlComment(stream.readLine()).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            break;
        }
        const int equals = unquotedEquals(line);
        if (equals < 0) {
            continue;
        }
        QString parsedKey = line.left(equals).trimmed();
        if (parsedKey.size() >= 2
            && ((parsedKey.front() == QLatin1Char('"') && parsedKey.back() == QLatin1Char('"'))
                || (parsedKey.front() == QLatin1Char('\'') && parsedKey.back() == QLatin1Char('\'')))) {
            parsedKey = parsedKey.mid(1, parsedKey.size() - 2);
        }
        if (parsedKey == key) {
            return tomlStringValue(line.mid(equals + 1));
        }
    }
    return {};
}

QString normalizedReviewer(const QString &value)
{
    const QString reviewer = tomlStringValue(value).trimmed().toLower();
    return reviewer == QLatin1String("auto_review") || reviewer == QLatin1String("user") ? reviewer : QString();
}

struct CodexInvocationConfig {
    QString profile;
    QString reviewer;
};

CodexInvocationConfig invocationConfig(const QStringList &arguments)
{
    CodexInvocationConfig config;
    for (int i = 0; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);
        QString override;
        if ((argument == QLatin1String("-c") || argument == QLatin1String("--config")) && i + 1 < arguments.size()) {
            override = arguments.at(++i);
        } else if (argument.startsWith(QLatin1String("--config="))) {
            override = argument.mid(9);
        }

        if (!override.isEmpty()) {
            const int equals = unquotedEquals(override);
            if (equals >= 0) {
                const QString key = override.left(equals).trimmed();
                if (key == QLatin1String("approvals_reviewer")) {
                    config.reviewer = normalizedReviewer(override.mid(equals + 1));
                } else if (key == QLatin1String("profile")) {
                    config.profile = tomlStringValue(override.mid(equals + 1));
                }
            }
        } else if ((argument == QLatin1String("-p") || argument == QLatin1String("--profile")) && i + 1 < arguments.size()) {
            config.profile = arguments.at(++i);
        } else if (argument.startsWith(QLatin1String("--profile="))) {
            config.profile = argument.mid(10);
        }
    }
    return config;
}

#ifdef Q_OS_LINUX
qlonglong processParentId(qlonglong processId)
{
    QFile statFile(QStringLiteral("/proc/%1/stat").arg(processId));
    if (!statFile.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QByteArray stat = statFile.readAll();
    const int commandEnd = stat.lastIndexOf(") ");
    if (commandEnd < 0) {
        return 0;
    }
    const QList<QByteArray> fields = stat.mid(commandEnd + 2).split(' ');
    return fields.size() >= 2 ? fields.at(1).toLongLong() : 0;
}

QStringList processArguments(qlonglong processId)
{
    QFile commandLine(QStringLiteral("/proc/%1/cmdline").arg(processId));
    if (!commandLine.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QList<QByteArray> encodedArguments = commandLine.readAll().split('\0');
    QStringList arguments;
    for (const QByteArray &argument : encodedArguments) {
        if (!argument.isEmpty()) {
            arguments.append(QString::fromLocal8Bit(argument));
        }
    }
    return arguments;
}
#endif

QStringList codexInvocationArguments()
{
#ifdef Q_OS_LINUX
    qlonglong processId = getppid();
    for (int depth = 0; processId > 1 && depth < 12; ++depth) {
        const QStringList arguments = processArguments(processId);
        const qsizetype candidates = std::min<qsizetype>(arguments.size(), 2);
        for (int i = 0; i < candidates; ++i) {
            if (QFileInfo(arguments.at(i)).fileName() == QLatin1String("codex")) {
                return arguments.mid(i + 1);
            }
        }
        processId = processParentId(processId);
    }
#endif
    return {};
}

QString hookWorkingDirectory()
{
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument payload = QJsonDocument::fromJson(input.readAll());
    return payload.object().value(QStringLiteral("cwd")).toString();
}

QString codexConfigHome()
{
    const QString configuredHome = qEnvironmentVariable("CODEX_HOME");
    return configuredHome.isEmpty() ? QDir::home().filePath(QStringLiteral(".codex")) : QFileInfo(configuredHome).absoluteFilePath();
}

QString trustedProjectRoot(const QString &userConfigPath, const QString &workingDirectory)
{
    QFile file(userConfigPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QRegularExpression projectTable(QStringLiteral(R"(^\s*\[\s*projects\s*\.\s*(["'])(.+)\1\s*\]\s*$)"));
    QString projectPath;
    QString closestTrustedPath;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stripTomlComment(stream.readLine()).trimmed();
        const QRegularExpressionMatch tableMatch = projectTable.match(line);
        if (tableMatch.hasMatch()) {
            projectPath = QDir::cleanPath(tableMatch.captured(2));
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            projectPath.clear();
            continue;
        }
        if (projectPath.isEmpty()) {
            continue;
        }
        const int equals = unquotedEquals(line);
        if (equals < 0 || line.left(equals).trimmed() != QLatin1String("trust_level") || tomlStringValue(line.mid(equals + 1)) != QLatin1String("trusted")) {
            continue;
        }
        const QString relative = QDir(projectPath).relativeFilePath(workingDirectory);
        if (relative != QLatin1String("..") && !relative.startsWith(QLatin1String("../")) && projectPath.size() > closestTrustedPath.size()) {
            closestTrustedPath = projectPath;
        }
    }
    return closestTrustedPath;
}

void applyReviewerFromConfig(const QString &path, QString *reviewer)
{
    const QString configuredReviewer = normalizedReviewer(topLevelTomlValue(path, QStringLiteral("approvals_reviewer")));
    if (!configuredReviewer.isEmpty()) {
        *reviewer = configuredReviewer;
    }
}

QString effectiveCodexReviewer(const QString &workingDirectory)
{
    QString reviewer = QStringLiteral("user");
    applyReviewerFromConfig(QStringLiteral("/etc/codex/config.toml"), &reviewer);

    const QString configHome = codexConfigHome();
    const QString userConfigPath = QDir(configHome).filePath(QStringLiteral("config.toml"));
    applyReviewerFromConfig(userConfigPath, &reviewer);

    const CodexInvocationConfig invocation = invocationConfig(codexInvocationArguments());
    QString profile = invocation.profile;
    if (profile.isEmpty()) {
        profile = topLevelTomlValue(userConfigPath, QStringLiteral("profile"));
    }
    if (!profile.isEmpty()) {
        applyReviewerFromConfig(QDir(configHome).filePath(QStringLiteral("%1.config.toml").arg(profile)), &reviewer);
    }

    const QString cleanWorkingDirectory = QDir::cleanPath(workingDirectory);
    const QString trustedRoot = trustedProjectRoot(userConfigPath, cleanWorkingDirectory);
    if (!trustedRoot.isEmpty()) {
        QString directory = trustedRoot;
        while (true) {
            applyReviewerFromConfig(QDir(directory).filePath(QStringLiteral(".codex/config.toml")), &reviewer);
            if (directory == cleanWorkingDirectory) {
                break;
            }
            const QString relative = QDir(directory).relativeFilePath(cleanWorkingDirectory);
            const QString nextComponent = relative.section(QLatin1Char('/'), 0, 0);
            if (nextComponent.isEmpty() || nextComponent == QLatin1String(".") || nextComponent == QLatin1String("..")) {
                break;
            }
            directory = QDir(directory).filePath(nextComponent);
        }
    }

    if (!invocation.reviewer.isEmpty()) {
        reviewer = invocation.reviewer;
    }
    return reviewer;
}

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
    QCoreApplication::setApplicationVersion(QStringLiteral(KMUX_VERSION));

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
    const QCommandLineOption codexPermissionRequestOption(QStringLiteral("codex-permission-request"),
                                                          QStringLiteral("Resolve PermissionRequest status from the effective Codex approval reviewer."));
    parser.addOption(hookModeOption);
    parser.addOption(agentPidOption);
    parser.addOption(agentOption);
    parser.addOption(eventOption);
    parser.addOption(codexPermissionRequestOption);
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
    QString status = args.first();
    if (parser.isSet(codexPermissionRequestOption) && effectiveCodexReviewer(hookWorkingDirectory()) == QLatin1String("auto_review")) {
        status = QStringLiteral("running");
    }
    const QString agent = parser.value(agentOption);
    const QString event = parser.value(eventOption);
    appendHookTrace(QStringLiteral("received"), agent, event, status, validAgentPid ? agentPid : 0, objectPath);
    if (service.isEmpty() || objectPath.isEmpty()) {
        const QString error = QStringLiteral("KMUX_DBUS_SERVICE and KMUX_DBUS_SESSION must be set.");
        appendHookTrace(QStringLiteral("failed"), agent, event, status, validAgentPid ? agentPid : 0, objectPath, error);
        printError(hookMode, error);
        return finishForHook(hookMode, 2);
    }

    QDBusInterface session(service, objectPath, QStringLiteral("io.github.vityas_off.kmux.Session"), QDBusConnection::sessionBus());
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
