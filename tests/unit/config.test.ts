import assert from "node:assert/strict";
import { test } from "node:test";

import {
  defineConfig,
  severityFor,
  type ConfigOverride,
  type SeverityProfile,
} from "../../runtime/config.ts";
import type { NativeDiagnostic } from "../../runtime/native.ts";

const diagnostic: NativeDiagnostic = {
  path: "src/main.ts",
  line: 1,
  column: 1,
  ruleId: "section-order",
  message: "imports must appear first",
  fixedError: false,
};

test("rule severity takes precedence over the profile default", () => {
  const rules = { "section-order": "error" as const };
  const local: SeverityProfile = { default: "warning", rules };
  const config = defineConfig({ profiles: { local } });

  assert.equal(severityFor(config, "local", diagnostic), "error");
});

test("fixed diagnostics cannot be downgraded", () => {
  const local: SeverityProfile = { default: "warning" };
  const config = defineConfig({ profiles: { local } });
  const fixedDiagnostic = { ...diagnostic, fixedError: true };

  assert.equal(severityFor(config, "local", fixedDiagnostic), "error");
});

test("the last matching file override wins", () => {
  const local: SeverityProfile = { default: "warning" };
  const errorLocal = { default: "error" as const };
  const warningLocal = { default: "warning" as const };
  const errorOverride: ConfigOverride = {
    files: ["**/*.ts"],
    profiles: { local: errorLocal },
  };
  const warningOverride: ConfigOverride = {
    files: ["src/**/*.ts"],
    profiles: { local: warningLocal },
  };
  const overrides = [errorOverride, warningOverride];
  const config = defineConfig({ profiles: { local }, overrides });

  assert.equal(severityFor(config, "local", diagnostic), "warning");
});
