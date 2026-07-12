/*
    SPDX-FileCopyrightText: 2007-2008 Robert Knight <robertknight@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef APPLICATION_H
#define APPLICATION_H

// Qt
#include <QCommandLineParser>

// Konsole
#include "konsoleapp_export.h"
#include "pluginsystem/PluginManager.h"
#include "profile/Profile.h"
#include "terminalDisplay/TerminalDisplay.h"
#include "widgets/ViewSplitter.h"

namespace Konsole
{
class MainWindow;
class Session;
class Profile;

/**
 * The Konsole Application.
 *
 * The application consists of one or more main windows and a set of
 * factories to create new sessions and views.
 *
 * To create a new main window with a default terminal session, call
 * the newInstance(). Empty main windows can be created using newMainWindow().
 *
 * The factory used to create new terminal sessions can be retrieved using
 * the sessionManager() accessor.
 */
class KONSOLEAPP_EXPORT Application : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.kmux_project.kmux.Application")

public:
    /** Constructs a new Konsole application. */
    explicit Application(QSharedPointer<QCommandLineParser> parser, const QStringList &customCommand);

    static void populateCommandLineParser(QCommandLineParser *parser);
    static QStringList getCustomCommand(QStringList &args);
    static bool processHelpArgs(const QCommandLineParser &parser);

    Q_SCRIPTABLE int requestActivation(const QStringList &args, const QString &workingDir, const QStringList &environment);

    ~Application() override;

    /** Creates a new main window and opens a default terminal session */
    int newInstance();

    /**
     * Creates a new, empty main window and connects its terminal detach signal.
     */
    MainWindow *newMainWindow();

private Q_SLOTS:
    void detachTerminals(MainWindow *currentWindow, ViewSplitter *splitter, const QHash<TerminalDisplay *, Session *> &sessionsMap);

    void toggleBackgroundInstance();

public Q_SLOTS:
    void slotActivateRequested(QStringList args, const QString &workingDir);

private:
    Q_DISABLE_COPY(Application)

    static void listAvailableProfiles();
    static void listProfilePropertyInfo();
    void startBackgroundMode(MainWindow *window);
    MainWindow *processWindowArgs(bool &createdNewMainWindow);
    QExplicitlySharedDataPointer<Profile> processProfileSelectArgs();
    QExplicitlySharedDataPointer<Profile> processProfileChangeArgs(QExplicitlySharedDataPointer<Profile> baseProfile);
    bool processTabsFromFileArgs(MainWindow *window);
    void createTabFromArgs(MainWindow *window, const QHash<QString, QString> &);
    bool shouldRestoreLastWorkspaceState(bool createdNewMainWindow) const;
    bool hasExplicitSessionRequest() const;
    QString resolveActivationPath(const QString &path) const;
    QString initialWorkingDirectory(const QString &requestedDirectory = QString()) const;
    void applyInitialWorkingDirectory(Session *session, const Profile::Ptr &profile, const QString &requestedDirectory, bool requestedExplicitly) const;
    bool handleActivationRequest(QStringList args, const QString &workingDir);

    MainWindow *_backgroundInstance;
    QSharedPointer<QCommandLineParser> m_parser;
    QStringList m_customCommand;
    QString m_activationWorkingDirectory;
    PluginManager m_pluginManager;
};
}
#endif // APPLICATION_H
