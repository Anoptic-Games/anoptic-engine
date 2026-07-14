#!/usr/bin/env bash
# run_stage.sh — one supervisor agent, one generation. A thin stage adapter over the
# generic fleet runner: picks the stage prompt, wires compose_and_test.sh around the
# frozen substrate as the fitness command, and delegates commissioning/scoring/ranking
# to run_fleet.sh. The Claude supervisor that invokes this then reviews results.tsv
# and may override the champion on quality.
set -u

usage() { cat <<'EOF'
usage:  CONTEST_ROOT=/harness WORK=/contest/agent-N  bash run_stage.sh <A|B|C>

  CONTEST_ROOT  holds prelude.rs, testmain.rs, compose_and_test.sh, prompts/prompt_<SLOT>.txt,
                and substrate/{A,B,C}.rs — seed substrate/ first: goldens for a fresh
                contest, or orchestrator/substrate/ to rebuild against the frozen winners
  WORK          this agent's output dir: candidates in $WORK/<SLOT>/, results.tsv beside
                them, champion at $WORK/champion_<SLOT>.rs
  RUN_FLEET     path to run_fleet.sh [auto-found beside this script, in the skill's
                reference/, or in .claude/tools/]
  fleet         hard-fired by run_fleet.sh: 4 Sol/high + 6 Luna/xhigh
  runner knobs  CLAUDEX and deadlines: run_fleet.sh -h
EOF
}
case "${1:-}" in
  A|B|C) SLOT=$1;;
  -h|--help) usage; exit 0;;
  *) echo "run_stage.sh: expected stage A, B, or C" >&2; usage >&2; exit 2;;
esac
ROOT="${CONTEST_ROOT:?set CONTEST_ROOT to the harness dir (run_stage.sh -h for usage)}"
: "${WORK:?set WORK to the output dir of this agent (run_stage.sh -h for usage)}"

for s in A B C; do
  [ "$s" = "$SLOT" ] || [ -f "$ROOT/substrate/$s.rs" ] || {
    echo "run_stage.sh: missing $ROOT/substrate/$s.rs — seed substrate/ first (see run_stage.sh -h)" >&2; exit 2; }
done

here=$(cd "$(dirname "$0")" && pwd)
for f in "${RUN_FLEET:-}" "$here/run_fleet.sh" "$here/../../reference/run_fleet.sh" "$here/../.claude/tools/run_fleet.sh"; do
  [ -n "$f" ] && [ -f "$f" ] && { RUN_FLEET=$f; break; }
done
[ -f "${RUN_FLEET:-}" ] || { echo "run_stage.sh: run_fleet.sh not found — set RUN_FLEET to its path" >&2; exit 2; }

export ROOT
case $SLOT in
  A) FITNESS_CMD='CONTEST_ROOT="$ROOT" bash "$ROOT/compose_and_test.sh" "$1" "$ROOT/substrate/B.rs" "$ROOT/substrate/C.rs"';;
  B) FITNESS_CMD='CONTEST_ROOT="$ROOT" bash "$ROOT/compose_and_test.sh" "$ROOT/substrate/A.rs" "$1" "$ROOT/substrate/C.rs"';;
  C) FITNESS_CMD='CONTEST_ROOT="$ROOT" bash "$ROOT/compose_and_test.sh" "$ROOT/substrate/A.rs" "$ROOT/substrate/B.rs" "$1"';;
esac

PROMPT_FILE="$ROOT/prompts/prompt_$SLOT.txt" FITNESS_CMD="$FITNESS_CMD" NAME="$SLOT" EXT=rs \
  exec bash "$RUN_FLEET"
