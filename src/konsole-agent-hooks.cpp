/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

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
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>
#include <utility>

namespace
{
constexpr auto HooksJsonMarker = "kmux-project-status";
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
    QString status;
    int timeout = 5;
    QString matcher;
};

// Codex SessionStart describes the agent process, not an active turn. Its
// PermissionRequest hook has no matching approval-resolved event, so Kmux
// clears that needs-input state when the terminal receives the decision key.
// Manual compaction is a standalone turn; automatic compaction inherits the
// running state of the turn that triggered it.
const QList<HookEvent> CodexHookEvents = {
    {QStringLiteral("SessionStart"), QStringLiteral("session_start"), QStringLiteral("idle"), 5, QString()},
    {QStringLiteral("UserPromptSubmit"), QStringLiteral("user_prompt_submit"), QStringLiteral("running"), 5, QString()},
    {QStringLiteral("PreToolUse"), QStringLiteral("pre_tool_use"), QStringLiteral("running"), 5, QString()},
    {QStringLiteral("PostToolUse"), QStringLiteral("post_tool_use"), QStringLiteral("running"), 5, QString()},
    {QStringLiteral("PreCompact"), QStringLiteral("pre_compact"), QStringLiteral("running"), 5, QStringLiteral("manual")},
    {QStringLiteral("PostCompact"), QStringLiteral("post_compact"), QStringLiteral("idle"), 5, QStringLiteral("manual")},
    {QStringLiteral("PermissionRequest"), QStringLiteral("permission_request"), QStringLiteral("needsInput"), 5, QString()},
    {QStringLiteral("Stop"), QStringLiteral("stop"), QStringLiteral("idle"), 5, QString()},
};

