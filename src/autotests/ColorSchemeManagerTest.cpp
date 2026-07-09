/*
    SPDX-FileCopyrightText: 2026 KDE Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "colorscheme/ColorSchemeManager.h"

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

QTEST_GUILESS_MAIN(ColorSchemeManagerTest)

#include "ColorSchemeManagerTest.moc"
