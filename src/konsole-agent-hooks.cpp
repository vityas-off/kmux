/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "config-konsole.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>
#include <utility>

namespace
{
constexpr auto HooksFeatureBegin = "# kmux-codex-hooks-feature begin";
constexpr auto HooksFeatureEnd = "# kmux-codex-hooks-feature end";
constexpr auto HooksFeaturePreviousLinePrefix = "# kmux-codex-hooks-feature previous line: ";
constexpr auto HookTrustBegin = "# kmux-codex-hook-trust begin";
constexpr auto HookTrustEnd = "# kmux-codex-hook-trust end";
constexpr auto LegacyHooksFeatureBegin = "# konsole-codex-hooks-feature begin";
constexpr auto LegacyHooksFeatureEnd = "# konsole-codex-hooks-feature end";
constexpr auto LegacyHooksFeaturePreviousLinePrefix = "# konsole-codex-hooks-feature previous line: ";
constexpr auto LegacyHookTrustBegin = "# konsole-codex-hook-trust begin";
constexpr auto LegacyHookTrustEnd = "# konsole-codex-hook-trust end";
constexpr auto CodexAgentName = "codex";
constexpr auto ClaudeAgentName = "claude";

struct HookEvent {
    QString eventName;
    QString eventLabel;
    // The kmux-project-status option that tells the hook apart.
    QString helperOption;
    int timeout = 5;
    QString matcher;
};

// Claude Code reports its running state with OSC 7501 itself. Its hooks tell
// Kmux only about a stop at a usage limit, which the protocol reports as an
// ordinary result: a graceful stop ends with Stop, a failed request with
// StopFailure.
const QList<HookEvent> ClaudeHookEvents = {
    {QStringLiteral("Stop"), QStringLiteral("stop"), QStringLiteral("--claude-stop"), 5, QString()},
    {QStringLiteral("StopFailure"), QStringLiteral("stop_failure"), QStringLiteral("--claude-stop-failure"), 5, QString()},
};

QString homePath()
{
    const QString home = qEnvironmentVariable("HOME");
    return home.isEmpty() ? QDir::homePath() : home;
}

QString codexHome(const QString &overridePath)
{
    if (!overridePath.isEmpty()) {
        return QFileInfo(overridePath).absoluteFilePath();
    }

    const QString envPath = qEnvironmentVariable("CODEX_HOME");
    if (!envPath.isEmpty()) {
        return QFileInfo(envPath).absoluteFilePath();
    }

    return QDir(homePath()).filePath(QStringLiteral(".codex"));
}

QString claudeHome(const QString &overridePath)
{
    if (!overridePath.isEmpty()) {
        return QFileInfo(overridePath).absoluteFilePath();
    }

    return QDir(homePath()).filePath(QStringLiteral(".claude"));
}

class HookTransactionLock
{
public:
    HookTransactionLock(const QString &agentName, const QString &configDir)
        : _path(QFileInfo(configDir).absoluteFilePath() + QStringLiteral(".kmux-%1-hooks.lock").arg(agentName))
        , _lock(_path)
    {
    }

    bool acquire(QString *error)
    {
        QDir parentDirectory = QFileInfo(_path).dir();
        if (!parentDirectory.exists() && !parentDirectory.mkpath(QStringLiteral("."))) {
            if (error != nullptr) {
                *error = QStringLiteral("Could not create lock directory %1").arg(parentDirectory.path());
            }
            return false;
        }
        if (!_lock.tryLock(30000)) {
            if (error != nullptr) {
                *error = QStringLiteral("Could not acquire hook transaction lock %1").arg(_path);
            }
            return false;
        }
        return true;
    }

private:
    const QString _path;
    QLockFile _lock;
};

QString hookScriptRootDirectory()
{
    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(dataRoot.isEmpty() ? QDir(homePath()).filePath(QStringLiteral(".local/share")) : dataRoot).filePath(QStringLiteral("kmux/hooks"));
}

QString legacyHookScriptRootDirectory()
{
    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(dataRoot.isEmpty() ? QDir(homePath()).filePath(QStringLiteral(".local/share")) : dataRoot).filePath(QStringLiteral("konsole/hooks"));
}

QString hookScriptDirectory(const QString &agentName, const QString &configDir)
{
    const QFileInfo configInfo(configDir);
    const QString canonicalConfigDir = configInfo.canonicalFilePath();
    const QByteArray identity = (canonicalConfigDir.isEmpty() ? configInfo.absoluteFilePath() : canonicalConfigDir).toUtf8();
    const QString homeId = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    return QDir(hookScriptRootDirectory()).filePath(QStringLiteral("%1-%2").arg(agentName, homeId));
}

QString shellQuote(const QString &value)
{
    QString quoted = QStringLiteral("'");
    for (const QChar ch : value) {
        if (ch == QLatin1Char('\'')) {
            quoted += QStringLiteral("'\"'\"'");
        } else {
            quoted += ch;
        }
    }
    quoted += QLatin1Char('\'');
    return quoted;
}

QString projectStatusHelperPath()
{
    const QString sibling = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("kmux-project-status"));
    if (QFileInfo(sibling).isExecutable()) {
        return sibling;
    }

