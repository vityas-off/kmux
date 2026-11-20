/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

class AgentHooksTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testClaudeLauncherInstallsHooks_data();
    void testClaudeLauncherInstallsHooks();
    void testClaudeLauncherSkipsSelfSymlink();
    void testClaudeLauncherRunsScriptWithoutShebang_data();
    void testClaudeLauncherRunsScriptWithoutShebang();
    void testClaudeLauncherReportsMissingAgent();
    void testClaudeCommandUsesTransparentLauncher();
    void testOpenCodeLauncherHookInstallation_data();
    void testOpenCodeLauncherHookInstallation();
    void testOpenCodeLifecycleConfiguration();
    void testOpenCodePluginApiSelection_data();
    void testOpenCodePluginApiSelection();
    void testOpenCodeStatusReportsApiMismatch();
    void testClaudeHooksReplaceEarlierHooks();

    void testClaudeUsageLimitWrapUp_data();
    void testClaudeUsageLimitWrapUp();
    void testClaudeStopFailureReportsRateLimit_data();
    void testClaudeStopFailureReportsRateLimit();
    void testHookHelperIgnoresEarlierHookArguments();
    void testCodexUninstallRemovesEarlierHooks();
    void testCodexInstallIsRejected();
    void testHookOperationsWaitForTransactionLock_data();
    void testHookOperationsWaitForTransactionLock();
    void testUnrelatedHooksArePreserved_data();
    void testUnrelatedHooksArePreserved();
    void testUninstallLeavesNoEmptyHooks_data();
    void testUninstallLeavesNoEmptyHooks();
    void testHomeScopedScripts_data();
    void testHomeScopedScripts();
    void testHookCommandsQuoteScriptPaths_data();
    void testHookCommandsQuoteScriptPaths();
    void testReinstallReplacesUnquotedHookCommands_data();
    void testReinstallReplacesUnquotedHookCommands();
};

namespace
{
struct ProcessResult {
    int exitCode = -1;
    QByteArray output;
    QByteArray error;
};

ProcessResult runProcess(const QString &program, const QStringList &arguments, const QProcessEnvironment &environment, const QByteArray &input = {})
{
    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(program, arguments);
    if (!process.waitForStarted()) {
        return {};
    }
    process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(10000) || process.exitStatus() != QProcess::NormalExit) {
        return {};
    }
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

QJsonObject hookGroup(const QString &command)
{
    return QJsonObject{
        {QStringLiteral("hooks"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("command")}, {QStringLiteral("command"), command}}}}};
}

QJsonObject readJsonFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeFile(const QString &path, const QByteArray &content, bool executable = false)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
        return false;
    }
    file.close();
    return !executable || QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

QJsonObject firstTraceRecord(const QString &tracePath)
{
    QFile trace(tracePath);
    if (!trace.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(trace.readLine()).object();
}
}

void AgentHooksTest::testClaudeLauncherInstallsHooks_data()
{
    QTest::addColumn<bool>("hooksDisabled");

    QTest::newRow("enabled") << false;
    QTest::newRow("disabled") << true;
}

void AgentHooksTest::testClaudeLauncherInstallsHooks()
{
    QFETCH(bool, hooksDisabled);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString binDir = temporaryDir.filePath(QStringLiteral("bin"));
    const QString homeDir = temporaryDir.filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(binDir));
    QVERIFY(QDir().mkpath(homeDir));
    QVERIFY(
        writeFile(QDir(binDir).filePath(QStringLiteral("claude")), QByteArrayLiteral("#!/bin/sh\nfor argument do printf '%s\\n' \"$argument\"; done\n"), true));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), binDir + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    environment.insert(QStringLiteral("HOME"), homeDir);
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    environment.remove(QStringLiteral("KMUX_CLAUDE_HOOKS_DISABLED"));
    if (hooksDisabled) {
        environment.insert(QStringLiteral("KMUX_CLAUDE_HOOKS_DISABLED"), QStringLiteral("1"));
    }

    // The launcher passes the arguments through unchanged.
    const QStringList arguments{QStringLiteral("--resume"), QStringLiteral("two words")};
    const ProcessResult result = runProcess(QStringLiteral(KMUX_CLAUDE_EXECUTABLE), arguments, environment);
    QVERIFY2(result.exitCode == 0, result.error.constData());
    QCOMPARE(QString::fromUtf8(result.output).split(QLatin1Char('\n'), Qt::SkipEmptyParts), arguments);
    QCOMPARE(QFileInfo::exists(QDir(homeDir).filePath(QStringLiteral(".claude/settings.json"))), !hooksDisabled);
}

void AgentHooksTest::testClaudeHooksReplaceEarlierHooks()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString configHome = temporaryDir.filePath(QStringLiteral("claude-home"));
    const QString settingsPath = QDir(configHome).filePath(QStringLiteral("settings.json"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    const QStringList install{QStringLiteral("--claude-home"), configHome, QStringLiteral("install"), QStringLiteral("claude"), QStringLiteral("--quiet")};

    ProcessResult result = runProcess(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), install, environment);
    QVERIFY2(result.exitCode == 0, result.error.constData());
    QJsonObject settings = readJsonFile(settingsPath);
    QJsonObject hooks = settings.value(QStringLiteral("hooks")).toObject();
    QCOMPARE(hooks.keys(), (QStringList{QStringLiteral("Stop"), QStringLiteral("StopFailure")}));
    const QString stopCommand = hooks.value(QStringLiteral("Stop"))
                                    .toArray()
                                    .first()
                                    .toObject()
                                    .value(QStringLiteral("hooks"))
                                    .toArray()
                                    .first()
                                    .toObject()
                                    .value(QStringLiteral("command"))
                                    .toString();
    const QString scriptDirectory = QFileInfo(stopCommand).absolutePath();

    // An earlier Kmux version installed hooks for events that Kmux no longer uses.
    const QString earlierScript = QDir(scriptDirectory).filePath(QStringLiteral("claude-pre_tool_use.sh"));
    QVERIFY(writeFile(earlierScript, QByteArrayLiteral("#!/bin/sh\n"), true));
    const QJsonObject userGroup = hookGroup(QStringLiteral("/opt/custom/user-hook"));
    hooks.insert(QStringLiteral("PreToolUse"), QJsonArray{hookGroup(earlierScript), userGroup});
    hooks.insert(QStringLiteral("Notification"), QJsonArray{hookGroup(QDir(scriptDirectory).filePath(QStringLiteral("claude-notification.sh")))});
    settings.insert(QStringLiteral("hooks"), hooks);
    QVERIFY(writeFile(settingsPath, QJsonDocument(settings).toJson()));

    result = runProcess(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), install, environment);
    QVERIFY2(result.exitCode == 0, result.error.constData());
    hooks = readJsonFile(settingsPath).value(QStringLiteral("hooks")).toObject();
    QCOMPARE(hooks.keys(), (QStringList{QStringLiteral("PreToolUse"), QStringLiteral("Stop"), QStringLiteral("StopFailure")}));
    QCOMPARE(hooks.value(QStringLiteral("PreToolUse")).toArray(), QJsonArray{userGroup});
    QCOMPARE(hooks.value(QStringLiteral("Stop")).toArray().size(), 1);
    QVERIFY(!QFileInfo::exists(earlierScript));
    QVERIFY(QFileInfo(stopCommand).isExecutable());
}

void AgentHooksTest::testClaudeStopFailureReportsRateLimit_data()
{
    QTest::addColumn<QString>("error");
    QTest::addColumn<QString>("expectedStatus");

    QTest::newRow("rate limit") << QStringLiteral("rate_limit") << QStringLiteral("rateLimited");
    QTest::newRow("server error") << QStringLiteral("server_error") << QStringLiteral("none");
}

