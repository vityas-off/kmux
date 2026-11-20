/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../ProgramStatus.h"

#include <QTest>

using namespace Konsole;

class ProgramStatusTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testParsesReports();
    void testSkipsMalformedPairs();
    void testIgnoresInvalidReports_data();
    void testIgnoresInvalidReports();
    void testDecodesText();
    void testParsesQueryAndClear();
    void testRecordsReplaceAndClear();
    void testRecordsDropTransientWork();
    void testRecordsEvictLeastRecentlyUpdated();
    void testRecordsInheritApp();

private:
    static ProgramStatusMessage parse(const QString &payload)
    {
        const auto message = parseProgramStatusMessage(payload);
        if (!message.has_value()) {
            qFatal("Unexpectedly ignored program status payload");
        }
        return *message;
    }

    static ProgramStatusMessage report(const QString &id, ProgramState state, const QString &app = {})
    {
        ProgramStatusMessage message;
        message.type = ProgramStatusMessage::Type::Report;
        message.record.id = id;
        message.record.state = state;
        message.record.app = app;
        return message;
    }
};

void ProgramStatusTest::testParsesReports()
{
    auto message = parse(QStringLiteral("state=blocked:app=codex:kind=permission:progress=40"));
    QCOMPARE(message.type, ProgramStatusMessage::Type::Report);
    QVERIFY(message.record.id.isEmpty());
    QCOMPARE(message.record.state, ProgramState::Blocked);
    QCOMPARE(message.record.app, QStringLiteral("codex"));
    QCOMPARE(message.record.kind, ProgramStatusKind::Permission);
    QCOMPARE(message.record.progress, 40);

    // Kind belongs to blocked reports and progress to running or blocked ones.
    message = parse(QStringLiteral("state=done:kind=permission:progress=40:id=build/test"));
    QCOMPARE(message.record.state, ProgramState::Done);
    QCOMPARE(message.record.id, QStringLiteral("build/test"));
    QCOMPARE(message.record.kind, ProgramStatusKind::None);
    QVERIFY(!message.record.progress.has_value());

    // An unknown kind and an invalid progress are treated as absent.
    message = parse(QStringLiteral("state=blocked:kind=coffee:progress=101"));
    QCOMPARE(message.record.kind, ProgramStatusKind::None);
    QVERIFY(!message.record.progress.has_value());
}

void ProgramStatusTest::testSkipsMalformedPairs()
{
    // Whitespace around keys and values is stripped, pairs without a value,
    // with an empty or invalid key, or with an invalid value are skipped,
    // unknown keys are ignored, and the last value of a key wins.
    const auto message = parse(QStringLiteral(" app = cargo :malformed:=x:Key=y:note=a b:future=1:state=idle:state=working"));
    QCOMPARE(message.record.state, ProgramState::Working);
    QCOMPARE(message.record.app, QStringLiteral("cargo"));
}

void ProgramStatusTest::testIgnoresInvalidReports_data()
{
    QTest::addColumn<QString>("payload");

    QTest::newRow("missing state") << QStringLiteral("app=codex");
    QTest::newRow("unknown state") << QStringLiteral("state=sleeping:app=codex");
    QTest::newRow("empty id") << QStringLiteral("state=idle:id=");
    QTest::newRow("empty id segment") << QStringLiteral("state=idle:id=build//test");
    QTest::newRow("long id segment") << QStringLiteral("state=idle:id=") + QString(33, QLatin1Char('a'));
    QTest::newRow("deep id") << QStringLiteral("state=idle:id=a/b/c/d/e/f/g/h/i");
    QTest::newRow("invalid id clear") << QStringLiteral("state=clear:id=a/");
    QTest::newRow("invalid app") << QStringLiteral("state=idle:app=a/b");
    QTest::newRow("long app") << QStringLiteral("state=idle:app=") + QString(33, QLatin1Char('a'));
    QTest::newRow("long key") << QStringLiteral("state=idle:") + QString(17, QLatin1Char('k')) + QStringLiteral("=1");
    QTest::newRow("invalid base64") << QStringLiteral("state=idle:msg=a-b");
    QTest::newRow("control character") << QStringLiteral("state=idle:msg=") + QString::fromLatin1(QByteArray("a\nb").toBase64());
    QTest::newRow("invalid UTF-8") << QStringLiteral("state=idle:title=") + QString::fromLatin1(QByteArray("\xff\xfe").toBase64());
    QTest::newRow("long title") << QStringLiteral("state=idle:title=") + QString::fromLatin1(QByteArray(193, 'a').toBase64());
    QTest::newRow("long payload") << QStringLiteral("state=idle:") + QString(4100, QLatin1Char('x'));
}

void ProgramStatusTest::testIgnoresInvalidReports()
{
    QFETCH(QString, payload);
    QVERIFY(!parseProgramStatusMessage(payload).has_value());
}

