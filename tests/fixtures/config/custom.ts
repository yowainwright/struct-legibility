// @tqs-script
import {
  defineConfig,
  start,
  type ConfigOverride,
  type Project,
  type RuleDiagnostic,
  type SeverityProfile,
} from "../../../runtime";

const local: SeverityProfile = {
  default: "warning",
  rules: { "section-order": "error", "entrypoint-name": "error" },
};

const ci: SeverityProfile = { default: "error" };

const entrypointNames = (project: Project): readonly RuleDiagnostic[] => {
  return project.files.flatMap((file) => {
    return file.declarations
      .filter((declaration) => declaration.kind === "function")
      .filter((declaration) => declaration.exported && declaration.name !== "main")
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

const profiles = { local, ci };
const overrideError: ConfigOverride = {
  files: ["overrides/**"],
  profiles: { local: { rules: { "section-order": "error" } } },
};
const overrideWarning: ConfigOverride = {
  files: ["overrides/**"],
  profiles: { local: { rules: { "section-order": "warning" } } },
};
const overrides = [overrideError, overrideWarning];
const config = defineConfig({ profiles, overrides, rules: [entrypointNames] });

start(config);