void AgentHooksTest::testClaudeStopFailureReportsRateLimit()
{
    QFETCH(QString, error);
    QFETCH(QString, expectedStatus);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString tracePath = temporaryDir.filePath(QStringLiteral("trace.jsonl"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("KMUX_AGENT_HOOK_LOG"), tracePath);
    environment.remove(QStringLiteral("KMUX_DBUS_SERVICE"));
    environment.remove(QStringLiteral("KMUX_DBUS_SESSION"));

    const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("error"), error}}).toJson(QJsonDocument::Compact);
    const ProcessResult result = runProcess(QStringLiteral(KMUX_PROJECT_STATUS_EXECUTABLE),
                                            {QStringLiteral("--hook-output"),
                                             QStringLiteral("--agent"),
                                             QStringLiteral("claude"),
                                             QStringLiteral("--event"),
                                             QStringLiteral("StopFailure"),
                                             QStringLiteral("--claude-stop-failure")},
                                            environment,
                                            payload);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.output.trimmed(), QByteArrayLiteral("{}"));
    QCOMPARE(firstTraceRecord(tracePath).value(QStringLiteral("status")).toString(), expectedStatus);
}

void AgentHooksTest::testHookHelperIgnoresEarlierHookArguments()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString tracePath = temporaryDir.filePath(QStringLiteral("trace.jsonl"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("KMUX_AGENT_HOOK_LOG"), tracePath);
    environment.remove(QStringLiteral("KMUX_DBUS_SERVICE"));
    environment.remove(QStringLiteral("KMUX_DBUS_SESSION"));

    // Hooks that an earlier Kmux version installed pass options that this
    // version no longer knows. A failing hook could stop the agent's action.
    ProcessResult result = runProcess(QStringLiteral(KMUX_PROJECT_STATUS_EXECUTABLE),
                                      {QStringLiteral("--hook-output"),
                                       QStringLiteral("--agent"),
                                       QStringLiteral("codex"),
                                       QStringLiteral("--event"),
                                       QStringLiteral("PermissionRequest"),
                                       QStringLiteral("--agent-pid"),
                                       QStringLiteral("1"),
                                       QStringLiteral("--codex-permission-request"),
                                       QStringLiteral("needsInput")},
                                      environment,
                                      QByteArrayLiteral("{}"));
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.output.trimmed(), QByteArrayLiteral("{}"));
    const QJsonObject record = firstTraceRecord(tracePath);
    QCOMPARE(record.value(QStringLiteral("event")).toString(), QStringLiteral("PermissionRequest"));
    QCOMPARE(record.value(QStringLiteral("status")).toString(), QStringLiteral("none"));

    // Outside hooks, an unknown option remains an error.
    result = runProcess(QStringLiteral(KMUX_PROJECT_STATUS_EXECUTABLE), {QStringLiteral("--codex-permission-request"), QStringLiteral("running")}, environment);
    QCOMPARE(result.exitCode, 2);
}

void AgentHooksTest::testCodexUninstallRemovesEarlierHooks()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString dataHome = temporaryDir.filePath(QStringLiteral("data"));
    const QString codexHome = temporaryDir.filePath(QStringLiteral("codex-home"));
    QVERIFY(QDir().mkpath(codexHome));

    // Earlier Kmux versions kept the scripts in a directory scoped to the Codex home.
    const QByteArray homeId = QCryptographicHash::hash(QFileInfo(codexHome).canonicalFilePath().toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString scriptDirectory = QDir(dataHome).filePath(QStringLiteral("kmux/hooks/codex-%1").arg(QString::fromLatin1(homeId)));
    QVERIFY(QDir().mkpath(scriptDirectory));
    const QString preToolUseScript = QDir(scriptDirectory).filePath(QStringLiteral("codex-pre_tool_use.sh"));
    const QString stopScript = QDir(scriptDirectory).filePath(QStringLiteral("codex-stop.sh"));
    QVERIFY(writeFile(preToolUseScript, QByteArrayLiteral("#!/bin/sh\n"), true));
    QVERIFY(writeFile(stopScript, QByteArrayLiteral("#!/bin/sh\n"), true));

    const QJsonObject userGroup = hookGroup(QStringLiteral("/opt/custom/user-hook"));
    const QJsonObject earlierHooks{{QStringLiteral("hooks"),
                                    QJsonObject{{QStringLiteral("PreToolUse"), QJsonArray{hookGroup(preToolUseScript)}},
                                                {QStringLiteral("Stop"), QJsonArray{hookGroup(stopScript), userGroup}}}}};
    const QString hooksPath = QDir(codexHome).filePath(QStringLiteral("hooks.json"));
    QVERIFY(writeFile(hooksPath, QJsonDocument(earlierHooks).toJson()));

    // The feature block keeps the line that it replaced; uninstalling restores it.
    const QString configPath = QDir(codexHome).filePath(QStringLiteral("config.toml"));
    QVERIFY(writeFile(configPath,
                      QByteArrayLiteral("model = \"gpt\"\n"
                                        "# kmux-codex-hooks-feature begin\n"
                                        "# kmux-codex-hooks-feature previous line: features.hooks = false\n"
                                        "features.hooks = true\n"
                                        "# kmux-codex-hooks-feature end\n"
                                        "# kmux-codex-hook-trust begin\n"
                                        "[hooks.state.\"stop\"]\n"
                                        "trusted_hash = \"sha256:0\"\n"
                                        "# kmux-codex-hook-trust end\n")));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_DATA_HOME"), dataHome);
    const ProcessResult result = runProcess(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                                            {QStringLiteral("--codex-home"), codexHome, QStringLiteral("uninstall"), QStringLiteral("codex")},
                                            environment);
    QVERIFY2(result.exitCode == 0, result.error.constData());

    QCOMPARE(readJsonFile(hooksPath), (QJsonObject{{QStringLiteral("hooks"), QJsonObject{{QStringLiteral("Stop"), QJsonArray{userGroup}}}}}));
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::ReadOnly));
    QCOMPARE(config.readAll(), QByteArrayLiteral("model = \"gpt\"\nfeatures.hooks = false\n"));
    QVERIFY(!QFileInfo::exists(scriptDirectory));
}

void AgentHooksTest::testCodexInstallIsRejected()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString codexHome = temporaryDir.filePath(QStringLiteral("codex-home"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));

    for (const QString &command : {QStringLiteral("install"), QStringLiteral("status")}) {
        const ProcessResult result =
            runProcess(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), {QStringLiteral("--codex-home"), codexHome, command, QStringLiteral("codex")}, environment);
        QCOMPARE(result.exitCode, 2);
        QVERIFY2(result.error.contains("kmux-agent-hooks uninstall codex"), result.error.constData());
    }
    QVERIFY(!QFileInfo::exists(codexHome));
}

void AgentHooksTest::testClaudeLauncherSkipsSelfSymlink()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString launcherBinDir = temporaryDir.filePath(QStringLiteral("launcher-bin"));
    const QString agentBinDir = temporaryDir.filePath(QStringLiteral("agent-bin"));
    QVERIFY(QDir().mkpath(launcherBinDir));
    QVERIFY(QDir().mkpath(agentBinDir));

    const QString launcherAlias = QDir(launcherBinDir).filePath(QStringLiteral("claude"));
    QVERIFY(QFile::link(QStringLiteral(KMUX_CLAUDE_EXECUTABLE), launcherAlias));

    const QString agentPath = QDir(agentBinDir).filePath(QStringLiteral("claude"));
    QFile agent(agentPath);
    QVERIFY(agent.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script = QByteArrayLiteral("#!/bin/sh\nprintf 'real-claude:%s\\n' \"$1\"\n");
    QCOMPARE(agent.write(script), script.size());
    agent.close();
    QVERIFY(QFile::setPermissions(agentPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), launcherBinDir + QDir::listSeparator() + agentBinDir);
    environment.insert(QStringLiteral("KMUX_CLAUDE_HOOKS_DISABLED"), QStringLiteral("1"));

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_CLAUDE_EXECUTABLE), {QStringLiteral("argument")});
    QVERIFY(process.waitForStarted());
    QVERIFY2(process.waitForFinished(5000), "kmux-claude recursively executed itself");
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    QCOMPARE(process.readAllStandardOutput().trimmed(), QByteArrayLiteral("real-claude:argument"));
}

