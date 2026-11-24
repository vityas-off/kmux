/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ProgramStatus.h"

#include <QDebug>
#include <QHash>
#include <QStringDecoder>

#include <algorithm>

using namespace Konsole;

namespace
{
// The whole sequence may take 4096 bytes, of which "ESC ] 7501 ;" and "ESC \"
// take 9. A valid payload is ASCII, so characters and bytes coincide.
constexpr qsizetype MaximumPayloadLength = 4096 - 9;
constexpr qsizetype MaximumKeyLength = 16;
constexpr qsizetype MaximumAppLength = 32;
constexpr qsizetype MaximumIdLength = 128;
constexpr qsizetype MaximumIdSegmentLength = 32;
constexpr qsizetype MaximumIdDepth = 8;
constexpr qsizetype MaximumEncodedTitleLength = 256;
constexpr qsizetype MaximumDecodedTitleLength = 192;
constexpr qsizetype MaximumEncodedMessageLength = 2732;
constexpr qsizetype MaximumDecodedMessageLength = 2048;
constexpr int MaximumProgress = 100;

bool isAsciiLetterOrDigit(QChar character)
{
    return (character >= QLatin1Char('a') && character <= QLatin1Char('z')) || (character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
        || (character >= QLatin1Char('0') && character <= QLatin1Char('9'));
}

bool isKeyCharacter(QChar character)
{
    return character >= QLatin1Char('a') && character <= QLatin1Char('z');
}

bool isValueCharacter(QChar character)
{
    return isAsciiLetterOrDigit(character) || QStringView(u"_.,+/=-").contains(character);
}

// App names and id segments share this alphabet.
bool isNameCharacter(QChar character)
{
    return isAsciiLetterOrDigit(character) || QStringView(u"_.+-").contains(character);
}

bool isValidName(QStringView name, qsizetype maximumLength)
{
    return !name.isEmpty() && name.size() <= maximumLength && std::all_of(name.begin(), name.end(), isNameCharacter);
}

bool isValidId(QStringView id)
{
    if (id.size() > MaximumIdLength) {
        return false;
    }
    const auto segments = id.split(QLatin1Char('/'));
    return segments.size() <= MaximumIdDepth && std::all_of(segments.begin(), segments.end(), [](QStringView segment) {
               return isValidName(segment, MaximumIdSegmentLength);
           });
}

std::optional<ProgramState> stateFromValue(QStringView value)
{
    if (value == u"idle") {
        return ProgramState::Idle;
    }
    if (value == u"working") {
        return ProgramState::Working;
    }
    if (value == u"done") {
        return ProgramState::Done;
    }
    if (value == u"blocked") {
        return ProgramState::Blocked;
    }
    if (value == u"error") {
        return ProgramState::Error;
    }
    return std::nullopt;
}

QLatin1String valueFromState(ProgramState state)
{
    switch (state) {
    case ProgramState::Idle:
        return QLatin1String("idle");
    case ProgramState::Working:
        return QLatin1String("working");
    case ProgramState::Done:
        return QLatin1String("done");
    case ProgramState::Blocked:
        return QLatin1String("blocked");
    case ProgramState::Error:
        return QLatin1String("error");
    }
    return QLatin1String("idle");
}

QLatin1String valueFromKind(ProgramStatusKind kind)
{
    switch (kind) {
    case ProgramStatusKind::None:
        return {};
    case ProgramStatusKind::Permission:
        return QLatin1String("permission");
    case ProgramStatusKind::Question:
        return QLatin1String("question");
    case ProgramStatusKind::Auth:
        return QLatin1String("auth");
    }
    return {};
}

ProgramStatusKind kindFromValue(QStringView value)
{
    if (value == u"permission") {
        return ProgramStatusKind::Permission;
    }
    if (value == u"question") {
        return ProgramStatusKind::Question;
    }
    if (value == u"auth") {
        return ProgramStatusKind::Auth;
    }
    return ProgramStatusKind::None;
}

std::optional<int> progressFromValue(QStringView value)
{
    if (value.isEmpty() || !std::all_of(value.begin(), value.end(), [](QChar character) {
            return character >= QLatin1Char('0') && character <= QLatin1Char('9');
        })) {
        return std::nullopt;
    }
    bool ok = false;
    const int progress = value.toInt(&ok);
    return ok && progress <= MaximumProgress ? std::optional<int>(progress) : std::nullopt;
}

bool isControlCharacter(QChar character)
{
    const char16_t code = character.unicode();
    return code <= 0x1f || (code >= 0x7f && code <= 0x9f);
}

// Decodes base64 UTF-8 text. Returns nothing when the protocol requires the
// whole report to be discarded.
std::optional<QString> decodedText(QStringView encoded, qsizetype maximumEncodedLength, qsizetype maximumDecodedLength)
{
    if (encoded.size() > maximumEncodedLength) {
        return std::nullopt;
    }

    const auto decoded = QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok || decoded.decoded.size() > maximumDecodedLength) {
        return std::nullopt;
    }

    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = decoder(decoded.decoded);
    if (decoder.hasError() || std::any_of(text.begin(), text.end(), isControlCharacter)) {
        return std::nullopt;
    }
    return text;
}

bool isSameOrBeneath(const QString &id, const QString &ancestor)
{
    return id == ancestor || (id.startsWith(ancestor) && id.size() > ancestor.size() && id.at(ancestor.size()) == QLatin1Char('/'));
}
}

