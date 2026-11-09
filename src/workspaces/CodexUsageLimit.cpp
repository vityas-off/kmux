/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "CodexUsageLimit.h"

#include "config-konsole.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

using namespace Konsole;

namespace
{
constexpr int ProbeTimeoutMs = 15000;
constexpr int ProcessExitTimeoutMs = 1000;
constexpr int InitializeRequestId = 1;
constexpr int RateLimitsRequestId = 2;
constexpr double ExhaustedPercent = 100.0;

QJsonObject message(const QString &method, std::optional<int> id = std::nullopt)
{
    QJsonObject object{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), method}, {QStringLiteral("params"), QJsonObject()}};
    if (id.has_value()) {
        object.insert(QStringLiteral("id"), *id);
    }
    return object;
}

// Returns when the latest exhausted window of a snapshot resets, if any.
QDateTime exhaustedWindowReset(const QJsonObject &snapshot)
{
    QDateTime resetsAt;
    for (const QString &windowName : {QStringLiteral("primary"), QStringLiteral("secondary")}) {
        const QJsonObject window = snapshot.value(windowName).toObject();
        if (window.value(QStringLiteral("usedPercent")).toDouble() < ExhaustedPercent || !window.value(QStringLiteral("resetsAt")).isDouble()) {
            continue;
        }
        // Codex reports reset times in seconds since the epoch.
        const QDateTime windowReset = QDateTime::fromSecsSinceEpoch(window.value(QStringLiteral("resetsAt")).toInteger());
        if (!resetsAt.isValid() || windowReset > resetsAt) {
            resetsAt = windowReset;
        }
    }
    return resetsAt;
}

void addReachedLimit(const QJsonObject &snapshot, CodexUsageLimit *usageLimit)
{
    usageLimit->reached = true;
    const QDateTime resetsAt = exhaustedWindowReset(snapshot);
    if (resetsAt.isValid() && (!usageLimit->resetsAt.isValid() || resetsAt > usageLimit->resetsAt)) {
        usageLimit->resetsAt = resetsAt;
    }
}
}

CodexUsageLimit Konsole::codexUsageLimitFromRateLimits(const QJsonObject &result)
{
    const QJsonObject rateLimits = result.value(QStringLiteral("rateLimits")).toObject();
    QList<QJsonObject> snapshots{rateLimits};
    const QJsonObject snapshotsByLimit = result.value(QStringLiteral("rateLimitsByLimitId")).toObject();
    for (auto snapshot = snapshotsByLimit.constBegin(); snapshot != snapshotsByLimit.constEnd(); ++snapshot) {
        snapshots.append(snapshot.value().toObject());
    }

    CodexUsageLimit usageLimit;
    for (const QJsonObject &snapshot : std::as_const(snapshots)) {
        if (snapshot.value(QStringLiteral("rateLimitReachedType")).isString()) {
            addReachedLimit(snapshot, &usageLimit);
        }
    }

    // Without its included usage, Codex works only on credits.
    if (!usageLimit.reached && result.value(QStringLiteral("ordinaryUsageAllowed")) == QJsonValue(false)) {
        const QJsonObject credits = rateLimits.value(QStringLiteral("credits")).toObject();
        if (!credits.value(QStringLiteral("hasCredits")).toBool() && !credits.value(QStringLiteral("unlimited")).toBool()) {
            addReachedLimit(rateLimits, &usageLimit);
        }
    }
    return usageLimit;
}

