# Contributing

Requires a C11 compiler, CMake 3.24 or newer, Git, and ShellCheck.
`mise install` can provision CMake and ShellCheck using `mise.toml`.

```sh
./scripts/check.sh
shellcheck scripts/*.sh scripts/hooks/* tests/e2e/*.sh
```

The check script builds into `.build`. Set `SL_BUILD_DIR` to use another
directory. `./scripts/build.sh` builds just the CLI; `./scripts/benchmark.sh`
measures the resulting executable.

## Changes

Keep changes focused and C functions small. Add C or end-to-end coverage for
behavior changes. Describe the problem, resulting behavior, and validation in
the pull request.

To install the optional pre-commit checks and commit-message hook, run
`./scripts/setup.sh`. Existing unmanaged hooks are preserved.

The release version is defined in `CMakeLists.txt`.

## Adding a language

Language packs belong in `src/languages/` and implement `SlLanguagePack` from
`src/language.h`. Each pack identifies declarations, imports, exports, calls,
and entry points using a Tree-sitter grammar.

Register the pack in `src/language.c`, link its grammar in `CMakeLists.txt`,
and add C and CLI fixtures. The analyzer and CLI are shared across languages.
