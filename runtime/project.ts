import type { Call, Declaration, Import, Project, SourceFile } from "./native";

const freezeDeclaration = (declaration: Declaration): Declaration => {
  const calls = Object.freeze([...declaration.calls]);
  const suppressions = Object.freeze([...declaration.suppressions]);
  const exportNames = Object.freeze([...declaration.exportNames]);
  return Object.freeze({ ...declaration, calls, suppressions, exportNames });
};

const freezeImport = (value: Import): Import => Object.freeze({ ...value });

const freezeFile = (file: SourceFile): SourceFile => {
  const declarations = Object.freeze([...file.declarations.map(freezeDeclaration)]);
  const imports = Object.freeze([...file.imports.map(freezeImport)]);
  return Object.freeze({ ...file, declarations, imports });
};

const freezeCall = (call: Call): Call => Object.freeze({ ...call });

export const freezeProject = (project: Project): Project => {
  const files = Object.freeze([...project.files.map(freezeFile)]);
  const calls = Object.freeze([...project.calls.map(freezeCall)]);
  return Object.freeze({ files, calls });
};