std::optional<ProgramStatusMessage> Konsole::parseProgramStatusMessage(QStringView payload)
{
    if (payload.trimmed().startsWith(QLatin1Char('?'))) {
        return ProgramStatusMessage{ProgramStatusMessage::Type::Query, {}};
    }
    if (payload.size() > MaximumPayloadLength) {
        return std::nullopt;
    }

    // Malformed pairs are skipped, unknown keys are ignored, and the last
    // value of a repeated key wins.
    QHash<QString, QString> values;
    for (const QStringView pair : payload.split(QLatin1Char(':'))) {
        const qsizetype separator = pair.indexOf(QLatin1Char('='));
        if (separator < 0) {
            continue;
        }
        const QStringView key = pair.first(separator).trimmed();
        const QStringView value = pair.sliced(separator + 1).trimmed();
        if (key.isEmpty() || !std::all_of(key.begin(), key.end(), isKeyCharacter) || !std::all_of(value.begin(), value.end(), isValueCharacter)) {
            continue;
        }
        if (key.size() > MaximumKeyLength) {
            return std::nullopt;
        }
        values.insert(key.toString(), value.toString());
    }

    const auto stateValue = values.constFind(QStringLiteral("state"));
    if (stateValue == values.cend()) {
        return std::nullopt;
    }

    ProgramStatusMessage message;
    const auto idValue = values.constFind(QStringLiteral("id"));
    if (idValue != values.cend()) {
        if (!isValidId(*idValue)) {
            return std::nullopt;
        }
        message.record.id = *idValue;
    }

    if (*stateValue == QLatin1String("clear")) {
        message.type = ProgramStatusMessage::Type::Clear;
        return message;
    }

    const auto state = stateFromValue(*stateValue);
    if (!state.has_value()) {
        return std::nullopt;
    }
    message.type = ProgramStatusMessage::Type::Report;
    message.record.state = *state;

    const auto appValue = values.constFind(QStringLiteral("app"));
    if (appValue != values.cend()) {
        if (!isValidName(*appValue, MaximumAppLength)) {
            return std::nullopt;
        }
        message.record.app = *appValue;
    }

    if (*state == ProgramState::Blocked) {
        message.record.kind = kindFromValue(values.value(QStringLiteral("kind")));
    }
    if (*state == ProgramState::Working || *state == ProgramState::Blocked) {
        message.record.progress = progressFromValue(values.value(QStringLiteral("progress")));
    }

    const auto titleValue = values.constFind(QStringLiteral("title"));
    if (titleValue != values.cend()) {
        const auto title = decodedText(*titleValue, MaximumEncodedTitleLength, MaximumDecodedTitleLength);
        if (!title.has_value()) {
            return std::nullopt;
        }
        message.record.title = *title;
    }

    const auto messageValue = values.constFind(QStringLiteral("msg"));
    if (messageValue != values.cend()) {
        const auto text = decodedText(*messageValue, MaximumEncodedMessageLength, MaximumDecodedMessageLength);
        if (!text.has_value()) {
            return std::nullopt;
        }
        message.record.message = *text;
    }

    return message;
}

