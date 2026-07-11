/*
    This source file is part of Konsole, a terminal emulator.

    SPDX-FileCopyrightText: 2007-2008 Robert Knight <robertknight@gmail.com>
    SPDX-FileCopyrightText: 2018 Harald Sitter <sitter@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ColorSchemeManager.h"

#include "colorschemedebug.h"

// Qt
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

// KDE
#include <KConfig>

using namespace Konsole;

namespace
{
QStringList kmuxColorSchemePaths(const QString &name)
{
    const QString path = QStringLiteral("kmux/") + name + QStringLiteral(".colorscheme");
    return QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, path);
}

QStringList colorSchemePaths(const QString &name)
{
    const QStringList searchPaths = {
        QStringLiteral("kmux/") + name + QStringLiteral(".colorscheme"),
        QStringLiteral("konsole/") + name + QStringLiteral(".colorscheme"),
        QStringLiteral("konsole/") + name + QStringLiteral(".schema"),
    };

    QStringList paths;
    for (const QString &searchPath : searchPaths) {
        const auto locations = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, searchPath);
        for (const QString &location : locations) {
            if (!paths.contains(location)) {
                paths.append(location);
            }
        }
    }

    const QString resource = QStringLiteral(":/kmux/color-schemes/") + name + QStringLiteral(".colorscheme");
    if (QFile::exists(resource)) {
        paths.append(resource);
    }

    return paths;
}
}

ColorSchemeManager::ColorSchemeManager()
{
}

Q_GLOBAL_STATIC(ColorSchemeManager, theColorSchemeManager)

ColorSchemeManager *ColorSchemeManager::instance()
{
    return theColorSchemeManager;
}

QList<std::shared_ptr<const ColorScheme>> ColorSchemeManager::allColorSchemes()
{
    int failed = 0;

    QList<std::shared_ptr<const ColorScheme>> ret;
    const auto schemes = listColorSchemes();
    for (const QString &name : schemes) {
        std::shared_ptr<const ColorScheme> scheme = findColorScheme(colorSchemeNameFromPath(name));
        if (!scheme) {
            failed++;
            continue;
        }
        ret.append(scheme);
    }

    if (failed > 0) {
        qCDebug(ColorSchemeDebug) << "failed to load " << failed << " color schemes.";
    }

    return ret;
}

std::shared_ptr<const ColorScheme> ColorSchemeManager::loadColorScheme(const QString &filePath)
{
    if (!pathIsColorScheme(filePath) || !QFile::exists(filePath)) {
        return nullptr;
    }

    const QString name = colorSchemeNameFromPath(filePath);

    KConfig config(filePath, KConfig::NoGlobals);
    std::shared_ptr<ColorScheme> scheme = std::make_shared<ColorScheme>();
    scheme->setName(name);
    scheme->read(config);

    if (scheme->name().isEmpty()) {
        qCDebug(ColorSchemeDebug) << "Color scheme in" << filePath << "does not have a valid name and was not loaded.";
        return nullptr;
    }

    if (!_colorSchemes.contains(name)) {
        _colorSchemes.insert(scheme->name(), scheme);
    } else {
        // qDebug() << "color scheme with name" << scheme->name() << "has already been" <<
        //         "found, replacing.";

        _colorSchemes[name] = scheme;
    }

    return scheme;
}

bool ColorSchemeManager::unloadColorScheme(const QString &filePath)
{
    if (!pathIsColorScheme(filePath)) {
        return false;
    }
    _colorSchemes.remove(colorSchemeNameFromPath(filePath));
    return true;
}

QString ColorSchemeManager::colorSchemeNameFromPath(const QString &path)
{
    if (!pathIsColorScheme(path)) {
        return QString();
    }
    return QFileInfo(path).completeBaseName();
}

QStringList ColorSchemeManager::listColorSchemes()
{
    QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("kmux"), QStandardPaths::LocateDirectory);
    dirs.append(QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("konsole"), QStandardPaths::LocateDirectory));
    dirs.append(QStringLiteral(":/kmux/color-schemes")); // fallback to bundled ones

    QStringList colorschemes;
    QSet<QString> schemeNames;
    for (const QString &dir : std::as_const(dirs)) {
        const QStringList fileNames = QDir(dir).entryList(QStringList() << QStringLiteral("*.colorscheme"));
        for (const QString &file : fileNames) {
            const QString path = dir + QLatin1Char('/') + file;
            const QString name = colorSchemeNameFromPath(path);
            if (!schemeNames.contains(name)) {
                schemeNames.insert(name);
                colorschemes.append(path);
            }
        }
    }
    return colorschemes;
}

const std::shared_ptr<const ColorScheme> &ColorSchemeManager::defaultColorScheme() const
{
    static const std::shared_ptr<const ColorScheme> defaultScheme = std::make_shared<const ColorScheme>();
    return defaultScheme;
}

void ColorSchemeManager::addColorScheme(const std::shared_ptr<ColorScheme> &scheme)
{
    _colorSchemes[scheme->name()] = scheme;

    // save changes to disk

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kmux/");
    QDir().mkpath(dir);
    const QString path = dir + scheme->name() + QStringLiteral(".colorscheme");
    KConfig config(path, KConfig::NoGlobals);

    scheme->write(config);
    config.sync();
}

bool ColorSchemeManager::deleteColorScheme(const QString &name)
{
    Q_ASSERT(_colorSchemes.contains(name));

    const QStringList paths = kmuxColorSchemePaths(name);
    const QString path = paths.isEmpty() ? QString() : paths.constFirst();
    if (QFile::remove(path)) {
        _colorSchemes.remove(name);
        return true;
    }
    qCDebug(ColorSchemeDebug) << "Failed to remove color scheme -" << path;
    return false;
}

std::shared_ptr<const ColorScheme> ColorSchemeManager::findColorScheme(const QString &name)
{
    if (name.isEmpty()) {
        return defaultColorScheme();
    }

    // A fix to prevent infinite loops if users puts / in ColorScheme name
    // Konsole will create a sub-folder in that case (bko 315086)
    // More code will have to go in to prevent the users from doing that.
    if (name.contains(QLatin1String("/"))) {
        qCDebug(ColorSchemeDebug) << name << " has an invalid character / in the name ... skipping";
        return defaultColorScheme();
    }

    if (_colorSchemes.contains(name)) {
        std::shared_ptr<const ColorScheme> colorScheme = _colorSchemes.value(name).lock();
        if (colorScheme) {
            return colorScheme;
        } else {
            // Remove outdated weak pointer
            _colorSchemes.remove(name);
        }
    }
    // look for this color scheme
    QString path = findColorSchemePath(name);
    if (path.isEmpty()) {
        qCDebug(ColorSchemeDebug) << "Could not find color scheme - " << name;
        return nullptr;
    }
    return loadColorScheme(path);
}

QString ColorSchemeManager::findColorSchemePath(const QString &name) const
{
    const QStringList paths = colorSchemePaths(name);
    return paths.isEmpty() ? QString() : paths.constFirst();
}

bool ColorSchemeManager::pathIsColorScheme(const QString &path)
{
    return path.endsWith(QLatin1String(".colorscheme"));
}

bool ColorSchemeManager::isColorSchemeDeletable(const QString &name)
{
    const QStringList paths = kmuxColorSchemePaths(name);
    if (paths.isEmpty()) {
        return false;
    }

    QFileInfo fileInfo(paths.constFirst());
    QFileInfo dirInfo(fileInfo.path());

    return dirInfo.isWritable();
}

bool ColorSchemeManager::canResetColorScheme(const QString &name)
{
    const QStringList paths = colorSchemePaths(name);
    const QStringList kmuxPaths = kmuxColorSchemePaths(name);

    // Resetting removes the preferred writable scheme and reveals a fallback.
    return paths.count() > 1 && !kmuxPaths.isEmpty() && paths.constFirst() == kmuxPaths.constFirst() && QFileInfo(paths.constFirst()).isWritable();
}