    return QStringLiteral("kmux-project-status");
}

QString hookScriptPath(const QString &scriptDirectory, const QString &agentName, const HookEvent &event)
{
    return QDir(scriptDirectory).filePath(QStringLiteral("%1-%2.sh").arg(agentName, event.eventLabel));
}

// Agents run a hook command through a shell. A plain script path stays
// unquoted, so existing configurations and trust hashes remain unchanged.
QString hookCommand(const QString &scriptPath)
{
    const QLatin1String shellSafeSymbols("/._-+,:@%=");
    const bool shellSafe = std::all_of(scriptPath.cbegin(), scriptPath.cend(), [shellSafeSymbols](QChar ch) {
        return ch.unicode() < 0x80 && (ch.isLetterOrNumber() || shellSafeSymbols.contains(ch));
    });
    return shellSafe ? scriptPath : shellQuote(scriptPath);
}

// Reverses hookCommand() by removing the shell quotes.
QString hookCommandScriptPath(const QString &command)
{
    QString scriptPath;
    QChar quote;
    for (const QChar ch : command) {
        if (!quote.isNull()) {
            if (ch == quote) {
                quote = QChar();
            } else {
                scriptPath += ch;
            }
        } else if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            quote = ch;
        } else {
            scriptPath += ch;
        }
    }
    return quote.isNull() ? scriptPath : command;
}

QString hookScriptContent(const QString &agentName, const HookEvent &event)
{
    return QStringLiteral(
               "#!/bin/sh\n"
               "# kmux-%1-hook v2\n"
               "helper=%2\n"
               "set -- --hook-output --agent %3 --event %4 %5\n"
               "if [ -x \"$helper\" ]; then\n"
               "    \"$helper\" \"$@\"\n"
               "elif command -v kmux-project-status >/dev/null 2>&1; then\n"
               "    kmux-project-status \"$@\"\n"
               "else\n"
               "    printf '{}\\n'\n"
               "fi\n")
        .arg(agentName, shellQuote(projectStatusHelperPath()), shellQuote(agentName), shellQuote(event.eventName), event.helperOption);
}

bool writeTextFileAtomically(const QString &path, const QString &content, QString *error)
{
    const QByteArray encodedContent = content.toUtf8();
    QFile existingFile(path);
    if (existingFile.open(QIODevice::ReadOnly) && existingFile.readAll() == encodedContent) {
        return true;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }

    file.write(encodedContent);
    if (!file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }

    return true;
}

bool writeJsonFileAtomically(const QString &path, const QJsonObject &object, QString *error)
{
    const QJsonDocument document(object);
    return writeTextFileAtomically(path, QString::fromUtf8(document.toJson(QJsonDocument::Indented)), error);
}

QJsonObject readJsonObject(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.exists()) {
        return {};
    }

    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 is not a valid JSON object").arg(path);
        }
        return {};
    }

    return document.object();
}

