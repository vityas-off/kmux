/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../workspaces/CodexUsageLimit.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace Konsole;

class CodexUsageLimitTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testReadsReachedLimit();
    void testCreditsKeepCodexWorking();
    void testReadsLimitOfAnotherBucket();
    void testProbeAsksAppServer();
    void testProbeReportsFailure();
    void testProbeRunsCodexOfProcess();

private:
    static QString writeFakeCodex(const QTemporaryDir &directory);
};

namespace
{
QJsonObject window(double usedPercent, qint64 resetsAt)
{
    return {{QStringLiteral("usedPercent"), usedPercent}, {QStringLiteral("windowDurationMins"), 300}, {QStringLiteral("resetsAt"), resetsAt}};
}

QJsonObject snapshot(const QJsonObject &primary, const QJsonObject &secondary, const QJsonValue &reachedType, bool hasCredits)
{
    return {{QStringLiteral("primary"), primary},
            {QStringLiteral("secondary"), secondary},
            {QStringLiteral("credits"), QJsonObject{{QStringLiteral("hasCredits"), hasCredits}, {QStringLiteral("unlimited"), false}}},
            {QStringLiteral("rateLimitReachedType"), reachedType}};
}

QByteArray rateLimitsResponse(const QJsonObject &result)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 2}, {QStringLiteral("result"), result}})
        .toJson(QJsonDocument::Compact);
}
}

void CodexUsageLimitTest::testReadsReachedLimit()
{
    const QJsonObject result{
        {QStringLiteral("ordinaryUsageAllowed"), false},
        {QStringLiteral("rateLimits"), snapshot(window(100, 1800000000), window(60, 1900000000), QStringLiteral("rate_limit_reached"), false)}};

    const CodexUsageLimit usageLimit = codexUsageLimitFromRateLimits(result);
    QVERIFY(usageLimit.reached);
    QCOMPARE(usageLimit.resetsAt, QDateTime::fromSecsSinceEpoch(1800000000));

    // When both windows are exhausted, work resumes after the later reset.
    const QJsonObject bothExhausted{
        {QStringLiteral("rateLimits"), snapshot(window(100, 1800000000), window(100, 1900000000), QStringLiteral("rate_limit_reached"), false)}};
    QCOMPARE(codexUsageLimitFromRateLimits(bothExhausted).resetsAt, QDateTime::fromSecsSinceEpoch(1900000000));

    // Codex may stop without naming the limit when its included usage is gone.
    const QJsonObject withoutType{{QStringLiteral("ordinaryUsageAllowed"), false},
                                  {QStringLiteral("rateLimits"), snapshot(window(100, 1800000000), window(60, 1900000000), QJsonValue::Null, false)}};
    QVERIFY(codexUsageLimitFromRateLimits(withoutType).reached);
}

void CodexUsageLimitTest::testCreditsKeepCodexWorking()
{
    const QJsonObject result{{QStringLiteral("ordinaryUsageAllowed"), false},
                             {QStringLiteral("rateLimits"), snapshot(window(100, 1800000000), window(60, 1900000000), QJsonValue::Null, true)}};
    QVERIFY(!codexUsageLimitFromRateLimits(result).reached);

    const QJsonObject belowLimit{{QStringLiteral("ordinaryUsageAllowed"), true},
                                 {QStringLiteral("rateLimits"), snapshot(window(74, 1800000000), window(73, 1900000000), QJsonValue::Null, false)}};
    QVERIFY(!codexUsageLimitFromRateLimits(belowLimit).reached);
    QVERIFY(!codexUsageLimitFromRateLimits({}).reached);
}

void CodexUsageLimitTest::testReadsLimitOfAnotherBucket()
{
    const QJsonObject result{
        {QStringLiteral("rateLimits"), snapshot(window(20, 1800000000), window(10, 1900000000), QJsonValue::Null, false)},
        {QStringLiteral("rateLimitsByLimitId"),
         QJsonObject{{QStringLiteral("codex_other"), snapshot(window(100, 1850000000), window(30, 1900000000), QStringLiteral("rate_limit_reached"), false)}}}};

    const CodexUsageLimit usageLimit = codexUsageLimitFromRateLimits(result);
    QVERIFY(usageLimit.reached);
    QCOMPARE(usageLimit.resetsAt, QDateTime::fromSecsSinceEpoch(1850000000));
}

