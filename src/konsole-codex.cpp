/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef KMUX_AGENT_NAME
#define KMUX_AGENT_NAME "codex"
#endif

#ifndef KMUX_AGENT_PID_ENV
#define KMUX_AGENT_PID_ENV "KMUX_CODEX_PID"
#endif

#ifndef KMUX_HOOKS_DISABLED_ENV
#define KMUX_HOOKS_DISABLED_ENV "KMUX_CODEX_HOOKS_DISABLED"
#endif

#ifndef KMUX_HOOKS_DISABLED_COMPAT_ENV
#define KMUX_HOOKS_DISABLED_COMPAT_ENV "KONSOLE_CODEX_HOOKS_DISABLED"
#endif

namespace
{
bool environmentDisablesHooks(const char *name)
{
    if (name[0] == '\0') {
        return false;
    }

    const char *value = std::getenv(name);
    return value != nullptr && std::strcmp(value, "1") == 0;
}

std::string agentHooksExecutable(const char *launcherPath)
{
    const std::string path = launcherPath;
    const auto separator = path.find_last_of('/');
    if (separator == std::string::npos) {
        return std::string("kmux-agent-hooks");
    }
    return path.substr(0, separator + 1) + "kmux-agent-hooks";
}

bool installTrustedHooks(const char *launcherPath)
{
    const std::string executable = agentHooksExecutable(launcherPath);
    const pid_t installerPid = fork();
    if (installerPid < 0) {
        return false;
    }

    if (installerPid == 0) {
        execlp(executable.c_str(), executable.c_str(), "--quiet", "install", KMUX_AGENT_NAME, nullptr);
        _exit(127);
    }

    int status = 0;
    while (waitpid(installerPid, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::vector<std::string> executableCandidates(const char *executable)
{
    if (std::strchr(executable, '/') != nullptr) {
        return {executable};
    }

    const char *pathEnvironment = std::getenv("PATH");
    const std::string searchPath = pathEnvironment != nullptr ? pathEnvironment : "/bin:/usr/bin";
    std::vector<std::string> candidates;
    std::string::size_type start = 0;
    do {
        const auto separator = searchPath.find(':', start);
        const std::string directory = searchPath.substr(start, separator - start);
        candidates.emplace_back((directory.empty() ? std::string(".") : directory) + '/' + executable);
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    } while (start <= searchPath.size());
    return candidates;
}

bool executableIdentity(const char *executable, struct stat &identity)
{
    for (const std::string &candidate : executableCandidates(executable)) {
        if (access(candidate.c_str(), X_OK) == 0 && stat(candidate.c_str(), &identity) == 0) {
            return true;
        }
    }
    return false;
}

void execShellScript(const std::string &script, const std::vector<char *> &args)
{
    std::vector<char *> shellArgs;
    shellArgs.reserve(args.size() + 1);
    shellArgs.push_back(const_cast<char *>(script.c_str()));
    shellArgs.insert(shellArgs.end(), args.begin() + 1, args.end());
    execv("/bin/sh", shellArgs.data());
}

void execAgent(const char *launcherPath, const std::vector<char *> &args)
{
    struct stat launcherIdentity;
    const bool hasLauncherIdentity = executableIdentity(launcherPath, launcherIdentity);
    bool skippedLauncher = false;
    bool permissionDenied = false;

    for (const std::string &candidate : executableCandidates(KMUX_AGENT_NAME)) {
        struct stat candidateIdentity;
        if (hasLauncherIdentity && stat(candidate.c_str(), &candidateIdentity) == 0 && candidateIdentity.st_dev == launcherIdentity.st_dev
            && candidateIdentity.st_ino == launcherIdentity.st_ino) {
            skippedLauncher = true;
            continue;
        }

        execv(candidate.c_str(), args.data());
        if (errno == ENOEXEC) {
            execShellScript(candidate, args);
            return;
        }
        if (errno == EACCES) {
            permissionDenied = true;
        } else if (errno != ENOENT && errno != ENOTDIR) {
            return;
        }
    }

    errno = permissionDenied ? EACCES : skippedLauncher ? ELOOP : ENOENT;
}
}

int main(int argc, char **argv)
{
    std::vector<std::string> args;
    args.emplace_back(KMUX_AGENT_NAME);

    if (!environmentDisablesHooks(KMUX_HOOKS_DISABLED_ENV) && !environmentDisablesHooks(KMUX_HOOKS_DISABLED_COMPAT_ENV)) {
        if (!installTrustedHooks(argv[0])) {
            std::cerr << "kmux-" KMUX_AGENT_NAME ": failed to install Kmux hooks; continuing without updating the agent configuration\n";
        }
    }

    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    const std::string agentPid = std::to_string(getpid());
    setenv(KMUX_AGENT_PID_ENV, agentPid.c_str(), 1);

    std::vector<char *> execArgs;
    execArgs.reserve(args.size() + 1);
    for (std::string &arg : args) {
        execArgs.push_back(arg.data());
    }
    execArgs.push_back(nullptr);

    execAgent(argv[0], execArgs);
    const int execError = errno;
    std::cerr << "kmux-" KMUX_AGENT_NAME ": failed to exec " KMUX_AGENT_NAME ": " << std::strerror(execError) << '\n';
    return 127;
}
