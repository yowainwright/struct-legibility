# struct-lint

A small structural linter for source files. It checks whether code reads top-down: imports and public types first, entry points and exported functions next, then private helpers.

The analyzer is C11 with Tree-sitter. scriptc compiles the TypeScript configuration and links the analyzer through native FFI. The standalone executable needs no Node.js or JavaScript runtime.

TypeScript (`.ts`) is the first language pack. Go, Python, and Bash are not implemented yet.

## Checks

<!-- built-in rules from src/analyzer.c and declaration categories from src/languages/typescript.c -->

| Rule | Behavior |
| --- | --- |
| `section-order` | Enforces imports, public types, constants, then functions. |
| `function-order` | Keeps `main` and exported functions above private helpers, and callees below their callers. Recursive cycles stay together. |
| `parse-error` | Reports invalid TypeScript as an error in every profile. |

Diagnostics are deterministic. Directory scans use available CPU cores and honor nested `.gitignore` files by default.

## Install

Download the archive for your system from [GitHub Releases](https://github.com/yowainwright/struct-lint/releases).

| System | Archive |
| --- | --- |
| Linux x64 | `struct-lint-linux-x64.tar.gz` |
| macOS Apple Silicon | `struct-lint-macos-arm64.tar.gz` |
| macOS Intel | `struct-lint-macos-x64.tar.gz` |

For example, on macOS Apple Silicon:

```sh
mkdir struct-lint
cd struct-lint
archive=struct-lint-macos-arm64.tar.gz
base=https://github.com/yowainwright/struct-lint/releases/latest/download
curl -fLO "$base/$archive"
curl -fLO "$base/SHA256SUMS"
grep -F " ./$archive" SHA256SUMS | shasum -a 256 -c -
tar -xzf "$archive"
./struct-lint --version
./struct-lint --help
```

On Linux, select the Linux archive and use `sha256sum --ignore-missing -c SHA256SUMS` to verify it.
Keep `LICENSE` and `LICENSES/` with redistributed binaries. Running a downloaded binary requires no build tools. Custom configurations require the source build below.

## Build

<!-- prerequisites and build commands from mise.toml, package.json, scripts/setup.sh, scripts/build.sh, and CMakeLists.txt -->

Requirements: mise, CMake 3.24 or newer, Clang, and Git. mise provisions Node.js 26 and Nub; Nub provisions pnpm. CI also tests Node.js 22 and 24.

```sh
mise install
nub install
nub run setup
nub run build
```

`nub run setup` installs managed hooks in `.git/hooks` and preserves existing unmanaged hooks.

The standalone binary is written to `.build/struct-lint`.

For the native C CLI without the embedded TypeScript configuration runtime:

```sh
cmake -S . -B .build/native -DCMAKE_BUILD_TYPE=Release
cmake --build .build/native --target struct-lint --parallel
```

## Dependency maintenance

<!-- dependency maintenance scripts and targets from package.json, .codependencerc, and .pastoralistrc -->

```sh
nub run deps:check
nub run deps:update
nub run overrides:check
nub run overrides:update
```

Codependence manages pnpm dependencies, Docker image tags, and GitHub Actions references. Pastoralist audits package-manager overrides and checks medium-or-higher vulnerabilities with OSV without applying automatic fixes.

## CLI

<!-- arguments, environment profile, defaults, and exits from runtime/index.ts and src/scriptc_bridge.c -->

```text
struct-lint [options] [path...]

options:
  --profile <name>       Select a compiled profile
  --format human|json    Select diagnostic output
  --no-ignore            Include paths excluded by .gitignore
  --help                 Show help
  --version              Show version
```

With no paths, the CLI scans the current directory.
Directory scans inherit `.gitignore` rules up to the repository root and support recursive `**` patterns. Explicit files are always analyzed. Overlapping paths are analyzed once.

```sh
.build/struct-lint src
STRUCT_LINT_PROFILE=ci .build/struct-lint src
.build/struct-lint --profile ci --format json src test.ts
```

The default binary has `local` and `ci` profiles. `local` emits warnings and exits `0`; `ci` emits errors and exits `1` when findings exist. Parse errors always exit `1`. Invalid usage and analysis failures exit `2`.

`STRUCT_LINT_PROFILE` selects the environment default. `--profile` takes precedence. Human diagnostics go to stderr; JSON goes to stdout.

## Configuration

<!-- public configuration API and severity resolution from runtime/config.ts and runtime/index.ts -->

A config is a TypeScript entry point compiled into its own binary by scriptc. It defines severity profiles, ordered file overrides, and optional project-level rules.

See the custom-rule example below for a complete compiled configuration.

```sh
./scripts/build.sh struct-lint.config.ts -o .build/struct-lint
```

Later matching overrides win. Custom rules have the type `(project: Project) => readonly RuleDiagnostic[]`. They receive immutable files, declarations, imports, local calls, and resolved cross-file call edges.

Configuration is compiled through scriptc's static tier. The build rejects `eval`, dynamic code, and other constructs that would require an embedded JavaScript engine.

## Examples

<!-- runnable source, config, commands, and diagnostics verified by the README E2E -->

Save a file that follows the built-in ordering rules as `main.ts`:

```ts
interface User {
  readonly name: string;
}

const greetingPrefix = "Hello";

export function main(user: User): string {
  return formatGreeting(user);
}

function formatGreeting(user: User): string {
  return `${greetingPrefix}, ${user.name}`;
}
```

```sh
nub run build
.build/struct-lint --profile ci main.ts
```

The command exits `0` without output.

### Custom rule

Save this config as `struct-lint.config.ts`:

```ts
import {
  defineConfig,
  start,
  type Config,
  type Project,
  type Rule,
  type RuleDiagnostic,
  type SeverityProfile,
} from "./runtime";

type Declaration = Project["files"][number]["declarations"][number];

const isInvalidEntrypoint = (declaration: Declaration): boolean => {
  const functionDeclaration = declaration.kind === "function";
  const exportedLaunch = declaration.exported && declaration.name === "launch";
  return functionDeclaration && exportedLaunch;
};

const toDiagnostic = (
  path: string,
  declaration: Declaration,
): RuleDiagnostic => {
  const ruleId = "entrypoint-name";
  const message = `exported function ${declaration.name} must be named main`;
  const line = declaration.line;
  const column = declaration.column;
  return { ruleId, message, path, line, column };
};

const entrypointNames = (project: Project): readonly RuleDiagnostic[] => {
  return project.files.flatMap((file) => {
    const declarations = file.declarations.filter(isInvalidEntrypoint);
    return declarations.map((declaration) => toDiagnostic(file.path, declaration));
  });
};

const localSeverity = "warning" as const;
const ciSeverity = "error" as const;
const local: SeverityProfile = { default: localSeverity };
const ci: SeverityProfile = { default: ciSeverity };
const profiles: Config["profiles"] = { local, ci };
const rules: readonly Rule[] = [entrypointNames];
const configInput: Config = { profiles, rules };
const config = defineConfig(configInput);

start(config);
```

Given `launch.ts`:

```ts
export function launch(): void {}
```

Compile the config and run its binary:

```sh
./scripts/build.sh struct-lint.config.ts \
  -o .build/struct-lint-custom
.build/struct-lint-custom --profile ci launch.ts
```

```text
launch.ts:1:1: error[entrypoint-name] exported function launch must be named main
```

## Suppressions

<!-- suppression syntax and scope from declaration_suppression in src/analyzer.c -->

Place `// struct-lint-disable-next <rule-id> -- <reason>` immediately above a top-level declaration. Built-in suppression IDs are `function-order` and `section-order`; custom rules match their own `ruleId`.

## C library

<!-- public analyzer API from include/struct_lint.h -->

The static `struct_lint` target exposes:

```c
SlStatus sl_analyze(const SlRequest *request, SlReport *report);
void sl_report_free(SlReport *report);
```

Set `SlRequest.collect_facts` to include declarations, imports, exports, and call edges in `SlReport`. The full ABI is in `include/struct_lint.h`.

## Adding a language

<!-- language-pack contract and registry from src/language.h and src/language.c -->

Language support is isolated behind `SlLanguagePack`. A pack supplies extensions, a Tree-sitter grammar, declaration and call extraction, import resolution, export detection, entry-point detection, and cross-file call resolution.

Add a pack under `src/languages/`, register it in `src/language.c`, link its grammar in `CMakeLists.txt`, and add C and end-to-end fixtures. The analyzer, CLI, config runtime, profiles, output, suppressions, and project graph remain shared.

## Development

<!-- development commands and limits from package.json, scripts/check.sh, and scripts/benchmark.sh -->

```sh
nub run typecheck
nub run test
nub run benchmark
```

The test command runs TypeScript unit tests, builds both CLIs, runs C and
end-to-end tests including the README snippets, and verifies static scriptc builds.
CI also extracts and tests release archives on all three release platforms.
The separate benchmark checks a large corpus against limits of 2 seconds,
64 MiB peak RSS, and a 5 MiB binary. CI enforces these limits on Linux x64 / Node 26.

## License

MIT. Third-party notices are in `LICENSES/`.
