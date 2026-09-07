import assert from "node:assert/strict";
import { test } from "node:test";

import type { Call, Declaration, Import, Project, SourceFile } from "../../runtime/native.ts";
import { freezeProject } from "../../runtime/project.ts";

const declaration: Declaration = {
  kind: "function",
  name: "main",
  exported: true,
  entrypoint: true,
  exportNames: ["main"],
  line: 1,
  column: 1,
  calls: ["helper"],
  suppressions: ["section-order"],
};
const imported: Import = {
  localName: "helper",
  importedName: "helper",
  source: "./helper",
  targetPath: "helper.ts",
};
const sourceFile: SourceFile = {
  path: "main.ts",
  declarations: [declaration],
  imports: [imported],
};
const call: Call = {
  callerPath: "main.ts",
  callerName: "main",
  calleePath: "helper.ts",
  calleeName: "helper",
};
const project: Project = { files: [sourceFile], calls: [call] };

test("custom rules receive immutable project facts", () => {
  const frozen = freezeProject(project);
  const file = frozen.files[0];
  const frozenDeclaration = file?.declarations[0];
  assert.ok(Object.isFrozen(frozen));
  assert.ok(Object.isFrozen(frozen.files));
  assert.ok(Object.isFrozen(frozen.calls[0]));
  assert.ok(Object.isFrozen(file));
  assert.ok(Object.isFrozen(file?.imports[0]));
  assert.ok(Object.isFrozen(frozenDeclaration));
  assert.ok(Object.isFrozen(frozenDeclaration?.calls));
  assert.ok(Object.isFrozen(frozenDeclaration?.exportNames));
  assert.ok(Object.isFrozen(frozenDeclaration?.suppressions));
});
