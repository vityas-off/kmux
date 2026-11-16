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
    void testCodexFeatureToml_data();
    void testCodexFeatureToml();
    void testCodexRepairsPreviousDottedInstall();
    void testCodexLauncherHookInstallation_data();
    void testCodexLauncherHookInstallation();
    void testLauncherEmbeddedServerArgument_data();
    void testLauncherEmbeddedServerArgument();
    void testCodexLauncherSkipsSelfSymlink();
    void testCodexLauncherRunsScriptWithoutShebang_data();
    void testCodexLauncherRunsScriptWithoutShebang();
    void testCodexLauncherReportsMissingAgent();
    void testCodexCommandUsesTransparentLauncher();
    void testCodexTrustHashesMatchCurrentIdentity();
    void testClaudeCommandUsesTransparentLauncher();
    void testOpenCodeLauncherHookInstallation_data();
    void testOpenCodeLauncherHookInstallation();
    void testOpenCodeLifecycleConfiguration();
    void testOpenCodePluginApiSelection_data();
    void testOpenCodePluginApiSelection();
    void testOpenCodeStatusReportsApiMismatch();
    void testCodexPermissionRequestUsesConfiguredReviewer();
    void testClaudeLifecycleConfiguration();
    void testClaudeUsageLimitWrapUp_data();
    void testClaudeUsageLimitWrapUp();
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

void AgentHooksTest::testCodexLauncherHookInstallation_data()
{
    QTest::addColumn<QString>("disabledVariable");
    QTest::addColumn<bool>("expectHooksInstalled");

    QTest::newRow("enabled") << QString() << true;
    QTest::newRow("kmux-disabled") << QStringLiteral("KMUX_CODEX_HOOKS_DISABLED") << false;
    QTest::newRow("konsole-compatibility-disabled") << QStringLiteral("KONSOLE_CODEX_HOOKS_DISABLED") << false;
}

void AgentHooksTest::testCodexLauncherHookInstallation()
{
    QFETCH(QString, disabledVariable);
    QFETCH(bool, expectHooksInstalled);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString binDir = temporaryDir.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDir));

    const QString codexPath = QDir(binDir).filePath(QStringLiteral("codex"));
    QFile codex(codexPath);
    QVERIFY(codex.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script = QByteArrayLiteral("#!/bin/sh\nprintf '%s\\n' \"$KMUX_CODEX_PID\"\n");
    QCOMPARE(codex.write(script), script.size());
    codex.close();
    QVERIFY(QFile::setPermissions(codexPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    const QString configHome = temporaryDir.filePath(QStringLiteral("codex-home"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), binDir + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    environment.insert(QStringLiteral("CODEX_HOME"), configHome);
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    environment.remove(QStringLiteral("KMUX_CODEX_HOOKS_DISABLED"));
    environment.remove(QStringLiteral("KONSOLE_CODEX_HOOKS_DISABLED"));
    if (!disabledVariable.isEmpty()) {
        environment.insert(disabledVariable, QStringLiteral("1"));
    }

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_CODEX_EXECUTABLE));
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    bool pidIsValid = false;
    process.readAllStandardOutput().trimmed().toLongLong(&pidIsValid);
    QVERIFY(pidIsValid);
    QCOMPARE(QFileInfo::exists(configHome), expectHooksInstalled);
}

void AgentHooksTest::testLauncherEmbeddedServerArgument_data()
{
    QTest::addColumn<QString>("agentName");
    QTest::addColumn<QString>("launcher");
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("disabledVariable");
    QTest::addColumn<QStringList>("expectedArguments");

    const QString codex = QStringLiteral("codex");
    const QString codexLauncher = QStringLiteral(KMUX_CODEX_EXECUTABLE);
    const QString embeddedServer = QStringLiteral("--no-daemon");
    const QString sessionId = QStringLiteral("01a0ddcf-49a8-74e0-91d1-6e0a1007bb7e");

    QTest::newRow("interactive") << codex << codexLauncher << QStringList() << QString() << QStringList{embeddedServer};
    QTest::newRow("prompt") << codex << codexLauncher << QStringList{QStringLiteral("fix the build")} << QString()
                            << QStringList{embeddedServer, QStringLiteral("fix the build")};
    QTest::newRow("resume") << codex << codexLauncher << QStringList{QStringLiteral("resume"), sessionId} << QString()
                            << QStringList{embeddedServer, QStringLiteral("resume"), sessionId};
    QTest::newRow("explicit") << codex << codexLauncher << QStringList{embeddedServer} << QString() << QStringList{embeddedServer};
    QTest::newRow("remote") << codex << codexLauncher << QStringList{QStringLiteral("--remote"), QStringLiteral("unix://")} << QString()
                            << QStringList{QStringLiteral("--remote"), QStringLiteral("unix://")};
    QTest::newRow("remote-assignment") << codex << codexLauncher << QStringList{QStringLiteral("--remote=unix://")} << QString()
                                       << QStringList{QStringLiteral("--remote=unix://")};
    QTest::newRow("agents") << codex << codexLauncher << QStringList{QStringLiteral("agents")} << QString() << QStringList{QStringLiteral("agents")};
    QTest::newRow("hooks-disabled") << codex << codexLauncher << QStringList() << QStringLiteral("KMUX_CODEX_HOOKS_DISABLED") << QStringList();
    QTest::newRow("claude") << QStringLiteral("claude") << QStringLiteral(KMUX_CLAUDE_EXECUTABLE) << QStringList() << QString() << QStringList();
}

void AgentHooksTest::testLauncherEmbeddedServerArgument()
{
    QFETCH(QString, agentName);
    QFETCH(QString, launcher);
    QFETCH(QStringList, arguments);
    QFETCH(QString, disabledVariable);
    QFETCH(QStringList, expectedArguments);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString binDir = temporaryDir.filePath(QStringLiteral("bin"));
    const QString homeDir = temporaryDir.filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(binDir));
    QVERIFY(QDir().mkpath(homeDir));

    const QString agentPath = QDir(binDir).filePath(agentName);
    QFile agent(agentPath);
    QVERIFY(agent.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script = QByteArrayLiteral("#!/bin/sh\nfor argument do printf '%s\\n' \"$argument\"; done\n");
    QCOMPARE(agent.write(script), script.size());
    agent.close();
    QVERIFY(QFile::setPermissions(agentPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), binDir + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    environment.insert(QStringLiteral("HOME"), homeDir);
    environment.insert(QStringLiteral("CODEX_HOME"), temporaryDir.filePath(QStringLiteral("codex-home")));
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    environment.remove(QStringLiteral("KMUX_CODEX_HOOKS_DISABLED"));
    environment.remove(QStringLiteral("KONSOLE_CODEX_HOOKS_DISABLED"));
    environment.remove(QStringLiteral("KMUX_CLAUDE_HOOKS_DISABLED"));
    if (!disabledVariable.isEmpty()) {
        environment.insert(disabledVariable, QStringLiteral("1"));
    }

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(launcher, arguments);
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished(5000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    QCOMPARE(output.split(QLatin1Char('\n'), Qt::SkipEmptyParts), expectedArguments);
}

void AgentHooksTest::testCodexLauncherSkipsSelfSymlink()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString launcherBinDir = temporaryDir.filePath(QStringLiteral("launcher-bin"));
    const QString agentBinDir = temporaryDir.filePath(QStringLiteral("agent-bin"));
    QVERIFY(QDir().mkpath(launcherBinDir));
    QVERIFY(QDir().mkpath(agentBinDir));

    const QString launcherAlias = QDir(launcherBinDir).filePath(QStringLiteral("codex"));
    QVERIFY(QFile::link(QStringLiteral(KMUX_CODEX_EXECUTABLE), launcherAlias));

    const QString agentPath = QDir(agentBinDir).filePath(QStringLiteral("codex"));
    QFile agent(agentPath);
    QVERIFY(agent.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script = QByteArrayLiteral("#!/bin/sh\nprintf 'real-codex:%s\\n' \"$1\"\n");
    QCOMPARE(agent.write(script), script.size());
    agent.close();
    QVERIFY(QFile::setPermissions(agentPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), launcherBinDir + QDir::listSeparator() + agentBinDir);
    environment.insert(QStringLiteral("KMUX_CODEX_HOOKS_DISABLED"), QStringLiteral("1"));

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_CODEX_EXECUTABLE), {QStringLiteral("argument")});
    QVERIFY(process.waitForStarted());
    QVERIFY2(process.waitForFinished(5000), "kmux-codex recursively executed itself");
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    QCOMPARE(process.readAllStandardOutput().trimmed(), QByteArrayLiteral("real-codex:argument"));
}

void AgentHooksTest::testCodexLauncherRunsScriptWithoutShebang_data()
{
    QTest::addColumn<QStringList>("arguments");

    QTest::newRow("no arguments") << QStringList();
    QTest::newRow("shell option and spaces") << QStringList{QStringLiteral("--help"), QStringLiteral("two words")};
}

void AgentHooksTest::testCodexLauncherRunsScriptWithoutShebang()
{
    QFETCH(QStringList, arguments);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString agentBinDir = temporaryDir.filePath(QStringLiteral("agent-bin"));
    QVERIFY(QDir().mkpath(agentBinDir));

    const QString agentPath = QDir(agentBinDir).filePath(QStringLiteral("codex"));
    QFile agent(agentPath);
    QVERIFY(agent.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script = QByteArrayLiteral("for argument do printf '%s\\n' \"$argument\"; done\n");
    QCOMPARE(agent.write(script), script.size());
    agent.close();
    QVERIFY(QFile::setPermissions(agentPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), agentBinDir + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    environment.insert(QStringLiteral("KMUX_CODEX_HOOKS_DISABLED"), QStringLiteral("1"));

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_CODEX_EXECUTABLE), arguments);
    QVERIFY(process.waitForStarted());
    QVERIFY2(process.waitForFinished(5000), "the shell did not run the agent script");
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    QCOMPARE(output.split(QLatin1Char('\n'), Qt::SkipEmptyParts), arguments);
}

void AgentHooksTest::testCodexLauncherReportsMissingAgent()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString launcherBinDir = temporaryDir.filePath(QStringLiteral("launcher-bin"));
    QVERIFY(QDir().mkpath(launcherBinDir));
    QVERIFY(QFile::link(QStringLiteral(KMUX_CODEX_EXECUTABLE), QDir(launcherBinDir).filePath(QStringLiteral("codex"))));

    // The launcher is the only codex in PATH, as inside Kmux without Codex.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), launcherBinDir);
    environment.insert(QStringLiteral("KMUX_CODEX_HOOKS_DISABLED"), QStringLiteral("1"));

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_CODEX_EXECUTABLE), {});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished(5000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 127);
    const QByteArray error = process.readAllStandardError();
    QVERIFY2(error.contains("No such file or directory"), error.constData());
}

