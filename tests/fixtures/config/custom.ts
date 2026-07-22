// @tqs-script
import {
  defineConfig,
  start,
  type ConfigOverride,
  type Project,
  type RuleDiagnostic,
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

const assertFrozen = (value: unknown): void => {
  if (!Object.isFrozen(value)) throw new Error("custom rule facts must be frozen");
};

const checkImmutableFacts = (project: Project): readonly RuleDiagnostic[] => {
  assertFrozen(project);
  assertFrozen(project.files);
  assertFrozen(project.calls);
  project.calls.forEach(assertFrozen);
  project.files.forEach((file) => {
    assertFrozen(file);
    assertFrozen(file.declarations);
    assertFrozen(file.imports);
    file.imports.forEach(assertFrozen);
    file.declarations.forEach((declaration) => {
      assertFrozen(declaration);
      assertFrozen(declaration.calls);
      assertFrozen(declaration.exportNames);
      assertFrozen(declaration.suppressions);
    });
  });
  return [];
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

const profiles = { local, ci };
const overrideErrorRules = { "section-order": "error" as const };
const overrideErrorLocal = { rules: overrideErrorRules };
const overrideErrorProfiles = { local: overrideErrorLocal };
const overrideError: ConfigOverride = {
  files: ["/overrides/**/*.ts"],
  profiles: overrideErrorProfiles,
};
const overrideWarningRules = { "section-order": "warning" as const };
const overrideWarningLocal = { rules: overrideWarningRules };
const overrideWarningProfiles = { local: overrideWarningLocal };
const overrideWarning: ConfigOverride = {
  files: ["/overrides/**/*.ts"],
  profiles: overrideWarningProfiles,
};
const overrides = [overrideError, overrideWarning];
const rules = [entrypointNames, checkImmutableFacts, callEdges, unexpectedEdges];
const config = defineConfig({ profiles, overrides, rules });

start(config);