void ProgramStatusTest::testDecodesText()
{
    const QString title = QString::fromLatin1(QStringLiteral("Сборка").toUtf8().toBase64());
    QString message = QString::fromLatin1(QByteArray("Waiting for review").toBase64());
    // Padding is optional.
    while (message.endsWith(QLatin1Char('='))) {
        message.chop(1);
    }

    const auto parsed = parse(QStringLiteral("state=working:title=%1:msg=%2").arg(title, message));
    QCOMPARE(parsed.record.title, QStringLiteral("Сборка"));
    QCOMPARE(parsed.record.message, QStringLiteral("Waiting for review"));
}

void ProgramStatusTest::testParsesQueryAndClear()
{
    QCOMPARE(parse(QStringLiteral("?")).type, ProgramStatusMessage::Type::Query);
    QVERIFY(programStatusQueryReply().startsWith("\033]7501;?"));
    QVERIFY(programStatusQueryReply().endsWith("\033\\"));

    auto message = parse(QStringLiteral("state=clear"));
    QCOMPARE(message.type, ProgramStatusMessage::Type::Clear);
    QVERIFY(message.record.id.isEmpty());

    message = parse(QStringLiteral("state=clear:id=build:app=a/b"));
    QCOMPARE(message.type, ProgramStatusMessage::Type::Clear);
    QCOMPARE(message.record.id, QStringLiteral("build"));
}

void ProgramStatusTest::testRecordsReplaceAndClear()
{
    ProgramStatusRecords records;
    QVERIFY(records.apply(report({}, ProgramState::Working, QStringLiteral("cargo"))));
    QVERIFY(records.apply(report(QStringLiteral("build"), ProgramState::Working)));
    QVERIFY(records.apply(report(QStringLiteral("build/test"), ProgramState::Working)));
    QVERIFY(records.apply(report(QStringLiteral("builder"), ProgramState::Working)));
    QVERIFY(!records.apply(report(QStringLiteral("build"), ProgramState::Working)));

    // Each report replaces its record completely.
    QVERIFY(records.apply(report({}, ProgramState::Done)));
    QCOMPARE(records.record({})->state, ProgramState::Done);
    QVERIFY(records.record({})->app.isEmpty());

    ProgramStatusMessage clear;
    clear.type = ProgramStatusMessage::Type::Clear;
    clear.record.id = QStringLiteral("build");
    QVERIFY(records.apply(clear));
    QVERIFY(!records.record(QStringLiteral("build")).has_value());
    QVERIFY(!records.record(QStringLiteral("build/test")).has_value());
    QVERIFY(records.record(QStringLiteral("builder")).has_value());
    QVERIFY(!records.apply(clear));

    clear.record.id.clear();
    QVERIFY(records.apply(clear));
    QVERIFY(records.isEmpty());
}

void ProgramStatusTest::testRecordsDropTransientWork()
{
    ProgramStatusRecords records;
    records.apply(report({}, ProgramState::Done));
    records.apply(report(QStringLiteral("a"), ProgramState::Working));
    records.apply(report(QStringLiteral("b"), ProgramState::Blocked));
    records.apply(report(QStringLiteral("c"), ProgramState::Idle));
    records.apply(report(QStringLiteral("d"), ProgramState::Error));

    QVERIFY(records.dropTransientRecords());
    QCOMPARE(records.records().size(), 2);
    QVERIFY(records.record({}).has_value());
    QVERIFY(records.record(QStringLiteral("d")).has_value());
    QVERIFY(!records.dropTransientRecords());
}

void ProgramStatusTest::testRecordsEvictLeastRecentlyUpdated()
{
    ProgramStatusRecords records;
    for (int index = 0; index < ProgramStatusRecords::MaximumRecordCount; ++index) {
        records.apply(report(QStringLiteral("task%1").arg(index), ProgramState::Working));
    }
    // Updating the oldest record makes it the most recent one.
    records.apply(report(QStringLiteral("task0"), ProgramState::Done));
    records.apply(report(QStringLiteral("extra"), ProgramState::Working));

    QCOMPARE(records.records().size(), ProgramStatusRecords::MaximumRecordCount);
    QVERIFY(records.record(QStringLiteral("task0")).has_value());
    QVERIFY(!records.record(QStringLiteral("task1")).has_value());
    QVERIFY(records.record(QStringLiteral("extra")).has_value());
}

void ProgramStatusTest::testRecordsInheritApp()
{
    ProgramStatusRecords records;
    records.apply(report({}, ProgramState::Working, QStringLiteral("claude-code")));
    records.apply(report(QStringLiteral("agents/a1"), ProgramState::Working));
    records.apply(report(QStringLiteral("tools"), ProgramState::Working, QStringLiteral("cargo")));
    records.apply(report(QStringLiteral("tools/test"), ProgramState::Working));

    QCOMPARE(records.app({}), QStringLiteral("claude-code"));
    QCOMPARE(records.app(QStringLiteral("agents/a1")), QStringLiteral("claude-code"));
    QCOMPARE(records.app(QStringLiteral("tools/test")), QStringLiteral("cargo"));
}

QTEST_GUILESS_MAIN(ProgramStatusTest)

#include "ProgramStatusTest.moc"
