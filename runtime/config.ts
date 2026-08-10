import type { NativeDiagnostic, Project } from "./native";

export type { Call, Declaration, DeclarationKind, Import, Project, SourceFile } from "./native";

export type Severity = "warning" | "error";
export type SeverityMap = Readonly<Record<string, Severity | undefined>>;

export interface SeverityProfile {
  readonly default: Severity;
  readonly rules?: SeverityMap;
}

export interface SeverityOverride {
  readonly default?: Severity;
  readonly rules?: SeverityMap;
}

export type SeverityOverrideMap = Readonly<Record<string, SeverityOverride | undefined>>;
export type SeverityProfileMap = Readonly<Record<string, SeverityProfile | undefined>>;

export interface ConfigOverride {
  readonly files: readonly string[];
  readonly profiles: SeverityOverrideMap;
}

export interface Config {
  readonly profiles: SeverityProfileMap;
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

const profileFor = (profiles: SeverityProfileMap, name: string): SeverityProfile | undefined => {
  const hasProfile = Object.keys(profiles).includes(name);
  if (!hasProfile) return undefined;
  return profiles[name];
};

export const hasProfile = (config: Config, profile: string): boolean => {
  return profileFor(config.profiles, profile) !== undefined;
};

const overrideProfileFor = (
  profiles: SeverityOverrideMap,
  name: string,
): SeverityOverride | undefined => {
  const hasProfile = Object.keys(profiles).includes(name);
  if (!hasProfile) return undefined;
  return profiles[name];
};

const ruleSeverityFor = (rules: SeverityMap | undefined, ruleId: string): Severity | undefined => {
  if (rules === undefined) return undefined;
  const hasRule = Object.keys(rules).includes(ruleId);
  if (!hasRule) return undefined;
  return rules[ruleId];
};

const globSource = (glob: string): string => {
  const directoryGlob = "\u0000";
  const anyGlob = "\u0001";
  return glob
    .replace(/[.+^${}()|[\]\\]/g, "\\$&")
    .replace(/\*\*\//g, directoryGlob)
    .replace(/\*\*/g, anyGlob)
    .replace(/\*/g, "[^/]*")
    .replace(/\?/g, "[^/]")
    .replace(new RegExp(directoryGlob, "g"), "(?:.*/)?")
    .replace(new RegExp(anyGlob, "g"), ".*");
};

const matchesGlob = (path: string, glob: string): boolean => {
  const normalizedPath = path.replace(/\\/g, "/");
  const normalizedGlob = glob.startsWith("/") ? glob.slice(1) : glob;
  const pattern = new RegExp(`^(?:.*/)?${globSource(normalizedGlob)}$`);
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
  const profile = overrideProfileFor(override.profiles, profileName);
  if (profile === undefined) return severity;
  const ruleSeverity = ruleSeverityFor(profile.rules, diagnostic.ruleId);
  if (ruleSeverity !== undefined) return ruleSeverity;
  return profile.default ?? severity;
};

export const severityFor = (
  config: Config,
  profileName: string,
  diagnostic: NativeDiagnostic,
): Severity => {
  if (diagnostic.fixedError) return "error";
  const profile = profileFor(config.profiles, profileName);
  if (profile === undefined) return "error";
  const ruleSeverity = ruleSeverityFor(profile.rules, diagnostic.ruleId);
  const baseSeverity = ruleSeverity ?? profile.default;
  const overrides = config.overrides ?? [];
  return overrides.reduce((severity, override) => {
    return applyOverride(severity, override, profileName, diagnostic);
  }, baseSeverity);
};
