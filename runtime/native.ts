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
