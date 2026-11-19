@KMUX_PLUGIN_MARKER@
import { spawn } from "node:child_process";

const KmuxAgentName = "opencode";

const KmuxFlag = Object.freeze({
  HOOK_OUTPUT: "--hook-output",
  AGENT: "--agent",
  EVENT: "--event",
  AGENT_PID: "--agent-pid",
  SESSION_TITLE: "--session-title",
  SESSION_MODEL: "--session-model",
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
  SESSION_UPDATED: "session.updated",
  MESSAGE_UPDATED: "message.updated",
  SESSION_IDLE: "session.idle",
  SESSION_ERROR: "session.error",
  PERMISSION_ASKED: "permission.asked",
  PERMISSION_UPDATED: "permission.updated",
  PERMISSION_REPLIED: "permission.replied",
  QUESTION_ASKED: "question.asked",
  QUESTION_REPLIED: "question.replied",
  QUESTION_REJECTED: "question.rejected",
});

const OpenCodeHook = Object.freeze({
  CHAT_MESSAGE: "chat.message",
  TOOL_EXECUTE_BEFORE: "tool.execute.before",
  TOOL_EXECUTE_AFTER: "tool.execute.after",
});

const OpenCodeField = Object.freeze({
  PROPERTIES: "properties",
  INFO: "info",
  TITLE: "title",
  ROLE: "role",
  MODEL: "model",
  MODEL_ID: "modelID",
  ID: "id",
  NAME: "name",
  PROVIDER_ID: "providerID",
  SESSION_ID: "sessionID",
});

const OpenCodeRole = Object.freeze({
  USER: "user",
});

const HELPER = process.env[KmuxEnv.HELPER_EXECUTABLE] ?? @KMUX_HELPER_PATH@;

const runHelper = (args, sessionId = "") => {
  try {
    const child = spawn(HELPER, args, { stdio: ["pipe", "ignore", "ignore"], env: process.env });
    // Spawn failures and early helper exits surface as events, not exceptions.
    child.on("error", () => {});
    child.stdin.on("error", () => {});
    child.stdin.end(JSON.stringify({ [KmuxPayloadKey.SESSION_ID]: sessionId }));
  } catch {}
};

const report = (event, status, sessionId) => {
  const args = [KmuxFlag.HOOK_OUTPUT, KmuxFlag.AGENT, KmuxAgentName, KmuxFlag.EVENT, event];
  const agentPid = process.env[KmuxEnv.AGENT_PID];
  if (agentPid) {
    args.push(KmuxFlag.AGENT_PID, String(agentPid));
  }
  args.push(status);
  runHelper(args, sessionId);
};

const reportTitle = (title) => {
  runHelper([KmuxFlag.HOOK_OUTPUT, KmuxFlag.AGENT, KmuxAgentName, KmuxFlag.SESSION_TITLE, title]);
};

let reportedModel = "";

const reportModel = (model) => {
  if (!model || model === reportedModel) {
    return;
  }
  reportedModel = model;
  runHelper([KmuxFlag.HOOK_OUTPUT, KmuxFlag.AGENT, KmuxAgentName, KmuxFlag.SESSION_MODEL, model]);
};

// Events carry only provider and model IDs, so resolve the display name from the
// provider catalog once and cache it for the lifetime of the plugin.
let modelCatalogPromise;

const loadModelCatalog = (client) => {
  modelCatalogPromise ??= (async () => {
    const names = new Map();
    try {
      const providers = (await client?.config?.providers?.())?.data?.providers ?? [];
      for (const { id: providerID, models = {} } of providers) {
        for (const [modelID, model] of Object.entries(models)) {
          if (model?.name) {
            names.set(`${providerID}/${modelID}`, model.name);
          }
        }
      }
    } catch {}
    return names;
  })();
  return modelCatalogPromise;
};

const resolveModelName = async (client, model) => {
  const providerID = model[OpenCodeField.PROVIDER_ID];
  const modelID = model[OpenCodeField.MODEL_ID] ?? model[OpenCodeField.ID];
  if (model[OpenCodeField.NAME]) {
    return model[OpenCodeField.NAME];
  }
  if (!modelID) {
    return undefined;
  }
  const names = await loadModelCatalog(client);
  return (providerID && names.get(`${providerID}/${modelID}`)) || modelID;
};

export default async ({ client } = {}) => ({
  event: async ({ event }) => {
    const properties = event[OpenCodeField.PROPERTIES];
    const sessionId = properties?.[OpenCodeField.SESSION_ID];
    switch (event.type) {
      case OpenCodeEvent.MESSAGE_UPDATED: {
        const info = properties?.[OpenCodeField.INFO];
        if (info?.[OpenCodeField.ROLE] !== OpenCodeRole.USER) {
          break;
        }
        const model = info[OpenCodeField.MODEL] ?? (info[OpenCodeField.MODEL_ID] ? { [OpenCodeField.MODEL_ID]: info[OpenCodeField.MODEL_ID] } : undefined);
        if (model) {
          resolveModelName(client, model).then((name) => reportModel(name)).catch(() => {});
        }
        break;
      }
      case OpenCodeEvent.SESSION_UPDATED: {
        const title = properties?.[OpenCodeField.INFO]?.[OpenCodeField.TITLE];
        if (title) {
          reportTitle(title);
        }
        break;
      }
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
  },
  [OpenCodeHook.CHAT_MESSAGE]: async ({ [OpenCodeField.SESSION_ID]: sessionId }) => {
    report(KmuxHookEvent.USER_PROMPT_SUBMIT, KmuxStatus.RUNNING, sessionId);
  },
  [OpenCodeHook.TOOL_EXECUTE_BEFORE]: async ({ [OpenCodeField.SESSION_ID]: sessionId }) => {
    report(KmuxHookEvent.PRE_TOOL_USE, KmuxStatus.RUNNING, sessionId);
  },
  [OpenCodeHook.TOOL_EXECUTE_AFTER]: async ({ [OpenCodeField.SESSION_ID]: sessionId }) => {
    report(KmuxHookEvent.POST_TOOL_USE, KmuxStatus.RUNNING, sessionId);
  },
});
