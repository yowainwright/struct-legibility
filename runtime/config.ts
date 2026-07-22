import type { NativeDiagnostic, Project } from "./native";

export type { Declaration, DeclarationKind, Project, SourceFile } from "./native";

export type Severity = "warning" | "error";

export interface SeverityProfile {
  readonly default: Severity;
  readonly rules?: Readonly<Record<string, Severity>>;
}

export interface SeverityOverride {
  readonly default?: Severity;
  readonly rules?: Readonly<Record<string, Severity>>;
}

export interface ConfigOverride {
  readonly files: readonly string[];
  readonly profiles: Readonly<Record<string, SeverityOverride>>;
}

export interface Config {
  readonly profiles: Readonly<Record<string, SeverityProfile>>;
  readonly overrides?: readonly ConfigOverride[];
  readonly rules?: readonly Rule[];
}

export interface RuleDiagnostic {
  readonly ruleId: string;
  readonly message: string;
  readonly path: string;
  readonly line: number;
  readonly column: number;
}

export type Rule = (project: Project) => readonly RuleDiagnostic[];

export const defineConfig = <ConfigType extends Config>(config: ConfigType): ConfigType => {
  return config;
};

export const hasProfile = (config: Config, profile: string): boolean => {
  return Object.hasOwn(config.profiles, profile);
};

const globSource = (glob: string): string => {
  return glob
    .replace(/[.+^${}()|[\]\\]/g, "\\$&")
    .replaceAll("**", "\u0000")
    .replaceAll("*", "[^/]*")
    .replaceAll("\u0000", ".*")
    .replaceAll("?", "[^/]");
};

const matchesGlob = (path: string, glob: string): boolean => {
  const normalizedPath = path.replaceAll("\\", "/");
  const prefix = glob.startsWith("/") ? "" : "(?:.*/)?";
  const pattern = new RegExp(`^${prefix}${globSource(glob)}$`);
  return pattern.test(normalizedPath);
};

const matchesOverride = (override: ConfigOverride, path: string): boolean => {
  return override.files.some((glob) => matchesGlob(path, glob));
};

const applyOverride = (
  severity: Severity,
  override: ConfigOverride,
  profileName: string,
  diagnostic: NativeDiagnostic,
): Severity => {
  if (!matchesOverride(override, diagnostic.path)) return severity;
  const profile = override.profiles[profileName];
  if (profile === undefined) return severity;
  const ruleSeverity = profile.rules?.[diagnostic.ruleId];
  if (ruleSeverity !== undefined) return ruleSeverity;
  return profile.default ?? severity;
};

export const severityFor = (
  config: Config,
  profileName: string,
  diagnostic: NativeDiagnostic,
): Severity => {
  if (diagnostic.fixedError) return "error";
  const profile = config.profiles[profileName];
  if (profile === undefined) return "error";
  const baseSeverity = profile.rules?.[diagnostic.ruleId] ?? profile.default;
  const overrides = config.overrides ?? [];
  return overrides.reduce((severity, override) => {
    return applyOverride(severity, override, profileName, diagnostic);
  }, baseSeverity);
};
