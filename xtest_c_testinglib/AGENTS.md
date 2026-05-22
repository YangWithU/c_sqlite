# Project Overview
This repository provides a small C testing framework (`xtest`) and a built-in runner for
assertion, fixture, benchmark, timeout, crash, and parallel execution workflows. It is designed
to be lightweight, header-first, and easy to integrate into C projects through macro-based test
registration and a single runner binary.

## Repository Structure
- `tests/` - Test suites that validate assertions, fixtures, benchmarks, networking, and parallel
  execution behavior.
- `.cache/` - > TODO: Document what cache artifacts are stored here and when they can be removed.
- `.sisyphus/` - > TODO: Document the purpose of this directory.
- `xtest.h` - Public framework header: test macros, assertions, registration, and runtime helpers.
- `xtest_runner.c` - CLI runner implementation, process isolation, timeout handling, XML output.
- `Makefile` - Primary build and test entrypoint used by local development and CI.
- `Makefile.example` - Example Makefile with the same target set as `Makefile`.
- `compile_commands.json` - Compilation database for tooling (e.g., editors and language servers).

## Build & Development Commands
1. Install / prerequisites
```sh
> TODO: Add repository-specific dependency installation steps (if any).
```

2. Build
```sh
make all
```

3. Test
```sh
make test
make test-asan
make test-tsan
make test-leak
make test-cov
make test-parallel
```

4. Lint
```sh
> TODO: Add lint command (no lint target is currently defined in Makefile).
```

5. Type-check
```sh
> TODO: Add static/type check command (no dedicated target is currently defined).
```

6. Run
```sh
./build/xtest_runner
./build/xtest_runner --help
./build/xtest_runner --list
```

7. Debug
```sh
./build/xtest_runner --no-fork -v
```

8. Deploy
```sh
> TODO: Add deploy/release command(s); none are currently documented.
```

## Code Style & Conventions
- Language standard and warnings are defined in `Makefile`: `-std=c11 -Wall -Wextra -pedantic`.
- Keep tests named via framework macros: `TEST(suite, name)`, `TEST_F`, `FAIL_TEST`,
  `DISABLED_TEST`, and `TEST_BENCH`.
- Use snake_case for C identifiers, and keep suite/test names short and descriptive.
- Co-locate test files in `tests/` with `test_*.c` naming so they are auto-included by Makefile.
- > TODO: Add formatter/linter config references (e.g., `.clang-format`, `clang-tidy`) if adopted.
- Commit message template:
```text
<type>(<scope>): <short summary>

<optional body>
```
- Suggested types: `feat`, `fix`, `test`, `build`, `docs`, `refactor`, `chore`.

## Architecture Notes
```mermaid
flowchart LR
  A[tests/test_*.c\nTEST macros] --> B[xtest.h\nregistration + assertions]
  B --> C[xtest_suites ELF section]
  C --> D[xtest_runner.c]
  D --> E[child process per test\n(or --no-fork mode)]
  E --> F[console summary]
  E --> G[JUnit XML output]
```

`TEST*` macros in `xtest.h` place test descriptors into a dedicated ELF section, and
`xtest_runner.c` enumerates that section at runtime to execute tests. The runner handles execution
mode selection (sequential/parallel), timeout and crash isolation, and final reporting to stdout
or optional XML output.

## Testing Strategy
1. Unit-style coverage: assertion behavior (`basic`, `str`, `float`, `array`) and fixture logic.
2. Integration-style coverage: runner features such as benchmark mode, parallel mode, and timeout
   defaults.
3. Environment-sensitive coverage: socket-based fixture behavior in
   `tests/test_network_package.c`.
4. Local execution:
```sh
make test
make test-asan
make test-tsan
make test-cov
```
5. CI execution:
```sh
> TODO: Add CI workflow path and exact command matrix (not present in repository files).
```
6. E2E:
```sh
> TODO: Define end-to-end scope if this repository is embedded into downstream projects.
```

## Security & Compliance
- Do not commit secrets or credentials; this repository currently has no secret management config.
- Prefer sanitizer targets (`make test-asan`, `make test-tsan`, `make test-leak`) in validation
  pipelines for memory/thread safety checks.
- > TODO: Add dependency scanning configuration (e.g., Dependabot, SCA, or SBOM workflow).
- > TODO: Add explicit license file/reference; no license metadata was provided in the file set.
- Guardrail: avoid changing signal, process, or timeout behavior in `xtest.h` / `xtest_runner.c`
  without corresponding regression tests.

## Agent Guardrails
1. Never edit generated or local-state directories unless explicitly asked:
   `.cache/`, `build/`, and `coverage/`.
2. Treat `xtest.h` macro and ABI-like surfaces as high-risk; require explicit review for changes
   that alter macro signatures, test registration, or exit codes.
3. For runner behavior changes, include updates in at least one `tests/test_*.c` file that proves
   expected outcomes.
4. Keep commands non-interactive and bounded; avoid long-running background processes unless needed.
5. > TODO: Add repository-specific rate limits / resource quotas for automated runs.

## Extensibility Hooks
- Test declaration hooks: `TEST`, `TEST_F`, `FAIL_TEST`, `DISABLED_TEST`, `TEST_BENCH`.
- Fixture hook points: `TEST_DEFINE_FIXTURE(set_up, tear_down)` used by `TEST_F` wrappers.
- Runner CLI extension surface (`xtest_runner.c`): `--parallel`, `--timeout`, `--repeat`,
  `--output=xml[:FILE]`, `--no-fork`, and color controls.
- Environment variable hooks:
  - `ASAN_OPTIONS=detect_leaks=1` (used with `make test-leak`).
  - > TODO: Document additional supported environment variables, if any.
- Feature flags:
  - Compile-time via `CFLAGS` and Make targets (`test-asan`, `test-tsan`, `test-cov`).
  - > TODO: Document any runtime feature flags beyond CLI options.

## Further Reading
- [`xtest.h`](./xtest.h)
- [`xtest_runner.c`](./xtest_runner.c)
- [`Makefile`](./Makefile)
- [`tests/`](./tests/)
- > TODO: Add architecture docs/ADRs when available (for example `docs/ARCH.md`, `docs/adr/`).
