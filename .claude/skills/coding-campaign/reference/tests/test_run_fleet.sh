#!/usr/bin/env bash
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
runner=$(cd "$here/.." && pwd)/run_fleet.sh
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

bash "$runner" -help | grep -q 'deprecated (ignored)'

PROMPT_FILE="$here/prompt.txt" \
FITNESS_CMD="bash \"$here/fitness.sh\" \"\$1\"" \
WORK="$work" NAME=self EXT=txt CLAUDEX="$here/fake_claudex.sh" \
ROUND_TIMEOUT=20 FITNESS_TIMEOUT=5 FAKE_SCENARIO=deadline \
SOL=99 LUNA=1 FLEET=garbage ALLOW_SMALLER_FLEET=nonsense \
SOL_MODEL=ignored SOL_EFFORT=ignored LUNA_MODEL=ignored LUNA_EFFORT=ignored \
CAMPAIGN_TEST_MODE=1 TEST_SOL_PASS_GRACE=3 TEST_SOL_PASS_WARNING=2 \
  bash "$runner"

results="$work/self/results.tsv"
test "$(wc -l < "$results")" -eq 10
grep -q $'^01\tpass=1/1\t' "$results"
grep -q $'^02\tpass=0/1\t' "$results"
for idx in {03..10}; do grep -q "^${idx}"$'\tCULLED\t' "$results"; done
grep -q '^PASS$' "$work/champion_self.txt"
test -s "$work/self/first_sol_pass"
for idx in {01..10}; do test -s "$work/self/warning_${idx}.txt"; done
grep -q 'SOL_PASS_ONE_MINUTE_WARNING' "$work/self/group_events.tsv"

PROMPT_FILE="$here/prompt.txt" FITNESS_CMD="bash \"$here/fitness.sh\" \"\$1\"" \
  WORK="$work/luna-trigger" NAME=self EXT=txt CLAUDEX="$here/fake_claudex.sh" \
  ROUND_TIMEOUT=20 FITNESS_TIMEOUT=5 FAKE_SCENARIO=luna-trigger \
  CAMPAIGN_TEST_MODE=1 TEST_SOL_PASS_GRACE=3 TEST_SOL_PASS_WARNING=2 \
  bash "$runner"
test "$(wc -l < "$work/luna-trigger/self/results.tsv")" -eq 10
grep -q $'^05\tpass=1/1\t' "$work/luna-trigger/self/results.tsv"
test ! -e "$work/luna-trigger/self/first_sol_pass"
test ! -e "$work/luna-trigger/self/group_events.tsv"

if PROMPT_FILE="$here/prompt.txt" FITNESS_CMD='sleep 8; echo "FITNESS pass=1/1 metric=1"' \
  WORK="$work/fitness" NAME=self EXT=txt CLAUDEX="$here/fake_claudex.sh" \
  ROUND_TIMEOUT=20 FITNESS_TIMEOUT=1 FAKE_SCENARIO=all-pass \
  CAMPAIGN_TEST_MODE=1 TEST_SOL_PASS_GRACE=3 TEST_SOL_PASS_WARNING=2 \
  bash "$runner" >/dev/null 2>&1; then
  echo 'expected timed-out fitness to prevent a champion' >&2
  exit 1
fi
test "$(grep -c $'\tTIMEOUT\t' "$work/fitness/self/results.tsv")" -eq 10

echo 'run_fleet.sh self-test passed'
