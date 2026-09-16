/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../widgets/ProjectIconDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Konsole;

class ProjectIconDialogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testSearchAndKeyboardSelection();
    void testFilteringPreservesSelection();
    void testCancelAndReset();
    void testCustomFileChoice_data();
    void testCustomFileChoice();
};

void ProjectIconDialogTest::testSearchAndKeyboardSelection()
{
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        const auto closeDialog = qScopeGuard([&] {
            if (dialog->isVisible()) {
                dialog->reject();
            }
        });
        auto *search = dialog->findChild<QLineEdit *>();
        auto *icons = dialog->findChild<QListView *>(QStringLiteral("projectIconList"));
        auto *collection = dialog->findChild<QComboBox *>();
        QVERIFY(search);
        QVERIFY(icons);
        QVERIFY(collection);
        QTRY_VERIFY(search->hasFocus());
        const auto *model = icons->model();
        const int total = model->rowCount();
        QVERIFY(total > 200);
        for (int row = 0; row < total; ++row) {
            const auto index = model->index(row, 0);
            const QString path = index.data(Qt::UserRole).toString();
            QVERIFY2(QFile::exists(path), qPrintable(path));
            const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            QVERIFY2(!icon.pixmap(32).isNull(), qPrintable(path));
        }

        search->setText(QStringLiteral("  DATABASE  "));
        QVERIFY(model->rowCount() > 1);
        QVERIFY(model->rowCount() < total);
        collection->setCurrentIndex(2); // Devicon
        QVERIFY(model->rowCount() > 1);
        for (int row = 0; row < model->rowCount(); ++row) {
            QVERIFY(model->index(row, 0).data(Qt::UserRole).toString().startsWith(QLatin1String(":/project-icons/devicon/")));
        }
        search->setText(QStringLiteral("C++"));
        QVERIFY(model->rowCount() >= 1);
        QVERIFY(model->index(0, 0).data(Qt::UserRole).toString().endsWith(QLatin1String("/cplusplus.svg")));
        search->setText(QStringLiteral(".*"));
        QCOMPARE(model->rowCount(), 0);
        QTest::keyClick(search, Qt::Key_Return);
        QVERIFY(dialog->isVisible());
        search->setText(QStringLiteral("PYTHON language"));
        QCOMPARE(model->rowCount(), 1);
        QTest::keyClick(search, Qt::Key_Return);
    });
    const auto selected = chooseProjectIcon({}, nullptr);
    QVERIFY(selected.has_value());
    QCOMPARE(*selected, QStringLiteral(":/project-icons/devicon/python.svg"));
}

void ProjectIconDialogTest::testFilteringPreservesSelection()
{
    const QString currentIcon = QStringLiteral(":/project-icons/devicon/monochrome/rust.svg");
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        const auto closeDialog = qScopeGuard([&] {
            if (dialog->isVisible()) {
                dialog->reject();
            }
        });
        auto *search = dialog->findChild<QLineEdit *>();
        auto *icons = dialog->findChild<QListView *>(QStringLiteral("projectIconList"));
        auto *collection = dialog->findChild<QComboBox *>();
        QCOMPARE(icons->currentIndex().data(Qt::UserRole).toString(), currentIcon);
        search->setText(QStringLiteral("rocket"));
        QCOMPARE(icons->model()->rowCount(), 1);
        QVERIFY(!icons->currentIndex().isValid());
        search->clear();
        QCOMPARE(icons->currentIndex().data(Qt::UserRole).toString(), currentIcon);
        collection->setCurrentIndex(1); // Material Symbols
        QVERIFY(!icons->currentIndex().isValid());
        collection->setCurrentIndex(0);
        QCOMPARE(icons->currentIndex().data(Qt::UserRole).toString(), currentIcon);
        search->setText(QStringLiteral("no_such_icon"));
        QCOMPARE(icons->model()->rowCount(), 0);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    const auto selected = chooseProjectIcon(currentIcon, nullptr);
    QVERIFY(selected.has_value());
    QCOMPARE(*selected, currentIcon);
}

