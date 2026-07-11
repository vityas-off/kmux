/*
    SPDX-FileCopyrightText: 2026 KDE Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "colorscheme/ColorSchemeManager.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTest>

using namespace Konsole;

class ColorSchemeManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testCustomSchemeReloadsFromKmuxDataDirectory();
    void testLegacySchemeIsReadOnly();
};

void ColorSchemeManagerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ColorSchemeManagerTest::testCustomSchemeReloadsFromKmuxDataDirectory()
{
    const QString name = QStringLiteral("KmuxReloadTest");
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kmux/") + name + QStringLiteral(".colorscheme");
    QFile::remove(path);

    auto scheme = std::make_shared<ColorScheme>();
    scheme->setName(name);

    ColorSchemeManager manager;
    manager.addColorScheme(scheme);
    QVERIFY2(QFile::exists(path), qPrintable(path));

    ColorSchemeManager reloadedManager;
    const auto reloadedScheme = reloadedManager.findColorScheme(name);
    QVERIFY(reloadedScheme != nullptr);
    QCOMPARE(reloadedScheme->name(), name);

    QVERIFY(QFile::remove(path));
}

void ColorSchemeManagerTest::testLegacySchemeIsReadOnly()
{
    const QString name = QStringLiteral("KmuxLegacyReadOnlyTest");
    const QString dataLocation = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString kmuxPath = dataLocation + QStringLiteral("/kmux/") + name + QStringLiteral(".colorscheme");
    const QString legacyPath = dataLocation + QStringLiteral("/konsole/") + name + QStringLiteral(".colorscheme");
    QFile::remove(kmuxPath);
    QFile::remove(legacyPath);
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).path()));

    QFile legacyFile(legacyPath);
    QVERIFY(legacyFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(legacyFile.write("[General]\nDescription=Legacy scheme\n"), 36);
    legacyFile.close();

    ColorSchemeManager manager;
    const auto legacyScheme = manager.findColorScheme(name);
    QVERIFY(legacyScheme != nullptr);
    QVERIFY(!manager.isColorSchemeDeletable(name));
    QVERIFY(!manager.canResetColorScheme(name));
    QVERIFY(!manager.deleteColorScheme(name));
    QVERIFY(QFile::exists(legacyPath));

    auto kmuxScheme = std::make_shared<ColorScheme>();
    kmuxScheme->setName(name);
    manager.addColorScheme(kmuxScheme);
    QVERIFY(QFile::exists(kmuxPath));
    QVERIFY(QFile::exists(legacyPath));
    QVERIFY(manager.isColorSchemeDeletable(name));
    QVERIFY(manager.canResetColorScheme(name));
    QVERIFY(manager.deleteColorScheme(name));
    QVERIFY(!QFile::exists(kmuxPath));
    QVERIFY(QFile::exists(legacyPath));
    QVERIFY(manager.findColorScheme(name) != nullptr);

    QVERIFY(QFile::remove(legacyPath));
}

QTEST_GUILESS_MAIN(ColorSchemeManagerTest)

#include "ColorSchemeManagerTest.moc"
