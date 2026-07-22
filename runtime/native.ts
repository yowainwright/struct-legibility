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
  readonly line: number;
  readonly column: number;
  readonly calls: readonly string[];
}

export interface SourceFile {
  readonly path: string;
  readonly declarations: readonly Declaration[];
}

export interface Project {
  readonly files: readonly SourceFile[];
}

declare global {
  const scriptArgs: readonly string[];
  function __slAnalyze(
    paths: readonly string[],
    useGitignore: boolean,
    collectFacts: boolean,
  ): NativeReport;
  function __slWrite(text: string, useStderr: boolean): void;
  function __slSetExitCode(code: number): void;
  function __slDefaultProfile(): string;
}
