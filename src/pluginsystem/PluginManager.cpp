/*  This file was part of the KDE libraries

    SPDX-FileCopyrightText: 2021 Tomaz Canabrava <tcanabrava@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "PluginManager.h"

#include "IKonsolePlugin.h"
#include "MainWindow.h"
#include "konsoledebug.h"

#include <KLocalizedString>
#include <KPluginFactory>
#include <KPluginMetaData>

#include <QAction>
#include <QVersionNumber>

namespace Konsole
{
struct PluginManagerPrivate {
    std::vector<IKonsolePlugin *> plugins;
};

PluginManager::PluginManager()
    : d(std::make_unique<PluginManagerPrivate>())
{
}

PluginManager::~PluginManager()
{
    qDeleteAll(d->plugins);
}

void PluginManager::loadAllPlugins()
{
    QVector<KPluginMetaData> pluginMetaData = KPluginMetaData::findPlugins(QStringLiteral("kmuxplugins"), [](const KPluginMetaData &data) {
        const QVersionNumber pluginVersion = QVersionNumber::fromString(data.version());
        const QVersionNumber applicationVersion = QVersionNumber::fromString(QLatin1String(KMUX_VERSION));
        if (pluginVersion.majorVersion() == applicationVersion.majorVersion() && pluginVersion.minorVersion() == applicationVersion.minorVersion()) {
            return true;
        } else {
            qCWarning(KonsoleDebug) << "Ignoring" << data.name() << "plugin version (" << pluginVersion << ") doesn't match application version ("
                                    << applicationVersion << ")";
            return false;
        }
    });
    for (const auto &metaData : std::as_const(pluginMetaData)) {
        const KPluginFactory::Result result = KPluginFactory::instantiatePlugin<IKonsolePlugin>(metaData);
        if (!result) {
            continue;
        }

        d->plugins.push_back(result.plugin);
    }
}

void PluginManager::registerMainWindow(Konsole::MainWindow *window)
{
    QList<QAction *> internalPluginSubmenus;
    for (auto *plugin : d->plugins) {
        plugin->addMainWindow(window);
        internalPluginSubmenus.append(plugin->menuBarActions(window));
        window->addPlugin(plugin);
    }

    if (internalPluginSubmenus.isEmpty()) {
        auto *emptyMenuAct = new QAction(i18n("No plugins available"), this);
        emptyMenuAct->setEnabled(false);
        internalPluginSubmenus.append(emptyMenuAct);
    }

    window->setPluginsActions(internalPluginSubmenus);
}

std::vector<IKonsolePlugin *> PluginManager::plugins() const
{
    return d->plugins;
}

}

#include "moc_PluginManager.cpp"
