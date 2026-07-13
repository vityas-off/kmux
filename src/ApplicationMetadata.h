/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace Konsole::ApplicationMetadata
{
inline QString componentName()
{
    return QStringLiteral("kmux");
}

inline QByteArray organizationDomain()
{
    return QByteArrayLiteral("vityas_off.github.io");
}

inline QString desktopFileName()
{
    return QStringLiteral("io.github.vityas_off.kmux");
}

inline QString dbusServiceName()
{
    QStringList domainParts = QString::fromLatin1(organizationDomain()).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    std::reverse(domainParts.begin(), domainParts.end());
    domainParts.append(componentName());
    return domainParts.join(QLatin1Char('.'));
}

inline QString localServerName()
{
    return desktopFileName() + QStringLiteral(".activation");
}
}