void ProjectIconDialogTest::testCancelAndReset()
{
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        dialog->findChild<QLineEdit *>()->setText(QStringLiteral("docker"));
        QTest::keyClick(dialog, Qt::Key_Escape);
    });
    QVERIFY(!chooseProjectIcon(QStringLiteral("folder"), nullptr).has_value());

    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        const auto closeDialog = qScopeGuard([&] {
            if (dialog->isVisible()) {
                dialog->reject();
            }
        });
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        buttons->button(QDialogButtonBox::RestoreDefaults)->click();
        auto *icons = dialog->findChild<QListView *>(QStringLiteral("projectIconList"));
        QVERIFY(!icons->currentIndex().isValid());
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    const auto reset = chooseProjectIcon(QStringLiteral(":/project-icons/devicon/python.svg"), nullptr);
    QVERIFY(reset.has_value());
    QVERIFY(reset->isEmpty());
}

void ProjectIconDialogTest::testCustomFileChoice_data()
{
    QTest::addColumn<bool>("cancelFile");
    QTest::addColumn<bool>("cancelPicker");
    QTest::newRow("import") << false << false;
    QTest::newRow("cancel-file") << true << false;
    QTest::newRow("cancel-picker") << false << true;
}

void ProjectIconDialogTest::testCustomFileChoice()
{
    QFETCH(bool, cancelFile);
    QFETCH(bool, cancelPicker);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("custom icon.png"));
    QImage source(24, 24, QImage::Format_ARGB32);
    source.fill(QColor(40, 180, 90));
    QVERIFY(source.save(sourcePath));

    const QByteArray previousDataHome = qgetenv("XDG_DATA_HOME");
    const bool previousNativeDialogs = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restoreEnvironment = qScopeGuard([&] {
        if (previousDataHome.isNull()) {
            qunsetenv("XDG_DATA_HOME");
        } else {
            qputenv("XDG_DATA_HOME", previousDataHome);
        }
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previousNativeDialogs);
    });
    qputenv("XDG_DATA_HOME", directory.path().toUtf8());
    // Exercise the file-import UI without depending on the desktop's native
    // file chooser, which runs outside the test process on some platforms.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    QTimer::singleShot(0, [&] {
        auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(picker);
        const auto closePicker = qScopeGuard([&] {
            if (picker->isVisible()) {
                picker->reject();
            }
        });
        QTimer::singleShot(0, [&] {
            auto *fileDialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            QVERIFY(fileDialog);
            if (cancelFile) {
                fileDialog->reject();
            } else {
                fileDialog->selectFile(sourcePath);
                QMetaObject::invokeMethod(fileDialog, "accept");
            }
        });
        auto *fileButton = picker->findChild<QPushButton *>(QStringLiteral("projectIconFromFile"));
        QVERIFY(fileButton);
        fileButton->click();
        QVERIFY(picker->isVisible());
        picker->findChild<QDialogButtonBox *>()->button(cancelPicker ? QDialogButtonBox::Cancel : QDialogButtonBox::Ok)->click();
    });
    const QString originalIcon = QStringLiteral(":/project-icons/material/code.svg");
    const auto selected = chooseProjectIcon(originalIcon, nullptr);
    if (cancelPicker) {
        QVERIFY(!selected.has_value());
    } else if (cancelFile) {
        QVERIFY(selected.has_value());
        QCOMPARE(*selected, originalIcon);
    } else {
        QVERIFY(selected.has_value());
        QVERIFY(*selected != sourcePath);
        QVERIFY(selected->startsWith(directory.path() + QLatin1Char('/')));
        QVERIFY(QFile::remove(sourcePath));
        QCOMPARE(QImage(*selected), source);
    }
}

QTEST_MAIN(ProjectIconDialogTest)

#include "ProjectIconDialogTest.moc"