const QList<HookEvent> ClaudeHookEvents = {
    {QStringLiteral("SessionStart"), QStringLiteral("session_start"), QStringLiteral("running"), 5, QString()},
    {QStringLiteral("UserPromptSubmit"), QStringLiteral("user_prompt_submit"), QStringLiteral("running"), 5, QString()},
    {QStringLiteral("PreToolUse"), QStringLiteral("pre_tool_use"), QStringLiteral("running"), 5, QString()},
    {QStringLiteral("PostToolUse"), QStringLiteral("post_tool_use"), QStringLiteral("running"), 5, QString()},
    {QStringLiteral("PostToolUseFailure"), QStringLiteral("post_tool_use_failure"), QStringLiteral("running"), 5, QString()},
    {QStringLiteral("PermissionRequest"), QStringLiteral("permission_request"), QStringLiteral("needsInput"), 5, QString()},
    {QStringLiteral("Notification"), QStringLiteral("notification"), QStringLiteral("needsInput"), 5, QStringLiteral("permission_prompt|idle_prompt")},
    {QStringLiteral("Stop"), QStringLiteral("stop"), QStringLiteral("idle"), 5, QString()},
    {QStringLiteral("StopFailure"), QStringLiteral("stop_failure"), QStringLiteral("idle"), 5, QString()},
    {QStringLiteral("SessionEnd"), QStringLiteral("session_end"), QStringLiteral("idle"), 5, QString()},
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

QString hookScriptRootDirectory()
{
    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(dataRoot.isEmpty() ? QDir(homePath()).filePath(QStringLiteral(".local/share")) : dataRoot).filePath(QStringLiteral("kmux/hooks"));
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

QString hookScriptContent(const QString &agentName, const HookEvent &event)
{
    const QString helper = projectStatusHelperPath();
    const QString quotedHelper = shellQuote(helper);
    const QString quotedAgentName = shellQuote(agentName);
    const QString quotedEventName = shellQuote(event.eventName);
    const QString agentProcessArguments = agentName == QLatin1String(CodexAgentName) ? QStringLiteral(
                                                                                           "if [ -n \"${KMUX_CODEX_PID:-}\" ]; then\n"
                                                                                           "    set -- \"$@\" --agent-pid \"$KMUX_CODEX_PID\"\n"
                                                                                           "fi\n")
                                                                                     : QString();

    return QStringLiteral(
               "#!/bin/sh\n"
               "# kmux-%1-hook v1\n"
               "helper=%2\n"
               "set -- --hook-output --agent %3 --event %4\n"
               "%5"
               "if [ -x \"$helper\" ]; then\n"
               "    \"$helper\" \"$@\" %6\n"
               "elif command -v kmux-project-status >/dev/null 2>&1; then\n"
               "    kmux-project-status \"$@\" %6\n"
               "else\n"
               "    printf '{}\\n'\n"
               "fi\n")
        .arg(agentName, quotedHelper, quotedAgentName, quotedEventName, agentProcessArguments, event.status);
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

bool commandIsKonsoleOwned(const QString &command, const QString &agentName)
{
    return command.contains(QStringLiteral("kmux/hooks/%1-").arg(agentName)) || command.contains(QStringLiteral("kmux-%1-hook").arg(agentName))
        || command.contains(QStringLiteral("konsole/hooks/%1-").arg(agentName)) || command.contains(QStringLiteral("konsole-%1-hook").arg(agentName))
        || (agentName == QLatin1String(CodexAgentName) && command.contains(QStringLiteral("konsole-project-status")))
        || (agentName == QLatin1String(CodexAgentName) && command.contains(QString::fromLatin1(HooksJsonMarker)));
}

QJsonArray removeKonsoleOwnedHookGroups(QJsonArray groups, const QString &agentName)
{
    QJsonArray rewrittenGroups;
    for (const QJsonValue &groupValue : std::as_const(groups)) {
        QJsonObject group = groupValue.toObject();
        QJsonArray hookList = group.value(QStringLiteral("hooks")).toArray();
        QJsonArray rewrittenHookList;
        for (const QJsonValue &hookValue : std::as_const(hookList)) {
            const QJsonObject hook = hookValue.toObject();
            if (!commandIsKonsoleOwned(hook.value(QStringLiteral("command")).toString(), agentName)) {
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

QJsonObject buildHookGroup(const QString &scriptDirectory, const QString &agentName, const HookEvent &event)
{
    QJsonObject hook;
    hook.insert(QStringLiteral("type"), QStringLiteral("command"));
    hook.insert(QStringLiteral("command"), hookScriptPath(scriptDirectory, agentName, event));
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

QString canonicalPathForTrust(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (!canonical.isEmpty()) {
        return canonical;
    }

    const QFileInfo parentInfo(QFileInfo(path).absolutePath());
    const QString parentCanonical = parentInfo.canonicalFilePath();
    if (!parentCanonical.isEmpty()) {
        return QDir(parentCanonical).filePath(QFileInfo(path).fileName());
    }

    return QFileInfo(path).absoluteFilePath();
}

QString hookTrustHash(const HookEvent &event, const QString &command)
{
    QJsonObject handler;
    handler.insert(QStringLiteral("async"), false);
    handler.insert(QStringLiteral("command"), command);
    handler.insert(QStringLiteral("timeout"), event.timeout);
    handler.insert(QStringLiteral("type"), QStringLiteral("command"));

    QJsonArray hooks;
    hooks.append(handler);

    QJsonObject identity;
    identity.insert(QStringLiteral("event_name"), event.eventLabel);
    identity.insert(QStringLiteral("hooks"), hooks);

    const QByteArray json = QJsonDocument(identity).toJson(QJsonDocument::Compact);
    const QByteArray digest = QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex();
    return QStringLiteral("sha256:%1").arg(QString::fromLatin1(digest));
}

QString tomlQuoted(const QString &value)
{
    QString escaped;
    escaped.reserve(value.size());
    for (const QChar ch : value) {
        switch (ch.unicode()) {
        case '\\':
            escaped += QStringLiteral("\\\\");
            break;
        case '"':
            escaped += QStringLiteral("\\\"");
            break;
        case '\n':
            escaped += QStringLiteral("\\n");
            break;
        case '\r':
            escaped += QStringLiteral("\\r");
            break;
        case '\t':
            escaped += QStringLiteral("\\t");
            break;
        default:
            escaped += ch;
            break;
        }
    }

    return escaped;
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

bool lineDefinesKey(const QString &line, const QString &key)
{
    const QString escapedKey = QRegularExpression::escape(key);
    return QRegularExpression(QStringLiteral(R"(^\s*(?:%1|"%1"|'%1')\s*=)").arg(escapedKey)).match(line).hasMatch();
}

bool lineDefinesDottedFeatureHooks(const QString &line)
{
    return QRegularExpression(QStringLiteral(R"(^\s*(?:features|"features"|'features')\s*\.\s*(?:hooks|"hooks"|'hooks')\s*=)")).match(line).hasMatch();
}

bool lineDefinesDottedFeature(const QString &line)
{
    return QRegularExpression(QStringLiteral(R"(^\s*(?:features|"features"|'features')\s*\.)")).match(line).hasMatch();
}

bool lineDefinesTrueValue(const QString &line, const QString &keyPattern)
{
    return QRegularExpression(QStringLiteral(R"(^\s*%1\s*=\s*true\s*(#.*)?$)").arg(keyPattern)).match(line).hasMatch();
}

QString inlineTableWithHooksEnabled(const QString &line)
{
    const QRegularExpression assignmentPattern(QStringLiteral(R"(^\s*(?:features|"features"|'features')\s*=\s*\{)"));
    const QRegularExpressionMatch assignment = assignmentPattern.match(line);
    if (!assignment.hasMatch()) {
        return {};
    }

    const int openingBrace = line.indexOf(QLatin1Char('{'), assignment.capturedStart());
    int closingBrace = -1;
    QChar quote;
    bool escaped = false;
    int nestedBraces = 0;
    int nestedBrackets = 0;
    for (int i = openingBrace + 1; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (!quote.isNull()) {
            if (quote == QLatin1Char('"') && ch == QLatin1Char('\\') && !escaped) {
                escaped = true;
                continue;
            }
            if (ch == quote && !escaped) {
                quote = QChar();
            }
            escaped = false;
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
        } else if (ch == QLatin1Char('{')) {
            ++nestedBraces;
        } else if (ch == QLatin1Char('}')) {
            if (nestedBraces == 0 && nestedBrackets == 0) {
                closingBrace = i;
                break;
            }
            --nestedBraces;
        } else if (ch == QLatin1Char('[')) {
            ++nestedBrackets;
        } else if (ch == QLatin1Char(']')) {
            --nestedBrackets;
        }
    }
    if (closingBrace < 0) {
        return {};
    }

    const QString contents = line.mid(openingBrace + 1, closingBrace - openingBrace - 1);
    const QString hooksKeyPattern = QStringLiteral(R"((?:hooks|"hooks"|'hooks'))");
    const QRegularExpression hooksPattern(QStringLiteral(R"(^\s*%1\s*=)").arg(hooksKeyPattern));
    const QRegularExpression hooksTruePattern(QStringLiteral(R"(^\s*%1\s*=\s*true\s*$)").arg(hooksKeyPattern));
    int entryStart = 0;
    quote = QChar();
    escaped = false;
    nestedBraces = 0;
    nestedBrackets = 0;
    for (int i = 0; i <= contents.size(); ++i) {
        const QChar ch = i < contents.size() ? contents.at(i) : QLatin1Char(',');
        if (!quote.isNull()) {
            if (quote == QLatin1Char('"') && ch == QLatin1Char('\\') && !escaped) {
                escaped = true;
                continue;
            }
            if (ch == quote && !escaped) {
                quote = QChar();
            }
            escaped = false;
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
        } else if (ch == QLatin1Char('{')) {
            ++nestedBraces;
        } else if (ch == QLatin1Char('}')) {
            --nestedBraces;
        } else if (ch == QLatin1Char('[')) {
            ++nestedBrackets;
        } else if (ch == QLatin1Char(']')) {
            --nestedBrackets;
        } else if (ch == QLatin1Char(',') && nestedBraces == 0 && nestedBrackets == 0) {
            const QString entry = contents.mid(entryStart, i - entryStart);
            if (hooksPattern.match(entry).hasMatch()) {
                if (hooksTruePattern.match(entry).hasMatch()) {
                    return line;
                }
                QString rewrittenContents = contents;
                rewrittenContents.replace(entryStart, i - entryStart, QStringLiteral(" hooks = true"));
                return line.left(openingBrace + 1) + rewrittenContents + line.mid(closingBrace);
            }
            entryStart = i + 1;
        }
    }

    const QString rewrittenContents =
        contents.trimmed().isEmpty() ? QStringLiteral(" hooks = true ") : QStringLiteral(" %1, hooks = true ").arg(contents.trimmed());
    return line.left(openingBrace + 1) + rewrittenContents + line.mid(closingBrace);
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

QString installHooksFeature(const QString &content)
{
    QStringList lines = splitTomlLines(content);
    removeKonsoleBlocks(&lines);

    const QString hooksPattern = QStringLiteral(R"((?:hooks|"hooks"|'hooks'))");
    const QString dottedHooksPattern = QStringLiteral(R"((?:features|"features"|'features')\s*\.\s*(?:hooks|"hooks"|'hooks'))");

    for (int i = 0; i < lines.size(); ++i) {
        if (lineStartsTable(lines.at(i), QStringLiteral("features"))) {
            const int end = tableEnd(lines, i);
            for (int j = i + 1; j < end; ++j) {
                if (!lineDefinesKey(lines.at(j), QStringLiteral("hooks"))) {
                    continue;
                }
                if (lineDefinesTrueValue(lines.at(j), hooksPattern)) {
                    return joinTomlLines(lines);
                }
                lines.replace(j,
                              QStringList{QString::fromLatin1(HooksFeatureBegin),
                                          QString::fromLatin1(HooksFeaturePreviousLinePrefix) + lines.at(j),
                                          QStringLiteral("hooks = true"),
                                          QString::fromLatin1(HooksFeatureEnd)}
                                  .join(QLatin1Char('\n')));
                const QString block = lines.takeAt(j);
                const QStringList blockLines = block.split(QLatin1Char('\n'));
                for (int k = 0; k < blockLines.size(); ++k) {
                    lines.insert(j + k, blockLines.at(k));
                }
                return joinTomlLines(lines);
            }

            lines.insert(i + 1, QString::fromLatin1(HooksFeatureEnd));
            lines.insert(i + 1, QStringLiteral("hooks = true"));
            lines.insert(i + 1, QString::fromLatin1(HooksFeatureBegin));
            return joinTomlLines(lines);
        }
    }

    int firstTable = lines.size();
    for (int i = 0; i < lines.size(); ++i) {
        if (lineStartsAnyTable(lines.at(i))) {
            firstTable = i;
            break;
        }
    }

    for (int i = 0; i < firstTable; ++i) {
        if (!lineDefinesDottedFeatureHooks(lines.at(i))) {
            continue;
        }
        if (lineDefinesTrueValue(lines.at(i), dottedHooksPattern)) {
            return joinTomlLines(lines);
        }
        lines.replace(i,
                      QStringList{QString::fromLatin1(HooksFeatureBegin),
                                  QString::fromLatin1(HooksFeaturePreviousLinePrefix) + lines.at(i),
                                  QStringLiteral("features.hooks = true"),
                                  QString::fromLatin1(HooksFeatureEnd)}
                          .join(QLatin1Char('\n')));
        const QString block = lines.takeAt(i);
        const QStringList blockLines = block.split(QLatin1Char('\n'));
        for (int k = 0; k < blockLines.size(); ++k) {
            lines.insert(i + k, blockLines.at(k));
        }
        return joinTomlLines(lines);
    }

    for (int i = 0; i < firstTable; ++i) {
        const QString rewrittenInlineTable = inlineTableWithHooksEnabled(lines.at(i));
        if (rewrittenInlineTable.isEmpty()) {
            continue;
        }
        if (rewrittenInlineTable == lines.at(i)) {
            return joinTomlLines(lines);
        }
        lines.replace(i,
                      QStringList{QString::fromLatin1(HooksFeatureBegin),
                                  QString::fromLatin1(HooksFeaturePreviousLinePrefix) + lines.at(i),
                                  rewrittenInlineTable,
                                  QString::fromLatin1(HooksFeatureEnd)}
                          .join(QLatin1Char('\n')));
        const QString block = lines.takeAt(i);
        const QStringList blockLines = block.split(QLatin1Char('\n'));
        for (int k = 0; k < blockLines.size(); ++k) {
            lines.insert(i + k, blockLines.at(k));
        }
        return joinTomlLines(lines);
    }

    for (int i = 0; i < firstTable; ++i) {
        if (!lineDefinesDottedFeature(lines.at(i))) {
            continue;
        }
        lines.insert(i + 1, QString::fromLatin1(HooksFeatureEnd));
        lines.insert(i + 1, QStringLiteral("features.hooks = true"));
        lines.insert(i + 1, QString::fromLatin1(HooksFeatureBegin));
        return joinTomlLines(lines);
    }

    const bool addFeatureSeparator = !lines.isEmpty() && !lines.last().isEmpty();
    lines.append(QString::fromLatin1(HooksFeatureBegin));
    if (addFeatureSeparator) {
        lines.append(QString());
    }
    lines.append(QStringLiteral("[features]"));
    lines.append(QStringLiteral("hooks = true"));
    lines.append(QString::fromLatin1(HooksFeatureEnd));
    return joinTomlLines(lines);
}

QString installHookTrust(const QString &content, const QString &hooksJsonPath, const QJsonObject &hooks)
{
    QStringList lines = splitTomlLines(content);
    if (removeKonsoleBlocks(&lines)) {
        removeEmptyFeaturesTable(&lines);
    }
    lines = splitTomlLines(installHooksFeature(joinTomlLines(lines)));

    QStringList trustLines;
    const QString keySource = canonicalPathForTrust(hooksJsonPath);
    for (const HookEvent &event : CodexHookEvents) {
        const QJsonArray groups = hooks.value(event.eventName).toArray();
        for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            const QJsonArray hookList = groups.at(groupIndex).toObject().value(QStringLiteral("hooks")).toArray();
            for (int hookIndex = 0; hookIndex < hookList.size(); ++hookIndex) {
                const QString command = hookList.at(hookIndex).toObject().value(QStringLiteral("command")).toString();
                if (!commandIsKonsoleOwned(command, QString::fromLatin1(CodexAgentName))) {
                    continue;
                }
                const QString key = QStringLiteral("%1:%2:%3:%4").arg(keySource, event.eventLabel).arg(groupIndex).arg(hookIndex);
                trustLines.append(QStringLiteral("[hooks.state.\"%1\"]").arg(tomlQuoted(key)));
                trustLines.append(QStringLiteral("trusted_hash = \"%1\"").arg(hookTrustHash(event, command)));
            }
        }
    }

    if (!trustLines.isEmpty()) {
        const bool addTrustSeparator = !lines.isEmpty() && !lines.last().isEmpty();
        lines.append(QString::fromLatin1(HookTrustBegin));
        if (addTrustSeparator) {
            lines.append(QString());
        }
        lines.append(trustLines);
        lines.append(QString::fromLatin1(HookTrustEnd));
    }

    return joinTomlLines(lines);
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

    return true;
}

bool uninstallHookScripts(const QString &scriptDirectory, const QString &agentName, const QList<HookEvent> &events, QString *error)
{
    for (const HookEvent &event : events) {
        QFile file(hookScriptPath(scriptDirectory, agentName, event));
        if (file.exists() && !file.remove()) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
    }
    QDir().rmdir(scriptDirectory);
    return true;
}

struct HookInstallationStatus {
    int groups = 0;
    int handlers = 0;
    int executableHandlers = 0;
    QStringList invalidHandlers;
};

HookInstallationStatus hookInstallationStatus(const QJsonObject &hooks, const QString &agentName)
{
    HookInstallationStatus status;
    for (const QString &eventName : hooks.keys()) {
        const QJsonArray groups = hooks.value(eventName).toArray();
        for (const QJsonValue &groupValue : groups) {
            const QJsonArray hookList = groupValue.toObject().value(QStringLiteral("hooks")).toArray();
            bool ownedGroup = false;
            for (const QJsonValue &hookValue : hookList) {
                const QString command = hookValue.toObject().value(QStringLiteral("command")).toString();
                if (!commandIsKonsoleOwned(command, agentName)) {
                    continue;
                }

                ownedGroup = true;
                ++status.handlers;
                const QFileInfo handlerInfo(command);
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

int installCodexHooks(const QString &codexHomeOverride, bool quiet)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    QString error;

    const QString configDir = codexHome(codexHomeOverride);
    if (!QDir().mkpath(configDir)) {
        err << "Could not create " << configDir << '\n';
        return 1;
    }
    const QString scriptDirectory = hookScriptDirectory(QString::fromLatin1(CodexAgentName), configDir);
    if (!installHookScripts(scriptDirectory, QString::fromLatin1(CodexAgentName), CodexHookEvents, &error)) {
        err << error << '\n';
        return 1;
    }

    const QString hooksPath = QDir(configDir).filePath(QStringLiteral("hooks.json"));
    QJsonObject root = readJsonObject(hooksPath, &error);
    if (!error.isEmpty()) {
        err << error << '\n';
        return 1;
    }

    QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();
    for (const HookEvent &event : CodexHookEvents) {
        QJsonArray groups = removeKonsoleOwnedHookGroups(hooks.value(event.eventName).toArray(), QString::fromLatin1(CodexAgentName));
        groups.append(buildHookGroup(scriptDirectory, QString::fromLatin1(CodexAgentName), event));
        hooks.insert(event.eventName, groups);
    }
    root.insert(QStringLiteral("hooks"), hooks);

    if (!writeJsonFileAtomically(hooksPath, root, &error)) {
        err << error << '\n';
        return 1;
    }

    const QString configPath = QDir(configDir).filePath(QStringLiteral("config.toml"));
    QString configContent;
    QFile configFile(configPath);
    if (configFile.exists()) {
        if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            err << configFile.errorString() << '\n';
            return 1;
        }
        configContent = QString::fromUtf8(configFile.readAll());
    }

    const QString newConfigContent = installHookTrust(configContent, hooksPath, hooks);
    if (!writeTextFileAtomically(configPath, newConfigContent, &error)) {
        err << error << '\n';
        return 1;
    }

    if (!quiet) {
        out << "Installed Codex hooks at " << hooksPath << '\n';
        out << "Enabled and trusted Codex hooks in " << configPath << '\n';
        out << "Hook scripts are in " << scriptDirectory << '\n';
    }
    return 0;
}

int uninstallCodexHooks(const QString &codexHomeOverride)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    QString error;

    const QString configDir = codexHome(codexHomeOverride);
    const QString scriptDirectory = hookScriptDirectory(QString::fromLatin1(CodexAgentName), configDir);
    const QString hooksPath = QDir(configDir).filePath(QStringLiteral("hooks.json"));
    QJsonObject root = readJsonObject(hooksPath, &error);
    if (!error.isEmpty()) {
        err << error << '\n';
        return 1;
    }

    QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();
    int removed = 0;
    for (const QString &eventName : hooks.keys()) {
        const QJsonArray originalGroups = hooks.value(eventName).toArray();
        const QJsonArray rewrittenGroups = removeKonsoleOwnedHookGroups(originalGroups, QString::fromLatin1(CodexAgentName));
        removed += originalGroups.size() - rewrittenGroups.size();
        if (rewrittenGroups.isEmpty()) {
            hooks.remove(eventName);
        } else {
            hooks.insert(eventName, rewrittenGroups);
        }
    }
    root.insert(QStringLiteral("hooks"), hooks);

    if (QFileInfo::exists(hooksPath) && !writeJsonFileAtomically(hooksPath, root, &error)) {
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

    if (!uninstallHookScripts(scriptDirectory, QString::fromLatin1(CodexAgentName), CodexHookEvents, &error)) {
        err << error << '\n';
        return 1;
    }

    out << "Removed " << removed << " Kmux Codex hook group(s) from " << hooksPath << '\n';
    out << "Removed Kmux Codex hook trust from " << configPath << '\n';
    return 0;
}

int statusCodexHooks(const QString &codexHomeOverride)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    const QString configDir = codexHome(codexHomeOverride);
    const QString scriptDirectory = hookScriptDirectory(QString::fromLatin1(CodexAgentName), configDir);
    const QString hooksPath = QDir(configDir).filePath(QStringLiteral("hooks.json"));
    const QString configPath = QDir(configDir).filePath(QStringLiteral("config.toml"));

    QString error;
    const QJsonObject root = readJsonObject(hooksPath, &error);
    if (!error.isEmpty()) {
        err << error << '\n';
        return 1;
    }
    const QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();
    const HookInstallationStatus status = hookInstallationStatus(hooks, QString::fromLatin1(CodexAgentName));

    out << "Codex home: " << configDir << '\n';
    out << "hooks.json: " << hooksPath << (QFileInfo::exists(hooksPath) ? QStringLiteral(" exists") : QStringLiteral(" missing")) << '\n';
    out << "config.toml: " << configPath << (QFileInfo::exists(configPath) ? QStringLiteral(" exists") : QStringLiteral(" missing")) << '\n';
    printHookInstallationStatus(out, status, scriptDirectory);
    return status.groups == CodexHookEvents.size() && status.handlers == CodexHookEvents.size() && status.executableHandlers == status.handlers ? 0 : 1;
}

int installClaudeHooks(const QString &claudeHomeOverride)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    QString error;

    const QString configDir = claudeHome(claudeHomeOverride);
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
    for (const HookEvent &event : ClaudeHookEvents) {
        QJsonArray groups = removeKonsoleOwnedHookGroups(hooks.value(event.eventName).toArray(), QString::fromLatin1(ClaudeAgentName));
        groups.append(buildHookGroup(scriptDirectory, QString::fromLatin1(ClaudeAgentName), event));
        hooks.insert(event.eventName, groups);
    }
    root.insert(QStringLiteral("hooks"), hooks);

    if (!writeJsonFileAtomically(settingsPath, root, &error)) {
        err << error << '\n';
        return 1;
    }

    out << "Installed Claude Code hooks in " << settingsPath << '\n';
    out << "Hook scripts are in " << scriptDirectory << '\n';
    return 0;
}

int uninstallClaudeHooks(const QString &claudeHomeOverride)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    QString error;

    const QString configDir = claudeHome(claudeHomeOverride);
    const QString scriptDirectory = hookScriptDirectory(QString::fromLatin1(ClaudeAgentName), configDir);
    const QString settingsPath = QDir(configDir).filePath(QStringLiteral("settings.json"));
    QJsonObject root = readJsonObject(settingsPath, &error);
    if (!error.isEmpty()) {
        err << error << '\n';
        return 1;
    }

    QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();
    int removed = 0;
    for (const QString &eventName : hooks.keys()) {
        const QJsonArray originalGroups = hooks.value(eventName).toArray();
        const QJsonArray rewrittenGroups = removeKonsoleOwnedHookGroups(originalGroups, QString::fromLatin1(ClaudeAgentName));
        removed += originalGroups.size() - rewrittenGroups.size();
        if (rewrittenGroups.isEmpty()) {
            hooks.remove(eventName);
        } else {
            hooks.insert(eventName, rewrittenGroups);
        }
    }
    root.insert(QStringLiteral("hooks"), hooks);

    if (QFileInfo::exists(settingsPath) && !writeJsonFileAtomically(settingsPath, root, &error)) {
        err << error << '\n';
        return 1;
    }

    if (!uninstallHookScripts(scriptDirectory, QString::fromLatin1(ClaudeAgentName), ClaudeHookEvents, &error)) {
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
    const QJsonObject root = readJsonObject(settingsPath, &error);
    if (!error.isEmpty()) {
        err << error << '\n';
        return 1;
    }
    const QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();
    const HookInstallationStatus status = hookInstallationStatus(hooks, QString::fromLatin1(ClaudeAgentName));

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
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

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
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("install, uninstall, or status."));
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

    if (command == QLatin1String("install")) {
        if (agent == QLatin1String(CodexAgentName)) {
            return installCodexHooks(parser.value(codexHomeOption), parser.isSet(quietOption));
        }
        return installClaudeHooks(parser.value(claudeHomeOption));
    }
    if (command == QLatin1String("uninstall")) {
        if (agent == QLatin1String(CodexAgentName)) {
            return uninstallCodexHooks(parser.value(codexHomeOption));
        }
        return uninstallClaudeHooks(parser.value(claudeHomeOption));
    }
    if (command == QLatin1String("status")) {
        if (agent == QLatin1String(CodexAgentName)) {
            return statusCodexHooks(parser.value(codexHomeOption));
        }
        return statusClaudeHooks(parser.value(claudeHomeOption));
    }

    QTextStream(stderr) << "Unknown command: " << command << '\n';
    return 2;
}
