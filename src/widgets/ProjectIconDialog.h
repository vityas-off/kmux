/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROJECTICONDIALOG_H
#define PROJECTICONDIALOG_H

#include "konsoleprivate_export.h"

#include <QString>
#include <optional>

class QWidget;

namespace Konsole
{
// An empty name resets the icon; nullopt leaves the project unchanged.
KONSOLEPRIVATE_EXPORT std::optional<QString> chooseProjectIcon(const QString &currentIcon, QWidget *parent);
}

#endif
