---
name: Implement a GitHub issue
description: Implement a GitHub issue in Albor through explicit approval gates — plan, review the plan with the user, record it on the issue, branch, implement, pass a read-only code review, get the solution approved, then commit, push, and optionally open a PR.
metadata:
  opencode/autoinvoke: false
---

# Skill: Implement a GitHub issue

The argument is a GitHub issue number for `AlborCore/albor`, for example `216`.
This skill is the executable form of `AGENTS.md`; when the two disagree, `AGENTS.md`
wins and the skill is corrected.

The run has four gates the user owns: **plan approval**, **branch choice**,
**solution approval**, and **PR choice**. Never pass a gate on the user's behalf,
and never read silence, a clarifying question, or a partial answer as approval.

## 0. Resolve the argument

- An issue number was passed: continue.
- Nothing was passed, or the value is not a number: ask for the number and stop.
  Do not guess an issue and do not pick one from the open list.

## 1. Read the issue

```sh
gh issue view <number> --json number,title,body,state,labels,comments,url
```

- Read **every** comment, not only the body: decisions are often settled there.
- Extract four things into working notes: the summary, the expected behavior, the
  acceptance criteria, and every spec reference (`§N`, `AD-NN`) the issue cites.
- Note the labels. `bug` means reproduce and fix; `enhancement` means design against
  the spec before planning; `phase-N` tells you which module owns the change.

If the issue has no acceptance criteria, derive them from the expected behavior and
say so in the plan. If it cites no spec section, find the one that owns the area in
the **Architecture Map** of `AGENTS.md`.

## 2. Read the contract

Read what the change is bound by, not the whole repository:

- The cited sections of `albor-design-v4.md`, and `§13` when a decision is touched.
- The **Non-negotiable Boundaries** of `AGENTS.md`. A change that crosses one is a
  stop condition, not a judgment call.
- `STYLE-GUIDE.md` before proposing C++.
- `macos-windows.md` when the change reaches a platform source.
- The owning module's `CMakeLists.txt` and public header, to see its current shape.

## 3. Explore before planning

Search for an existing implementation first (`rg`, the module directory, the test
suite). Most issues name a symptom and a call site; confirm both, then look for the
sibling that already does it correctly — the macOS adapter, the software renderer,
the Direct3D path — so the plan follows the established shape instead of inventing
one.

Understand ownership and lifetime before planning: who allocates, who releases,
which thread the code runs on (`§10.2`, `§10.5`), and whether the node is alive when
the new code runs. Issues in this repository are frequently lifetime bugs.

## 4. Draft the plan

The plan is a document the user can reject, so it states decisions, not intentions.
It contains:

1. **Problem** — what is wrong or missing today, with the file and line that prove
   it, in the issue's terms.
2. **Approach** — the chosen design, and the one or two alternatives rejected, with
   the reason each was rejected.
3. **Work items** — files to add or change, grouped by module, each with what
   changes and why.
4. **Tests** — the test per acceptance criterion, and the suite that owns it.
5. **Parity** — the Windows counterpart of every macOS touch (and vice versa), the
   Metal/Direct3D pair, and the `macos-windows.md` entry.
6. **Risks** — what could break, what the plan deliberately does not cover, and any
   spec ambiguity the plan resolves.
7. **Acceptance map** — each criterion from the issue paired with the work item and
   test that satisfies it.

Keep it as long as the change needs and no longer. A one-line fix gets a short plan.

Stop and ask — inside the plan, as an open question — when the change would reopen a
settled decision (`§13`), weaken a boundary, change a public header's shape, or when
two requirements contradict. Do not resolve that silently in the plan.

## 5. Gate 1: plan approval

Present the plan and ask for approval. Then iterate:

- Every objection gets a revised plan, restated in full or as a diff against the
  previous version, whichever is shorter for the user to read.
- Keep iterating until the user says the plan is approved, in words.
- Do not start implementing because the plan "looks accepted". The gate is explicit.

## 6. Record the plan on the issue

Once approved, post the plan to the issue as a comment, so the issue keeps its
original text and gains the agreed plan:

```sh
gh issue comment <number> --body-file <path>
```

- Write the comment in the issue's voice: the approved plan, the acceptance map, and
  the open questions the user answered.
- Put the body in a temporary file outside the repository, not in the tree.
- Quote the posted URL in the reply.
- Do not edit the issue body unless the user asked for it.
- A later design change (step 11) gets a follow-up comment and a fresh approval, not
  a silent rewrite of the posted plan.

## 7. Gate 2: branch

Ask whether the work goes on a new branch or aggregates on the current one.

- Show the current branch (`git branch --show-current`) and any uncommitted work
  (`git status --short`).
- For a new branch, propose a name derived from the issue, for example
  `gh-<number>-<slug>`. Let the user create it, or offer to create it if they
  prefer; either way, wait until the branch exists and is checked out.
- Never switch, rename, reset, or stash a branch that holds someone else's
  uncommitted work.

## 8. Snapshot the tree

The recovery point comes before the edit, not after the failure:

```sh
bash build/snapshot.sh gh-<number>
```

## 9. Implement

- Minimal change in the module the Architecture Map names. Do not refactor
  neighboring code the issue did not ask about.
- **Platform parity in the same change** (`§5.3`, AD-16): a macOS source carries its
  Windows twin (`*.macos.mm` ↔ `*.win32.c++`), the same tests, and an updated
  `macos-windows.md`. Native render backends are symmetric too (`§6.3`, AD-05):
  Metal and Direct3D 11 move together.
- File names follow the platform-infix rule; a source that reaches a platform API
  carries that platform's infix, and the module `CMakeLists.txt` and every `#include`
  are updated with it.
