/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
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
#define KMUX_HOOKS_DISABLED_ENV "KONSOLE_CODEX_HOOKS_DISABLED"
#endif

namespace
{
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
}

int main(int argc, char **argv)
{
    std::vector<std::string> args;
    args.emplace_back(KMUX_AGENT_NAME);

    const char *disabled = std::getenv(KMUX_HOOKS_DISABLED_ENV);
    if (disabled == nullptr || std::strcmp(disabled, "1") != 0) {
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

    execvp(KMUX_AGENT_NAME, execArgs.data());
    std::cerr << "kmux-" KMUX_AGENT_NAME ": failed to exec " KMUX_AGENT_NAME ": " << std::strerror(errno) << '\n';
    return 127;
}
