/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "LocalActivationServer.h"

#include <QDataStream>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QVariant>

#include <utility>

using namespace Konsole;

namespace
{
constexpr int ConnectionTimeout = 30000;
constexpr auto StreamVersion = QDataStream::Qt_6_5;
}

LocalActivationServer::LocalActivationServer(const QString &serverName, ActivationHandler handler, QObject *parent)
    : QObject(parent)
    , _serverName(serverName)
    , _handler(std::move(handler))
    , _server(std::make_unique<QLocalServer>())
{
    _server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(_server.get(), &QLocalServer::newConnection, this, &LocalActivationServer::acceptConnection);
}

LocalActivationServer::~LocalActivationServer() = default;

bool LocalActivationServer::listen(QString *error)
{
    QLocalServer::removeServer(_serverName);
    if (_server->listen(_serverName)) {
        return true;
    }
    if (error != nullptr) {
        *error = _server->errorString();
    }
    return false;
}

LocalActivationServer::RequestResult LocalActivationServer::requestExisting(const QString &serverName,
                                                                            const QStringList &arguments,
                                                                            const QString &workingDirectory,
                                                                            const QStringList &environment,
                                                                            int *exitCode,
                                                                            QString *error)
{
    QLocalSocket socket;
    socket.connectToServer(serverName, QIODevice::ReadWrite);
    if (!socket.waitForConnected(250)) {
        if (socket.error() == QLocalSocket::ServerNotFoundError || socket.error() == QLocalSocket::ConnectionRefusedError) {
            return RequestResult::NoServer;
        }
        if (error != nullptr) {
            *error = socket.errorString();
        }
        return RequestResult::Error;
    }

    QDataStream output(&socket);
    output.setVersion(StreamVersion);
    output << arguments << workingDirectory << environment;
    if (!socket.waitForBytesWritten(ConnectionTimeout)) {
        if (error != nullptr) {
            *error = socket.errorString();
        }
        return RequestResult::Error;
    }

    QElapsedTimer timer;
    timer.start();
    QDataStream input(&socket);
    input.setVersion(StreamVersion);
    qint32 result = 1;
    for (;;) {
        input.startTransaction();
        input >> result;
        if (input.commitTransaction()) {
            if (exitCode != nullptr) {
                *exitCode = result;
            }
            return RequestResult::Delivered;
        }

        const int remaining = ConnectionTimeout - int(timer.elapsed());
        if (remaining <= 0 || !socket.waitForReadyRead(remaining)) {
            if (error != nullptr) {
                *error = socket.errorString().isEmpty() ? QStringLiteral("Timed out waiting for activation response") : socket.errorString();
            }
            return RequestResult::Error;
        }
    }
}

void LocalActivationServer::acceptConnection()
{
    while (QLocalSocket *socket = _server->nextPendingConnection()) {
        socket->setParent(this);
        const auto readRequest = [this, socket]() {
            if (socket->property("activationHandled").toBool()) {
                return;
            }

            QDataStream input(socket);
            input.setVersion(StreamVersion);
            QStringList arguments;
            QString workingDirectory;
            QStringList environment;
            input.startTransaction();
            input >> arguments >> workingDirectory >> environment;
            if (!input.commitTransaction()) {
                return;
            }

            socket->setProperty("activationHandled", true);
            const qint32 result = _handler(arguments, workingDirectory, environment);
            QDataStream output(socket);
            output.setVersion(StreamVersion);
            output << result;
            socket->disconnectFromServer();
        };
        connect(socket, &QLocalSocket::readyRead, this, readRequest);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        if (socket->bytesAvailable() > 0) {
            readRequest();
        }
    }
}