void AgentHooksTest::testCodexCommandUsesTransparentLauncher()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString shimDir = temporaryDir.filePath(QStringLiteral("kmux-agent-shims"));
    const QString agentBinDir = temporaryDir.filePath(QStringLiteral("agent-bin"));
    QVERIFY(QDir().mkpath(shimDir));
    QVERIFY(QDir().mkpath(agentBinDir));

    const QString launcherAlias = QDir(shimDir).filePath(QStringLiteral("codex"));
    QVERIFY(QFile::copy(QStringLiteral(KMUX_CODEX_EXECUTABLE), launcherAlias));
    const QString hookInstaller = QDir(shimDir).filePath(QStringLiteral("kmux-agent-hooks"));
    QVERIFY(QFile::copy(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE), hookInstaller));
    const QString projectStatusHelper = QDir(shimDir).filePath(QStringLiteral("kmux-project-status"));
    QVERIFY(QFile::copy(QStringLiteral(KMUX_PROJECT_STATUS_EXECUTABLE), projectStatusHelper));
    QVERIFY(QFileInfo(launcherAlias).isExecutable());
    QVERIFY(QFileInfo(hookInstaller).isExecutable());
    QVERIFY(QFileInfo(projectStatusHelper).isExecutable());

    const QString agentPath = QDir(agentBinDir).filePath(QStringLiteral("codex"));
    QFile agent(agentPath);
    QVERIFY(agent.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script = QByteArrayLiteral("#!/bin/sh\nprintf 'real-codex:%s\\n' \"$KMUX_CODEX_PID\"\n");
    QCOMPARE(agent.write(script), script.size());
    agent.close();
    QVERIFY(QFile::setPermissions(agentPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    const QString configHome = temporaryDir.filePath(QStringLiteral("codex-home"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), shimDir + QDir::listSeparator() + agentBinDir);
    environment.insert(QStringLiteral("CODEX_HOME"), configHome);
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    environment.remove(QStringLiteral("KMUX_CODEX_HOOKS_DISABLED"));
    environment.remove(QStringLiteral("KONSOLE_CODEX_HOOKS_DISABLED"));

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(launcherAlias);
    QVERIFY(process.waitForStarted());
    QVERIFY2(process.waitForFinished(5000), "transparent codex launcher recursively executed itself");
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    QVERIFY(process.readAllStandardOutput().trimmed().startsWith(QByteArrayLiteral("real-codex:")));

    QFile hooksFile(QDir(configHome).filePath(QStringLiteral("hooks.json")));
    QVERIFY(hooksFile.open(QIODevice::ReadOnly));
    const QJsonObject hooks = QJsonDocument::fromJson(hooksFile.readAll()).object().value(QStringLiteral("hooks")).toObject();
    const QString hookCommand = hooks.value(QStringLiteral("UserPromptSubmit"))
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
    QVERIFY(hookProcess.waitForFinished());
    QCOMPARE(hookProcess.exitStatus(), QProcess::NormalExit);

    QFile traceFile(tracePath);
    QVERIFY(traceFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QJsonObject firstTraceRecord = QJsonDocument::fromJson(traceFile.readLine()).object();
    QCOMPARE(firstTraceRecord.value(QStringLiteral("phase")).toString(), QStringLiteral("received"));
    QCOMPARE(firstTraceRecord.value(QStringLiteral("event")).toString(), QStringLiteral("UserPromptSubmit"));
    QCOMPARE(firstTraceRecord.value(QStringLiteral("status")).toString(), QStringLiteral("running"));
}

void AgentHooksTest::testCodexTrustHashesMatchCurrentIdentity()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString configHome = temporaryDir.filePath(QStringLiteral("codex-home"));

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                  {QStringLiteral("--codex-home"), configHome, QStringLiteral("install"), QStringLiteral("codex"), QStringLiteral("--quiet")});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    const QString hooksPath = QDir(configHome).filePath(QStringLiteral("hooks.json"));
    QFile hooksFile(hooksPath);
    QVERIFY(hooksFile.open(QIODevice::ReadOnly));
    const QJsonObject hooks = QJsonDocument::fromJson(hooksFile.readAll()).object().value(QStringLiteral("hooks")).toObject();

    QFile configFile(QDir(configHome).filePath(QStringLiteral("config.toml")));
    QVERIFY(configFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString config = QString::fromUtf8(configFile.readAll());

    const QHash<QString, QString> eventLabels = {
        {QStringLiteral("SessionStart"), QStringLiteral("session_start")},
        {QStringLiteral("UserPromptSubmit"), QStringLiteral("user_prompt_submit")},
        {QStringLiteral("PreToolUse"), QStringLiteral("pre_tool_use")},
        {QStringLiteral("PostToolUse"), QStringLiteral("post_tool_use")},
        {QStringLiteral("PreCompact"), QStringLiteral("pre_compact")},
        {QStringLiteral("PostCompact"), QStringLiteral("post_compact")},
        {QStringLiteral("PermissionRequest"), QStringLiteral("permission_request")},
        {QStringLiteral("Stop"), QStringLiteral("stop")},
        {QStringLiteral("Interrupt"), QStringLiteral("interrupt")},
    };
    const QString keySource = QFileInfo(hooksPath).canonicalFilePath();
    int checkedHashes = 0;
    for (auto event = eventLabels.constBegin(); event != eventLabels.constEnd(); ++event) {
        const QJsonArray groups = hooks.value(event.key()).toArray();
        for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            const QJsonObject group = groups.at(groupIndex).toObject();
            const QJsonArray handlers = group.value(QStringLiteral("hooks")).toArray();
            for (int handlerIndex = 0; handlerIndex < handlers.size(); ++handlerIndex) {
                const QJsonObject configuredHandler = handlers.at(handlerIndex).toObject();
                QJsonObject normalizedHandler;
                normalizedHandler.insert(QStringLiteral("async"), configuredHandler.value(QStringLiteral("async")).toBool(false));
                normalizedHandler.insert(QStringLiteral("command"), configuredHandler.value(QStringLiteral("command")));
                normalizedHandler.insert(QStringLiteral("timeout"), configuredHandler.value(QStringLiteral("timeout")));
                normalizedHandler.insert(QStringLiteral("type"), configuredHandler.value(QStringLiteral("type")));

                QJsonObject identity;
                identity.insert(QStringLiteral("event_name"), event.value());
                identity.insert(QStringLiteral("hooks"), QJsonArray{normalizedHandler});
                if (group.contains(QStringLiteral("matcher"))) {
                    identity.insert(QStringLiteral("matcher"), group.value(QStringLiteral("matcher")));
                }

                const QByteArray encodedIdentity = QJsonDocument(identity).toJson(QJsonDocument::Compact);
                const QString hash =
                    QStringLiteral("sha256:%1").arg(QString::fromLatin1(QCryptographicHash::hash(encodedIdentity, QCryptographicHash::Sha256).toHex()));
                const QString key = QStringLiteral("%1:%2:%3:%4").arg(keySource, event.value()).arg(groupIndex).arg(handlerIndex);
                const QString trustedState = QStringLiteral("[hooks.state.\"%1\"]\ntrusted_hash = \"%2\"").arg(key, hash);
                QVERIFY2(config.contains(trustedState), qPrintable(QStringLiteral("Missing current Codex trust state for %1").arg(key)));
                ++checkedHashes;
            }
        }
    }
    QCOMPARE(checkedHashes, 9);
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
    const QByteArray script = QByteArrayLiteral("#!/bin/sh\nprintf 'real-claude:%s\\n' \"$KMUX_CLAUDE_PID\"\n");
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
    process.start(launcherAlias);
    QVERIFY(process.waitForStarted());
    QVERIFY2(process.waitForFinished(5000), "transparent claude launcher recursively executed itself");
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    const QByteArray output = process.readAllStandardOutput().trimmed();
    QVERIFY(output.startsWith(QByteArrayLiteral("real-claude:")));
    bool pidIsValid = false;
    output.mid(QByteArrayLiteral("real-claude:").size()).toLongLong(&pidIsValid);
    QVERIFY(pidIsValid);

    QFile settings(QDir(homeDir).filePath(QStringLiteral(".claude/settings.json")));
    QVERIFY(settings.open(QIODevice::ReadOnly));
    const QJsonObject hooks = QJsonDocument::fromJson(settings.readAll()).object().value(QStringLiteral("hooks")).toObject();
    const QString hookCommand = hooks.value(QStringLiteral("UserPromptSubmit"))
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
    QCOMPARE(firstTraceRecord.value(QStringLiteral("event")).toString(), QStringLiteral("UserPromptSubmit"));
    QCOMPARE(firstTraceRecord.value(QStringLiteral("status")).toString(), QStringLiteral("running"));
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
    // Event and status names live in constants; the switch maps them to Kmux events.
    QVERIFY(pluginContent.contains(QStringLiteral("AGENT_PID: \"KMUX_OPENCODE_PID\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("AGENT: \"--agent\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("const KmuxAgentName = \"opencode\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("SESSION_IDLE: \"session.idle\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("PERMISSION_UPDATED: \"permission.updated\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("PERMISSION_ASKED: \"permission.asked\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("QUESTION_ASKED: \"question.asked\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("QUESTION_ASKED: \"QuestionAsked\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("CHAT_MESSAGE: \"chat.message\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("TOOL_EXECUTE_BEFORE: \"tool.execute.before\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("STOP: \"Stop\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("NEEDS_INPUT: \"needsInput\"")));
    QVERIFY(pluginContent.contains(QStringLiteral("report(KmuxHookEvent.STOP, KmuxStatus.IDLE, sessionId)")));
    QVERIFY(pluginContent.contains(QStringLiteral("report(KmuxHookEvent.PERMISSION_REQUEST, KmuxStatus.NEEDS_INPUT, sessionId)")));
    QVERIFY(pluginContent.contains(QStringLiteral("case OpenCodeEvent.PERMISSION_UPDATED:")));
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

void AgentHooksTest::testCodexPermissionRequestUsesConfiguredReviewer()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString configHome = temporaryDir.filePath(QStringLiteral("codex-home"));
    QVERIFY(QDir().mkpath(configHome));
    QFile config(QDir(configHome).filePath(QStringLiteral("config.toml")));
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray configContents = QByteArrayLiteral("approvals_reviewer = \"auto_review\"\n");
    QCOMPARE(config.write(configContents), configContents.size());
    config.close();

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                  {QStringLiteral("--codex-home"), configHome, QStringLiteral("install"), QStringLiteral("codex"), QStringLiteral("--quiet")});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    QFile settings(QDir(configHome).filePath(QStringLiteral("hooks.json")));
    QVERIFY(settings.open(QIODevice::ReadOnly));
    const QJsonArray permissionRequests =
        QJsonDocument::fromJson(settings.readAll()).object().value(QStringLiteral("hooks")).toObject().value(QStringLiteral("PermissionRequest")).toArray();
    QCOMPARE(permissionRequests.size(), 1);
    const QString permissionRequestCommand =
        permissionRequests.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile permissionRequestScript(permissionRequestCommand);
    QVERIFY(permissionRequestScript.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString permissionRequestScriptText = QString::fromUtf8(permissionRequestScript.readAll());
    QVERIFY(permissionRequestScriptText.contains(QStringLiteral("--event 'PermissionRequest'")));
    QVERIFY(permissionRequestScriptText.contains(QStringLiteral("--codex-permission-request")));
    QVERIFY(permissionRequestScriptText.contains(QStringLiteral("\"$@\" needsInput")));

    const QString tracePath = temporaryDir.filePath(QStringLiteral("hook-trace.jsonl"));
    const QByteArray hookPayload = QJsonDocument(QJsonObject{{QStringLiteral("cwd"), temporaryDir.path()}}).toJson(QJsonDocument::Compact);
    auto runPermissionHook = [&](const QString &program, const QStringList &arguments, QProcessEnvironment hookEnvironment, const QString &expectedStatus) {
        QFile::remove(tracePath);
        hookEnvironment.insert(QStringLiteral("CODEX_HOME"), configHome);
        hookEnvironment.insert(QStringLiteral("KMUX_AGENT_HOOK_LOG"), tracePath);
        hookEnvironment.remove(QStringLiteral("KMUX_DBUS_SERVICE"));
        hookEnvironment.remove(QStringLiteral("KMUX_DBUS_SESSION"));

        QProcess hookProcess;
        hookProcess.setProcessEnvironment(hookEnvironment);
        hookProcess.start(program, arguments);
        QVERIFY(hookProcess.waitForStarted());
        QCOMPARE(hookProcess.write(hookPayload), hookPayload.size());
        hookProcess.closeWriteChannel();
        QVERIFY(hookProcess.waitForFinished());
        QCOMPARE(hookProcess.exitStatus(), QProcess::NormalExit);
        QCOMPARE(hookProcess.exitCode(), 0);

        QFile trace(tracePath);
        QVERIFY(trace.open(QIODevice::ReadOnly | QIODevice::Text));
        QString receivedStatus;
        while (!trace.atEnd()) {
            const QJsonObject record = QJsonDocument::fromJson(trace.readLine()).object();
            if (record.value(QStringLiteral("phase")) == QLatin1String("received")) {
                receivedStatus = record.value(QStringLiteral("status")).toString();
                break;
            }
        }
        QCOMPARE(receivedStatus, expectedStatus);
    };

    runPermissionHook(permissionRequestCommand, {}, environment, QStringLiteral("running"));

    const QString fakeBin = temporaryDir.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(fakeBin));
    const QString fakeCodexPath = QDir(fakeBin).filePath(QStringLiteral("codex"));
    QFile fakeCodex(fakeCodexPath);
    QVERIFY(fakeCodex.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray fakeCodexContents = QByteArrayLiteral("#!/bin/sh\n\"$KMUX_TEST_PERMISSION_HOOK\"\nhook_status=$?\nexit \"$hook_status\"\n");
    QCOMPARE(fakeCodex.write(fakeCodexContents), fakeCodexContents.size());
    fakeCodex.close();
    QVERIFY(QFile::setPermissions(fakeCodexPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    environment.insert(QStringLiteral("KMUX_TEST_PERMISSION_HOOK"), permissionRequestCommand);
    runPermissionHook(fakeCodexPath, {QStringLiteral("-c"), QStringLiteral("approvals_reviewer=\"user\"")}, environment, QStringLiteral("needsInput"));
}

void AgentHooksTest::testClaudeLifecycleConfiguration()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString configHome = temporaryDir.filePath(QStringLiteral("claude-home"));

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                  {QStringLiteral("--claude-home"), configHome, QStringLiteral("install"), QStringLiteral("claude"), QStringLiteral("--quiet")});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    QFile settings(QDir(configHome).filePath(QStringLiteral("settings.json")));
    QVERIFY(settings.open(QIODevice::ReadOnly));
    const QJsonObject hooks = QJsonDocument::fromJson(settings.readAll()).object().value(QStringLiteral("hooks")).toObject();
    const QJsonArray notifications = hooks.value(QStringLiteral("Notification")).toArray();
    QCOMPARE(notifications.size(), 1);
    QCOMPARE(notifications.first().toObject().value(QStringLiteral("matcher")).toString(), QStringLiteral("permission_prompt|idle_prompt|elicitation_dialog"));
    const QString notificationCommand =
        notifications.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile notificationScript(notificationCommand);
    QVERIFY(notificationScript.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(QString::fromUtf8(notificationScript.readAll()).contains(QStringLiteral("--claude-notification")));

    const QString notificationTracePath = temporaryDir.filePath(QStringLiteral("notification-trace.jsonl"));
    QProcessEnvironment notificationEnvironment = environment;
    notificationEnvironment.insert(QStringLiteral("KMUX_AGENT_HOOK_LOG"), notificationTracePath);
    notificationEnvironment.remove(QStringLiteral("KMUX_DBUS_SERVICE"));
    notificationEnvironment.remove(QStringLiteral("KMUX_DBUS_SESSION"));

    QProcess notificationProcess;
    notificationProcess.setProcessEnvironment(notificationEnvironment);
    notificationProcess.start(notificationCommand);
    QVERIFY(notificationProcess.waitForStarted());
    const QByteArray notificationPayload =
        QJsonDocument(QJsonObject{{QStringLiteral("notification_type"), QStringLiteral("idle_prompt")}}).toJson(QJsonDocument::Compact);
    QCOMPARE(notificationProcess.write(notificationPayload), notificationPayload.size());
    notificationProcess.closeWriteChannel();
    QVERIFY(notificationProcess.waitForFinished());
    QCOMPARE(notificationProcess.exitStatus(), QProcess::NormalExit);
    QCOMPARE(notificationProcess.exitCode(), 0);

    QFile notificationTrace(notificationTracePath);
    QVERIFY(notificationTrace.open(QIODevice::ReadOnly | QIODevice::Text));
    const QJsonObject notificationRecord = QJsonDocument::fromJson(notificationTrace.readLine()).object();
    QCOMPARE(notificationRecord.value(QStringLiteral("phase")).toString(), QStringLiteral("received"));
    QCOMPARE(notificationRecord.value(QStringLiteral("event")).toString(), QStringLiteral("IdlePrompt"));
    QCOMPARE(notificationRecord.value(QStringLiteral("status")).toString(), QStringLiteral("idle"));

    const QJsonArray elicitationResults = hooks.value(QStringLiteral("ElicitationResult")).toArray();
    QCOMPARE(elicitationResults.size(), 1);
    const QString elicitationResultCommand =
        elicitationResults.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile elicitationResultScript(elicitationResultCommand);
    QVERIFY(elicitationResultScript.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString elicitationResultScriptText = QString::fromUtf8(elicitationResultScript.readAll());
    QVERIFY(elicitationResultScriptText.contains(QStringLiteral("--event 'ElicitationResult'")));
    QVERIFY(elicitationResultScriptText.contains(QStringLiteral("\"$@\" running")));

    const QJsonArray preCompacts = hooks.value(QStringLiteral("PreCompact")).toArray();
    QCOMPARE(preCompacts.size(), 1);
    QCOMPARE(preCompacts.first().toObject().value(QStringLiteral("matcher")).toString(), QStringLiteral("manual"));
    const QString preCompactCommand =
        preCompacts.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile preCompactScript(preCompactCommand);
    QVERIFY(preCompactScript.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(QString::fromUtf8(preCompactScript.readAll()).contains(QStringLiteral("\"$@\" running")));

    const QJsonArray postCompacts = hooks.value(QStringLiteral("PostCompact")).toArray();
    QCOMPARE(postCompacts.size(), 1);
    QCOMPARE(postCompacts.first().toObject().value(QStringLiteral("matcher")).toString(), QStringLiteral("manual"));
    const QString postCompactCommand =
        postCompacts.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile postCompactScript(postCompactCommand);
    QVERIFY(postCompactScript.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(QString::fromUtf8(postCompactScript.readAll()).contains(QStringLiteral("\"$@\" idle")));

    const QJsonArray permissionDenials = hooks.value(QStringLiteral("PermissionDenied")).toArray();
    QCOMPARE(permissionDenials.size(), 1);
    const QString permissionDeniedCommand =
        permissionDenials.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile permissionDeniedScript(permissionDeniedCommand);
    QVERIFY(permissionDeniedScript.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString permissionDeniedScriptText = QString::fromUtf8(permissionDeniedScript.readAll());
    QVERIFY(permissionDeniedScriptText.contains(QStringLiteral("--event 'PermissionDenied'")));
    QVERIFY(permissionDeniedScriptText.contains(QStringLiteral("\"$@\" running")));

    const QJsonArray sessionStarts = hooks.value(QStringLiteral("SessionStart")).toArray();
    QCOMPARE(sessionStarts.size(), 1);
    // Compaction restarts the session under a turn that is still running, so
    // that source must not reach the tab as an idle session start.
    QCOMPARE(sessionStarts.first().toObject().value(QStringLiteral("matcher")).toString(), QStringLiteral("startup|resume|clear|fork"));
    const QString sessionStartCommand =
        sessionStarts.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile sessionStartScript(sessionStartCommand);
    QVERIFY(sessionStartScript.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString sessionStartScriptText = QString::fromUtf8(sessionStartScript.readAll());
    QVERIFY(sessionStartScriptText.contains(QStringLiteral("--event 'SessionStart'")));
    QVERIFY(sessionStartScriptText.contains(QStringLiteral("\"$@\" idle")));

    const QString subagentTracePath = temporaryDir.filePath(QStringLiteral("subagent-trace.jsonl"));
    QProcessEnvironment subagentEnvironment = environment;
    subagentEnvironment.insert(QStringLiteral("KMUX_AGENT_HOOK_LOG"), subagentTracePath);
    subagentEnvironment.remove(QStringLiteral("KMUX_DBUS_SERVICE"));
    subagentEnvironment.remove(QStringLiteral("KMUX_DBUS_SESSION"));

    QProcess subagentProcess;
    subagentProcess.setProcessEnvironment(subagentEnvironment);
    subagentProcess.start(sessionStartCommand);
    QVERIFY(subagentProcess.waitForStarted());
    const QByteArray subagentPayload = QJsonDocument(QJsonObject{{QStringLiteral("agent_id"), QStringLiteral("agent-1")}}).toJson(QJsonDocument::Compact);
    QCOMPARE(subagentProcess.write(subagentPayload), subagentPayload.size());
    subagentProcess.closeWriteChannel();
    QVERIFY(subagentProcess.waitForFinished());
    QCOMPARE(subagentProcess.exitStatus(), QProcess::NormalExit);
    QCOMPARE(subagentProcess.exitCode(), 0);

    QFile subagentTrace(subagentTracePath);
    QVERIFY(subagentTrace.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QJsonDocument::fromJson(subagentTrace.readLine()).object().value(QStringLiteral("phase")).toString(), QStringLiteral("received"));
    const QJsonObject ignoredSubagentRecord = QJsonDocument::fromJson(subagentTrace.readLine()).object();
    QCOMPARE(ignoredSubagentRecord.value(QStringLiteral("phase")).toString(), QStringLiteral("ignored"));
    QVERIFY(ignoredSubagentRecord.value(QStringLiteral("error")).toString().contains(QStringLiteral("subagent")));
    QVERIFY(subagentTrace.atEnd());

    const QJsonArray preToolUses = hooks.value(QStringLiteral("PreToolUse")).toArray();
    QCOMPARE(preToolUses.size(), 1);
    const QString preToolUseCommand =
        preToolUses.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    const QString subagentResolutionTracePath = temporaryDir.filePath(QStringLiteral("subagent-resolution-trace.jsonl"));
    QProcessEnvironment subagentResolutionEnvironment = environment;
    subagentResolutionEnvironment.insert(QStringLiteral("KMUX_AGENT_HOOK_LOG"), subagentResolutionTracePath);
    subagentResolutionEnvironment.remove(QStringLiteral("KMUX_DBUS_SERVICE"));
    subagentResolutionEnvironment.remove(QStringLiteral("KMUX_DBUS_SESSION"));

    QProcess subagentResolutionProcess;
    subagentResolutionProcess.setProcessEnvironment(subagentResolutionEnvironment);
    subagentResolutionProcess.start(preToolUseCommand);
    QVERIFY(subagentResolutionProcess.waitForStarted());
    QCOMPARE(subagentResolutionProcess.write(subagentPayload), subagentPayload.size());
    subagentResolutionProcess.closeWriteChannel();
    QVERIFY(subagentResolutionProcess.waitForFinished());
    QCOMPARE(subagentResolutionProcess.exitStatus(), QProcess::NormalExit);
    QCOMPARE(subagentResolutionProcess.exitCode(), 0);

    QFile subagentResolutionTrace(subagentResolutionTracePath);
    QVERIFY(subagentResolutionTrace.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QJsonDocument::fromJson(subagentResolutionTrace.readLine()).object().value(QStringLiteral("phase")).toString(), QStringLiteral("received"));
    QCOMPARE(QJsonDocument::fromJson(subagentResolutionTrace.readLine()).object().value(QStringLiteral("phase")).toString(), QStringLiteral("failed"));
    QVERIFY(subagentResolutionTrace.atEnd());

    const QJsonArray stops = hooks.value(QStringLiteral("Stop")).toArray();
    QCOMPARE(stops.size(), 1);
    const QString stopCommand =
        stops.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile stopScript(stopCommand);
    QVERIFY(stopScript.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString stopScriptText = QString::fromUtf8(stopScript.readAll());
    QVERIFY(stopScriptText.contains(QStringLiteral("--event 'Stop'")));
    QVERIFY(stopScriptText.contains(QStringLiteral("--claude-stop")));
    QVERIFY(stopScriptText.contains(QStringLiteral("\"$@\" idle")));

    const QJsonArray stopFailures = hooks.value(QStringLiteral("StopFailure")).toArray();
    QCOMPARE(stopFailures.size(), 1);
    const QString stopFailureCommand =
        stopFailures.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile stopFailureScript(stopFailureCommand);
    QVERIFY(stopFailureScript.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString stopFailureScriptText = QString::fromUtf8(stopFailureScript.readAll());
    QVERIFY(stopFailureScriptText.contains(QStringLiteral("--event 'StopFailure'")));
    QVERIFY(stopFailureScriptText.contains(QStringLiteral("--claude-stop-failure")));
    QVERIFY(stopFailureScriptText.contains(QStringLiteral("\"$@\" idle")));

    const QJsonArray sessionEnds = hooks.value(QStringLiteral("SessionEnd")).toArray();
    QCOMPARE(sessionEnds.size(), 1);
    const QString sessionEndCommand =
        sessionEnds.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile sessionEndScript(sessionEndCommand);
    QVERIFY(sessionEndScript.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString sessionEndScriptText = QString::fromUtf8(sessionEndScript.readAll());
    QVERIFY(sessionEndScriptText.contains(QStringLiteral("--event 'SessionEnd'")));
    QVERIFY(sessionEndScriptText.contains(QStringLiteral("\"$@\" none")));

    const QString tracePath = temporaryDir.filePath(QStringLiteral("hook-trace.jsonl"));
    auto runHook = [&](const QString &command, const QJsonObject &payload, const QString &expectedStatus, const QString &expectedEvent) {
        QFile::remove(tracePath);
        QProcessEnvironment hookEnvironment = environment;
        hookEnvironment.insert(QStringLiteral("KMUX_AGENT_HOOK_LOG"), tracePath);
        hookEnvironment.remove(QStringLiteral("KMUX_DBUS_SERVICE"));
        hookEnvironment.remove(QStringLiteral("KMUX_DBUS_SESSION"));

        QProcess hookProcess;
        hookProcess.setProcessEnvironment(hookEnvironment);
        hookProcess.start(command);
        QVERIFY(hookProcess.waitForStarted());
        const QByteArray encodedPayload = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        QCOMPARE(hookProcess.write(encodedPayload), encodedPayload.size());
        hookProcess.closeWriteChannel();
        QVERIFY(hookProcess.waitForFinished());
        QCOMPARE(hookProcess.exitStatus(), QProcess::NormalExit);
        QCOMPARE(hookProcess.exitCode(), 0);

        QFile trace(tracePath);
        QVERIFY(trace.open(QIODevice::ReadOnly | QIODevice::Text));
        QString receivedStatus;
        QString receivedEvent;
        while (!trace.atEnd()) {
            const QJsonObject record = QJsonDocument::fromJson(trace.readLine()).object();
            if (record.value(QStringLiteral("phase")) == QLatin1String("received")) {
                receivedStatus = record.value(QStringLiteral("status")).toString();
                receivedEvent = record.value(QStringLiteral("event")).toString();
                break;
            }
        }
        QCOMPARE(receivedStatus, expectedStatus);
        QCOMPARE(receivedEvent, expectedEvent);
    };
    auto runStopHook = [&](const QJsonObject &payload, const QString &expectedStatus) {
        runHook(stopCommand, payload, expectedStatus, QStringLiteral("Stop"));
    };

    runStopHook(QJsonObject{{QStringLiteral("background_tasks"), QJsonArray{}}}, QStringLiteral("idle"));
    runStopHook(QJsonObject{{QStringLiteral("background_tasks"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("shell")}}}}},
                QStringLiteral("idle"));
    runStopHook(QJsonObject{{QStringLiteral("background_tasks"),
                             QJsonArray{QJsonObject{
                                 {QStringLiteral("type"), QStringLiteral("subagent")},
                                 {QStringLiteral("status"), QStringLiteral("completed")},
                             }}}},
                QStringLiteral("idle"));
    runStopHook(QJsonObject{{QStringLiteral("background_tasks"),
                             QJsonArray{QJsonObject{
                                 {QStringLiteral("type"), QStringLiteral("shell")},
                                 {QStringLiteral("status"), QStringLiteral("running")},
                             }}}},
                QStringLiteral("idle"));
    runStopHook(QJsonObject{{QStringLiteral("background_tasks"),
                             QJsonArray{QJsonObject{
                                 {QStringLiteral("type"), QStringLiteral("monitor")},
                                 {QStringLiteral("status"), QStringLiteral("running")},
                             }}}},
                QStringLiteral("idle"));
    runStopHook(QJsonObject{{QStringLiteral("background_tasks"),
                             QJsonArray{QJsonObject{
                                 {QStringLiteral("type"), QStringLiteral("subagent")},
                                 {QStringLiteral("status"), QStringLiteral("running")},
                             }}}},
                QStringLiteral("running"));
    runStopHook(QJsonObject{{QStringLiteral("background_tasks"),
                             QJsonArray{QJsonObject{
                                 {QStringLiteral("type"), QStringLiteral("workflow")},
                                 {QStringLiteral("status"), QStringLiteral("running")},
                             }}}},
                QStringLiteral("running"));
    runStopHook(QJsonObject{{QStringLiteral("background_tasks"),
                             QJsonArray{QJsonObject{
                                 {QStringLiteral("type"), QStringLiteral("dream")},
                                 {QStringLiteral("status"), QStringLiteral("running")},
                             }}}},
                QStringLiteral("idle"));
    runStopHook(QJsonObject{{QStringLiteral("session_crons"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("cron-1")}}}}},
                QStringLiteral("idle"));
    runStopHook(QJsonObject{{QStringLiteral("background_tasks"),
                             QJsonArray{
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("monitor")},
                                     {QStringLiteral("status"), QStringLiteral("running")},
                                 },
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("shell")},
                                     {QStringLiteral("status"), QStringLiteral("running")},
                                 },
                             }}},
                QStringLiteral("idle"));
    runStopHook(QJsonObject{{QStringLiteral("background_tasks"),
                             QJsonArray{
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("shell")},
                                     {QStringLiteral("status"), QStringLiteral("running")},
                                 },
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("subagent")},
                                     {QStringLiteral("status"), QStringLiteral("running")},
                                 },
                             }}},
                QStringLiteral("running"));
    runHook(stopFailureCommand,
            QJsonObject{{QStringLiteral("error"), QStringLiteral("rate_limit")}},
            QStringLiteral("rateLimited"),
            QStringLiteral("RateLimit"));
    runHook(stopFailureCommand,
            QJsonObject{{QStringLiteral("error"), QStringLiteral("authentication_failed")}},
            QStringLiteral("idle"),
            QStringLiteral("StopFailure"));
    runHook(sessionEndCommand, QJsonObject{{QStringLiteral("reason"), QStringLiteral("exit")}}, QStringLiteral("none"), QStringLiteral("SessionEnd"));
}

