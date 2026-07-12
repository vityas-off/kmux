/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../LocalActivationServer.h"

#include <QScopeGuard>
#include <QTest>
#include <QUuid>

#include <atomic>
#include <thread>

using namespace Konsole;

class LocalActivationServerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testNoExistingServer();
    void testActivationRoundTrip();
};

QString uniqueServerName()
{
    return QStringLiteral("kmux-local-activation-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void LocalActivationServerTest::testNoExistingServer()
{
    int exitCode = -1;
    QString error;
    QCOMPARE(LocalActivationServer::requestExisting(uniqueServerName(), {}, QString(), {}, &exitCode, &error), LocalActivationServer::RequestResult::NoServer);
}

void LocalActivationServerTest::testActivationRoundTrip()
{
    const QString serverName = uniqueServerName();
    const QStringList expectedArguments = {QStringLiteral("--new-tab"), QStringLiteral("-p"), QStringLiteral("TabColor=#123456")};
    const QString expectedWorkingDirectory = QStringLiteral("/activation/working/directory");
    const QStringList expectedEnvironment = {QStringLiteral("KMUX_TEST_VALUE=secondary"), QStringLiteral("KMUX_TEST_TOKEN=private")};

    QStringList receivedArguments;
    QString receivedWorkingDirectory;
    QStringList receivedEnvironment;
    LocalActivationServer server(serverName, [&](const QStringList &arguments, const QString &workingDirectory, const QStringList &environment) {
        receivedArguments = arguments;
        receivedWorkingDirectory = workingDirectory;
        receivedEnvironment = environment;
        return 23;
    });
    QString listenError;
    QVERIFY2(server.listen(&listenError), qPrintable(listenError));

    std::atomic_bool finished = false;
    LocalActivationServer::RequestResult requestResult = LocalActivationServer::RequestResult::Error;
    int exitCode = -1;
    QString requestError;
    std::thread client([&]() {
        requestResult =
            LocalActivationServer::requestExisting(serverName, expectedArguments, expectedWorkingDirectory, expectedEnvironment, &exitCode, &requestError);
        finished = true;
    });
    const auto joinClient = qScopeGuard([&]() {
        client.join();
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
    QCOMPARE(requestResult, LocalActivationServer::RequestResult::Delivered);
    QCOMPARE(exitCode, 23);
    QVERIFY2(requestError.isEmpty(), qPrintable(requestError));
    QCOMPARE(receivedArguments, expectedArguments);
    QCOMPARE(receivedWorkingDirectory, expectedWorkingDirectory);
    QCOMPARE(receivedEnvironment, expectedEnvironment);
}

QTEST_GUILESS_MAIN(LocalActivationServerTest)

#include "LocalActivationServerTest.moc"
