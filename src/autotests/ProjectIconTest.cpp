/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../workspaces/ProjectIcon.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPalette>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

using namespace Konsole;

class ProjectIconTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testCustomIconSurvivesSourceRemoval();
    void testMaterialIconsFollowPalette();
    void testDeviconColorsArePreserved();
};

void ProjectIconTest::testCustomIconSurvivesSourceRemoval()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataHome = qgetenv("XDG_DATA_HOME");
    const auto restoreDataHome = qScopeGuard([&] {
        if (previousDataHome.isNull()) {
            qunsetenv("XDG_DATA_HOME");
        } else {
            qputenv("XDG_DATA_HOME", previousDataHome);
        }
    });
    qputenv("XDG_DATA_HOME", directory.path().toUtf8());

    const QString sourcePath = directory.filePath(QStringLiteral("custom icon.png"));
    QImage source(24, 24, QImage::Format_ARGB32);
    source.fill(QColor(180, 40, 90));
    QVERIFY(source.save(sourcePath));

    QString error;
    const QString imported = ProjectIcon::importFile(sourcePath, error);
    QVERIFY2(!imported.isEmpty(), qPrintable(error));
    QVERIFY(imported != sourcePath);
    QVERIFY(imported.startsWith(directory.path() + QLatin1Char('/')));
    QCOMPARE(ProjectIcon::importFile(sourcePath, error), imported);
    QVERIFY(QFile::remove(sourcePath));
    QCOMPARE(ProjectIcon::icon(imported).pixmap(24).toImage().pixelColor(12, 12), QColor(180, 40, 90));

    QFile invalid(directory.filePath(QStringLiteral("invalid.svg")));
    QVERIFY(invalid.open(QIODevice::WriteOnly));
    invalid.write("not an image");
    invalid.close();
    QVERIFY(ProjectIcon::importFile(invalid.fileName(), error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(QFile::exists(imported));
}

void ProjectIconTest::testMaterialIconsFollowPalette()
{
    const QPalette originalPalette = QApplication::palette();
    const auto restorePalette = qScopeGuard([&] {
        QApplication::setPalette(originalPalette);
    });
    QStringList files;
    for (const QString &directory : {QStringLiteral(":/project-icons/material"), QStringLiteral(":/project-icons/devicon/monochrome")}) {
        const QDir icons(directory);
        for (const auto &file : icons.entryList({QStringLiteral("*.svg")}, QDir::Files)) {
            files.append(icons.filePath(file));
        }
    }
    QVERIFY(!files.isEmpty());

    for (const auto &file : files) {
        const QIcon icon = ProjectIcon::icon(file);
        // Reuse the same icon across palette changes, as the project rail does.
        for (const QColor &color : {QColor(30, 35, 40), QColor(230, 235, 240)}) {
            QPalette palette = originalPalette;
            palette.setColor(QPalette::Text, color);
            QApplication::setPalette(palette);
            const QPixmap pixmap = icon.pixmap(QSize(24, 24), 2.0);
            QCOMPARE(pixmap.size(), QSize(48, 48));
            QCOMPARE(pixmap.devicePixelRatio(), 2.0);
            const QImage pixels = pixmap.toImage();
            bool foundOpaquePixel = false;
            for (int y = 0; y < pixels.height(); ++y) {
                for (int x = 0; x < pixels.width(); ++x) {
                    if (pixels.pixelColor(x, y).alpha() == 255) {
                        QCOMPARE(pixels.pixelColor(x, y), color);
                        foundOpaquePixel = true;
                    }
                }
            }
            QVERIFY2(foundOpaquePixel, qPrintable(file));
        }
    }
}

void ProjectIconTest::testDeviconColorsArePreserved()
{
    const QDir icons(QStringLiteral(":/project-icons/devicon"));
    const auto files = icons.entryList({QStringLiteral("*.svg")}, QDir::Files);
    QVERIFY(!files.isEmpty());
    for (const auto &file : files) {
        const QString path = icons.filePath(file);
        const QImage original = QIcon(path).pixmap(32).toImage();
        QVERIFY2(!original.isNull(), qPrintable(file));
        QCOMPARE(ProjectIcon::icon(path, Qt::white).pixmap(32).toImage(), original);
        QCOMPARE(ProjectIcon::icon(path, Qt::black).pixmap(32).toImage(), original);
    }
}

QTEST_MAIN(ProjectIconTest)

#include "ProjectIconTest.moc"