void AgentHooksTest::testClaudeLauncherRunsScriptWithoutShebang_data()
{
    QTest::addColumn<QStringList>("arguments");

    QTest::newRow("no arguments") << QStringList();
    QTest::newRow("shell option and spaces") << QStringList{QStringLiteral("--help"), QStringLiteral("two words")};
}

void AgentHooksTest::testClaudeLauncherRunsScriptWithoutShebang()
{
    QFETCH(QStringList, arguments);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString agentBinDir = temporaryDir.filePath(QStringLiteral("agent-bin"));
    QVERIFY(QDir().mkpath(agentBinDir));

    const QString agentPath = QDir(agentBinDir).filePath(QStringLiteral("claude"));
    QFile agent(agentPath);
    QVERIFY(agent.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script = QByteArrayLiteral("for argument do printf '%s\\n' \"$argument\"; done\n");
    QCOMPARE(agent.write(script), script.size());
    agent.close();
    QVERIFY(QFile::setPermissions(agentPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), agentBinDir + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    environment.insert(QStringLiteral("KMUX_CLAUDE_HOOKS_DISABLED"), QStringLiteral("1"));

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_CLAUDE_EXECUTABLE), arguments);
    QVERIFY(process.waitForStarted());
    QVERIFY2(process.waitForFinished(5000), "the shell did not run the agent script");
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    QCOMPARE(output.split(QLatin1Char('\n'), Qt::SkipEmptyParts), arguments);
}

void AgentHooksTest::testClaudeLauncherReportsMissingAgent()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString launcherBinDir = temporaryDir.filePath(QStringLiteral("launcher-bin"));
    QVERIFY(QDir().mkpath(launcherBinDir));
    QVERIFY(QFile::link(QStringLiteral(KMUX_CLAUDE_EXECUTABLE), QDir(launcherBinDir).filePath(QStringLiteral("claude"))));

    // The launcher is the only claude in PATH, as inside Kmux without Claude Code.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), launcherBinDir);
    environment.insert(QStringLiteral("KMUX_CLAUDE_HOOKS_DISABLED"), QStringLiteral("1"));

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_CLAUDE_EXECUTABLE), {});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished(5000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 127);
    const QByteArray error = process.readAllStandardError();
    QVERIFY2(error.contains("No such file or directory"), error.constData());
}



void AgentHooksTest::testClaudeCommandUsesTransparentLauncher()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString shimDir = temporaryDir.filePath(QStringLiteral("kmux-agent-shims"));
    const QString agentBinDir = temporaryDir.filePath(QStringLiteral("agent-bin"));
    QVERIFY(QDir().mkpath(shimDir));
    QVERIFY(QDir().mkpath(agentBinDir));

    const QString launcherAlias = QDir(shimDir).filePath(QStringLiteral("claude"));
    QVERIFY(QFile::copy(QStringLiteral(KMUX_CLAUDE_EXECUTABLE), launcherAlias));
    const QString hookInstaller = QDir(shimDir).filePath(QStringLiteral("kmux-agent-hooks"));
    QVERIFY(QFile::copy(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), hookInstaller));
    const QString projectStatusHelper = QDir(shimDir).filePath(QStringLiteral("kmux-project-status"));
    QVERIFY(QFile::copy(QStringLiteral(KMUX_PROJECT_STATUS_EXECUTABLE), projectStatusHelper));
    QVERIFY(QFileInfo(launcherAlias).isExecutable());
    QVERIFY(QFileInfo(hookInstaller).isExecutable());
    QVERIFY(QFileInfo(projectStatusHelper).isExecutable());

    const QString agentPath = QDir(agentBinDir).filePath(QStringLiteral("claude"));
    QFile agent(agentPath);
    QVERIFY(agent.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script = QByteArrayLiteral("#!/bin/sh\nprintf 'real-claude:%s\\n' \"$1\"\n");
    QCOMPARE(agent.write(script), script.size());
    agent.close();
    QVERIFY(QFile::setPermissions(agentPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    const QString homeDir = temporaryDir.filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(homeDir));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), shimDir + QDir::listSeparator() + agentBinDir);
    environment.insert(QStringLiteral("HOME"), homeDir);
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    environment.remove(QStringLiteral("KMUX_CLAUDE_HOOKS_DISABLED"));

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(launcherAlias, {QStringLiteral("argument")});
    QVERIFY(process.waitForStarted());
    QVERIFY2(process.waitForFinished(5000), "transparent claude launcher recursively executed itself");
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    QCOMPARE(process.readAllStandardOutput().trimmed(), QByteArrayLiteral("real-claude:argument"));

    QFile settings(QDir(homeDir).filePath(QStringLiteral(".claude/settings.json")));
    QVERIFY(settings.open(QIODevice::ReadOnly));
    const QJsonObject hooks = QJsonDocument::fromJson(settings.readAll()).object().value(QStringLiteral("hooks")).toObject();
    const QString hookCommand = hooks.value(QStringLiteral("Stop"))
                                    .toArray()
                                    .first()
                                    .toObject()
                                    .value(QStringLiteral("hooks"))
                                    .toArray()
                                    .first()
                                    .toObject()
                                    .value(QStringLiteral("command"))
                                    .toString();
    QFile hookScript(hookCommand);
    QVERIFY(hookScript.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString hookScriptText = QString::fromUtf8(hookScript.readAll());
    QVERIFY2(hookScriptText.contains(QStringLiteral("helper='%1'").arg(projectStatusHelper)), qPrintable(hookScriptText));

    const QString tracePath = temporaryDir.filePath(QStringLiteral("hook-trace.jsonl"));
    environment.insert(QStringLiteral("KMUX_AGENT_HOOK_LOG"), tracePath);
    environment.remove(QStringLiteral("KMUX_DBUS_SERVICE"));
    environment.remove(QStringLiteral("KMUX_DBUS_SESSION"));
    QProcess hookProcess;
    hookProcess.setProcessEnvironment(environment);
    hookProcess.start(hookCommand);
    QVERIFY(hookProcess.waitForStarted());
    const QJsonObject hookInput{
        {QStringLiteral("session_id"), QStringLiteral("session-1")},
        {QStringLiteral("prompt_id"), QStringLiteral("prompt-1")},
    };
    const QByteArray hookPayload = QJsonDocument(hookInput).toJson(QJsonDocument::Compact);
    QCOMPARE(hookProcess.write(hookPayload), hookPayload.size());
    hookProcess.closeWriteChannel();
    QVERIFY(hookProcess.waitForFinished());
    QCOMPARE(hookProcess.exitStatus(), QProcess::NormalExit);

    QFile traceFile(tracePath);
    QVERIFY(traceFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QJsonObject firstTraceRecord = QJsonDocument::fromJson(traceFile.readLine()).object();
    QCOMPARE(firstTraceRecord.value(QStringLiteral("phase")).toString(), QStringLiteral("received"));
    QCOMPARE(firstTraceRecord.value(QStringLiteral("event")).toString(), QStringLiteral("Stop"));
    QCOMPARE(firstTraceRecord.value(QStringLiteral("status")).toString(), QStringLiteral("none"));
    QCOMPARE(firstTraceRecord.value(QStringLiteral("session_id")).toString(), QStringLiteral("session-1"));
    QCOMPARE(firstTraceRecord.value(QStringLiteral("prompt_id")).toString(), QStringLiteral("prompt-1"));
}

void AgentHooksTest::testOpenCodeLauncherHookInstallation_data()
{
    QTest::addColumn<QString>("disabledVariable");
    QTest::addColumn<bool>("expectPluginInstalled");

    QTest::newRow("enabled") << QString() << true;
    QTest::newRow("kmux-disabled") << QStringLiteral("KMUX_OPENCODE_HOOKS_DISABLED") << false;
}

void AgentHooksTest::testOpenCodeLauncherHookInstallation()
{
    QFETCH(QString, disabledVariable);
    QFETCH(bool, expectPluginInstalled);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString binDir = temporaryDir.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDir));

    const QString opencodePath = QDir(binDir).filePath(QStringLiteral("opencode"));
    QFile opencode(opencodePath);
    QVERIFY(opencode.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script = QByteArrayLiteral("#!/bin/sh\nprintf 'real-opencode:%s:%s\\n' \"$KMUX_OPENCODE_PID\" \"$KMUX_PROJECT_STATUS_EXECUTABLE\"\n");
    QCOMPARE(opencode.write(script), script.size());
    opencode.close();
    QVERIFY(QFile::setPermissions(opencodePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    const QString configHome = temporaryDir.filePath(QStringLiteral("opencode-home"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), binDir + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    environment.insert(QStringLiteral("OPENCODE_CONFIG_DIR"), configHome);
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    environment.remove(QStringLiteral("KMUX_OPENCODE_HOOKS_DISABLED"));
    if (!disabledVariable.isEmpty()) {
        environment.insert(disabledVariable, QStringLiteral("1"));
    }

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_OPENCODE_EXECUTABLE));
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    const QByteArray output = process.readAllStandardOutput().trimmed();
    QVERIFY2(output.startsWith(QByteArrayLiteral("real-opencode:")), output.constData());
    const QStringList outputFields = QString::fromUtf8(output).section(QLatin1Char(':'), 1).split(QLatin1Char(':'));
    bool pidIsValid = false;
    outputFields.at(0).toLongLong(&pidIsValid);
    QVERIFY(pidIsValid);
    QVERIFY(!outputFields.at(1).isEmpty());
    QVERIFY(QFileInfo(outputFields.at(1)).isExecutable());

    const QString pluginPath = QDir(QDir(configHome).filePath(QStringLiteral("plugins"))).filePath(QStringLiteral("kmux-opencode.js"));
    QCOMPARE(QFileInfo::exists(pluginPath), expectPluginInstalled);
}

// Writes a stand-in opencode CLI that prints versionOutput for any arguments.
static QString writeOpenCodeStub(const QString &binDir, const QByteArray &versionOutput)
{
    const QString path = QDir(binDir).filePath(QStringLiteral("opencode"));
    QFile stub(path);
    if (!stub.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return {};
    }
    const QByteArray script = QByteArrayLiteral("#!/bin/sh\necho '") + versionOutput + QByteArrayLiteral("'\n");
    if (stub.write(script) != script.size()) {
        return {};
    }
    stub.close();
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        return {};
    }
    return path;
}

