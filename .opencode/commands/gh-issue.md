---
description: Implement GitHub issue <number> through approval gates
subagent: false
---

Implement GitHub issue $1 in this repository, following the `gh-issue` skill.

If $1 is empty or not an issue number, ask for the number and stop.

Call the `skill` tool with id `gh-issue`, then follow its workflow exactly. The four
gates belong to me and are never passed on my behalf:

1. Plan approval — draft the plan, present it, iterate with me until I say it is
   approved, then post it as a comment on the issue.
2. Branch — ask whether to work on a new branch or the current one, and wait until
   the branch exists.
3. Solution approval — after implementation, a read-only reviewer subagent, and the
   fixes it triggers, present the uncommitted solution and wait for my approval.
4. Pull request — after commit and push, ask whether to open one.

Stop at each gate and wait. Before gate 3, the build, `bash build/check-format.sh`,
and the full unit test suite (`./.build-debug/tests/albor-tests`) must pass. Do not
commit before I approve the solution, and do not open a PR before I ask for one.
