/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LOCALACTIVATIONSERVER_H
#define LOCALACTIVATIONSERVER_H

#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>

class QLocalServer;

namespace Konsole
{
class LocalActivationServer : public QObject
{
    Q_OBJECT

public:
    using ActivationHandler = std::function<int(const QStringList &, const QString &, const QStringList &)>;

    enum class RequestResult {
        NoServer,
        Delivered,
        Error,
    };

    LocalActivationServer(const QString &serverName, ActivationHandler handler, QObject *parent = nullptr);
    ~LocalActivationServer() override;

    bool listen(QString *error);

    static RequestResult requestExisting(const QString &serverName,
                                         const QStringList &arguments,
                                         const QString &workingDirectory,
                                         const QStringList &environment,
                                         int *exitCode,
                                         QString *error);

private:
    void acceptConnection();

    const QString _serverName;
    const ActivationHandler _handler;
    std::unique_ptr<QLocalServer> _server;
};
}

#endif // LOCALACTIVATIONSERVER_H
