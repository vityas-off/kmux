/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
std::string statusHookCommand(const char *status)
{
    return std::string("command -v kmux-project-status >/dev/null 2>&1 && kmux-project-status --hook-output ") + status + " || printf '{}\\n'";
}

std::string codexHookConfig(const char *event, const char *status, int timeout)
{
    return std::string("hooks.") + event + "=[{hooks=[{type=\"command\",command='''" + statusHookCommand(status) + "''',timeout=" + std::to_string(timeout)
        + "}]}]";
}

void appendHook(std::vector<std::string> &args, const char *event, const char *status, int timeout)
{
    args.emplace_back("-c");
    args.push_back(codexHookConfig(event, status, timeout));
}
}

int main(int argc, char **argv)
{
    std::vector<std::string> args;
    args.emplace_back("codex");

    const char *disabled = std::getenv("KONSOLE_CODEX_HOOKS_DISABLED");
    if (disabled == nullptr || std::strcmp(disabled, "1") != 0) {
        args.emplace_back("--enable");
        args.emplace_back("hooks");
        args.emplace_back("--dangerously-bypass-hook-trust");

        appendHook(args, "SessionStart", "running", 10000);
        appendHook(args, "UserPromptSubmit", "running", 10000);
        appendHook(args, "PreToolUse", "running", 10000);
        appendHook(args, "PostToolUse", "running", 10000);
        appendHook(args, "PermissionRequest", "needsInput", 120000);
        appendHook(args, "Stop", "idle", 10000);
    }

    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    std::vector<char *> execArgs;
    execArgs.reserve(args.size() + 1);
    for (std::string &arg : args) {
        execArgs.push_back(arg.data());
    }
    execArgs.push_back(nullptr);

    execvp("codex", execArgs.data());
    std::cerr << "kmux-codex: failed to exec codex: " << std::strerror(errno) << '\n';
    return 127;
}
