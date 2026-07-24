// @tqs-script
import {
  defineConfig,
  start,
  type Project,
  type RuleDiagnostic,
} from "../../../runtime";

type Declaration = Project["files"][number]["declarations"][number];

const isInvalidEntrypoint = (declaration: Declaration): boolean => {
  const functionDeclaration = declaration.kind === "function";
  const exportedLaunch = declaration.exported && declaration.name === "launch";
  return functionDeclaration && exportedLaunch;
};

const toDiagnostic = (
  path: string,
  declaration: Declaration,
): RuleDiagnostic => {
  const ruleId = "entrypoint-name";
  const message = `exported function ${declaration.name} must be named main`;
  const line = declaration.line;
  const column = declaration.column;
  return { ruleId, message, path, line, column };
};

const entrypointNames = (project: Project): readonly RuleDiagnostic[] => {
  return project.files.flatMap((file) => {
    const declarations = file.declarations.filter(isInvalidEntrypoint);
    return declarations.map((declaration) => toDiagnostic(file.path, declaration));
  });
};

const local = { default: "warning" as const };
const ci = { default: "error" as const };
const profiles = { local, ci };
const rules = [entrypointNames];
const config = defineConfig({ profiles, rules });

start(config);