bool isKmuxHookScript(const QString &scriptPath, const QString &scriptDirectory, const QString &agentName)
{
    const QFileInfo commandInfo(scriptPath);
    const QString commandDirectory = QDir::cleanPath(commandInfo.absolutePath());
    const QString currentScriptDirectory = QDir::cleanPath(QFileInfo(scriptDirectory).absoluteFilePath());
    const QString previousScriptDirectory = QDir::cleanPath(QFileInfo(hookScriptRootDirectory()).absoluteFilePath());
    const QString legacyScriptDirectory = QDir::cleanPath(QFileInfo(legacyHookScriptRootDirectory()).absoluteFilePath());
    const QString scriptNamePrefix = agentName + QLatin1Char('-');

    return (commandDirectory == currentScriptDirectory || commandDirectory == previousScriptDirectory || commandDirectory == legacyScriptDirectory)
        && commandInfo.fileName().startsWith(scriptNamePrefix) && commandInfo.fileName().endsWith(QLatin1String(".sh"));
}

// Returns the Kmux script that a hook command runs, or an empty string for a
// command that Kmux does not own. Earlier versions wrote the script path
// without shell quotes, and such a path can itself contain quotes, so the
// command is checked as written before its quotes are removed.
QString kmuxHookScriptPath(const QString &command, const QString &scriptDirectory, const QString &agentName)
{
    for (const QString &scriptPath : {command, hookCommandScriptPath(command)}) {
        if (isKmuxHookScript(scriptPath, scriptDirectory, agentName)) {
            return scriptPath;
        }
    }
    return {};
}

bool commandIsKmuxOwned(const QString &command, const QString &scriptDirectory, const QString &agentName)
{
    return !kmuxHookScriptPath(command, scriptDirectory, agentName).isEmpty();
}

QJsonArray removeKmuxOwnedHookGroups(QJsonArray groups, const QString &scriptDirectory, const QString &agentName)
{
    QJsonArray rewrittenGroups;
    for (const QJsonValue &groupValue : std::as_const(groups)) {
        QJsonObject group = groupValue.toObject();
        QJsonArray hookList = group.value(QStringLiteral("hooks")).toArray();
        QJsonArray rewrittenHookList;
        for (const QJsonValue &hookValue : std::as_const(hookList)) {
            const QJsonObject hook = hookValue.toObject();
            if (!commandIsKmuxOwned(hook.value(QStringLiteral("command")).toString(), scriptDirectory, agentName)) {
                rewrittenHookList.append(hook);
            }
        }

        if (rewrittenHookList.isEmpty()) {
            continue;
        }

        group.insert(QStringLiteral("hooks"), rewrittenHookList);
        rewrittenGroups.append(group);
    }

    return rewrittenGroups;
}

// Stores the remaining hook groups without leaving an empty "hooks" object
// that the configuration did not have before Kmux installed its hooks.
void setRemainingHooks(QJsonObject *root, const QJsonObject &hooks)
{
    if (hooks.isEmpty()) {
        root->remove(QStringLiteral("hooks"));
    } else {
        root->insert(QStringLiteral("hooks"), hooks);
    }
}

QJsonObject buildHookGroup(const QString &scriptDirectory, const QString &agentName, const HookEvent &event)
{
    QJsonObject hook;
    hook.insert(QStringLiteral("type"), QStringLiteral("command"));
    hook.insert(QStringLiteral("command"), hookCommand(hookScriptPath(scriptDirectory, agentName, event)));
    hook.insert(QStringLiteral("timeout"), event.timeout);

    QJsonArray hooks;
    hooks.append(hook);

    QJsonObject group;
    if (!event.matcher.isEmpty()) {
        group.insert(QStringLiteral("matcher"), event.matcher);
    }
    group.insert(QStringLiteral("hooks"), hooks);
    return group;
}