static QProcessEnvironment opencodeStubEnvironment(const QString &binDir, const QString &configHome)
{
    // PATH holds only the stub directory, so the version probe cannot find a
    // real opencode installed on the host.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), binDir);
    environment.insert(QStringLiteral("OPENCODE_CONFIG_DIR"), configHome);
    environment.remove(QStringLiteral("KMUX_OPENCODE_HOOKS_DISABLED"));
    return environment;
}

void AgentHooksTest::testOpenCodePluginApiSelection_data()
{
    QTest::addColumn<QByteArray>("versionOutput");
    QTest::addColumn<int>("expectedApiVersion");
    QTest::addColumn<bool>("expectWarning");

    QTest::newRow("v1") << QByteArray("1.18.32") << 1 << false;
    QTest::newRow("v2") << QByteArray("2.0.0") << 2 << false;
    QTest::newRow("unsupported-major") << QByteArray("3.0.0") << 1 << true;
    QTest::newRow("unparseable") << QByteArray("opencode build") << 1 << true;
}

void AgentHooksTest::testOpenCodePluginApiSelection()
{
    QFETCH(QByteArray, versionOutput);
    QFETCH(int, expectedApiVersion);
    QFETCH(bool, expectWarning);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString binDir = temporaryDir.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDir));
    QVERIFY(!writeOpenCodeStub(binDir, versionOutput).isEmpty());

    const QString configHome = temporaryDir.filePath(QStringLiteral("opencode-home"));
    const QString pluginPath = QDir(QDir(configHome).filePath(QStringLiteral("plugins"))).filePath(QStringLiteral("kmux-opencode.js"));

    QProcess process;
    process.setProcessEnvironment(opencodeStubEnvironment(binDir, configHome));
    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                  {QStringLiteral("--opencode-home"), configHome, QStringLiteral("install"), QStringLiteral("opencode"), QStringLiteral("--quiet")});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    const QString stderrText = QString::fromUtf8(process.readAllStandardError());
    QCOMPARE(stderrText.contains(QStringLiteral("installing the v1 plugin")), expectWarning);

    QFile plugin(pluginPath);
    QVERIFY(plugin.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString pluginContent = QString::fromUtf8(plugin.readAll());
    QVERIFY(pluginContent.startsWith(QStringLiteral("// kmux-opencode-plugin v%1\n").arg(expectedApiVersion)));
    QVERIFY(!pluginContent.contains(QStringLiteral("@KMUX_")));
    QVERIFY(pluginContent.contains(QStringLiteral("HELPER_EXECUTABLE: \"KMUX_PROJECT_STATUS_EXECUTABLE\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("import { spawn } from \"node:child_process\"")));
    QVERIFY(!pluginContent.contains(QStringLiteral("Bun.spawn")));
    if (expectedApiVersion == 2) {
        QVERIFY(pluginContent.contains(QStringLiteral("Plugin.define")));
        QVERIFY(pluginContent.contains(QStringLiteral("PROMPT: \"prompt\"")));
        QVERIFY(pluginContent.contains(QStringLiteral("TOOL_EXECUTE_BEFORE: \"execute.before\"")));
        QVERIFY(pluginContent.contains(QStringLiteral("ctx.session.hook(OpenCodeHookName.PROMPT")));
        QVERIFY(!pluginContent.contains(QStringLiteral("chat.message")));
    } else {
        QVERIFY(pluginContent.contains(QStringLiteral("CHAT_MESSAGE: \"chat.message\"")));
        QVERIFY(pluginContent.contains(QStringLiteral("[OpenCodeHook.CHAT_MESSAGE]: async")));
        QVERIFY(!pluginContent.contains(QStringLiteral("Plugin.define")));
    }
}

void AgentHooksTest::testOpenCodeStatusReportsApiMismatch()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString binDir = temporaryDir.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDir));
    const QString configHome = temporaryDir.filePath(QStringLiteral("opencode-home"));
    const QProcessEnvironment environment = opencodeStubEnvironment(binDir, configHome);

    auto runHooks = [&](const QStringList &arguments, QString *standardOutput) {
        QProcess process;
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), arguments);
        if (!process.waitForStarted() || !process.waitForFinished()) {
            return -1;
        }
        if (standardOutput) {
            *standardOutput = QString::fromUtf8(process.readAllStandardOutput());
        }
        return process.exitCode();
    };
    const QStringList installArguments = {QStringLiteral("--opencode-home"), configHome, QStringLiteral("install"), QStringLiteral("opencode"), QStringLiteral("--quiet")};
    const QStringList statusArguments = {QStringLiteral("--opencode-home"), configHome, QStringLiteral("status"), QStringLiteral("opencode")};

    // Install the V2 plugin while opencode 2.x is on PATH.
    QVERIFY(!writeOpenCodeStub(binDir, "2.0.0").isEmpty());
    QCOMPARE(runHooks(installArguments, nullptr), 0);
    QString output;
    QCOMPARE(runHooks(statusArguments, &output), 0);
    QVERIFY(output.contains(QStringLiteral("installed (API v2)")));

    // The same plugin is stale once opencode 1.x is the active CLI.
    QVERIFY(!writeOpenCodeStub(binDir, "1.18.32").isEmpty());
    QCOMPARE(runHooks(statusArguments, &output), 1);
    QVERIFY(output.contains(QStringLiteral("run install")));

    // Reinstalling writes the V1 plugin, which matches again.
    QCOMPARE(runHooks(installArguments, nullptr), 0);
    QCOMPARE(runHooks(statusArguments, &output), 0);
    QVERIFY(output.contains(QStringLiteral("installed (API v1)")));
}

