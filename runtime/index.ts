import { hasProfile, severityFor, type Config, type RuleDiagnostic, type Severity } from "./config";
import type { NativeDiagnostic, NativeReport } from "./native";

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

const defaultOptions = (config: Config): CliOptions | null => {
  const profile = __slDefaultProfile();
  if (!hasProfile(config, profile)) return null;
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
  if (isProfile && !hasProfile(config, value)) return null;
  if (isProfile) return { ...options, profile: value };
  const isOutputFormat = value === "human" || value === "json";
  if (argument !== "--format" || !isOutputFormat) return null;
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
  const [argument, value] = args;
  if (argument === undefined) return withDefaultPath(options);
  if (!argument.startsWith("-")) return { ...options, paths: args };
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

const customDiagnostics = (report: NativeReport, config: Config): readonly NativeDiagnostic[] => {
  const rules = config.rules ?? [];
  return rules.flatMap((rule) => rule(report.project).map(asNativeDiagnostic));
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
  if (output.length > 0) __slWrite(`${output}\n`, options.format === "human");
  return hasErrors ? 1 : 0;
};

const usageError = (): number => {
  __slWrite("usage: struct-legibility [options] [path...]\n", true);
  return 2;
};

const run = (config: Config): number => {
  const defaults = defaultOptions(config);
  if (defaults === null) return usageError();
  const options = parseOptions(scriptArgs.slice(1), defaults, config);
  if (options === null) return usageError();
  try {
    const collectFacts = (config.rules?.length ?? 0) > 0;
    const report = __slAnalyze(options.paths, options.useGitignore, collectFacts);
    return emitReport(report, options, config);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    __slWrite(`struct-legibility: ${message}\n`, true);
    return 2;
  }
};

export const start = (config: Config): void => {
  __slSetExitCode(run(config));
};