void AgentHooksTest::testClaudeUsageLimitWrapUp_data()
{
    QTest::addColumn<QJsonArray>("transcriptRows");
    QTest::addColumn<QJsonObject>("payloadFields");
    QTest::addColumn<QString>("event");
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
    const QString stop = QStringLiteral("Stop");
    const QString rateLimited = QStringLiteral("rateLimited");
    const QString idle = QStringLiteral("idle");

    QTest::newRow("graceful-stop") << QJsonArray{prompt, wrapUp, assistant} << payload << stop << rateLimited;
    QTest::newRow("idle-reminder") << QJsonArray{prompt, wrapUp, assistant} << payload << QStringLiteral("Notification") << rateLimited;
    QTest::newRow("ordinary-idle-reminder") << QJsonArray{prompt, assistant} << payload << QStringLiteral("Notification") << idle;
    QTest::newRow("permission-notification") << QJsonArray{prompt, wrapUp, assistant}
                                             << QJsonObject{{QStringLiteral("prompt_id"), QStringLiteral("current-prompt")},
                                                            {QStringLiteral("notification_type"), QStringLiteral("permission_prompt")}}
                                             << QStringLiteral("Notification") << QStringLiteral("needsInput");
    QTest::newRow("background-agents") << QJsonArray{prompt, wrapUp, assistant}
                                       << QJsonObject{{QStringLiteral("prompt_id"), QStringLiteral("current-prompt")},
                                                      {QStringLiteral("background_tasks"),
                                                       QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("subagent")},
                                                                              {QStringLiteral("status"), QStringLiteral("running")}}}}}
                                       << stop << rateLimited;
    QTest::newRow("ordinary-stop") << QJsonArray{prompt, assistant} << payload << stop << idle;
    QTest::newRow("missing-transcript") << QJsonArray{} << payload << stop << idle;
    QTest::newRow("old-prompt") << QJsonArray{wrapUp, assistant} << QJsonObject{{QStringLiteral("prompt_id"), QStringLiteral("next-prompt")}} << stop << idle;
    QTest::newRow("new-prompt-without-hook-id") << QJsonArray{wrapUp, prompt, assistant} << QJsonObject{} << stop << idle;
    QTest::newRow("wrap-up-without-hook-id") << QJsonArray{prompt, wrapUp, assistant} << QJsonObject{} << stop << rateLimited;

    QJsonObject note = wrapUp;
    note.insert(QStringLiteral("usageLimitNote"), QStringLiteral("release"));
    QTest::newRow("limit-released") << QJsonArray{prompt, wrapUp, note, assistant} << payload << stop << idle;
    note.insert(QStringLiteral("usageLimitNote"), QStringLiteral("near_limit"));
    QTest::newRow("near-limit") << QJsonArray{prompt, note, assistant} << payload << stop << idle;
    note = wrapUp;
    note.insert(QStringLiteral("isSidechain"), true);
    QTest::newRow("sidechain-note") << QJsonArray{prompt, note, assistant} << payload << stop << idle;
    note = wrapUp;
    note.insert(QStringLiteral("isMeta"), false);
    QTest::newRow("user-text-is-not-a-limit-signal") << QJsonArray{prompt, note, assistant} << payload << stop << idle;
    note = wrapUp;
    note.insert(QStringLiteral("sessionId"), QStringLiteral("other-session"));
    QTest::newRow("other-session") << QJsonArray{prompt, note, assistant}
                                   << QJsonObject{{QStringLiteral("prompt_id"), QStringLiteral("current-prompt")},
                                                  {QStringLiteral("session_id"), QStringLiteral("current-session")}}
                                   << stop << idle;
    QTest::newRow("resumed-work") << QJsonArray{prompt, wrapUp, assistant} << payload << QStringLiteral("PreToolUse") << QStringLiteral("running");
    QTest::newRow("subagent-stop") << QJsonArray{prompt, wrapUp, assistant}
                                   << QJsonObject{{QStringLiteral("prompt_id"), QStringLiteral("current-prompt")},
                                                  {QStringLiteral("agent_id"), QStringLiteral("subagent")}}
                                   << stop << idle;
}

