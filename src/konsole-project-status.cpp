/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusReply>
#include <QTextStream>

namespace
{
int finishForHook(bool hookMode, int code)
{
    if (hookMode) {
        QTextStream(stdout) << "{}\n";
        return 0;
    }

    return code;
}

void printError(bool hookMode, const QString &message)
{
    if (!hookMode) {
        QTextStream(stderr) << message << '\n';
    }
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("konsole-project-status"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Set the project status for the current Konsole terminal session."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption hookModeOption(QStringLiteral("hook-output"),
                                            QStringLiteral("Print an empty JSON object for agent hook protocols after updating the status."));
    parser.addOption(hookModeOption);
    parser.addPositionalArgument(QStringLiteral("status"), QStringLiteral("Project status: running, idle, needsInput, unknown, or none."));
    parser.process(app);

    const bool hookMode = parser.isSet(hookModeOption);
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        printError(hookMode, QStringLiteral("Missing status."));
        if (hookMode) {
            return finishForHook(hookMode, 2);
        }
        parser.showHelp(2);
    }

    const QString service = qEnvironmentVariable("KONSOLE_DBUS_SERVICE");
    const QString objectPath = qEnvironmentVariable("KONSOLE_DBUS_SESSION");
    if (service.isEmpty() || objectPath.isEmpty()) {
        printError(hookMode, QStringLiteral("KONSOLE_DBUS_SERVICE and KONSOLE_DBUS_SESSION must be set."));
        return finishForHook(hookMode, 2);
    }

    QDBusInterface session(service, objectPath, QStringLiteral("org.kde.konsole.Session"), QDBusConnection::sessionBus());
    if (!session.isValid()) {
        printError(hookMode, session.lastError().message());
        return finishForHook(hookMode, 3);
    }

    const QDBusReply<void> reply = session.call(QStringLiteral("setProjectStatus"), args.first());
    if (!reply.isValid()) {
        printError(hookMode, reply.error().message());
        return finishForHook(hookMode, 3);
    }

    return finishForHook(hookMode, 0);
}
