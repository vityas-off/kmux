/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TERMINALTABSTATUS_H
#define TERMINALTABSTATUS_H

namespace Konsole
{

enum class TerminalTabStatus {
    None,
    ForegroundProcess,
    AgentIdle,
    AgentRunning,
    NeedsInput,
};

}

#endif
