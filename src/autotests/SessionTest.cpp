/*
    SPDX-FileCopyrightText: 2013 Kurt Hindenburg <kurt.hindenburg@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "SessionTest.h"

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

QTEST_MAIN(SessionTest)

#include "moc_SessionTest.cpp"
