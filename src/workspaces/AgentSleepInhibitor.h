/*
    SPDX-FileCopyrightText: 2026 Kmux Authors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef AGENTSLEEPINHIBITOR_H
#define AGENTSLEEPINHIBITOR_H

#include "konsoleprivate_export.h"

#include <QHash>
#include <QObject>
#include <QSet>

#include <optional>

namespace Konsole
{
class ViewManagerTest;

class KONSOLEPRIVATE_EXPORT AgentSleepInhibitor final : public QObject
{
public:
    static AgentSleepInhibitor *instance();

    void setAgentRunning(const QObject *source, bool running);

private:
    explicit AgentSleepInhibitor(QObject *parent);
    ~AgentSleepInhibitor() override;

    void updateInhibition();
    void requestInhibition();
    void releaseInhibition();
    void releaseCookie(uint cookie);

    QSet<const QObject *> _activeSources;
    QHash<const QObject *, QMetaObject::Connection> _sourceDestroyedConnections;
    bool _inhibitionRequested = false;
    bool _inhibitCallPending = false;
    bool _requestFailed = false;
    quint64 _requestGeneration = 0;
    std::optional<uint> _inhibitionCookie;

    friend class ViewManagerTest;
};
}

#endif
