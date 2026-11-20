@KMUX_PLUGIN_MARKER@
import { spawn } from "node:child_process";

const KmuxFlag = Object.freeze({
  AGENT_PID: "--agent-pid",
  SESSION_TITLE: "--session-title",
  SESSION_MODEL: "--session-model",
});

const KmuxEnv = Object.freeze({
  HELPER_EXECUTABLE: "KMUX_PROJECT_STATUS_EXECUTABLE",
  AGENT_PID: "KMUX_OPENCODE_PID",
});

const KmuxStatus = Object.freeze({
  IDLE: "idle",
  RUNNING: "running",
  NEEDS_INPUT: "needsInput",
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
});

const OpenCodeRole = Object.freeze({
  USER: "user",
});

const HELPER = process.env[KmuxEnv.HELPER_EXECUTABLE] || @KMUX_HELPER_PATH@;

const runHelper = (args) => {
  try {
    const child = spawn(HELPER, args, { stdio: ["ignore", "ignore", "ignore"], env: process.env });
    child.on("error", () => {});
  } catch {}
};

// opencode does not speak OSC 7501, so the plugin reports plain statuses for
// this terminal. The helper tags them with this process id and Kmux clears
// the status when the process exits.
const report = (status) => {
  const args = [];
  const agentPid = process.env[KmuxEnv.AGENT_PID];
  if (agentPid) {
    args.push(KmuxFlag.AGENT_PID, String(agentPid));
  }
  args.push(status);
  runHelper(args);
};

let reportedTitle = "";

const reportTitle = (title) => {
  if (!title || title === reportedTitle) {
    return;
  }
  reportedTitle = title;
  runHelper([KmuxFlag.SESSION_TITLE, title]);
};

let reportedModel = "";

const reportModel = (model) => {
  if (!model || model === reportedModel) {
    return;
  }
  reportedModel = model;
  runHelper([KmuxFlag.SESSION_MODEL, model]);
};

// Events carry only provider and model IDs, so resolve the display name from
// the provider catalog once and cache it.
let modelCatalogPromise;

const loadModelCatalog = (client) => {
  modelCatalogPromise ??= (async () => {
    const names = new Map();
    try {
      const providers = (await client?.config?.providers?.())?.data?.providers ?? [];
      for (const { id: providerID, models = {} } of providers) {
        for (const [id, model] of Object.entries(models)) {
          if (model?.name) {
            names.set(`${providerID}/${id}`, model.name);
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
    switch (event.type) {
      case OpenCodeEvent.MESSAGE_UPDATED: {
        const info = properties?.[OpenCodeField.INFO];
        // Only user messages carry the model selected for the turn.
        if (info?.[OpenCodeField.ROLE] !== OpenCodeRole.USER) {
          break;
        }
        const model = info[OpenCodeField.MODEL] ?? (info[OpenCodeField.MODEL_ID] ? { [OpenCodeField.MODEL_ID]: info[OpenCodeField.MODEL_ID] } : undefined);
        if (model) {
          resolveModelName(client, model).then((name) => reportModel(name)).catch(() => {});
        }
        break;
      }
      case OpenCodeEvent.SESSION_UPDATED:
        reportTitle(properties?.[OpenCodeField.INFO]?.[OpenCodeField.TITLE]);
        break;
      case OpenCodeEvent.SESSION_IDLE:
      case OpenCodeEvent.SESSION_ERROR:
        report(KmuxStatus.IDLE);
        break;
      case OpenCodeEvent.PERMISSION_ASKED:
      case OpenCodeEvent.PERMISSION_UPDATED:
      case OpenCodeEvent.QUESTION_ASKED:
        report(KmuxStatus.NEEDS_INPUT);
        break;
      case OpenCodeEvent.PERMISSION_REPLIED:
      case OpenCodeEvent.QUESTION_REPLIED:
      case OpenCodeEvent.QUESTION_REJECTED:
        report(KmuxStatus.RUNNING);
        break;
    }
  },
  [OpenCodeHook.CHAT_MESSAGE]: async () => {
    report(KmuxStatus.RUNNING);
  },
  [OpenCodeHook.TOOL_EXECUTE_BEFORE]: async () => {
    report(KmuxStatus.RUNNING);
  },
  [OpenCodeHook.TOOL_EXECUTE_AFTER]: async () => {
    report(KmuxStatus.RUNNING);
  },
});
