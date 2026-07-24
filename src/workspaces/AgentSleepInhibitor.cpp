/*
    SPDX-FileCopyrightText: 2026 Kmux Authors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AgentSleepInhibitor.h"

#include "KonsoleSettings.h"
#include "config-konsole.h"
#include "konsoledebug.h"

#include <QCoreApplication>

#if HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>

#include <KLocalizedString>
#endif

using namespace Konsole;

namespace
{
#if HAVE_DBUS
constexpr auto PowerManagementService = "org.freedesktop.PowerManagement";
constexpr auto PowerManagementPath = "/org/freedesktop/PowerManagement/Inhibit";
constexpr auto PowerManagementInterface = "org.freedesktop.PowerManagement.Inhibit";
#endif
}

AgentSleepInhibitor *AgentSleepInhibitor::instance()
{
    static auto *inhibitor = new AgentSleepInhibitor(QCoreApplication::instance());
    return inhibitor;
}

AgentSleepInhibitor::AgentSleepInhibitor(QObject *parent)
    : QObject(parent)
{
    connect(KonsoleSettings::self(), &KonsoleSettings::configChanged, this, &AgentSleepInhibitor::updateInhibition);

#if HAVE_DBUS
    auto *serviceWatcher = new QDBusServiceWatcher(QString::fromLatin1(PowerManagementService),
                                                   QDBusConnection::sessionBus(),
                                                   QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
                                                   this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        ++_requestGeneration;
        _inhibitCallPending = false;
        _inhibitionCookie.reset();
    });
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        ++_requestGeneration;
        _inhibitCallPending = false;
        _requestFailed = false;
        _inhibitionCookie.reset();
        updateInhibition();
    });
#endif
}

AgentSleepInhibitor::~AgentSleepInhibitor()
{
    _activeSources.clear();
    _inhibitionRequested = false;
    releaseInhibition();
}

void AgentSleepInhibitor::setAgentRunning(const QObject *source, bool running)
{
    if (source == nullptr) {
        return;
    }

    if (!_sourceDestroyedConnections.contains(source)) {
        _sourceDestroyedConnections.insert(source, connect(source, &QObject::destroyed, this, [this](QObject *object) {
                                               _activeSources.remove(object);
                                               _sourceDestroyedConnections.remove(object);
                                               updateInhibition();
                                           }));
    }

    if (running) {
        _activeSources.insert(source);
    } else {
        _activeSources.remove(source);
    }
    updateInhibition();
}

void AgentSleepInhibitor::updateInhibition()
{
    const bool shouldInhibit = KonsoleSettings::preventSleepWhileAgentsRun() && !_activeSources.isEmpty();
    if (_inhibitionRequested != shouldInhibit) {
        _inhibitionRequested = shouldInhibit;
        _requestFailed = false;
    }

    if (_inhibitionRequested) {
        requestInhibition();
    } else {
        releaseInhibition();
    }
}

void AgentSleepInhibitor::requestInhibition()
{
#if HAVE_DBUS
    if (_inhibitionCookie.has_value() || _inhibitCallPending || _requestFailed) {
        return;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(PowerManagementService),
                                                          QString::fromLatin1(PowerManagementPath),
                                                          QString::fromLatin1(PowerManagementInterface),
                                                          QStringLiteral("Inhibit"));
    message.setArguments({QStringLiteral("Kmux"), i18nc("@info:reason for sleep inhibition", "A terminal agent is working")});

    _inhibitCallPending = true;
    const quint64 generation = ++_requestGeneration;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *finishedWatcher) {
        const QDBusPendingReply<uint> reply = *finishedWatcher;
        finishedWatcher->deleteLater();
        if (generation != _requestGeneration) {
            return;
        }

        _inhibitCallPending = false;
        if (reply.isError()) {
            _requestFailed = true;
            qCDebug(KonsoleDebug) << "Unable to inhibit automatic sleep while an agent is working:" << reply.error().message();
            return;
        }

        const uint cookie = reply.value();
        if (_inhibitionRequested) {
            _inhibitionCookie = cookie;
        } else {
            releaseCookie(cookie);
        }
    });
#endif
}

void AgentSleepInhibitor::releaseInhibition()
{
#if HAVE_DBUS
    if (!_inhibitionCookie.has_value()) {
        return;
    }

    const uint cookie = *_inhibitionCookie;
    _inhibitionCookie.reset();
    releaseCookie(cookie);
#endif
}

void AgentSleepInhibitor::releaseCookie(uint cookie)
{
#if HAVE_DBUS
    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(PowerManagementService),
                                                          QString::fromLatin1(PowerManagementPath),
                                                          QString::fromLatin1(PowerManagementInterface),
                                                          QStringLiteral("UnInhibit"));
    message.setArguments({cookie});
    QDBusConnection::sessionBus().asyncCall(message);
#else
    Q_UNUSED(cookie)
#endif
}
