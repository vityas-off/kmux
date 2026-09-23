/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROJECTICON_H
#define PROJECTICON_H

#include "konsoleprivate_export.h"

#include <QColor>
#include <QIcon>
#include <QString>

namespace Konsole::ProjectIcon
{
KONSOLEPRIVATE_EXPORT QIcon icon(const QString &name, const QColor &color = {});
// Returns an app-owned file path, or an empty string and an error on failure.
KONSOLEPRIVATE_EXPORT QString importFile(const QString &path, QString &error);
}

#endif