void AgentHooksTest::testOpenCodeLifecycleConfiguration()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString binDir = temporaryDir.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDir));
    QVERIFY(!writeOpenCodeStub(binDir, "1.18.32").isEmpty());
    const QString configHome = temporaryDir.filePath(QStringLiteral("opencode-home"));
    const QString pluginPath = QDir(QDir(configHome).filePath(QStringLiteral("plugins"))).filePath(QStringLiteral("kmux-opencode.js"));

    QProcess process;
    QProcessEnvironment environment = opencodeStubEnvironment(binDir, configHome);
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                  {QStringLiteral("--opencode-home"), configHome, QStringLiteral("install"), QStringLiteral("opencode"), QStringLiteral("--quiet")});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    QFile plugin(pluginPath);
    QVERIFY(plugin.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString pluginContent = QString::fromUtf8(plugin.readAll());
    QVERIFY(pluginContent.startsWith(QStringLiteral("// kmux-opencode-plugin v1\n")));
    // Event and status names live in constants; the switch maps them to Kmux statuses.
    QVERIFY(pluginContent.contains(QStringLiteral("AGENT_PID: \"KMUX_OPENCODE_PID\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("AGENT_PID: \"--agent-pid\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("SESSION_IDLE: \"session.idle\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("PERMISSION_UPDATED: \"permission.updated\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("PERMISSION_ASKED: \"permission.asked\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("QUESTION_ASKED: \"question.asked\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("CHAT_MESSAGE: \"chat.message\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("TOOL_EXECUTE_BEFORE: \"tool.execute.before\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("SESSION_UPDATED: \"session.updated\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("SESSION_TITLE: \"--session-title\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("MESSAGE_UPDATED: \"message.updated\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("SESSION_MODEL: \"--session-model\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("NEEDS_INPUT: \"needsInput\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("reportTitle(properties?.[OpenCodeField.INFO]?.[OpenCodeField.TITLE]);")));
    QVERIFY(pluginContent.contains(QStringLiteral("report(KmuxStatus.IDLE);")));
    QVERIFY(pluginContent.contains(QStringLiteral("report(KmuxStatus.NEEDS_INPUT);")));
    QVERIFY(pluginContent.contains(QStringLiteral("report(KmuxStatus.RUNNING);")));
    QVERIFY(pluginContent.contains(QStringLiteral("case OpenCodeEvent.PERMISSION_UPDATED:")));
    // Hook-mode flags belong to the Claude path; the plugin must not use them.
    QVERIFY(!pluginContent.contains(QStringLiteral("--hook-output")));
    // No raw event-name literals may remain at call sites.
    QVERIFY(!pluginContent.contains(QStringLiteral("report(\"")));
    QVERIFY(!pluginContent.contains(QStringLiteral("case \"session.idle\"")));
    plugin.close();

    QFile installed(pluginPath);
    QVERIFY(installed.open(QIODevice::ReadOnly));
    const QByteArray firstInstall = installed.readAll();
    installed.close();
    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                  {QStringLiteral("--opencode-home"), configHome, QStringLiteral("install"), QStringLiteral("opencode"), QStringLiteral("--quiet")});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 0);
    QFile reinstalled(pluginPath);
    QVERIFY(reinstalled.open(QIODevice::ReadOnly));
    QCOMPARE(reinstalled.readAll(), firstInstall);
    reinstalled.close();

    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                  {QStringLiteral("--opencode-home"), configHome, QStringLiteral("status"), QStringLiteral("opencode")});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 0);

    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                  {QStringLiteral("--opencode-home"), configHome, QStringLiteral("uninstall"), QStringLiteral("opencode")});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    QVERIFY(!QFileInfo::exists(pluginPath));

    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                  {QStringLiteral("--opencode-home"), configHome, QStringLiteral("status"), QStringLiteral("opencode")});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 1);

    QFile userPlugin(QDir(QDir(configHome).filePath(QStringLiteral("plugins"))).filePath(QStringLiteral("user.js")));
    QVERIFY(QDir(QDir(configHome).filePath(QStringLiteral("plugins"))).mkpath(QStringLiteral(".")));
    QVERIFY(userPlugin.open(QIODevice::WriteOnly));
    userPlugin.write("export default async () => ({});\n");
    userPlugin.close();
    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                  {QStringLiteral("--opencode-home"), configHome, QStringLiteral("uninstall"), QStringLiteral("opencode")});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(QFileInfo::exists(userPlugin.fileName()));
}



void AgentHooksTest::testClaudeUsageLimitWrapUp_data()
{
    QTest::addColumn<QJsonArray>("transcriptRows");
    QTest::addColumn<QJsonObject>("payloadFields");
    QTest::addColumn<QString>("expectedStatus");

    const QJsonObject prompt{
        {QStringLiteral("type"), QStringLiteral("user")},
        {QStringLiteral("promptId"), QStringLiteral("current-prompt")},
        {QStringLiteral("message"), QJsonObject{{QStringLiteral("content"), QStringLiteral("Continue working")}}},
    };
    const QJsonObject wrapUp{
        {QStringLiteral("type"), QStringLiteral("user")},
        {QStringLiteral("promptId"), QStringLiteral("current-prompt")},
        {QStringLiteral("isMeta"), true},
        {QStringLiteral("turnCompanion"), true},
        {QStringLiteral("usageLimitNote"), QStringLiteral("wrap_up")},
    };
    const QJsonObject assistant{{QStringLiteral("type"), QStringLiteral("assistant")}};
    const QJsonObject payload{{QStringLiteral("prompt_id"), QStringLiteral("current-prompt")}};
    const QString rateLimited = QStringLiteral("rateLimited");
    const QString none = QStringLiteral("none");

    QTest::newRow("graceful-stop") << QJsonArray{prompt, wrapUp, assistant} << payload << rateLimited;
    QTest::newRow("background-agents") << QJsonArray{prompt, wrapUp, assistant}
                                       << QJsonObject{{QStringLiteral("prompt_id"), QStringLiteral("current-prompt")},
                                                      {QStringLiteral("background_tasks"),
                                                       QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("subagent")},
                                                                              {QStringLiteral("status"), QStringLiteral("running")}}}}}
                                       << rateLimited;
    QTest::newRow("ordinary-stop") << QJsonArray{prompt, assistant} << payload << none;
    QTest::newRow("missing-transcript") << QJsonArray{} << payload << none;
    QTest::newRow("old-prompt") << QJsonArray{wrapUp, assistant} << QJsonObject{{QStringLiteral("prompt_id"), QStringLiteral("next-prompt")}} << none;
    QTest::newRow("new-prompt-without-hook-id") << QJsonArray{wrapUp, prompt, assistant} << QJsonObject{} << none;
    QTest::newRow("wrap-up-without-hook-id") << QJsonArray{prompt, wrapUp, assistant} << QJsonObject{} << rateLimited;

    QJsonObject note = wrapUp;
    note.insert(QStringLiteral("usageLimitNote"), QStringLiteral("release"));
    QTest::newRow("limit-released") << QJsonArray{prompt, wrapUp, note, assistant} << payload << none;
    note.insert(QStringLiteral("usageLimitNote"), QStringLiteral("near_limit"));
    QTest::newRow("near-limit") << QJsonArray{prompt, note, assistant} << payload << none;
    note = wrapUp;
    note.insert(QStringLiteral("isSidechain"), true);
    QTest::newRow("sidechain-note") << QJsonArray{prompt, note, assistant} << payload << none;
    note = wrapUp;
    note.insert(QStringLiteral("isMeta"), false);
    QTest::newRow("user-text-is-not-a-limit-signal") << QJsonArray{prompt, note, assistant} << payload << none;
    note = wrapUp;
    note.insert(QStringLiteral("sessionId"), QStringLiteral("other-session"));
    QTest::newRow("other-session") << QJsonArray{prompt, note, assistant}
                                   << QJsonObject{{QStringLiteral("prompt_id"), QStringLiteral("current-prompt")},
                                                  {QStringLiteral("session_id"), QStringLiteral("current-session")}}
                                   << none;
    QTest::newRow("subagent-stop") << QJsonArray{prompt, wrapUp, assistant}
                                   << QJsonObject{{QStringLiteral("prompt_id"), QStringLiteral("current-prompt")},
                                                  {QStringLiteral("agent_id"), QStringLiteral("subagent")}}
                                   << none;
}

