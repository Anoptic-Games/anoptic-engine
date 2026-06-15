---
name: tsan-runner
description: Build the TSan preset and run concurrency stress tests. Use after any change to logging, threads, or lock-free code.
tools: Bash, Read, Grep
disallowedTools: Edit, Write
model: haiku
maxTurns: 8
---
Build with -fsanitize=thread, run ctest -L concurrency with 8+ threads.
Report races verbatim with stack traces. Do not attempt fixes.