void AgentHooksTest::testClaudeUsageLimitWrapUp()
{
    QFETCH(QJsonArray, transcriptRows);
    QFETCH(QJsonObject, payloadFields);
    QFETCH(QString, event);
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

    QStringList arguments{QStringLiteral("--hook-output"), QStringLiteral("--agent"), QStringLiteral("claude"), QStringLiteral("--event"), event};
    if (event == QLatin1String("Stop")) {
        arguments << QStringLiteral("--claude-stop");
    } else if (event == QLatin1String("Notification")) {
        arguments << QStringLiteral("--claude-notification");
        if (!payloadFields.contains(QStringLiteral("notification_type"))) {
            payloadFields.insert(QStringLiteral("notification_type"), QStringLiteral("idle_prompt"));
        }
    }
    arguments << (event == QLatin1String("PreToolUse")         ? QStringLiteral("running")
                      : event == QLatin1String("Notification") ? QStringLiteral("needsInput")
                                                               : QStringLiteral("idle"));
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
    if (event == QLatin1String("Notification") && payloadFields.value(QStringLiteral("notification_type")) == QLatin1String("idle_prompt")) {
        event = QStringLiteral("IdlePrompt");
    }
    QCOMPARE(record.value(QStringLiteral("event")).toString(), expectedStatus == QLatin1String("rateLimited") ? QStringLiteral("RateLimit") : event);
}