QStringList splitTomlLines(const QString &content)
{
    if (content.isEmpty()) {
        return {};
    }

    QStringList lines = content.split(QLatin1Char('\n'));
    if (content.endsWith(QLatin1Char('\n')) && !lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    return lines;
}

QString joinTomlLines(const QStringList &lines)
{
    return lines.isEmpty() ? QString() : lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool lineStartsTable(const QString &line, const QString &table)
{
    const QString key = QRegularExpression::escape(table);
    return QRegularExpression(QStringLiteral(R"(^\s*\[\s*(?:%1|"%1"|'%1')\s*\]\s*(#.*)?$)").arg(key)).match(line).hasMatch();
}

bool lineStartsAnyTable(const QString &line)
{
    return QRegularExpression(QStringLiteral(R"(^\s*\[\[?\s*[^]]+\s*\]\]?\s*(#.*)?$)")).match(line).hasMatch();
}

bool removeKonsoleBlocks(QStringList *lines)
{
    bool removedFeatureBlock = false;
    for (int i = 0; i < lines->size();) {
        const QString line = lines->at(i);
        const bool legacyFeatureBlock = line == QString::fromLatin1(LegacyHooksFeatureBegin);
        const bool legacyTrustBlock = line == QString::fromLatin1(LegacyHookTrustBegin);
        const bool featureBlock = line == QString::fromLatin1(HooksFeatureBegin) || legacyFeatureBlock;
        const bool trustBlock = line == QString::fromLatin1(HookTrustBegin) || legacyTrustBlock;
        if (!featureBlock && !trustBlock) {
            ++i;
            continue;
        }
        removedFeatureBlock = removedFeatureBlock || featureBlock;

        const QString endMarker = legacyFeatureBlock ? QString::fromLatin1(LegacyHooksFeatureEnd)
            : legacyTrustBlock                       ? QString::fromLatin1(LegacyHookTrustEnd)
            : featureBlock                           ? QString::fromLatin1(HooksFeatureEnd)
                                                     : QString::fromLatin1(HookTrustEnd);
        const QString previousLinePrefix =
            legacyFeatureBlock ? QString::fromLatin1(LegacyHooksFeaturePreviousLinePrefix) : QString::fromLatin1(HooksFeaturePreviousLinePrefix);
        int end = i;
        while (end < lines->size() && lines->at(end) != endMarker) {
            ++end;
        }

        QStringList replacements;
        if (featureBlock && end < lines->size()) {
            for (int j = i + 1; j < end; ++j) {
                if (lines->at(j).startsWith(previousLinePrefix)) {
                    replacements.append(lines->at(j).mid(previousLinePrefix.size()));
                }
            }
        }

        const int removeCount = end < lines->size() ? end - i + 1 : 1;
        lines->erase(lines->begin() + i, lines->begin() + i + removeCount);
        for (int j = 0; j < replacements.size(); ++j) {
            lines->insert(i + j, replacements.at(j));
        }
        i += replacements.size();
    }
    return removedFeatureBlock;
}

int tableEnd(const QStringList &lines, int tableStart)
{
    int index = tableStart + 1;
    while (index < lines.size()) {
        if (lineStartsAnyTable(lines.at(index))) {
            return index;
        }
        ++index;
    }
    return lines.size();
}

void removeEmptyFeaturesTable(QStringList *lines)
{
    for (int i = 0; i < lines->size(); ++i) {
        if (!lineStartsTable(lines->at(i), QStringLiteral("features"))) {
            continue;
        }

        const int end = tableEnd(*lines, i);
        bool hasContent = false;
        for (int j = i + 1; j < end; ++j) {
            const QString trimmed = lines->at(j).trimmed();
            if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#'))) {
                hasContent = true;
                break;
            }
        }
        if (!hasContent) {
            lines->erase(lines->begin() + i, lines->begin() + end);
        }
        return;
    }
}

// Removes the agent's Kmux hook scripts in the managed directory, except those
// for the given events, so events that Kmux no longer uses leave no scripts.
bool removeHookScripts(const QString &scriptDirectory, const QString &agentName, const QList<HookEvent> &keptEvents, QString *error)
{
    QStringList keptScripts;
    for (const HookEvent &event : keptEvents) {
        keptScripts.append(QFileInfo(hookScriptPath(scriptDirectory, agentName, event)).fileName());
    }

    const QDir dir(scriptDirectory);
    const QStringList scripts = dir.entryList({agentName + QStringLiteral("-*.sh")}, QDir::Files);
    for (const QString &script : scripts) {
        if (keptScripts.contains(script)) {
            continue;
        }
        QFile file(dir.filePath(script));
        if (!file.remove()) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
    }
    return true;
}

bool installHookScripts(const QString &scriptDirectory, const QString &agentName, const QList<HookEvent> &events, QString *error)
{
    QDir dir(scriptDirectory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not create %1").arg(dir.path());
        }
        return false;
    }

    for (const HookEvent &event : events) {
        const QString path = hookScriptPath(scriptDirectory, agentName, event);
        if (!writeTextFileAtomically(path, hookScriptContent(agentName, event), error)) {
            return false;
        }
        if (!QFile::setPermissions(path,
                                   QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                       | QFileDevice::ReadOther | QFileDevice::ExeOther)) {
            if (error != nullptr) {
                *error = QStringLiteral("Could not make %1 executable").arg(path);
            }
            return false;
        }
    }

    return removeHookScripts(scriptDirectory, agentName, events, error);
}