- Keep generated code generated (`§14`, AD-17): fix the generator, never the output.
- No platform or renderer type in a public header (`§3.5`, AD-05, AD-19).
- Dev/Test-only behavior is excluded at link time, never with `#ifdef` (AD-04).
- Reference the spec in the code where the decision matters, as `§N` / `AD-NN`.
- Comments: one line above the statement, no consecutive comment block
  (`STYLE-GUIDE.md`). The formatter check enforces this in touched code.

Tests are part of the change, not a follow-up: one focused Google Test per
acceptance criterion, self-contained, no helpers or fixtures beyond the shared test
double. The macOS case is mirrored in the Windows suite.

## 10. Build, lint, and test

```sh
cmake --preset debug            # only when .build-debug is missing or stale
cmake --build --preset debug
bash build/check-format.sh
./.build-debug/tests/albor-tests
```

Fix every warning, error, and comment-rule violation in the files the change
touched. Do not reformat untouched files and do not touch `.clang-format`.

Run the suite by executing the binary, never through `ctest` (`AGENTS.md`). The
suite is part of the gate, not an optional check:

- Run the whole binary. Use `--gtest_filter='<Suite>.*'` only to iterate while
  debugging, then run the full suite before moving on.
- A failing test is a defect in the change, not in the test. Fix the code, and touch
  a test only when the test itself asserts the wrong contract.
- Never leave a test skipped, disabled, or commented out to make the run green.
- A crash or an abort is a failure even when the reporter prints no assertion.
- Keep the last summary line (tests, failures, skips) for the report in step 13.

## 11. Read-only code review

Spawn the `reviewer` subagent (`.opencode/agents/reviewer.md`). It is read-only by
permission: edit and shell are denied, so it cannot change the tree, commit, or run
a build. Its only job is to find defects in the uncommitted change. If the `reviewer`
agent is unavailable, use the general subagent and state the restriction in the
prompt, because the guarantee then comes only from the prompt.

Give the reviewer, in the prompt:

- The issue number, its acceptance criteria, and the approved plan.
- The spec sections and boundaries that apply.
- Where the change lives: `git status --short` plus the output of `git diff`, with
  untracked files read directly from disk.
- The rule that a finding without evidence is not a finding.

Ask for this output, in this order:

1. **Critical** — correctness, lifetime, threading, boundary violation, missing
   parity, or a defect that makes the change wrong.
2. **Design** — the approach is wrong or fights the architecture, with the reason.
3. **Should fix** — real but non-blocking defects, each with `file:line`.
4. **Additional tests** — cases the acceptance criteria imply but the change does
   not cover, each with the criterion it protects.
5. **No findings** when there are none; do not invent issues to look useful.

## 12. Work the review

Triage every finding explicitly:

- **Critical and Design** — fix them. A fix that invalidates the approved plan stops
  the run: present the revised approach, get approval again, and post a follow-up
  comment on the issue before continuing.
- **Should fix** — fix when it is genuinely a defect; otherwise state why it is out
  of scope for this issue.
- **Additional tests** — add them when they cover an acceptance criterion or a bug
  the reviewer demonstrated; this is the cheap place to catch a regression.
- **Nitpicks** — decline with a reason. Do not let the review expand the issue's
  scope.

Re-run the build, `bash build/check-format.sh`, and the test suite after the fixes,
then ask the reviewer to look at the fixes when the change is non-trivial or the
design was involved. Iterate until no critical or design finding remains.

## 13. Gate 3: solution approval

Present the uncommitted solution. The user approves it before anything is committed:

1. What changed, by module, with the spec reference for each.
2. `git diff --stat`, and the untracked files the change adds.
3. How each acceptance criterion is met, and where its test lives.
4. Verification: build result, format result, and the suite's summary line (tests,
   failures, skips).
5. Reviewer outcome: what was fixed, what was declined and why.
6. Open points from the issue and how the implementation resolved them.
7. The draft commit message, so approval covers the message too.

Iterate on the code until the user approves. Gate 3 is explicit, like gate 1.

## 14. Commit and push

After approval, commit on the current branch in the repository's voice: lowercase
prose, one line per changed group, `§N` / `AD-NN` where a decision is cited. Never
commit files the change did not touch, and never amend a commit the user did not ask
to amend.

```sh
git add <paths the change touched>
git commit -m "<message>"
git push -u origin <branch>
```

Then confirm the result with `git status --short` and `git log -1 --stat`, and say
what was pushed and where.

## 15. Gate 4: pull request

Ask whether a pull request should be opened. If yes, target `main` from the branch
that was pushed, and write the body with the issue link and a real summary:

```sh
gh pr create --base main --head <branch> --title "<title>" --body-file <path>
```

Use `Closes #<number>` in the body when the PR is meant to resolve the issue. If the
user declines, stop here and report the pushed branch.

Do not close the issue, merge the PR, or edit anything on GitHub beyond what the
user approved.

## Definition of done

- [ ] The plan was approved explicitly, and the branch choice was made by the user.
- [ ] The approved plan is on the issue, with any later design change recorded.
- [ ] Every acceptance criterion is implemented and has a test.
- [ ] The build is clean, with no new warning.
- [ ] `bash build/check-format.sh` passes.
- [ ] `./.build-debug/tests/albor-tests` was run and passes, with no test skipped.
- [ ] Platform parity is satisfied, or the change is provably portable.
- [ ] `macos-windows.md` is current when a platform source changed.
- [ ] The read-only review ran, and every finding was fixed or declined with a reason.
- [ ] No boundary in `AGENTS.md` was crossed.
- [ ] The uncommitted solution was approved before the commit.
- [ ] The report states what ran, what was skipped, and what was pushed.