void AgentHooksTest::testClaudeUsageLimitWrapUp()
{
    QFETCH(QJsonArray, transcriptRows);
    QFETCH(QJsonObject, payloadFields);
    QFETCH(QString, expectedStatus);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString transcriptPath = temporaryDir.filePath(QStringLiteral("transcript.jsonl"));
    if (!transcriptRows.isEmpty()) {
        QFile transcript(transcriptPath);
        QVERIFY(transcript.open(QIODevice::WriteOnly));
        // Real transcripts grow to tens of megabytes; only their tail is relevant.
        const QByteArray prefix =
            QJsonDocument(QJsonObject{{QStringLiteral("padding"), QString(1024 * 1024, QLatin1Char('x'))}}).toJson(QJsonDocument::Compact);
        QCOMPARE(transcript.write(prefix + '\n'), prefix.size() + 1);
        for (const QJsonValue &row : transcriptRows) {
            const QByteArray line = QJsonDocument(row.toObject()).toJson(QJsonDocument::Compact) + '\n';
            QCOMPARE(transcript.write(line), line.size());
        }
    }
    payloadFields.insert(QStringLiteral("transcript_path"), transcriptPath);
    const QString tracePath = temporaryDir.filePath(QStringLiteral("trace.jsonl"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("KMUX_AGENT_HOOK_LOG"), tracePath);
    environment.remove(QStringLiteral("KMUX_DBUS_SERVICE"));
    environment.remove(QStringLiteral("KMUX_DBUS_SESSION"));

    const QStringList arguments{QStringLiteral("--hook-output"),
                                QStringLiteral("--agent"),
                                QStringLiteral("claude"),
                                QStringLiteral("--event"),
                                QStringLiteral("Stop"),
                                QStringLiteral("--claude-stop")};
    const QByteArray payload = QJsonDocument(payloadFields).toJson(QJsonDocument::Compact);

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_PROJECT_STATUS_EXECUTABLE), arguments);
    QVERIFY(process.waitForStarted());
    QCOMPARE(process.write(payload), payload.size());
    process.closeWriteChannel();
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);

    QFile trace(tracePath);
    QVERIFY(trace.open(QIODevice::ReadOnly));
    const QJsonObject record = QJsonDocument::fromJson(trace.readLine()).object();
    QCOMPARE(record.value(QStringLiteral("phase")).toString(), QStringLiteral("received"));
    QCOMPARE(record.value(QStringLiteral("status")).toString(), expectedStatus);
    QCOMPARE(record.value(QStringLiteral("event")).toString(), QStringLiteral("Stop"));
}




void AgentHooksTest::testHookOperationsWaitForTransactionLock_data()
{
    QTest::addColumn<QString>("agent");
    QTest::addColumn<QString>("homeOption");
    QTest::addColumn<QString>("settingsFile");

    QTest::newRow("claude") << QStringLiteral("claude") << QStringLiteral("--claude-home") << QStringLiteral("settings.json");
}

void AgentHooksTest::testHookOperationsWaitForTransactionLock()
{
    QFETCH(QString, agent);
    QFETCH(QString, homeOption);
    QFETCH(QString, settingsFile);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString configHome = temporaryDir.filePath(QStringLiteral("agent-home"));
    const QString lockPath = QFileInfo(configHome).absoluteFilePath() + QStringLiteral(".kmux-%1-hooks.lock").arg(agent);
    QLockFile lock(lockPath);
    QVERIFY(lock.tryLock());

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), {homeOption, configHome, QStringLiteral("install"), agent, QStringLiteral("--quiet")});
    QVERIFY(process.waitForStarted());
    QVERIFY(!process.waitForFinished(200));

    QVERIFY(QDir().mkpath(configHome));
    QFile settings(QDir(configHome).filePath(settingsFile));
    QVERIFY(settings.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray externalSettings = QByteArrayLiteral("{\"externalEdit\":true}\n");
    QCOMPARE(settings.write(externalSettings), externalSettings.size());
    settings.close();

    lock.unlock();
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    QVERIFY(settings.open(QIODevice::ReadOnly | QIODevice::Text));
    const QJsonObject installedSettings = QJsonDocument::fromJson(settings.readAll()).object();
    QVERIFY(installedSettings.value(QStringLiteral("externalEdit")).toBool());
    QVERIFY(!installedSettings.value(QStringLiteral("hooks")).toObject().isEmpty());
}

void AgentHooksTest::testUnrelatedHooksArePreserved_data()
{
    QTest::addColumn<QString>("agent");
    QTest::addColumn<QString>("homeOption");
    QTest::addColumn<QString>("settingsFile");

    QTest::newRow("claude") << QStringLiteral("claude") << QStringLiteral("--claude-home") << QStringLiteral("settings.json");
}

void AgentHooksTest::testUnrelatedHooksArePreserved()
{
    QFETCH(QString, agent);
    QFETCH(QString, homeOption);
    QFETCH(QString, settingsFile);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString configHome = temporaryDir.filePath(QStringLiteral("agent-home"));
    QVERIFY(QDir().mkpath(configHome));

    QJsonArray userHooks;
    const QStringList userCommands = {
        QStringLiteral("/opt/custom/kmux/hooks/%1-user.sh").arg(agent),
        QStringLiteral("/opt/custom/kmux-%1-hook-wrapper").arg(agent),
        QStringLiteral("/opt/custom/konsole-project-status-wrapper"),
        QStringLiteral("/opt/custom/kmux-project-status-wrapper"),
    };
    for (const QString &command : userCommands) {
        userHooks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("command")}, {QStringLiteral("command"), command}});
    }

    QJsonObject userGroup;
    userGroup.insert(QStringLiteral("hooks"), userHooks);
    QJsonArray sessionStartGroups;
    sessionStartGroups.append(userGroup);
    QJsonObject hooks;
    hooks.insert(QStringLiteral("SessionStart"), sessionStartGroups);
    QJsonObject originalSettings;
    originalSettings.insert(QStringLiteral("hooks"), hooks);

    QFile settings(QDir(configHome).filePath(settingsFile));
    QVERIFY(settings.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray encodedSettings = QJsonDocument(originalSettings).toJson(QJsonDocument::Indented);
    QCOMPARE(settings.write(encodedSettings), encodedSettings.size());
    settings.close();

    const auto runHooks = [&temporaryDir, &homeOption, &configHome, &agent](const QString &command) {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), {homeOption, configHome, command, agent, QStringLiteral("--quiet")});
        if (!process.waitForStarted() || !process.waitForFinished()) {
            return QStringLiteral("Could not run kmux-agent-hooks: %1").arg(process.errorString());
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return QString::fromUtf8(process.readAllStandardError());
        }
        return QString();
    };

    QString error = runHooks(QStringLiteral("install"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = runHooks(QStringLiteral("uninstall"));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QVERIFY(settings.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(settings.readAll()).object(), originalSettings);
}

void AgentHooksTest::testUninstallLeavesNoEmptyHooks_data()
{
    QTest::addColumn<QString>("agent");
    QTest::addColumn<QString>("homeOption");
    QTest::addColumn<QString>("settingsFile");
    QTest::addColumn<QByteArray>("originalSettings");

    QTest::newRow("claude without hooks") << QStringLiteral("claude") << QStringLiteral("--claude-home") << QStringLiteral("settings.json")
                                          << QByteArrayLiteral(R"({"model": "opus"})");
}

void AgentHooksTest::testUninstallLeavesNoEmptyHooks()
{
    QFETCH(QString, agent);
    QFETCH(QString, homeOption);
    QFETCH(QString, settingsFile);
    QFETCH(QByteArray, originalSettings);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString configHome = temporaryDir.filePath(QStringLiteral("agent-home"));
    QVERIFY(QDir().mkpath(configHome));
    const QString settingsPath = QDir(configHome).filePath(settingsFile);
    if (!originalSettings.isEmpty()) {
        QFile settings(settingsPath);
        QVERIFY(settings.open(QIODevice::WriteOnly | QIODevice::Text));
        QCOMPARE(settings.write(originalSettings), originalSettings.size());
    }

    const auto runHooks = [&temporaryDir, &homeOption, &configHome, &agent](const QString &command) {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), {homeOption, configHome, command, agent, QStringLiteral("--quiet")});
        if (!process.waitForStarted() || !process.waitForFinished()) {
            return QStringLiteral("Could not run kmux-agent-hooks: %1").arg(process.errorString());
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return QString::fromUtf8(process.readAllStandardError());
        }
        return QString();
    };

    QString error = runHooks(QStringLiteral("install"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo::exists(settingsPath));
    error = runHooks(QStringLiteral("uninstall"));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    if (originalSettings.isEmpty()) {
        QVERIFY(!QFileInfo::exists(settingsPath));
        return;
    }
    QFile settings(settingsPath);
    QVERIFY(settings.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(settings.readAll()).object(), QJsonDocument::fromJson(originalSettings).object());
}

