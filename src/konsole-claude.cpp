/*
    SPDX-FileCopyrightText: 2026 Kmux contributors

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

// Inside Kmux terminals, the agent command runs this launcher first. It installs
// or repairs the Kmux hooks that report what the OSC 7501 protocol cannot
// express (for opencode it installs the Kmux plugin), then runs the real agent
// from the rest of PATH. One source builds the "claude" and "opencode" shims.

#ifndef KMUX_AGENT_NAME
#define KMUX_AGENT_NAME "claude"
#endif

#ifndef KMUX_HOOKS_DISABLED_ENV
#define KMUX_HOOKS_DISABLED_ENV "KMUX_CLAUDE_HOOKS_DISABLED"
#endif

// Some agents have no generated hook script that could carry the process id or
// pass a status explicitly; for them the launcher exports the pid so its plugin
// can report with it. An empty name skips the export.
#ifndef KMUX_AGENT_PID_ENV
#define KMUX_AGENT_PID_ENV ""
#endif

namespace
{
bool hooksDisabled()
{
    const char *value = std::getenv(KMUX_HOOKS_DISABLED_ENV);
    return value != nullptr && std::strcmp(value, "1") == 0;
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

std::string siblingExecutable(const char *launcherPath, const char *name)
{
    for (const std::string &candidate : executableCandidates(launcherPath)) {
        if (access(candidate.c_str(), X_OK) != 0) {
            continue;
        }

        const auto separator = candidate.find_last_of('/');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string directory = candidate.substr(0, separator + 1);
        const std::string sibling = directory + name;
        if (access(sibling.c_str(), X_OK) == 0) {
            return sibling;
        }

        // Keep compatibility with an earlier layout where transparent shims
        // lived one directory below the public Kmux helpers.
        const std::string parentSibling = directory + "../" + name;
        if (access(parentSibling.c_str(), X_OK) == 0) {
            return parentSibling;
        }
    }

    return std::string();
}

std::string agentHooksExecutable(const char *launcherPath)
{
    const std::string executable = siblingExecutable(launcherPath, "kmux-agent-hooks");
    return executable.empty() ? std::string("kmux-agent-hooks") : executable;
}

bool installHooks(const char *launcherPath)
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

bool executableIdentity(const char *executable, struct stat &identity)
{
    for (const std::string &candidate : executableCandidates(executable)) {
        if (access(candidate.c_str(), X_OK) == 0 && stat(candidate.c_str(), &identity) == 0) {
            return true;
        }
    }
    return false;
}

// Runs an executable without a "#!" line as execvp() does: the shell reads the
// script, and the agent arguments follow the script path.
void execShellScript(const std::string &script, const std::vector<char *> &args)
{
    std::vector<char *> shellArgs;
    shellArgs.reserve(args.size() + 1);
    shellArgs.push_back(const_cast<char *>("/bin/sh"));
    shellArgs.push_back(const_cast<char *>(script.c_str()));
    // args starts with the agent name and ends with the null terminator.
    shellArgs.insert(shellArgs.end(), args.begin() + 1, args.end());
    execv("/bin/sh", shellArgs.data());
}

void execAgent(const char *launcherPath, const std::vector<char *> &args)
{
    struct stat launcherIdentity;
    const bool hasLauncherIdentity = executableIdentity(launcherPath, launcherIdentity);
    bool permissionDenied = false;

    for (const std::string &candidate : executableCandidates(KMUX_AGENT_NAME)) {
        struct stat candidateIdentity;
        if (hasLauncherIdentity && stat(candidate.c_str(), &candidateIdentity) == 0 && candidateIdentity.st_dev == launcherIdentity.st_dev
            && candidateIdentity.st_ino == launcherIdentity.st_ino) {
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

    // Finding only this launcher means that the agent is not installed.
    errno = permissionDenied ? EACCES : ENOENT;
}
}

int main(int argc, char **argv)
{
    std::vector<std::string> args;
    args.emplace_back(KMUX_AGENT_NAME);

    if (!hooksDisabled() && !installHooks(argv[0])) {
        std::cerr << "kmux-" KMUX_AGENT_NAME ": failed to install Kmux hooks; continuing without updating the agent configuration\n";
    }

    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    // Plugins that run in the agent process, such as the opencode one, have no
    // generated hook script to carry these values, so the launcher exports them.
    if (KMUX_AGENT_PID_ENV[0] != '\0') {
        setenv(KMUX_AGENT_PID_ENV, std::to_string(getpid()).c_str(), 1);
    }

    const std::string projectStatusExecutable = siblingExecutable(argv[0], "kmux-project-status");
    if (!projectStatusExecutable.empty()) {
        setenv("KMUX_PROJECT_STATUS_EXECUTABLE", projectStatusExecutable.c_str(), 1);
    }
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