QString CodexUsageLimitTest::writeFakeCodex(const QTemporaryDir &directory)
{
    // Answers the app-server requests like Codex; run without arguments, it
    // reports a failed turn and stays in the foreground.
    const QString path = directory.filePath(QStringLiteral("codex"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(
        "#!/bin/sh\n"
        "case \" $* \" in\n"
        "*\" app-server \"*)\n"
        "    while IFS= read -r line; do\n"
        "        case \"$line\" in\n"
        "        *'\"id\":1'*) printf '%s\\n' '{\"id\":1,\"jsonrpc\":\"2.0\",\"result\":{}}' ;;\n"
        "        *'\"id\":2'*) printf '%s\\n' \"$KMUX_TEST_RATE_LIMITS\"; exit 0 ;;\n"
        "        esac\n"
        "    done\n"
        "    ;;\n"
        "*)\n"
        "    printf '\\033]7501;state=working:app=codex\\033\\\\'\n"
        "    printf '\\033]7501;state=error:app=codex\\033\\\\'\n"
        "    sleep 30\n"
        "    ;;\n"
        "esac\n");
    file.close();
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}

void CodexUsageLimitTest::testProbeAsksAppServer()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString codex = writeFakeCodex(directory);
    QVERIFY(!codex.isEmpty());

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(
        QStringLiteral("KMUX_TEST_RATE_LIMITS"),
        QString::fromUtf8(rateLimitsResponse(
            {{QStringLiteral("rateLimits"), snapshot(window(100, 1800000000), window(60, 1900000000), QStringLiteral("rate_limit_reached"), false)}})));

    std::optional<std::optional<CodexUsageLimit>> answer;
    auto *probe = new CodexUsageLimitProbe(codex, environment, directory.path());
    QPointer<CodexUsageLimitProbe> guardedProbe(probe);
    probe->start([&answer](std::optional<CodexUsageLimit> usageLimit) {
        answer = usageLimit;
    });
    QTRY_VERIFY_WITH_TIMEOUT(answer.has_value(), 10000);
    QVERIFY(answer->has_value());
    QVERIFY((*answer)->reached);
    QCOMPARE((*answer)->resetsAt, QDateTime::fromSecsSinceEpoch(1800000000));
    QTRY_VERIFY(guardedProbe.isNull());
}

void CodexUsageLimitTest::testProbeReportsFailure()
{
    std::optional<std::optional<CodexUsageLimit>> answer;
    auto *probe = new CodexUsageLimitProbe(QStringLiteral("/bin/false"), QProcessEnvironment::systemEnvironment(), QString());
    probe->start([&answer](std::optional<CodexUsageLimit> usageLimit) {
        answer = usageLimit;
    });
    QTRY_VERIFY_WITH_TIMEOUT(answer.has_value(), 10000);
    QVERIFY(!answer->has_value());
}

void CodexUsageLimitTest::testProbeRunsCodexOfProcess()
{
#ifndef Q_OS_LINUX
    QSKIP("Inspecting another process is only implemented on Linux.");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeFakeCodex(directory).isEmpty());

    // The agent's own environment finds its Codex and carries its settings.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), directory.path() + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    environment.insert(
        QStringLiteral("KMUX_TEST_RATE_LIMITS"),
        QString::fromUtf8(rateLimitsResponse(
            {{QStringLiteral("rateLimits"), snapshot(window(100, 1800000000), window(60, 1900000000), QStringLiteral("rate_limit_reached"), false)}})));
    QProcess agent;
    agent.setProcessEnvironment(environment);
    agent.setWorkingDirectory(directory.path());
    agent.start(directory.filePath(QStringLiteral("codex")), {});
    QVERIFY(agent.waitForStarted());

    std::optional<std::optional<CodexUsageLimit>> answer;
    auto *probe = CodexUsageLimitProbe::forProcess(agent.processId(), nullptr);
    QVERIFY(probe != nullptr);
    probe->start([&answer](std::optional<CodexUsageLimit> usageLimit) {
        answer = usageLimit;
    });
    QTRY_VERIFY_WITH_TIMEOUT(answer.has_value(), 10000);
    QVERIFY(answer->has_value());
    QVERIFY((*answer)->reached);

    agent.kill();
    agent.waitForFinished();
    QVERIFY(CodexUsageLimitProbe::forProcess(agent.processId(), nullptr) == nullptr);
#endif
}

QTEST_GUILESS_MAIN(CodexUsageLimitTest)

#include "CodexUsageLimitTest.moc"