void AgentHooksTest::testCodexFeatureToml_data()
{
    QTest::addColumn<QString>("original");
    QTest::addColumn<QString>("installedFeature");
    QTest::addColumn<QString>("forbiddenFeature");

    QTest::newRow("empty") << QString() << QStringLiteral("[features]") << QString();
    QTest::newRow("unrelated-table") << QStringLiteral("[other]\nvalue = true\n") << QStringLiteral("[features]") << QString();
    QTest::newRow("dotted-key") << QStringLiteral("features.experimental_mode = true\n") << QStringLiteral("features.hooks = true")
                                << QStringLiteral("[features]");
    QTest::newRow("quoted-dotted-key") << QStringLiteral("\"features\".experimental_mode = true\n") << QStringLiteral("features.hooks = true")
                                       << QStringLiteral("[features]");
    QTest::newRow("quoted-table") << QStringLiteral("[\"features\"]\nexperimental_mode = true\n") << QStringLiteral("hooks = true")
                                  << QStringLiteral("features.hooks = true");
    QTest::newRow("literal-quoted-table") << QStringLiteral("['features']\nexperimental_mode = true\n") << QStringLiteral("hooks = true")
                                          << QStringLiteral("features.hooks = true");
    QTest::newRow("inline-table") << QStringLiteral("features = { experimental_mode = true }\n") << QStringLiteral("hooks = true")
                                  << QStringLiteral("[features]");
    QTest::newRow("nested-inline-hooks") << QStringLiteral("features = { nested = { hooks = false }, experimental_mode = true }\n")
                                         << QStringLiteral("experimental_mode = true, hooks = true") << QStringLiteral("[features]");
    QTest::newRow("inline-hooks-disabled") << QStringLiteral("features = { experimental_mode = true, hooks = false }\n") << QStringLiteral("hooks = true")
                                           << QStringLiteral("[features]");
    QTest::newRow("dotted-hooks-disabled") << QStringLiteral("features.hooks = false\n") << QStringLiteral("features.hooks = true")
                                           << QStringLiteral("[features]");
    QTest::newRow("dotted-hooks-enabled") << QStringLiteral("features.hooks = true\n") << QStringLiteral("features.hooks = true")
                                          << QStringLiteral("[features]");
    QTest::newRow("table-hooks-disabled") << QStringLiteral("[features]\nhooks = false\n") << QStringLiteral("hooks = true")
                                          << QStringLiteral("features.hooks = true");
    QTest::newRow("quoted-table-hooks-enabled") << QStringLiteral("[features]\n\"hooks\" = true\n") << QStringLiteral("\"hooks\" = true")
                                                << QStringLiteral("features.hooks = true");
    QTest::newRow("inline-hooks-enabled") << QStringLiteral("features = { experimental_mode = true, hooks = true }\n") << QStringLiteral("hooks = true")
                                          << QStringLiteral("[features]");
}

