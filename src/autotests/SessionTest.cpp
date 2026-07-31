/*
    SPDX-FileCopyrightText: 2013 Kurt Hindenburg <kurt.hindenburg@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "SessionTest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

// Konsole
#include "../Emulation.h"
#include "../config-konsole.h"
#include "../profile/Profile.h"
#include "../session/Session.h"
#include "../session/SessionManager.h"

using namespace Konsole;

void SessionTest::testNoProfile()
{
    auto session = new Session();

    // No profile loaded, nothing to run
    QCOMPARE(session->isRunning(), false);
    QCOMPARE(session->sessionId(), 1);
    QCOMPARE(session->isRemote(), false);
    QCOMPARE(session->program(), QString());
    QCOMPARE(session->arguments(), QStringList());
    QCOMPARE(session->tabTitleFormat(Session::LocalTabTitle), QString());
    QCOMPARE(session->tabTitleFormat(Session::RemoteTabTitle), QString());

    delete session;
}

void SessionTest::testEmulation()
{
    auto session = new Session();

    Emulation *emulation = session->emulation();

    QCOMPARE(emulation->lineCount(), 40);

    delete session;
}

void SessionTest::testVersionEnvironment()
{
    QVERIFY(QLatin1String(KMUX_VERSION) != QLatin1String(KONSOLE_VERSION));
    QCOMPARE(QLatin1String(KONSOLE_VERSION), QLatin1String("26.07.70"));

    Session session;
    Profile::Ptr profile(new Profile);
    profile->setProperty(Profile::Environment, QStringList());
    SessionManager manager;
    manager.setSessionProfile(&session, profile);

    QVERIFY(session.environment().contains(QLatin1String("KONSOLE_VERSION=260770")));
    QVERIFY(!session.environment().contains(QLatin1String("KONSOLE_VERSION=000100")));
}

void SessionTest::testKmuxAgentShimsPrecedeProfilePath()
{
#ifdef Q_OS_WIN
    QSKIP("Agent command shims are only installed on Unix platforms.");
#else
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString shimPath = temporaryDir.filePath(QStringLiteral("claude"));
    QFile shim(shimPath);
    QVERIFY(shim.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(shim.write("#!/bin/sh\n"), qint64(10));
    shim.close();
    QVERIFY(QFile::setPermissions(shimPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    Session session;
    const QString previousApplicationName = QCoreApplication::applicationName();
    const bool hadShimOverride = qEnvironmentVariableIsSet("KMUX_AGENT_SHIM_DIRECTORY");
    const QByteArray previousShimOverride = qgetenv("KMUX_AGENT_SHIM_DIRECTORY");
    const auto restoreEnvironment = qScopeGuard([&]() {
        QCoreApplication::setApplicationName(previousApplicationName);
        if (hadShimOverride) {
            qputenv("KMUX_AGENT_SHIM_DIRECTORY", previousShimOverride);
        } else {
            qunsetenv("KMUX_AGENT_SHIM_DIRECTORY");
        }
    });
    QCoreApplication::setApplicationName(QStringLiteral("kmux"));
    qputenv("KMUX_AGENT_SHIM_DIRECTORY", temporaryDir.path().toLocal8Bit());

    session.setEnvironment({QStringLiteral("PATH=/profile/bin")});
    session.prependKmuxAgentShimsToPath();

    const QString expectedPath = QStringLiteral("PATH=") + temporaryDir.path() + QDir::listSeparator() + QStringLiteral("/profile/bin");
    QVERIFY(session.environment().contains(expectedPath));
#endif
}

QTEST_MAIN(SessionTest)

#include "moc_SessionTest.cpp"
