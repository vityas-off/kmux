/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

class AgentHooksTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testCodexFeatureToml_data();
    void testCodexFeatureToml();
    void testCodexRepairsPreviousDottedInstall();
};

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

QTEST_GUILESS_MAIN(AgentHooksTest)

#include "AgentHooksTest.moc"
