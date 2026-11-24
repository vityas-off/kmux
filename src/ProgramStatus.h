/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROGRAMSTATUS_H
#define PROGRAMSTATUS_H

#include "konsoleprivate_export.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

class QDebug;

namespace Konsole
{
/**
 * Program Status Protocol (OSC 7501): programs report their own state to the
 * terminal, and the terminal keeps one record per reported id.
 * See https://www.superlogical.com/rex/docs/build/program-status
 */
enum class ProgramState {
    /** At rest, waiting for the next instruction. */
    Idle,
    /** Running. */
    Working,
    /** Finished, with a result that the user has not seen yet. */
    Done,
    /** Unable to continue until the user acts. */
    Blocked,
    /** Failed and stopped. */
    Error,
};

/** What a blocked program waits for. */
enum class ProgramStatusKind {
    None,
    Permission,
    Question,
    Auth,
};

struct ProgramStatusRecord {
    /** A "/" separated path, or empty for the root record. */
    QString id;
    ProgramState state = ProgramState::Idle;
    ProgramStatusKind kind = ProgramStatusKind::None;
    std::optional<int> progress;
    /** The app key of this report; children inherit it, see ProgramStatusRecords::app(). */
    QString app;
    QString title;
    QString message;

    bool operator==(const ProgramStatusRecord &other) const = default;
};

/** Writes the record in the protocol's own key=value terms, for status logs. */
KONSOLEPRIVATE_EXPORT QDebug operator<<(QDebug debug, const ProgramStatusRecord &record);

struct ProgramStatusMessage {
    enum class Type {
        /** A program asks whether the terminal supports the protocol. */
        Query,
        /** A report that replaces the record with the same id. */
        Report,
        /** Removes the record with this id and the records beneath it, or all records without an id. */
        Clear,
    };

    Type type = Type::Query;
    /** For Clear, only the id is set. */
    ProgramStatusRecord record;
};

/**
 * Parses the payload that follows "OSC 7501 ;". Returns nothing when the
 * protocol requires the terminal to ignore the whole sequence.
 */
KONSOLEPRIVATE_EXPORT std::optional<ProgramStatusMessage> parseProgramStatusMessage(QStringView payload);

/** The reply to a support query, listing the accepted states and kinds. */
KONSOLEPRIVATE_EXPORT QByteArray programStatusQueryReply();

class KONSOLEPRIVATE_EXPORT ProgramStatusRecords
{
public:
    /** The protocol requires terminals to keep at least this many records. */
    static constexpr qsizetype MaximumRecordCount = 64;

    /** Applies a report or a removal. Returns whether the records changed. */
    bool apply(const ProgramStatusMessage &message);

    /**
     * Drops the records that describe running work after the program exits
     * or a new shell prompt starts. Done and error results remain until they
     * are replaced or cleared. Returns whether the records changed.
     */
    bool dropTransientRecords();

    /** Removes every record, as a full terminal reset does. Returns whether the records changed. */
    bool clear();

    bool isEmpty() const;
    /** Returns the record with this id, or nothing. The root record has an empty id. */
    std::optional<ProgramStatusRecord> record(const QString &id) const;
    /** Returns the records, least recently updated first. */
    QList<ProgramStatusRecord> records() const;
    /** Returns the app of a record, inherited from its nearest ancestor that reports one. */
    QString app(const QString &id) const;

private:
    QList<ProgramStatusRecord> _records;
};
}

#endif
