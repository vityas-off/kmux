/*
    SPDX-FileCopyrightText: 2013 Kurt Hindenburg <kurt.hindenburg@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "TerminalTest.h"

#include <QMouseEvent>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

// Konsole
#include "../Emulation.h"
#include "../filterHotSpots/Filter.h"
#include "../filterHotSpots/FilterChain.h"
#include "../filterHotSpots/HotSpot.h"
#include "../profile/Profile.h"
#include "../session/Session.h"
#include "../session/SessionController.h"
#include "../session/SessionManager.h"
#include "../terminalDisplay/TerminalColor.h"
#include "../terminalDisplay/TerminalDisplay.h"
#include "../terminalDisplay/TerminalScrollBar.h"

#include "../characters/CharacterColor.h"

using namespace Konsole;

void TerminalTest::testScrollBarPositions()
{
    TerminalDisplay display(nullptr);

    // ScrollBar Positions
    display.scrollBar()->setScrollBarPosition(Enum::ScrollBarLeft);
    QCOMPARE(display.scrollBar()->scrollBarPosition(), Enum::ScrollBarLeft);
    display.scrollBar()->setScrollBarPosition(Enum::ScrollBarRight);
    QCOMPARE(display.scrollBar()->scrollBarPosition(), Enum::ScrollBarRight);
    display.scrollBar()->setScrollBarPosition(Enum::ScrollBarHidden);
    QCOMPARE(display.scrollBar()->scrollBarPosition(), Enum::ScrollBarHidden);
}

void TerminalTest::testColorTable()
{
    // These are from ColorScheme.cpp but they can be anything to test
    const QColor defaultTable[TABLE_COLORS] = {QColor(0x00, 0x00, 0x00), QColor(0xFF, 0xFF, 0xFF), QColor(0x00, 0x00, 0x00), QColor(0xB2, 0x18, 0x18),
                                               QColor(0x18, 0xB2, 0x18), QColor(0xB2, 0x68, 0x18), QColor(0x18, 0x18, 0xB2), QColor(0xB2, 0x18, 0xB2),
                                               QColor(0x18, 0xB2, 0xB2), QColor(0xB2, 0xB2, 0xB2), QColor(0x00, 0x00, 0x00), QColor(0xFF, 0xFF, 0xFF),
                                               QColor(0x68, 0x68, 0x68), QColor(0xFF, 0x54, 0x54), QColor(0x54, 0xFF, 0x54), QColor(0xFF, 0xFF, 0x54),
                                               QColor(0x54, 0x54, 0xFF), QColor(0xFF, 0x54, 0xFF), QColor(0x54, 0xFF, 0xFF), QColor(0x00, 0xFF, 0xFF)};

    TerminalDisplay display(nullptr);

    display.terminalColor()->setColorTable(defaultTable);

    const QColor *colorTable = display.terminalColor()->colorTable();

    for (int i = 0; i < TABLE_COLORS; i++) {
        QCOMPARE(colorTable[i], defaultTable[i]);
    }

    QColor colorEntry = QColor(0x00, 0x00, 0x00);
    QVERIFY(colorTable[1] != colorEntry);
}

void TerminalTest::testSize()
{
    TerminalDisplay display(nullptr);

    QCOMPARE(display.columns(), 1);
    QCOMPARE(display.lines(), 1);

    // TODO: setSize doesn't change size...
    // display.setSize(80, 25);
}

namespace
{
class CountingLinkHotSpot : public HotSpot
{
public:
    CountingLinkHotSpot()
        : HotSpot(0, 0, 0, 4)
    {
        setType(EscapedUrl);
    }

    void activate(QObject *) override
    {
        ++activationCount;
    }

    int activationCount = 0;
};

class CountingLinkFilter : public Filter
{
public:
    void process() override
    {
        addHotSpot(spot);
    }

    QSharedPointer<CountingLinkHotSpot> spot = QSharedPointer<CountingLinkHotSpot>::create();
};
}

void TerminalTest::testLinkClickMouseTracking_data()
{
    QTest::addColumn<bool>("mouseTracking");
    QTest::addColumn<bool>("directClick");
    QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
    QTest::addColumn<bool>("onLink");
    QTest::addColumn<bool>("releaseOnLink");
    QTest::addColumn<int>("expectedActivations");
    QTest::addColumn<int>("expectedMouseEvents");

    QTest::newRow("ctrl-link-tracking") << true << false << Qt::KeyboardModifiers(Qt::ControlModifier) << true << true << 1 << 0;
    QTest::newRow("ctrl-link-no-tracking") << false << false << Qt::KeyboardModifiers(Qt::ControlModifier) << true << true << 1 << 0;
    QTest::newRow("plain-link-tracking") << true << false << Qt::KeyboardModifiers() << true << true << 0 << 3;
    QTest::newRow("plain-link-no-tracking") << false << false << Qt::KeyboardModifiers() << true << true << 0 << 0;
    QTest::newRow("ctrl-outside-link") << true << false << Qt::KeyboardModifiers(Qt::ControlModifier) << false << false << 0 << 3;
    QTest::newRow("ctrl-move-onto-link") << true << false << Qt::KeyboardModifiers(Qt::ControlModifier) << false << true << 0 << 3;
    QTest::newRow("direct-link-tracking") << true << true << Qt::KeyboardModifiers() << true << true << 1 << 0;
    QTest::newRow("shift-link-tracking") << true << false << Qt::KeyboardModifiers(Qt::ShiftModifier) << true << true << 0 << 0;
    QTest::newRow("ctrl-shift-link-tracking") << true << false << Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier) << true << true << 1 << 0;
}

void TerminalTest::testLinkClickMouseTracking()
{
    QFETCH(bool, mouseTracking);
    QFETCH(bool, directClick);
    QFETCH(Qt::KeyboardModifiers, modifiers);
    QFETCH(bool, onLink);
    QFETCH(bool, releaseOnLink);
    QFETCH(int, expectedActivations);
    QFETCH(int, expectedMouseEvents);

    Profile::Ptr profile(new Profile);
    profile->useBuiltin();
    profile->setProperty(Profile::OpenLinksByDirectClickEnabled, directClick);

    Session session;
    disconnect(session.emulation(), &Emulation::imageSizeInitialized, &session, &Session::run);
    SessionManager::instance()->setSessionProfile(&session, profile);

    TerminalDisplay display;
    SessionController controller(&session, &display, nullptr);
    display.applyProfile(profile);
    session.addView(&display);
    const auto detachScreenWindow = qScopeGuard([&display]() {
        display.setScreenWindow(nullptr);
    });
    display.resize(800, 400);
    display.setAttribute(Qt::WA_DontShowOnScreen);
    display.show();
    QCoreApplication::processEvents();

    auto *filter = new CountingLinkFilter;
    display.filterChain()->addFilter(filter);
    QSignalSpy outputChanges(session.emulation(), &Emulation::outputChanged);
    session.emulation()->receiveData("link", 4);
    QTRY_VERIFY(!outputChanges.isEmpty());
    display.setUsesMouseTracking(mouseTracking);
    display.processFilters();

    QSignalSpy mouseEvents(&display, &TerminalDisplay::mouseSignal);
    QSignalSpy exactMouseEvents(&display, &TerminalDisplay::exactMouseSignal);
    const QPoint clickPosition = display.topLeftWidgetPos(onLink ? 1 : 10, 0) + QPoint(1, 1);
    QCOMPARE(display.getCharacterPosition(clickPosition, false), qMakePair(0, onLink ? 1 : 10));
    QCOMPARE(display.filterActions(clickPosition).get(), onLink ? filter->spot.get() : nullptr);
    const QPoint releasePosition = display.topLeftWidgetPos(releaseOnLink ? 1 : 10, 0) + QPoint(1, 1);
    QTest::mousePress(&display, Qt::LeftButton, modifiers, clickPosition);
    if (mouseTracking) {
        QMouseEvent move(QEvent::MouseMove, releasePosition, display.mapToGlobal(releasePosition), Qt::NoButton, Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(&display, &move);
    }
    QTest::mouseRelease(&display, Qt::LeftButton, modifiers, releasePosition);

    QCOMPARE(filter->spot->activationCount, expectedActivations);
    QCOMPARE(mouseEvents.count(), expectedMouseEvents);
    QCOMPARE(exactMouseEvents.count(), expectedMouseEvents);

    mouseEvents.clear();
    exactMouseEvents.clear();
    QTest::mouseClick(&display, Qt::LeftButton, Qt::NoModifier, display.topLeftWidgetPos(10, 0) + QPoint(1, 1));
    QCOMPARE(filter->spot->activationCount, expectedActivations);
    QCOMPARE(mouseEvents.count(), mouseTracking ? 2 : 0);
    QCOMPARE(exactMouseEvents.count(), mouseTracking ? 2 : 0);
}

QTEST_MAIN(TerminalTest)

#include "moc_TerminalTest.cpp"