bool uninstallHookScripts(const QString &scriptDirectory, const QString &agentName, QString *error)
{
    if (!removeHookScripts(scriptDirectory, agentName, {}, error)) {
        return false;
    }
    QDir().rmdir(scriptDirectory);
    return true;
}

// Removes the hook groups that Kmux owns from every event, including events
// that Kmux no longer uses. Returns the number of removed groups.
int removeKmuxOwnedHooks(QJsonObject *hooks, const QString &scriptDirectory, const QString &agentName)
{
    int removed = 0;
    for (const QString &eventName : hooks->keys()) {
        const QJsonArray originalGroups = hooks->value(eventName).toArray();
        const QJsonArray rewrittenGroups = removeKmuxOwnedHookGroups(originalGroups, scriptDirectory, agentName);
        removed += originalGroups.size() - rewrittenGroups.size();
        if (rewrittenGroups.isEmpty()) {
            hooks->remove(eventName);
        } else {
            hooks->insert(eventName, rewrittenGroups);
        }
    }
    return removed;
}

struct HookInstallationStatus {
    int groups = 0;
    int handlers = 0;
    int executableHandlers = 0;
    QStringList invalidHandlers;
};

HookInstallationStatus hookInstallationStatus(const QJsonObject &hooks, const QString &scriptDirectory, const QString &agentName)
{
    HookInstallationStatus status;
    for (const QString &eventName : hooks.keys()) {
        const QJsonArray groups = hooks.value(eventName).toArray();
        for (const QJsonValue &groupValue : groups) {
            const QJsonArray hookList = groupValue.toObject().value(QStringLiteral("hooks")).toArray();
            bool ownedGroup = false;
            for (const QJsonValue &hookValue : hookList) {
                const QString command = hookValue.toObject().value(QStringLiteral("command")).toString();
                const QString scriptPath = kmuxHookScriptPath(command, scriptDirectory, agentName);
                if (scriptPath.isEmpty()) {
                    continue;
                }

                ownedGroup = true;
                ++status.handlers;
                const QFileInfo handlerInfo(scriptPath);
                if (handlerInfo.isFile() && handlerInfo.isExecutable()) {
                    ++status.executableHandlers;
                } else {
                    status.invalidHandlers.append(command);
                }
            }
            if (ownedGroup) {
                ++status.groups;
            }
        }
    }
    return status;
}

void printHookInstallationStatus(QTextStream &out, const HookInstallationStatus &status, const QString &scriptDirectory)
{
    out << "Kmux hook groups: " << status.groups << '\n';
    out << "Hook scripts: " << status.executableHandlers << '/' << status.handlers << " executable\n";
    out << "Managed hook directory: " << scriptDirectory << '\n';
    for (const QString &handler : status.invalidHandlers) {
        out << "Invalid hook script: " << handler << '\n';
    }
}

