/*
    This source file is part of Konsole, a terminal emulator.

    SPDX-FileCopyrightText: 2007-2008 Robert Knight <robertknight@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KEYBOARDTRANSLATOR_MANAGER_H
#define KEYBOARDTRANSLATOR_MANAGER_H

// Qt
#include <QHash>
#include <QStringList>

// Konsole
#include "KeyboardTranslator.h"

class QIODevice;

namespace Konsole
{
/**
 * Manages the keyboard translations available for use by terminal sessions,
 * see KeyboardTranslator.
 */
class KeyboardTranslatorManager
{
public:
    /**
     * Constructs a new KeyboardTranslatorManager and loads the list of
     * available keyboard translations.
     *
     * The keyboard translations themselves are not loaded until they are
     * first requested via a call to findTranslator()
     */
    KeyboardTranslatorManager();
    ~KeyboardTranslatorManager();

    KeyboardTranslatorManager(const KeyboardTranslatorManager &) = delete;
    KeyboardTranslatorManager &operator=(const KeyboardTranslatorManager &) = delete;

    /**
     * Adds a new translator.  If a translator with the same name
     * already exists, it will be replaced by the new translator.
     *
     * TODO: More documentation.
     */
    void addTranslator(KeyboardTranslator *translator);

    /**
     * Deletes a translator.  Returns true on successful deletion or false otherwise.
     *
     * TODO: More documentation
     */
    bool deleteTranslator(const QString &name);

    /**
     * Checks whether a translator belongs to Kmux and can be deleted from
     * its writable data directory. Legacy Konsole translators are read-only.
     */
    bool isTranslatorDeletable(const QString &name) const;

    /**
     * Checks whether a writable Kmux translator overrides another translator
     * with the same name. Resetting deletes the Kmux-owned translator, then
     * reloads the fallback.
     */
    bool isTranslatorResettable(const QString &name) const;

    /** Returns the default translator for Konsole. */
    const KeyboardTranslator *defaultTranslator();

    /**
     * Returns the keyboard translator with the given name or 0 if no translator
     * with that name exists.
     *
     * The first time that a translator with a particular name is requested,
     * the on-disk .keytab file is loaded and parsed.
     */
    const KeyboardTranslator *findTranslator(const QString &name);
    /**
     * Returns a list of the names of available keyboard translators.
     *
     * The first time this is called, a search for available
     * translators is started.
     */
    const QStringList allTranslators();

    /** Returns the global KeyboardTranslatorManager instance. */
    static KeyboardTranslatorManager *instance();

    /** Returns the translator path */
    const QString findTranslatorPath(const QString &name) const;

private:
    void findTranslators(); // locate all available translators

    // loads the translator with the given name
    KeyboardTranslator *loadTranslator(const QString &name);
    KeyboardTranslator *loadTranslator(QIODevice *source, const QString &name);

    bool saveTranslator(const KeyboardTranslator *translator);

    bool _haveLoadedAll;

    QHash<QString, KeyboardTranslator *> _translators;
};
}

#endif // KEYBOARDTRANSLATOR_MANAGER_H