QByteArray Konsole::programStatusQueryReply()
{
    return QByteArrayLiteral("\033]7501;?:states=idle,working,done,blocked,error:kinds=permission,question,auth\033\\");
}

bool ProgramStatusRecords::apply(const ProgramStatusMessage &message)
{
    switch (message.type) {
    case ProgramStatusMessage::Type::Query:
        return false;
    case ProgramStatusMessage::Type::Clear: {
        if (message.record.id.isEmpty()) {
            return clear();
        }
        const auto removed = _records.removeIf([&message](const ProgramStatusRecord &record) {
            return isSameOrBeneath(record.id, message.record.id);
        });
        return removed > 0;
    }
    case ProgramStatusMessage::Type::Report:
        break;
    }

    // Each report replaces its record completely and makes it the most
    // recently updated one.
    const auto existing = std::find_if(_records.begin(), _records.end(), [&message](const ProgramStatusRecord &record) {
        return record.id == message.record.id;
    });
    bool changed = true;
    if (existing != _records.end()) {
        changed = *existing != message.record;
        _records.erase(existing);
    }
    _records.append(message.record);
    while (_records.size() > MaximumRecordCount) {
        _records.removeFirst();
    }
    return changed;
}

bool ProgramStatusRecords::dropTransientRecords()
{
    const auto removed = _records.removeIf([](const ProgramStatusRecord &record) {
        return record.state == ProgramState::Working || record.state == ProgramState::Blocked || record.state == ProgramState::Idle;
    });
    return removed > 0;
}

bool ProgramStatusRecords::clear()
{
    if (_records.isEmpty()) {
        return false;
    }
    _records.clear();
    return true;
}

bool ProgramStatusRecords::isEmpty() const
{
    return _records.isEmpty();
}

std::optional<ProgramStatusRecord> ProgramStatusRecords::record(const QString &id) const
{
    const auto found = std::find_if(_records.cbegin(), _records.cend(), [&id](const ProgramStatusRecord &record) {
        return record.id == id;
    });
    return found != _records.cend() ? std::optional<ProgramStatusRecord>(*found) : std::nullopt;
}

QList<ProgramStatusRecord> ProgramStatusRecords::records() const
{
    return _records;
}

QString ProgramStatusRecords::app(const QString &id) const
{
    QString ancestor = id;
    while (true) {
        if (const auto found = record(ancestor); found.has_value() && !found->app.isEmpty()) {
            return found->app;
        }
        if (ancestor.isEmpty()) {
            return {};
        }
        const qsizetype separator = ancestor.lastIndexOf(QLatin1Char('/'));
        ancestor = separator < 0 ? QString() : ancestor.first(separator);
    }
}

QDebug Konsole::operator<<(QDebug debug, const ProgramStatusRecord &record)
{
    const QDebugStateSaver saver(debug);
    debug.nospace() << "{id=" << record.id << " state=" << valueFromState(record.state);
    if (record.kind != ProgramStatusKind::None) {
        debug << " kind=" << valueFromKind(record.kind);
    }
    if (record.progress.has_value()) {
        debug << " progress=" << *record.progress;
    }
    if (!record.app.isEmpty()) {
        debug << " app=" << record.app;
    }
    if (!record.title.isEmpty()) {
        debug << " title=" << record.title;
    }
    if (!record.message.isEmpty()) {
        debug << " msg=" << record.message;
    }
    debug << '}';
    return debug;
}