CodexUsageLimitProbe::CodexUsageLimitProbe(const QString &program, const QProcessEnvironment &environment, const QString &workingDirectory, QObject *parent)
    : QObject(parent)
{
    _process.setProgram(program);
    // The server answers one request: it needs no approvals, writes nothing,
    // and loads no plugins.
    _process.setArguments({QStringLiteral("-c"),
                           QStringLiteral("approval_policy=never"),
                           QStringLiteral("-c"),
                           QStringLiteral("features.plugins=false"),
                           QStringLiteral("-s"),
                           QStringLiteral("read-only"),
                           QStringLiteral("-a"),
                           QStringLiteral("never"),
                           QStringLiteral("app-server")});
    _process.setProcessEnvironment(environment);
    if (!workingDirectory.isEmpty()) {
        _process.setWorkingDirectory(workingDirectory);
    }
    _process.setStandardErrorFile(QProcess::nullDevice());

    _timeout.setSingleShot(true);
    _timeout.setInterval(ProbeTimeoutMs);
    connect(&_timeout, &QTimer::timeout, this, [this]() {
        finish(std::nullopt);
    });
    connect(&_process, &QProcess::started, this, [this]() {
        QJsonObject initialize = message(QStringLiteral("initialize"), InitializeRequestId);
        initialize.insert(
            QStringLiteral("params"),
            QJsonObject{{QStringLiteral("clientInfo"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("kmux")}, {QStringLiteral("version"), QStringLiteral(KMUX_DISPLAY_VERSION)}}}});
        send(initialize);
    });
    connect(&_process, &QProcess::readyReadStandardOutput, this, &CodexUsageLimitProbe::readOutput);
    connect(&_process, &QProcess::errorOccurred, this, [this]() {
        finish(std::nullopt);
    });
    connect(&_process, &QProcess::finished, this, [this]() {
        finish(std::nullopt);
    });
}

CodexUsageLimitProbe::~CodexUsageLimitProbe()
{
    // A probe destroyed early, for example together with its window, answers
    // nobody: stopping the process would otherwise report a failure.
    _finished = true;
    if (_process.state() != QProcess::NotRunning) {
        _process.kill();
        _process.waitForFinished(ProcessExitTimeoutMs);
    }
}

CodexUsageLimitProbe *CodexUsageLimitProbe::forProcess(qint64 processId, QObject *parent)
{
#ifdef Q_OS_LINUX
    QFile environmentFile(QStringLiteral("/proc/%1/environ").arg(processId));
    if (processId <= 0 || !environmentFile.open(QIODevice::ReadOnly)) {
        return nullptr;
    }

    QProcessEnvironment environment;
    const QList<QByteArray> entries = environmentFile.readAll().split('\0');
    for (const QByteArray &entry : entries) {
        const qsizetype separator = entry.indexOf('=');
        if (separator > 0) {
            environment.insert(QString::fromLocal8Bit(entry.first(separator)), QString::fromLocal8Bit(entry.sliced(separator + 1)));
        }
    }

    const QString program =
        QStandardPaths::findExecutable(QStringLiteral("codex"), environment.value(QStringLiteral("PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts));
    if (program.isEmpty()) {
        return nullptr;
    }
    const QString workingDirectory = QFileInfo(QStringLiteral("/proc/%1/cwd").arg(processId)).symLinkTarget();
    return new CodexUsageLimitProbe(program, environment, workingDirectory, parent);
#else
    Q_UNUSED(processId)
    Q_UNUSED(parent)
    return nullptr;
#endif
}

void CodexUsageLimitProbe::start(Callback callback)
{
    _callback = std::move(callback);
    _timeout.start();
    _process.start();
}

void CodexUsageLimitProbe::readOutput()
{
    _output += _process.readAllStandardOutput();
    qsizetype lineEnd;
    while (!_finished && (lineEnd = _output.indexOf('\n')) >= 0) {
        const QJsonObject response = QJsonDocument::fromJson(_output.first(lineEnd)).object();
        _output.remove(0, lineEnd + 1);

        const QJsonValue id = response.value(QStringLiteral("id"));
        if (id == QJsonValue(InitializeRequestId)) {
            send(message(QStringLiteral("initialized")));
            send(message(QStringLiteral("account/rateLimits/read"), RateLimitsRequestId));
        } else if (id == QJsonValue(RateLimitsRequestId)) {
            const QJsonValue result = response.value(QStringLiteral("result"));
            finish(result.isObject() ? std::optional<CodexUsageLimit>(codexUsageLimitFromRateLimits(result.toObject())) : std::nullopt);
        }
    }
}

void CodexUsageLimitProbe::send(const QJsonObject &message)
{
    _process.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void CodexUsageLimitProbe::finish(std::optional<CodexUsageLimit> usageLimit)
{
    if (_finished) {
        return;
    }
    _finished = true;
    _timeout.stop();
    if (_process.state() != QProcess::NotRunning) {
        _process.kill();
    }
    if (_callback) {
        _callback(usageLimit);
    }
    deleteLater();
}