void AgentHooksTest::testHomeScopedScripts_data()
{
    QTest::addColumn<QString>("agent");
    QTest::addColumn<QString>("homeOption");
    QTest::addColumn<QString>("settingsFile");
    QTest::addColumn<int>("handlerCount");

    QTest::newRow("claude") << QStringLiteral("claude") << QStringLiteral("--claude-home") << QStringLiteral("settings.json") << 2;
}

void AgentHooksTest::testHomeScopedScripts()
{
    QFETCH(QString, agent);
    QFETCH(QString, homeOption);
    QFETCH(QString, settingsFile);
    QFETCH(int, handlerCount);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString dataHome = temporaryDir.filePath(QStringLiteral("data"));
    const QString firstHome = temporaryDir.filePath(QStringLiteral("home-a"));
    const QString secondHome = temporaryDir.filePath(QStringLiteral("home-b"));

    const auto runHooks = [&dataHome, &homeOption, &agent](const QString &home, const QString &command) {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_DATA_HOME"), dataHome);
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), {homeOption, home, command, agent, QStringLiteral("--quiet")});
        if (!process.waitForStarted() || !process.waitForFinished()) {
            return qMakePair(-1, QStringLiteral("Could not run kmux-agent-hooks: %1").arg(process.errorString()));
        }
        const QString output = QString::fromUtf8(process.readAllStandardOutput()) + QString::fromUtf8(process.readAllStandardError());
        return qMakePair(process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1, output);
    };
    const auto hookCommands = [&settingsFile](const QString &home) {
        QFile file(QDir(home).filePath(settingsFile));
        if (!file.open(QIODevice::ReadOnly)) {
            return QStringList();
        }

        QStringList commands;
        const QJsonObject hooks = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("hooks")).toObject();
        for (const QJsonValue &eventValue : hooks) {
            for (const QJsonValue &groupValue : eventValue.toArray()) {
                for (const QJsonValue &hookValue : groupValue.toObject().value(QStringLiteral("hooks")).toArray()) {
                    const QString command = hookValue.toObject().value(QStringLiteral("command")).toString();
                    if (command.contains(QStringLiteral("/kmux/hooks/"))) {
                        commands.append(command);
                    }
                }
            }
        }
        return commands;
    };

    auto result = runHooks(firstHome, QStringLiteral("install"));
    QVERIFY2(result.first == 0, qPrintable(result.second));
    result = runHooks(secondHome, QStringLiteral("install"));
    QVERIFY2(result.first == 0, qPrintable(result.second));

    const QStringList firstCommands = hookCommands(firstHome);
    const QStringList secondCommands = hookCommands(secondHome);
    QCOMPARE(firstCommands.size(), handlerCount);
    QCOMPARE(secondCommands.size(), handlerCount);
    QCOMPARE(QSet<QString>(firstCommands.begin(), firstCommands.end()).size(), handlerCount);
    QCOMPARE(QSet<QString>(secondCommands.begin(), secondCommands.end()).size(), handlerCount);
    QVERIFY(QFileInfo(firstCommands.constFirst()).absolutePath() != QFileInfo(secondCommands.constFirst()).absolutePath());
    const QString scopedRoot = QDir(dataHome).filePath(QStringLiteral("kmux/hooks/%1-").arg(agent));
    for (const QString &command : firstCommands) {
        QVERIFY(command.startsWith(scopedRoot));
        QVERIFY2(QFileInfo(command).isExecutable(), qPrintable(command));
    }
    for (const QString &command : secondCommands) {
        QVERIFY(command.startsWith(scopedRoot));
        QVERIFY2(QFileInfo(command).isExecutable(), qPrintable(command));
    }

    QFile hookScript(secondCommands.constFirst());
    QVERIFY(hookScript.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString hookScriptText = QString::fromUtf8(hookScript.readAll());
    QVERIFY(hookScriptText.contains(QStringLiteral("--claude-stop")));

    result = runHooks(firstHome, QStringLiteral("uninstall"));
    QVERIFY2(result.first == 0, qPrintable(result.second));
    for (const QString &command : firstCommands) {
        QVERIFY(!QFileInfo::exists(command));
    }
    for (const QString &command : secondCommands) {
        QVERIFY2(QFileInfo(command).isExecutable(), qPrintable(command));
    }

    result = runHooks(secondHome, QStringLiteral("status"));
    QVERIFY2(result.first == 0, qPrintable(result.second));
    QVERIFY(result.second.contains(QStringLiteral("%1/%1 executable").arg(handlerCount)));

    const QString brokenHandler = secondCommands.constFirst();
    QVERIFY(QFile::setPermissions(brokenHandler, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    result = runHooks(secondHome, QStringLiteral("status"));
    QVERIFY(result.first != 0);
    QVERIFY(result.second.contains(QStringLiteral("%1/%2 executable").arg(handlerCount - 1).arg(handlerCount)));
    QVERIFY(result.second.contains(QStringLiteral("Invalid hook script: %1").arg(brokenHandler)));

    QVERIFY(QFile::remove(brokenHandler));
    result = runHooks(secondHome, QStringLiteral("status"));
    QVERIFY(result.first != 0);
    QVERIFY(result.second.contains(QStringLiteral("Invalid hook script: %1").arg(brokenHandler)));
}

void AgentHooksTest::testHookCommandsQuoteScriptPaths_data()
{
    QTest::addColumn<QString>("agent");
    QTest::addColumn<QString>("homeOption");
    QTest::addColumn<QString>("settingsFile");
    QTest::addColumn<int>("handlerCount");

    QTest::newRow("claude") << QStringLiteral("claude") << QStringLiteral("--claude-home") << QStringLiteral("settings.json") << 2;
}

void AgentHooksTest::testHookCommandsQuoteScriptPaths()
{
    QFETCH(QString, agent);
    QFETCH(QString, homeOption);
    QFETCH(QString, settingsFile);
    QFETCH(int, handlerCount);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString dataHome = temporaryDir.filePath(QStringLiteral("agent data's home"));
    const QString configHome = temporaryDir.filePath(QStringLiteral("agent-home"));

    const auto runHooks = [&dataHome, &homeOption, &configHome, &agent](const QString &command) {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_DATA_HOME"), dataHome);
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), {homeOption, configHome, command, agent});
        if (!process.waitForStarted() || !process.waitForFinished()) {
            return qMakePair(-1, QStringLiteral("Could not run kmux-agent-hooks: %1").arg(process.errorString()));
        }
        const QString output = QString::fromUtf8(process.readAllStandardOutput()) + QString::fromUtf8(process.readAllStandardError());
        return qMakePair(process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1, output);
    };
    const auto hookCommands = [&configHome, &settingsFile]() {
        QFile file(QDir(configHome).filePath(settingsFile));
        if (!file.open(QIODevice::ReadOnly)) {
            return QStringList();
        }

        QStringList commands;
        const QJsonObject hooks = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("hooks")).toObject();
        for (const QJsonValue &eventValue : hooks) {
            for (const QJsonValue &groupValue : eventValue.toArray()) {
                for (const QJsonValue &hookValue : groupValue.toObject().value(QStringLiteral("hooks")).toArray()) {
                    const QString command = hookValue.toObject().value(QStringLiteral("command")).toString();
                    if (command.contains(QStringLiteral("/kmux/hooks/"))) {
                        commands.append(command);
                    }
                }
            }
        }
        return commands;
    };

    auto result = runHooks(QStringLiteral("install"));
    QVERIFY2(result.first == 0, qPrintable(result.second));
    // A repeated installation must recognize the quoted commands as its own.
    result = runHooks(QStringLiteral("install"));
    QVERIFY2(result.first == 0, qPrintable(result.second));

    const QStringList commands = hookCommands();
    QCOMPARE(commands.size(), handlerCount);
    for (const QString &command : commands) {
        QVERIFY2(command.startsWith(QLatin1Char('\'')), qPrintable(command));
    }

    // Agents run hook commands through a shell. Without the Kmux session
    // variables the helper only answers the hook.
    QProcessEnvironment hookEnvironment = QProcessEnvironment::systemEnvironment();
    hookEnvironment.remove(QStringLiteral("KMUX_DBUS_SERVICE"));
    hookEnvironment.remove(QStringLiteral("KMUX_DBUS_SESSION"));
    hookEnvironment.remove(QStringLiteral("KMUX_AGENT_HOOK_LOG"));
    QProcess hook;
    hook.setProcessEnvironment(hookEnvironment);
    hook.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), commands.constFirst()});
    QVERIFY(hook.waitForStarted());
    hook.write("{}\n");
    hook.closeWriteChannel();
    QVERIFY(hook.waitForFinished(5000));
    QCOMPARE(hook.exitStatus(), QProcess::NormalExit);
    QVERIFY2(hook.exitCode() == 0, hook.readAllStandardError().constData());
    QCOMPARE(hook.readAllStandardOutput().trimmed(), QByteArrayLiteral("{}"));

    result = runHooks(QStringLiteral("status"));
    QVERIFY2(result.first == 0, qPrintable(result.second));
    QVERIFY2(result.second.contains(QStringLiteral("Hook scripts: %1/%1 executable").arg(handlerCount)), qPrintable(result.second));

    result = runHooks(QStringLiteral("uninstall"));
    QVERIFY2(result.first == 0, qPrintable(result.second));
    QVERIFY(hookCommands().isEmpty());
}

