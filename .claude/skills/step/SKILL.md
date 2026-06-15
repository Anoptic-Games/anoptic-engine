---
name: step
description: Implement one step from the build sequence in docs/notes.md
argument-hint: "[step number]"
---
Read docs/notes.md, find Step $ARGUMENTS. Restate the acceptance criteria as a
checklist. Present an implementation plan and WAIT for approval.
After approval: implement, test, run @tsan-runner if concurrent code was
touched, then show `git diff --stat` and the checklist with pass/fail.
Anything out of scope goes in a "Deferred findings" list at the end.