void AgentHooksTest::testCodexFeatureToml()
{
    QFETCH(QString, original);
    QFETCH(QString, installedFeature);
    QFETCH(QString, forbiddenFeature);

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString configPath = temporaryDir.filePath(QStringLiteral("config.toml"));
    QFile configFile(configPath);
    QVERIFY(configFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(configFile.write(original.toUtf8()), original.toUtf8().size());
    configFile.close();

    const auto runHooks = [&temporaryDir](const QString &command) {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                      {QStringLiteral("--codex-home"), temporaryDir.path(), command, QStringLiteral("codex"), QStringLiteral("--quiet")});
        if (!process.waitForStarted() || !process.waitForFinished()) {
            return QStringLiteral("Could not run kmux-agent-hooks: %1").arg(process.errorString());
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return QString::fromUtf8(process.readAllStandardError());
        }
        return QString();
    };
    const auto readConfig = [&configPath]() {
        QFile file(configPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    };

    const QString installError = runHooks(QStringLiteral("install"));
    QVERIFY2(installError.isEmpty(), qPrintable(installError));
    const QString installed = readConfig();
    QVERIFY(installed.contains(installedFeature));
    if (!forbiddenFeature.isEmpty()) {
        QVERIFY(!installed.contains(forbiddenFeature));
    }

    const QString reinstallError = runHooks(QStringLiteral("install"));
    QVERIFY2(reinstallError.isEmpty(), qPrintable(reinstallError));
    QCOMPARE(readConfig(), installed);

    const QString uninstallError = runHooks(QStringLiteral("uninstall"));
    QVERIFY2(uninstallError.isEmpty(), qPrintable(uninstallError));
    QCOMPARE(readConfig(), original);
}

void AgentHooksTest::testCodexRepairsPreviousDottedInstall()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString configPath = temporaryDir.filePath(QStringLiteral("config.toml"));
    QFile configFile(configPath);
    QVERIFY(configFile.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray previousInstall = QByteArrayLiteral(
        "features.experimental_mode = true\n"
        "\n"
        "[features]\n"
        "# kmux-codex-hooks-feature begin\n"
        "hooks = true\n"
        "# kmux-codex-hooks-feature end\n");
    QCOMPARE(configFile.write(previousInstall), previousInstall.size());
    configFile.close();

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_DATA_HOME"), temporaryDir.filePath(QStringLiteral("data")));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(KMUX_AGENT_HOOKS_EXECUTABLE),
                  {QStringLiteral("--codex-home"), temporaryDir.path(), QStringLiteral("install"), QStringLiteral("codex"), QStringLiteral("--quiet")});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

    QVERIFY(configFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString repaired = QString::fromUtf8(configFile.readAll());
    QVERIFY(repaired.contains(QStringLiteral("features.hooks = true")));
    QVERIFY(!repaired.contains(QStringLiteral("\n[features]\n")));
}

void AgentHooksTest::testHookOperationsWaitForTransactionLock_data()
{
    QTest::addColumn<QString>("agent");
    QTest::addColumn<QString>("homeOption");
    QTest::addColumn<QString>("settingsFile");

    QTest::newRow("codex") << QStringLiteral("codex") << QStringLiteral("--codex-home") << QStringLiteral("hooks.json");
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

    QTest::newRow("codex") << QStringLiteral("codex") << QStringLiteral("--codex-home") << QStringLiteral("hooks.json");
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

    // An empty original means that the file did not exist.
    QTest::newRow("codex without hooks.json") << QStringLiteral("codex") << QStringLiteral("--codex-home") << QStringLiteral("hooks.json") << QByteArray();
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

    QTest::newRow("codex") << QStringLiteral("codex") << QStringLiteral("--codex-home") << QStringLiteral("hooks.json") << 9;
    QTest::newRow("claude") << QStringLiteral("claude") << QStringLiteral("--claude-home") << QStringLiteral("settings.json") << 14;
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
    const QString agentPidEnvironment = agent == QLatin1String("codex") ? QStringLiteral("KMUX_CODEX_PID") : QStringLiteral("KMUX_CLAUDE_PID");
    QVERIFY(hookScriptText.contains(agentPidEnvironment));

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

    QTest::newRow("codex") << QStringLiteral("codex") << QStringLiteral("--codex-home") << QStringLiteral("hooks.json") << 9;
    QTest::newRow("claude") << QStringLiteral("claude") << QStringLiteral("--claude-home") << QStringLiteral("settings.json") << 14;
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
        QTest::addRow("codex, %s", qPrintable(dataDirectory))
            << QStringLiteral("codex") << QStringLiteral("--codex-home") << QStringLiteral("hooks.json") << 9 << dataDirectory;
        QTest::addRow("claude, %s", qPrintable(dataDirectory))
            << QStringLiteral("claude") << QStringLiteral("--claude-home") << QStringLiteral("settings.json") << 14 << dataDirectory;
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
