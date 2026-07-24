# Contributing

## Setup

Requirements are Bun, CMake 3.24 or newer, a C11 compiler, Git, and ShellCheck.

```sh
bun install --frozen-lockfile
bun run test
```

## Changes

1. Branch from `main`.
2. Keep each change focused.
3. Add TypeScript unit, C, or end-to-end coverage for behavior changes.
4. Run `bun run test` and `shellcheck scripts/*.sh tests/e2e/*.sh`.
5. Open a pull request with the problem, approach, and validation.

Keep C and TypeScript functions small and single-purpose. Prefer early returns,
immutable values, and direct control flow.

Language packs belong in `src/languages/`. Register each pack in
`src/language.c`, link its Tree-sitter grammar, and add API and CLI fixtures.
