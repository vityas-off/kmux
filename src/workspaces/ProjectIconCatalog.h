/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROJECTICONCATALOG_H
#define PROJECTICONCATALOG_H

#include <QList>
#include <QString>

namespace Konsole::ProjectIcon
{
enum class Collection {
    Material,
    Devicon
};

struct CatalogEntry {
    QString resource;
    QString label;
    QString keywords;
    Collection collection;
};

QList<CatalogEntry> catalog();
}

#endif
