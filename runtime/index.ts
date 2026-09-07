import { hasProfile, severityFor, type Config, type RuleDiagnostic, type Severity } from "./config";
import type { Declaration, NativeDiagnostic, NativeReport, Project } from "./native";
import { analyze, defaultProfile, printInfo, write } from "./native";
import { freezeProject } from "./project";

export {
  defineConfig,
  type ConfigOverride,
  type Config,
  type Project,
  type Rule,
  type RuleDiagnostic,
  type SeverityProfile,
  type SeverityOverride,
} from "./config";

type OutputFormat = "human" | "json";

interface CliOptions {
  readonly profile: string;
  readonly format: OutputFormat;
  readonly useGitignore: boolean;
  readonly paths: readonly string[];
}

interface OutputDiagnostic {
  readonly ruleId: string;
  readonly severity: Severity;
  readonly path: string;
  readonly line: number;
  readonly column: number;
  readonly message: string;
  readonly related: null;
}

const withDefaultPath = (options: CliOptions): CliOptions => {
  if (options.paths.length > 0) return options;
  return { ...options, paths: ["."] };
};

const defaultOptions = (): CliOptions => {
  const profile = defaultProfile();
  return { profile, format: "human", useGitignore: true, paths: [] };
};

const parseNamedOption = (
  argument: string,
  value: string | undefined,
  options: CliOptions,
  config: Config,
): CliOptions | null => {
  if (argument === "--no-ignore") return { ...options, useGitignore: false };
  if (value === undefined) return null;
  const isProfile = argument === "--profile";
  const isUnknownProfile = isProfile && !hasProfile(config, value);
  if (isUnknownProfile) return null;
  if (isProfile) return { ...options, profile: value };
  const isOutputFormat = value === "human" || value === "json";
  const isInvalidFormat = argument !== "--format" || !isOutputFormat;
  if (isInvalidFormat) return null;
  const format: OutputFormat = value;
  return { ...options, format };
};

const optionWidth = (argument: string): number => {
  return argument === "--no-ignore" ? 1 : 2;
};

const parseOptions = (
  args: readonly string[],
  options: CliOptions,
  config: Config,
): CliOptions | null => {
  if (args.length === 0) return withDefaultPath(options);
  const argument = args[0];
  if (!argument.startsWith("-")) return { ...options, paths: args };
  const value = args.length > 1 ? args[1] : undefined;
  const next = parseNamedOption(argument, value, options, config);
  if (next === null) return null;
  return parseOptions(args.slice(optionWidth(argument)), next, config);
};

const outputDiagnostic = (
  diagnostic: NativeDiagnostic,
  profile: string,
  config: Config,
): OutputDiagnostic => {
  const severity = severityFor(config, profile, diagnostic);
  const ruleId = diagnostic.ruleId;
  const path = diagnostic.path;
  const line = diagnostic.line;
  const column = diagnostic.column;
  const message = diagnostic.message;
  const related = null;
  return { ruleId, severity, path, line, column, message, related };
};

const humanDiagnostic = (diagnostic: OutputDiagnostic): string => {
  const location = `${diagnostic.path}:${diagnostic.line}:${diagnostic.column}`;
  return `${location}: ${diagnostic.severity}[${diagnostic.ruleId}] ${diagnostic.message}`;
};

const formatJson = (diagnostics: readonly OutputDiagnostic[]): string => {
  const errors = diagnostics.filter((diagnostic) => diagnostic.severity === "error").length;
  const warnings = diagnostics.length - errors;
  const summary = { errors, warnings };
  return JSON.stringify({ diagnostics, summary });
};

const asNativeDiagnostic = (diagnostic: RuleDiagnostic): NativeDiagnostic => {
  const fixedError = false;
  return { ...diagnostic, fixedError };
};

const declarationSuppresses = (declaration: Declaration, diagnostic: RuleDiagnostic): boolean => {
  if (declaration.line !== diagnostic.line) return false;
  return declaration.suppressions.includes(diagnostic.ruleId);
};

const diagnosticIsSuppressed = (project: Project, diagnostic: RuleDiagnostic): boolean => {
  return project.files.some((file) => {
    if (file.path !== diagnostic.path) return false;
    return file.declarations.some((declaration) => declarationSuppresses(declaration, diagnostic));
  });
};

const customDiagnostics = (report: NativeReport, config: Config): readonly NativeDiagnostic[] => {
  const rules = config.rules ?? [];
  const project = freezeProject(report.project);
  const diagnostics = rules.flatMap((rule) => rule(project));
  const active = diagnostics.filter((diagnostic) => !diagnosticIsSuppressed(project, diagnostic));
  return active.map(asNativeDiagnostic);
};

const formattedOutput = (
  diagnostics: readonly OutputDiagnostic[],
  format: OutputFormat,
): string => {
  if (format === "json") return formatJson(diagnostics);
  return diagnostics.map(humanDiagnostic).join("\n");
};

const emitReport = (report: NativeReport, options: CliOptions, config: Config): number => {
  const configuredDiagnostics = customDiagnostics(report, config);
  const nativeDiagnostics = [...report.diagnostics, ...configuredDiagnostics];
  const diagnostics = nativeDiagnostics.map((diagnostic) => {
    return outputDiagnostic(diagnostic, options.profile, config);
  });
  const hasErrors = diagnostics.some((diagnostic) => diagnostic.severity === "error");
  const output = formattedOutput(diagnostics, options.format);
  if (output.length > 0) write(`${output}\n`, options.format === "human");
  return hasErrors ? 1 : 0;
};

const usageError = (): number => {
  write("usage: struct-lint [options] [path...]\n", true);
  return 2;
};

const run = (config: Config): number => {
  const args = process.argv.slice(2);
  const info = printInfo(args[0] ?? "");
  if (info >= 0) return info;
  const options = parseOptions(args, defaultOptions(), config);
  if (options === null || !hasProfile(config, options.profile)) return usageError();
  try {
    const collectFacts = (config.rules?.length ?? 0) > 0;
    const report = analyze(options.paths, options.useGitignore, collectFacts);
    return emitReport(report, options, config);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    write(`struct-lint: ${message}\n`, true);
    return 2;
  }
};

export const start = (config: Config): void => {
  process.exit(run(config));
};
