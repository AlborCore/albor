# Backlog

Work that is agreed but not done yet. Every entry names its fix precisely, so it can be
picked up without re-deciding anything.

## Phase 5 is deferred

Phase 5 (devrt, automation, language server, standalone preview host, accessibility
adapters, inspector, windows/linux platform adapters) starts only after the platform
renders a real application. The application-readiness plan and its cluster order live in
`PRE-PHASE-5-PLAN.md`.

## Collapse the consecutive comments in the test suites

Every test file pairs an `// arrange` / `// act` / `// assert` marker with an explanatory
comment line beneath it. Two comment lines above one statement violate the single-line rule
in `STYLE-GUIDE.md` (§Comments), so the sweep keeps only the marker and drops the
explanation: `// assert` alone above the assertion, because the test name and the assertion
already say what the check means.

162 pairs across 6 files:

| file | pairs |
| --- | --- |
| `tests/compiler_type_checker.test.cpp` | 83 (82 marker pairs plus one markerless pair) |
| `tests/compiler_parser.test.cpp` | 35 |
| `tests/manifest.test.cpp` | 21 |
| `tests/compiler_value_type.test.cpp` | 18 |
| `tests/compiler_lexer.test.cpp` | 3 |
| `tests/compiler_ast.test.cpp` | 2 |

The one markerless pair sits at `tests/compiler_type_checker.test.cpp:84` — two explanation
lines above `catalog.add_native_data`, which collapse into a single line instead.

`tests/compiler_diagnostic.test.cpp` is already clean.

Until the sweep runs, the rule applies per file: whenever a file is modified and carries
this violation, it is fixed in the same change (see `AGENTS.md`, Workflow).