// Codex reports its state with OSC 7501, so Kmux no longer installs Codex
// hooks. This removes the hooks that earlier versions installed.
int uninstallCodexHooks(const QString &codexHomeOverride)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    QString error;

    const QString configDir = codexHome(codexHomeOverride);
    HookTransactionLock transactionLock(QString::fromLatin1(CodexAgentName), configDir);
    if (!transactionLock.acquire(&error)) {
        err << error << '\n';
        return 1;
    }
    const QString scriptDirectory = hookScriptDirectory(QString::fromLatin1(CodexAgentName), configDir);
    const QString hooksPath = QDir(configDir).filePath(QStringLiteral("hooks.json"));
    QJsonObject root = readJsonObject(hooksPath, &error);
    if (!error.isEmpty()) {
        err << error << '\n';
        return 1;
    }

    QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();
    const int removed = removeKmuxOwnedHooks(&hooks, scriptDirectory, QString::fromLatin1(CodexAgentName));
    setRemainingHooks(&root, hooks);

    // hooks.json holds nothing but hooks, so remove it when none remain.
    if (root.isEmpty()) {
        if (QFileInfo::exists(hooksPath) && !QFile::remove(hooksPath)) {
            err << "Could not remove " << hooksPath << '\n';
            return 1;
        }
    } else if (QFileInfo::exists(hooksPath) && !writeJsonFileAtomically(hooksPath, root, &error)) {
        err << error << '\n';
        return 1;
    }

    const QString configPath = QDir(configDir).filePath(QStringLiteral("config.toml"));
    QFile configFile(configPath);
    if (configFile.exists()) {
        if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            err << configFile.errorString() << '\n';
            return 1;
        }
        QStringList lines = splitTomlLines(QString::fromUtf8(configFile.readAll()));
        removeKonsoleBlocks(&lines);
        removeEmptyFeaturesTable(&lines);
        if (!writeTextFileAtomically(configPath, joinTomlLines(lines), &error)) {
            err << error << '\n';
            return 1;
        }
    }

    if (!uninstallHookScripts(scriptDirectory, QString::fromLatin1(CodexAgentName), &error)) {
        err << error << '\n';
        return 1;
    }

    out << "Removed " << removed << " Kmux Codex hook group(s) from " << hooksPath << '\n';
    out << "Removed Kmux Codex hook trust from " << configPath << '\n';
    return 0;
}

int installClaudeHooks(const QString &claudeHomeOverride, bool quiet)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    QString error;

    const QString configDir = claudeHome(claudeHomeOverride);
    HookTransactionLock transactionLock(QString::fromLatin1(ClaudeAgentName), configDir);
    if (!transactionLock.acquire(&error)) {
        err << error << '\n';
        return 1;
    }
    if (!QDir().mkpath(configDir)) {
        err << "Could not create " << configDir << '\n';
        return 1;
    }
    const QString scriptDirectory = hookScriptDirectory(QString::fromLatin1(ClaudeAgentName), configDir);
    if (!installHookScripts(scriptDirectory, QString::fromLatin1(ClaudeAgentName), ClaudeHookEvents, &error)) {
        err << error << '\n';
        return 1;
    }

    const QString settingsPath = QDir(configDir).filePath(QStringLiteral("settings.json"));
    QJsonObject root = readJsonObject(settingsPath, &error);
    if (!error.isEmpty()) {
        err << error << '\n';
        return 1;
    }

    QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();
    removeKmuxOwnedHooks(&hooks, scriptDirectory, QString::fromLatin1(ClaudeAgentName));
    for (const HookEvent &event : ClaudeHookEvents) {
        QJsonArray groups = hooks.value(event.eventName).toArray();
        groups.append(buildHookGroup(scriptDirectory, QString::fromLatin1(ClaudeAgentName), event));
        hooks.insert(event.eventName, groups);
    }
    root.insert(QStringLiteral("hooks"), hooks);

    if (!writeJsonFileAtomically(settingsPath, root, &error)) {
        err << error << '\n';
        return 1;
    }

    if (!quiet) {
        out << "Installed Claude Code hooks in " << settingsPath << '\n';
        out << "Hook scripts are in " << scriptDirectory << '\n';
    }
    return 0;
}

int uninstallClaudeHooks(const QString &claudeHomeOverride)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    QString error;

    const QString configDir = claudeHome(claudeHomeOverride);
    HookTransactionLock transactionLock(QString::fromLatin1(ClaudeAgentName), configDir);
    if (!transactionLock.acquire(&error)) {
        err << error << '\n';
        return 1;
    }
    const QString scriptDirectory = hookScriptDirectory(QString::fromLatin1(ClaudeAgentName), configDir);
    const QString settingsPath = QDir(configDir).filePath(QStringLiteral("settings.json"));
    QJsonObject root = readJsonObject(settingsPath, &error);
    if (!error.isEmpty()) {
        err << error << '\n';
        return 1;
    }

    QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();
    const int removed = removeKmuxOwnedHooks(&hooks, scriptDirectory, QString::fromLatin1(ClaudeAgentName));
    setRemainingHooks(&root, hooks);

    if (QFileInfo::exists(settingsPath) && !writeJsonFileAtomically(settingsPath, root, &error)) {
        err << error << '\n';
        return 1;
    }

    if (!uninstallHookScripts(scriptDirectory, QString::fromLatin1(ClaudeAgentName), &error)) {
        err << error << '\n';
        return 1;
    }

    out << "Removed " << removed << " Kmux Claude Code hook group(s) from " << settingsPath << '\n';
    return 0;
}

