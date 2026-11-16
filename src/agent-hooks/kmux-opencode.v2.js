@KMUX_PLUGIN_MARKER@
import { Plugin } from "@opencode/plugin";
import { spawn } from "node:child_process";

const KmuxAgentName = "opencode";

const KmuxFlag = Object.freeze({
  HOOK_OUTPUT: "--hook-output",
  AGENT: "--agent",
  EVENT: "--event",
  AGENT_PID: "--agent-pid",
});

const KmuxEnv = Object.freeze({
  HELPER_EXECUTABLE: "KMUX_PROJECT_STATUS_EXECUTABLE",
  AGENT_PID: "KMUX_OPENCODE_PID",
});

const KmuxHookEvent = Object.freeze({
  STOP: "Stop",
  STOP_FAILURE: "StopFailure",
  PERMISSION_REQUEST: "PermissionRequest",
  PERMISSION_REPLIED: "PermissionReplied",
  QUESTION_ASKED: "QuestionAsked",
  QUESTION_REPLIED: "QuestionReplied",
  USER_PROMPT_SUBMIT: "UserPromptSubmit",
  PRE_TOOL_USE: "PreToolUse",
  POST_TOOL_USE: "PostToolUse",
});

const KmuxStatus = Object.freeze({
  IDLE: "idle",
  RUNNING: "running",
  NEEDS_INPUT: "needsInput",
});

const KmuxPayloadKey = Object.freeze({
  SESSION_ID: "session_id",
});

const OpenCodeEvent = Object.freeze({
  SESSION_IDLE: "session.idle",
  SESSION_ERROR: "session.error",
  PERMISSION_ASKED: "permission.asked",
  PERMISSION_UPDATED: "permission.updated",
  PERMISSION_REPLIED: "permission.replied",
  QUESTION_ASKED: "question.asked",
  QUESTION_REPLIED: "question.replied",
  QUESTION_REJECTED: "question.rejected",
});

const OpenCodeHookName = Object.freeze({
  PROMPT: "prompt",
  TOOL_EXECUTE_BEFORE: "execute.before",
  TOOL_EXECUTE_AFTER: "execute.after",
});

const OpenCodeField = Object.freeze({
  PROPERTIES: "properties",
  SESSION_ID: "sessionID",
});

const HELPER = process.env[KmuxEnv.HELPER_EXECUTABLE] || @KMUX_HELPER_PATH@;

function report(event, status, sessionId) {
  const args = [KmuxFlag.HOOK_OUTPUT, KmuxFlag.AGENT, KmuxAgentName, KmuxFlag.EVENT, event];
  const agentPid = process.env[KmuxEnv.AGENT_PID];
  if (agentPid) {
    args.push(KmuxFlag.AGENT_PID, String(agentPid));
  }
  args.push(status);
  try {
    const child = spawn(HELPER, args, { stdio: ["pipe", "ignore", "ignore"], env: process.env });
    // Asynchronous failures (helper missing, or helper exits before reading stdin)
    // are reported as events, not thrown, so they must be handled here.
    child.on("error", () => {});
    child.stdin.on("error", () => {});
    child.stdin.end(JSON.stringify({ [KmuxPayloadKey.SESSION_ID]: sessionId || "" }));
  } catch {}
}

export default Plugin.define({
  id: "kmux.opencode",
  async setup(ctx) {
    const controller = new AbortController();

    void (async () => {
      for await (const event of ctx.event.subscribe({ signal: controller.signal })) {
        const properties = event[OpenCodeField.PROPERTIES];
        const sessionId = properties ? properties[OpenCodeField.SESSION_ID] : undefined;
        switch (event.type) {
          case OpenCodeEvent.SESSION_IDLE:
            report(KmuxHookEvent.STOP, KmuxStatus.IDLE, sessionId);
            break;
          case OpenCodeEvent.SESSION_ERROR:
            report(KmuxHookEvent.STOP_FAILURE, KmuxStatus.IDLE, sessionId);
            break;
          case OpenCodeEvent.PERMISSION_ASKED:
          case OpenCodeEvent.PERMISSION_UPDATED:
            report(KmuxHookEvent.PERMISSION_REQUEST, KmuxStatus.NEEDS_INPUT, sessionId);
            break;
          case OpenCodeEvent.PERMISSION_REPLIED:
            report(KmuxHookEvent.PERMISSION_REPLIED, KmuxStatus.RUNNING, sessionId);
            break;
          case OpenCodeEvent.QUESTION_ASKED:
            report(KmuxHookEvent.QUESTION_ASKED, KmuxStatus.NEEDS_INPUT, sessionId);
            break;
          case OpenCodeEvent.QUESTION_REPLIED:
          case OpenCodeEvent.QUESTION_REJECTED:
            report(KmuxHookEvent.QUESTION_REPLIED, KmuxStatus.RUNNING, sessionId);
            break;
        }
      }
    })().catch(() => {});

    await ctx.session.hook(OpenCodeHookName.PROMPT, (event) => {
      report(KmuxHookEvent.USER_PROMPT_SUBMIT, KmuxStatus.RUNNING, event[OpenCodeField.SESSION_ID]);
    });

    await ctx.tool.hook(OpenCodeHookName.TOOL_EXECUTE_BEFORE, (event) => {
      report(KmuxHookEvent.PRE_TOOL_USE, KmuxStatus.RUNNING, event[OpenCodeField.SESSION_ID]);
    });

    await ctx.tool.hook(OpenCodeHookName.TOOL_EXECUTE_AFTER, (event) => {
      report(KmuxHookEvent.POST_TOOL_USE, KmuxStatus.RUNNING, event[OpenCodeField.SESSION_ID]);
    });

    return () => controller.abort();
  },
});
