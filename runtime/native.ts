import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

export interface NativeDiagnostic {
  readonly path: string;
  readonly line: number;
  readonly column: number;
  readonly ruleId: string;
  readonly message: string;
  readonly fixedError: boolean;
}

export interface NativeReport {
  readonly diagnostics: readonly NativeDiagnostic[];
  readonly project: Project;
}

export type DeclarationKind = "import" | "type" | "constant" | "function";

export interface Declaration {
  readonly kind: DeclarationKind;
  readonly name: string;
  readonly exported: boolean;
  readonly entrypoint: boolean;
  readonly exportNames: readonly string[];
  readonly line: number;
  readonly column: number;
  readonly calls: readonly string[];
  readonly suppressions: readonly string[];
}

export interface Import {
  readonly localName: string;
  readonly importedName: string;
  readonly source: string;
  readonly targetPath: string | null;
}

export interface SourceFile {
  readonly path: string;
  readonly declarations: readonly Declaration[];
  readonly imports: readonly Import[];
}

export interface Call {
  readonly callerPath: string;
  readonly callerName: string;
  readonly calleePath: string;
  readonly calleeName: string;
}

export interface Project {
  readonly files: readonly SourceFile[];
  readonly calls: readonly Call[];
}

declare global {
  function slScriptcAnalyze(
    paths: string,
    useGitignore: boolean,
    collectFacts: boolean,
    outputPath: string,
  ): number;

  function slScriptcWrite(text: string, useStderr: boolean): number;
}

const reportFromFile = (path: string): NativeReport => {
  const report = readFileSync(path, "utf8");
  return JSON.parse(report) as NativeReport;
};

const analyzeToFile = (
  paths: readonly string[],
  useGitignore: boolean,
  collectFacts: boolean,
  outputPath: string,
): void => {
  const encodedPaths = paths.join("\0");
  const status = slScriptcAnalyze(encodedPaths, useGitignore, collectFacts, outputPath);
  const failed = status !== 0;
  if (failed) throw new Error(`analysis failed (${status})`);
};

export const analyze = (
  paths: readonly string[],
  useGitignore: boolean,
  collectFacts: boolean,
): NativeReport => {
  const temporaryDirectory = mkdtempSync(join(tmpdir(), "struct-legibility-"));
  const reportPath = join(temporaryDirectory, "report.json");
  try {
    analyzeToFile(paths, useGitignore, collectFacts, reportPath);
    return reportFromFile(reportPath);
  } finally {
    rmSync(temporaryDirectory, { recursive: true, force: true });
  }
};

export const defaultProfile = (): string => {
  const configuredProfile = process.env.STRUCT_LEGIBILITY_PROFILE;
  return configuredProfile || "local";
};

export const write = (text: string, useStderr: boolean): void => {
  const status = slScriptcWrite(text, useStderr);
  const failed = status !== 0;
  if (failed) throw new Error(`write failed (${status})`);
};
