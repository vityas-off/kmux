/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    void testClaudeLifecycleConfiguration();
    void testHookOperationsWaitForTransactionLock_data();
    void testHookOperationsWaitForTransactionLock();
    void testHomeScopedScripts_data();
    void testHomeScopedScripts();
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
    QCOMPARE(notifications.first().toObject().value(QStringLiteral("matcher")).toString(), QStringLiteral("permission_prompt|elicitation_dialog"));

    const QJsonArray elicitationResults = hooks.value(QStringLiteral("ElicitationResult")).toArray();
    QCOMPARE(elicitationResults.size(), 1);
    const QString elicitationResultCommand =
        elicitationResults.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile elicitationResultScript(elicitationResultCommand);
    QVERIFY(elicitationResultScript.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString elicitationResultScriptText = QString::fromUtf8(elicitationResultScript.readAll());
    QVERIFY(elicitationResultScriptText.contains(QStringLiteral("--event 'ElicitationResult'")));
    QVERIFY(elicitationResultScriptText.contains(QStringLiteral("\"$@\" running")));

    const QJsonArray sessionStarts = hooks.value(QStringLiteral("SessionStart")).toArray();
    QCOMPARE(sessionStarts.size(), 1);
    const QString sessionStartCommand =
        sessionStarts.first().toObject().value(QStringLiteral("hooks")).toArray().first().toObject().value(QStringLiteral("command")).toString();
    QFile sessionStartScript(sessionStartCommand);
    QVERIFY(sessionStartScript.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString sessionStartScriptText = QString::fromUtf8(sessionStartScript.readAll());
    QVERIFY(sessionStartScriptText.contains(QStringLiteral("--event 'SessionStart'")));
    QVERIFY(sessionStartScriptText.contains(QStringLiteral("\"$@\" idle")));
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

void AgentHooksTest::testHomeScopedScripts_data()
{
    QTest::addColumn<QString>("agent");
    QTest::addColumn<QString>("homeOption");
    QTest::addColumn<QString>("settingsFile");
    QTest::addColumn<int>("handlerCount");

    QTest::newRow("codex") << QStringLiteral("codex") << QStringLiteral("--codex-home") << QStringLiteral("hooks.json") << 8;
    QTest::newRow("claude") << QStringLiteral("claude") << QStringLiteral("--claude-home") << QStringLiteral("settings.json") << 11;
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

QTEST_GUILESS_MAIN(AgentHooksTest)

#include "AgentHooksTest.moc"
