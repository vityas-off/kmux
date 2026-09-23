/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "widgets/ProjectIconDialog.h"
#include "workspaces/ProjectIcon.h"
#include "workspaces/ProjectIconCatalog.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

#include <KIconDialog>
#include <KLocalizedString>
#include <KMessageBox>

#include <algorithm>

namespace
{
enum IconRole {
    ResourceRole = Qt::UserRole,
    SearchRole,
    CollectionRole
};

class IconSearchEdit : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            Q_EMIT returnPressed();
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};

class IconFilterModel : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setFilter(const QString &query, int collection)
    {
        _words = query.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        _collection = collection;
        invalidate();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const auto index = sourceModel()->index(row, 0, parent);
        if (_collection != -1 && index.data(CollectionRole).toInt() != _collection) {
            return false;
        }
        const QString text = index.data(SearchRole).toString();
        return std::all_of(_words.cbegin(), _words.cend(), [&](const QString &word) {
            return text.contains(word, Qt::CaseInsensitive);
        });
    }

private:
    QStringList _words;
    int _collection = -1;
};
}

std::optional<QString> Konsole::chooseProjectIcon(const QString &currentIcon, QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(i18nc("@title:window", "Project Icon"));
    dialog.resize(720, 580);
    auto *layout = new QVBoxLayout(&dialog);
    auto *searchRow = new QHBoxLayout;
    auto *search = new IconSearchEdit(&dialog);
    search->setPlaceholderText(i18nc("@info:placeholder", "Search icons…"));
    search->setAccessibleName(i18nc("@label", "Search icons"));
    search->setClearButtonEnabled(true);
    search->addAction(QIcon::fromTheme(QStringLiteral("edit-find")), QLineEdit::LeadingPosition);
    auto *collection = new QComboBox(&dialog);
    collection->setAccessibleName(i18nc("@label", "Icon collection"));
    collection->addItem(i18nc("@item:inlistbox", "All icons"), -1);
    collection->addItem(QStringLiteral("Material Symbols"), int(ProjectIcon::Collection::Material));
    collection->addItem(QStringLiteral("Devicon"), int(ProjectIcon::Collection::Devicon));
    searchRow->addWidget(search, 1);
    searchRow->addWidget(collection);
    layout->addLayout(searchRow);

    const QSize iconCellSize(100, 76);
    auto *model = new QStandardItemModel(&dialog);
    for (const auto &entry : ProjectIcon::catalog()) {
        auto *item = new QStandardItem(ProjectIcon::icon(entry.resource, dialog.palette().color(QPalette::Text)), entry.label);
        item->setSizeHint(iconCellSize);
        item->setData(entry.resource, ResourceRole);
        const QString searchText = entry.label + QLatin1Char(' ') + entry.keywords;
        item->setData(searchText, SearchRole);
        item->setData(int(entry.collection), CollectionRole);
        const auto collectionName = entry.collection == ProjectIcon::Collection::Material ? QStringLiteral("Material Symbols") : QStringLiteral("Devicon");
        item->setToolTip(entry.label + QStringLiteral(" — ") + collectionName);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        model->appendRow(item);
    }
    auto *filter = new IconFilterModel(&dialog);
    filter->setSourceModel(model);
    auto *icons = new QListView(&dialog);
    icons->setObjectName(QStringLiteral("projectIconList"));
    icons->setAccessibleName(i18nc("@label", "Project icons"));
    icons->setModel(filter);
    icons->setViewMode(QListView::IconMode);
    icons->setResizeMode(QListView::Adjust);
    icons->setMovement(QListView::Static);
    icons->setSelectionMode(QAbstractItemView::SingleSelection);
    icons->setEditTriggers(QAbstractItemView::NoEditTriggers);
    icons->setIconSize(QSize(32, 32));
    icons->setGridSize(iconCellSize);
    icons->setUniformItemSizes(true);
    icons->setWordWrap(true);
    icons->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    icons->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *results = new QStackedWidget(&dialog);
    results->setMinimumSize(440, 280);
    results->addWidget(icons);
    auto *empty = new QLabel(i18nc("@info", "No matching icons. Try another search or collection."), &dialog);
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    results->addWidget(empty);
    layout->addWidget(results, 1);
    auto *count = new QLabel(&dialog);
    layout->addWidget(count);

    QString selectedIcon = currentIcon;
    auto *preview = new QLabel(&dialog);
    preview->setFixedSize(32, 32);
    preview->setAccessibleName(i18nc("@label", "Selected icon"));
    auto *selectedLabel = new QLabel(&dialog);
    selectedLabel->setTextFormat(Qt::PlainText);
    selectedLabel->setWordWrap(true);
    auto updateSelection = [&](const QString &name) {
        selectedIcon = name;
        preview->setPixmap(ProjectIcon::icon(name, dialog.palette().color(QPalette::Text)).pixmap(QSize(32, 32), dialog.devicePixelRatioF()));
        QString label = name;
        if (name.isEmpty()) {
            label = i18nc("@item:inlistbox", "Default folder");
        } else if (!name.startsWith(QLatin1Char(':')) && QDir::isAbsolutePath(name)) {
            label = i18nc("@item:inlistbox", "Custom icon");
        }
        QModelIndex selectedIndex;
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto index = model->index(row, 0);
            if (index.data(ResourceRole).toString() == name) {
                label = index.data(Qt::DisplayRole).toString();
                selectedIndex = filter->mapFromSource(index);
                break;
            }
        }
        selectedLabel->setText(label);
        const QSignalBlocker blocker(icons->selectionModel());
        icons->selectionModel()->clear();
        icons->setCurrentIndex(selectedIndex);
    };
    QObject::connect(icons->selectionModel(), &QItemSelectionModel::currentChanged, &dialog, [&](const QModelIndex &index) {
        if (index.isValid()) {
            updateSelection(index.data(ResourceRole).toString());
        }
    });
    auto updateFilter = [&] {
        // Filtering must not replace a selection that is temporarily hidden.
        const QSignalBlocker blocker(icons->selectionModel());
        filter->setFilter(search->text(), collection->currentData().toInt());
        results->setCurrentIndex(filter->rowCount() == 0 ? 1 : 0);
        count->setText(i18ncp("@label", "%1 icon", "%1 icons", filter->rowCount()));
        updateSelection(selectedIcon);
        icons->scrollToTop();
    };
    QObject::connect(search, &QLineEdit::textChanged, &dialog, updateFilter);
    QObject::connect(collection, &QComboBox::currentIndexChanged, &dialog, updateFilter);

    auto *sources = new QHBoxLayout;
    sources->addWidget(preview);
    sources->addWidget(selectedLabel, 1);
    auto *themeButton = new QPushButton(i18nc("@action:button", "KDE Icons…"), &dialog);
    auto *fileButton = new QPushButton(i18nc("@action:button", "From File…"), &dialog);
    fileButton->setObjectName(QStringLiteral("projectIconFromFile"));
    sources->addWidget(themeButton);
    sources->addWidget(fileButton);
    layout->addLayout(sources);

    QObject::connect(themeButton, &QPushButton::clicked, &dialog, [&] {
        KIconDialog iconDialog(&dialog);
        iconDialog.setWindowTitle(i18nc("@title:window", "Project Icon"));
        iconDialog.setup(KIconLoader::Desktop, KIconLoader::Place, false, 32);
        iconDialog.setSelectedIcon(selectedIcon);
        const QString name = iconDialog.openDialog();
        if (!name.isEmpty()) {
            updateSelection(name);
        }
    });
    QObject::connect(fileButton, &QPushButton::clicked, &dialog, [&] {
        const QString path = QFileDialog::getOpenFileName(&dialog,
                                                          i18nc("@title:window", "Choose Project Icon"),
                                                          QString(),
                                                          i18nc("@item:inlistbox", "Images (*.svg *.svgz *.png *.jpg *.jpeg *.webp *.ico *.xpm)"));
        if (!path.isEmpty()) {
            updateSelection(path);
        }
    });

    auto *dialogButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
    // Enter in the search field chooses the first result; Enter in the grid
    // accepts its current selection. An empty result must not accept a stale icon.
    dialogButtons->button(QDialogButtonBox::Ok)->setAutoDefault(false);
    dialogButtons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
    themeButton->setAutoDefault(false);
    fileButton->setAutoDefault(false);
    dialogButtons->button(QDialogButtonBox::RestoreDefaults)->setAutoDefault(false);
    QObject::connect(search, &QLineEdit::returnPressed, &dialog, [&] {
        if (filter->rowCount() > 0) {
            updateSelection(filter->index(0, 0).data(ResourceRole).toString());
            dialog.accept();
        }
    });
    QObject::connect(icons, &QListView::activated, &dialog, [&](const QModelIndex &index) {
        updateSelection(index.data(ResourceRole).toString());
        dialog.accept();
    });
    QObject::connect(dialogButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(dialogButtons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&] {
        updateSelection({});
    });
    layout->addWidget(dialogButtons);
    updateFilter();
    QTimer::singleShot(0, &dialog, [icons] {
        icons->scrollTo(icons->currentIndex());
    });
    search->setFocus();

    while (dialog.exec() == QDialog::Accepted) {
        if (!selectedIcon.startsWith(QLatin1Char(':')) && QDir::isAbsolutePath(selectedIcon)) {
            QString error;
            const QString importedIcon = ProjectIcon::importFile(selectedIcon, error);
            if (importedIcon.isEmpty()) {
                KMessageBox::error(&dialog, error, i18nc("@title:window", "Could Not Import Icon"));
                continue;
            }
            return importedIcon;
        }
        return selectedIcon;
    }
    return std::nullopt;
}