int statusClaudeHooks(const QString &claudeHomeOverride)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    const QString configDir = claudeHome(claudeHomeOverride);
    const QString scriptDirectory = hookScriptDirectory(QString::fromLatin1(ClaudeAgentName), configDir);
    const QString settingsPath = QDir(configDir).filePath(QStringLiteral("settings.json"));

    QString error;
    HookTransactionLock transactionLock(QString::fromLatin1(ClaudeAgentName), configDir);
    if (!transactionLock.acquire(&error)) {
        err << error << '\n';
        return 1;
    }
    const QJsonObject root = readJsonObject(settingsPath, &error);
    if (!error.isEmpty()) {
        err << error << '\n';
        return 1;
    }
    const QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();
    const HookInstallationStatus status = hookInstallationStatus(hooks, scriptDirectory, QString::fromLatin1(ClaudeAgentName));

    out << "Claude home: " << configDir << '\n';
    out << "settings.json: " << settingsPath << (QFileInfo::exists(settingsPath) ? QStringLiteral(" exists") : QStringLiteral(" missing")) << '\n';
    printHookInstallationStatus(out, status, scriptDirectory);
    return status.groups == ClaudeHookEvents.size() && status.handlers == ClaudeHookEvents.size() && status.executableHandlers == status.handlers ? 0 : 1;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kmux-agent-hooks"));
    QCoreApplication::setApplicationVersion(QStringLiteral(KMUX_DISPLAY_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Install terminal agent hooks for Kmux project workspaces."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsOptions);
    const QCommandLineOption quietOption(QStringLiteral("quiet"), QStringLiteral("Do not print successful installation details."));
    parser.addOption(quietOption);
    const QCommandLineOption codexHomeOption(QStringLiteral("codex-home"),
                                             QStringLiteral("Use a custom Codex config directory instead of CODEX_HOME or ~/.codex."),
                                             QStringLiteral("path"));
    parser.addOption(codexHomeOption);
    const QCommandLineOption claudeHomeOption(QStringLiteral("claude-home"),
                                              QStringLiteral("Use a custom Claude Code config directory instead of ~/.claude."),
                                              QStringLiteral("path"));
    parser.addOption(claudeHomeOption);
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("install, uninstall, or status; only uninstall for codex."));
    parser.addPositionalArgument(QStringLiteral("agent"), QStringLiteral("Agent name: codex or claude."));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.size() != 2) {
        parser.showHelp(2);
    }

    const QString command = args.at(0);
    const QString agent = args.at(1);
    if (agent != QLatin1String(CodexAgentName) && agent != QLatin1String(ClaudeAgentName)) {
        QTextStream(stderr) << "Supported agents are: codex, claude.\n";
        return 2;
    }

    if (agent == QLatin1String(CodexAgentName) && command != QLatin1String("uninstall")) {
        QTextStream(stderr) << "Codex reports its state to Kmux itself, so Kmux no longer installs Codex hooks.\n"
                               "Run \"kmux-agent-hooks uninstall codex\" to remove the hooks that earlier versions installed.\n";
        return 2;
    }

    if (command == QLatin1String("install")) {
        return installClaudeHooks(parser.value(claudeHomeOption), parser.isSet(quietOption));
    }
    if (command == QLatin1String("uninstall")) {
        if (agent == QLatin1String(CodexAgentName)) {
            return uninstallCodexHooks(parser.value(codexHomeOption));
        }
        return uninstallClaudeHooks(parser.value(claudeHomeOption));
    }
    if (command == QLatin1String("status")) {
        return statusClaudeHooks(parser.value(claudeHomeOption));
    }

    QTextStream(stderr) << "Unknown command: " << command << '\n';
    return 2;
}
