/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CODEXUSAGELIMIT_H
#define CODEXUSAGELIMIT_H

#include "konsoleprivate_export.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

#include <functional>
#include <optional>

namespace Konsole
{
struct CodexUsageLimit {
    /** Codex cannot work until the limit resets. */
    bool reached = false;
    /** When the exhausted usage window resets, if Codex reports it. */
    QDateTime resetsAt;
};

/** Reads the result of the Codex app-server request "account/rateLimits/read". */
KONSOLEPRIVATE_EXPORT CodexUsageLimit codexUsageLimitFromRateLimits(const QJsonObject &result);

/**
 * Asks a short-lived Codex app-server for the usage limits of the signed-in
 * account. Codex reports a stop at a usage limit only as a failed turn.
 */
class KONSOLEPRIVATE_EXPORT CodexUsageLimitProbe : public QObject
{
    Q_OBJECT

public:
    /** Receives the usage limit, or nothing when the request failed. */
    using Callback = std::function<void(std::optional<CodexUsageLimit>)>;

    CodexUsageLimitProbe(const QString &program, const QProcessEnvironment &environment, const QString &workingDirectory, QObject *parent = nullptr);
    ~CodexUsageLimitProbe() override;

    /**
     * Returns a probe that runs Codex as the process with this id does: with
     * its environment, in its working directory. Returns nullptr when that
     * process cannot be inspected or no Codex is on its PATH.
     */
    static CodexUsageLimitProbe *forProcess(qint64 processId, QObject *parent);

    /** Starts the request. The callback runs once, and the probe then deletes itself. */
    void start(Callback callback);

private:
    void readOutput();
    void send(const QJsonObject &message);
    void finish(std::optional<CodexUsageLimit> usageLimit);

    QProcess _process;
    QTimer _timeout;
    QByteArray _output;
    Callback _callback;
    bool _finished = false;
};
}

#endif
