import {
  defineConfig,
  start,
  type Config,
  type ConfigOverride,
  type Project,
  type Rule,
  type RuleDiagnostic,
  type SeverityOverride,
  type SeverityProfile,
} from "../../../runtime";

const localRules = {
  "section-order": "error" as const,
  "entrypoint-name": "error" as const,
  "call-edge": "error" as const,
  "unexpected-edge": "error" as const,
};
const local: SeverityProfile = { default: "warning", rules: localRules };

const ci: SeverityProfile = { default: "error" };

const entrypointNames = (project: Project): readonly RuleDiagnostic[] => {
  return project.files.flatMap((file) => {
    return file.declarations
      .filter((declaration) => declaration.kind === "function")
      .filter((declaration) => declaration.exported && declaration.name === "launch")
      .map((declaration) => {
        const ruleId = "entrypoint-name";
        const message = `exported entrypoint ${declaration.name} must be named main`;
        return {
          ruleId,
          message,
          path: file.path,
          line: declaration.line,
          column: declaration.column,
        };
      });
  });
};

const callEdges = (project: Project): readonly RuleDiagnostic[] => {
  return project.calls
    .filter((call) => call.callerName === "crossFileMain")
    .map((call) => {
      const ruleId = "call-edge";
      const message = `${call.callerName} calls ${call.calleeName} in ${call.calleePath}`;
      return { ruleId, message, path: call.callerPath, line: 1, column: 1 };
    });
};

const unexpectedCallers = ["outer", "shadowMain", "unrelatedMain"];
const unexpectedEdges = (project: Project): readonly RuleDiagnostic[] => {
  return project.calls
    .filter((call) => unexpectedCallers.includes(call.callerName))
    .map((call) => {
      const ruleId = "unexpected-edge";
      const message = `${call.callerName} was incorrectly linked to ${call.calleeName}`;
      return { ruleId, message, path: call.callerPath, line: 1, column: 1 };
    });
};

const profiles: Config["profiles"] = { local, ci };
const overrideErrorRules = { "section-order": "error" as const };
const overrideErrorLocal: SeverityOverride = { rules: overrideErrorRules };
const overrideErrorProfiles: ConfigOverride["profiles"] = { local: overrideErrorLocal };
const overrideError: ConfigOverride = {
  files: ["/overrides/**/*.ts"],
  profiles: overrideErrorProfiles,
};
const overrideWarningRules = { "section-order": "warning" as const };
const overrideWarningLocal: SeverityOverride = { rules: overrideWarningRules };
const overrideWarningProfiles: ConfigOverride["profiles"] = { local: overrideWarningLocal };
const overrideWarning: ConfigOverride = {
  files: ["/overrides/**/*.ts"],
  profiles: overrideWarningProfiles,
};
const overrides = [overrideError, overrideWarning];
const rules: readonly Rule[] = [entrypointNames, callEdges, unexpectedEdges];
const config: Config = defineConfig({ profiles, overrides, rules });

start(config);