void AgentHooksTest::testReinstallReplacesUnquotedHookCommands_data()
{
    QTest::addColumn<QString>("agent");
    QTest::addColumn<QString>("homeOption");
    QTest::addColumn<QString>("settingsFile");
    QTest::addColumn<int>("handlerCount");
    QTest::addColumn<QString>("dataDirectory");

    const QStringList dataDirectories = {QStringLiteral("data home"), QStringLiteral("data home's"), QStringLiteral("data 'quoted' home")};
    for (const QString &dataDirectory : dataDirectories) {
        QTest::addRow("claude, %s", qPrintable(dataDirectory))
            << QStringLiteral("claude") << QStringLiteral("--claude-home") << QStringLiteral("settings.json") << 2 << dataDirectory;
    }
}

void AgentHooksTest::testReinstallReplacesUnquotedHookCommands()
{
    QFETCH(QString, agent);
    QFETCH(QString, homeOption);
    QFETCH(QString, settingsFile);
    QFETCH(int, handlerCount);
    QFETCH(QString, dataDirectory);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString dataHome = temporaryDir.filePath(dataDirectory);
    const QString configHome = temporaryDir.filePath(QStringLiteral("agent-home"));
    const QString settingsPath = QDir(configHome).filePath(settingsFile);

    const auto runHooks = [&dataHome, &homeOption, &configHome, &agent](const QString &command) {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_DATA_HOME"), dataHome);
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), {homeOption, configHome, command, agent, QStringLiteral("--quiet")});
        if (!process.waitForStarted() || !process.waitForFinished()) {
            return QStringLiteral("Could not run kmux-agent-hooks: %1").arg(process.errorString());
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return QString::fromUtf8(process.readAllStandardError());
        }
        return QString();
    };
    const auto hookCommands = [&settingsPath]() {
        QFile file(settingsPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return QStringList();
        }

        QStringList commands;
        const QJsonObject hooks = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("hooks")).toObject();
        for (const QJsonValue &eventValue : hooks) {
            for (const QJsonValue &groupValue : eventValue.toArray()) {
                for (const QJsonValue &hookValue : groupValue.toObject().value(QStringLiteral("hooks")).toArray()) {
                    const QString command = hookValue.toObject().value(QStringLiteral("command")).toString();
                    if (command.contains(QStringLiteral("/kmux/hooks/"))) {
                        commands.append(command);
                    }
                }
            }
        }
        return commands;
    };

    QString error = runHooks(QStringLiteral("install"));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // Earlier versions wrote the script path as it is, without shell quotes.
    QFile settings(settingsPath);
    QVERIFY(settings.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(settings.readAll()).object();
    settings.close();
    int legacyCommands = 0;
    QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();
    for (const QString &eventName : hooks.keys()) {
        QJsonArray groups = hooks.value(eventName).toArray();
        for (qsizetype groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            QJsonObject group = groups.at(groupIndex).toObject();
            QJsonArray handlers = group.value(QStringLiteral("hooks")).toArray();
            for (qsizetype handlerIndex = 0; handlerIndex < handlers.size(); ++handlerIndex) {
                QJsonObject handler = handlers.at(handlerIndex).toObject();
                QString command = handler.value(QStringLiteral("command")).toString();
                if (command.startsWith(QLatin1Char('\''))) {
                    command = command.mid(1, command.size() - 2).replace(QStringLiteral("'\"'\"'"), QStringLiteral("'"));
                    handler.insert(QStringLiteral("command"), command);
                    handlers[handlerIndex] = handler;
                    ++legacyCommands;
                }
            }
            group.insert(QStringLiteral("hooks"), handlers);
            groups[groupIndex] = group;
        }
        hooks.insert(eventName, groups);
    }
    root.insert(QStringLiteral("hooks"), hooks);
    QCOMPARE(legacyCommands, handlerCount);
    QVERIFY(settings.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray legacySettings = QJsonDocument(root).toJson();
    QCOMPARE(settings.write(legacySettings), legacySettings.size());
    settings.close();

    error = runHooks(QStringLiteral("install"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QStringList commands = hookCommands();
    QCOMPARE(commands.size(), handlerCount);
    for (const QString &command : commands) {
        QVERIFY2(command.startsWith(QLatin1Char('\'')), qPrintable(command));
    }

    error = runHooks(QStringLiteral("uninstall"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(hookCommands().isEmpty());
}

QTEST_GUILESS_MAIN(AgentHooksTest)

#include "AgentHooksTest.moc"
