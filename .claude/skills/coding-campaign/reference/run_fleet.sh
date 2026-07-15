#!/usr/bin/env bash
# run_fleet.sh — one bounded squad contest. Every IU generates and clears fitness under
# hard deadlines. Results persist per candidate; the first verified Sol pass starts one
# fixed group clock; only fully green candidates can become champion.
set -u -o pipefail

usage() { cat <<'EOF'
usage:  PROMPT_FILE=prompts/parser.txt FITNESS_CMD='bash fit.sh "$1"' \
        WORK=/contest/squad-1 NAME=parser bash run_fleet.sh

required (env)
  PROMPT_FILE       generation prompt; include exact contracts and required tests
  FITNESS_CMD       shell snippet run once per candidate; path arrives as $1; must print
                    'FITNESS pass=X/Y metric=N' or BUILD_FAIL / TIMEOUT / CRASH
  WORK              output root; candidates and incremental results live under $WORK/$NAME

optional (env)                                                   [default]
  NAME / EXT        round label / candidate extension            [round / rs]
  ROUND_TIMEOUT     group deadline when no Sol passes, seconds   [1200]
  FITNESS_TIMEOUT   independent fitness deadline, seconds        [180]
  CLAUDEX           backend CLI                                  [PATH or ~/.local/bin/claudex]

deprecated (ignored)
  SOL LUNA FLEET ALLOW_SMALLER_FLEET SOL_MODEL SOL_EFFORT LUNA_MODEL LUNA_EFFORT

The fleet is always 4 gpt-5.6-sol/high + 6 gpt-5.6-luna/xhigh.

All fleet units launch together. Each IU owns green tests on time. Only the first verified
Sol pass arms the fixed clock: warn all IUs and the SL at four minutes, cull unfinished IUs
at five minutes. Luna results never arm or alter the clock.
EOF
}

case "${1:-}" in -h|-help|--help) usage; exit 0;; esac

fail() { echo "run_fleet.sh: $*" >&2; exit 2; }
positive_int() { [[ "$1" =~ ^[1-9][0-9]*$ ]]; }

for v in PROMPT_FILE FITNESS_CMD WORK; do
  [ -n "${!v:-}" ] || fail "\$$v is not set (run_fleet.sh -h for usage)"
done
[ -r "$PROMPT_FILE" ] || fail "cannot read PROMPT_FILE '$PROMPT_FILE'"
for cmd in timeout ps awk grep sed; do command -v "$cmd" >/dev/null || fail "required command '$cmd' is unavailable"; done

CLAUDEX="${CLAUDEX:-$(command -v claudex || echo "$HOME/.local/bin/claudex")}"
[ -x "$CLAUDEX" ] || fail "backend '$CLAUDEX' not found — install claudex or set CLAUDEX"

NAME="${NAME:-round}" EXT="${EXT:-rs}"
readonly SOL=4 LUNA=6
ROUND_TIMEOUT="${ROUND_TIMEOUT:-1200}" FITNESS_TIMEOUT="${FITNESS_TIMEOUT:-180}"
SOL_PASS_GRACE=300 SOL_PASS_WARNING=240
if [ "${CAMPAIGN_TEST_MODE:-0}" = 1 ]; then
  SOL_PASS_GRACE="${TEST_SOL_PASS_GRACE:-300}" SOL_PASS_WARNING="${TEST_SOL_PASS_WARNING:-240}"
  positive_int "$SOL_PASS_GRACE" || fail "TEST_SOL_PASS_GRACE must be a positive integer"
  positive_int "$SOL_PASS_WARNING" || fail "TEST_SOL_PASS_WARNING must be a positive integer"
  ((SOL_PASS_WARNING < SOL_PASS_GRACE)) || fail "TEST_SOL_PASS_WARNING must be earlier than TEST_SOL_PASS_GRACE"
elif [ -n "${TEST_SOL_PASS_GRACE:-}" ] || [ -n "${TEST_SOL_PASS_WARNING:-}" ]; then
  fail "the five-minute Sol-pass clock is fixed; timing overrides are test-only"
fi
for n in ROUND_TIMEOUT FITNESS_TIMEOUT; do positive_int "${!n}" || fail "$n must be a positive integer"; done

mapfile -t FLEET_ROWS <<'EOF'
01 gpt-5.6-sol high
02 gpt-5.6-sol high
03 gpt-5.6-sol high
04 gpt-5.6-sol high
05 gpt-5.6-luna xhigh
06 gpt-5.6-luna xhigh
07 gpt-5.6-luna xhigh
08 gpt-5.6-luna xhigh
09 gpt-5.6-luna xhigh
10 gpt-5.6-luna xhigh
EOF
readonly FLEET_COUNT=10

ROUND_DIR="$WORK/$NAME"
mkdir -p "$ROUND_DIR"
rmdir "$ROUND_DIR/first_sol_pass.lock" 2>/dev/null || true
rm -f "$ROUND_DIR"/result_*.tsv "$ROUND_DIR"/done_* "$ROUND_DIR"/pid_* \
  "$ROUND_DIR"/cand_*."$EXT" "$ROUND_DIR"/response_*.txt "$ROUND_DIR"/stderr_*.log \
  "$ROUND_DIR"/warning_*.txt "$ROUND_DIR/first_sol_pass" "$ROUND_DIR/group_events.tsv" "$ROUND_DIR/results.tsv"

PROMPT="$(cat "$PROMPT_FILE")"
PROMPT+=$'\n\nCAMPAIGN IU LAW: You own this artifact through green completion. Run the required tests in your isolated environment; reasoning that they ought to pass is not testing. Repair your own work and reach the required passing tally inside the deadline. Never return partial or non-green work for the SL to fix. Return only the requested artifact.\nIDENTICAL PROMPT LAW: Every IU in this workgroup receives these exact prompt bytes. Do not expect role-, index-, or model-specific instructions. Read your isolated output path from CAMPAIGN_CANDIDATE_PATH and the group warning path from CAMPAIGN_WARNING_PATH. Check CAMPAIGN_WARNING_PATH before every test/repair iteration; if the warning file appears, finish and return within the stated final minute.\nINDEPENDENT FITNESS TEMPLATE (the runner will repeat it; substitute your candidate path for $1): '
PROMPT+="$FITNESS_CMD"

export WORK NAME EXT CLAUDEX PROMPT FITNESS_CMD FITNESS_TIMEOUT ROUND_DIR

write_result() {  # idx fitness metric lines role
  local tmp="$ROUND_DIR/result_${1}.tsv.tmp.$$"
  printf '%s\t%s\tmetric=%s\t%sL\n' "$1" "$2" "$3" "$4" > "$tmp"
  mv -f "$tmp" "$ROUND_DIR/result_${1}.tsv"
  mark_first_sol_pass "$1" "$2" "${5:-}"
}
export -f write_result

mark_first_sol_pass() {  # idx fitness role
  local passed total
  [ "$3" = Sol ] || return 0
  [[ "$2" =~ ^pass=([0-9]+)/([0-9]+)$ ]] || return 0
  passed=${BASH_REMATCH[1]} total=${BASH_REMATCH[2]}
  ((total > 0 && passed == total)) || return 0
  if mkdir "$ROUND_DIR/first_sol_pass.lock" 2>/dev/null; then
    printf '%s\t%s\n' "$(date +%s)" "$1" > "$ROUND_DIR/first_sol_pass"
  fi
}
export -f mark_first_sol_pass

gen_and_fit() {  # idx model effort role
  local idx="$1" model="$2" effort="$3" role="$4"
  local out="$ROUND_DIR/cand_${idx}.$EXT" raw="$ROUND_DIR/response_${idx}.txt" log="$ROUND_DIR/stderr_${idx}.log"
  local fit pass met status lines gen_rc fit_rc
  echo "$BASHPID" > "$ROUND_DIR/pid_${idx}"

  CAMPAIGN_CANDIDATE_PATH="$out" CAMPAIGN_WARNING_PATH="$ROUND_DIR/warning_${idx}.txt" \
    "$CLAUDEX" -p --model "$model" --effort "$effort" "$PROMPT" > "$raw" 2> "$log"
  gen_rc=$?
  if [ ! -s "$out" ]; then
    grep -vi 'connectors are disabled' < "$raw" | sed '/^```/d' > "$out"
  fi
  lines=$(wc -l < "$out" | tr -d ' ')

  if ((gen_rc != 0)); then
    if ((gen_rc == 124 || gen_rc == 137)); then status=TIMEOUT; else status=CRASH; fi
    write_result "$idx" "$status" NA "$lines" "$role"
    : > "$ROUND_DIR/done_${idx}"
    return
  fi
  if [ ! -s "$out" ]; then
    write_result "$idx" NO_OUTPUT NA 0 "$role"
    : > "$ROUND_DIR/done_${idx}"
    return
  fi

  fit=$(timeout --signal=TERM --kill-after=5 "$FITNESS_TIMEOUT" \
    bash -c "$FITNESS_CMD" fitness "$out" 2>&1)
  fit_rc=$?
  pass=$(grep -oE 'pass=[0-9]+/[0-9]+' <<<"$fit" | head -1)
  met=$(grep -oE '(metric|reduction)=[0-9]+' <<<"$fit" | head -1 | grep -oE '[0-9]+$')

  if ((fit_rc == 124 || fit_rc == 137)); then
    write_result "$idx" TIMEOUT "${met:-NA}" "$lines" "$role"
  elif ((fit_rc != 0)); then
    write_result "$idx" CRASH "${met:-NA}" "$lines" "$role"
  elif [ -n "$pass" ]; then
    write_result "$idx" "$pass" "${met:-NA}" "$lines" "$role"
  else
    status=$(awk '{print $1; exit}' <<<"$fit")
    write_result "$idx" "${status:-NO_OUTPUT}" "${met:-NA}" "$lines" "$role"
  fi
  : > "$ROUND_DIR/done_${idx}"
}
export -f gen_and_fit

is_running() {
  jobs -pr | grep -qx "$1"
}

kill_tree() {  # signal pid
  local signal="$1" pid="$2" child
  while read -r child; do [ -n "$child" ] && kill_tree "$signal" "$child"; done < <(ps -ef | awk -v parent="$pid" 'NR > 1 && $3 == parent { print $2 }')
  kill "-$signal" "$pid" 2>/dev/null || true
}

declare -A PID_TO_IDX
echo "[$WORK $NAME] commissioning $FLEET_COUNT candidates (Sol=$SOL Luna=$LUNA, round=${ROUND_TIMEOUT}s fitness=${FITNESS_TIMEOUT}s Sol-pass clock=${SOL_PASS_GRACE}s) ..."
ordinal=0
for row in "${FLEET_ROWS[@]}"; do
  read -r idx model effort <<<"$row"
  if ((ordinal < SOL)); then role=Sol; else role=Luna; fi
  gen_and_fit "$idx" "$model" "$effort" "$role" &
  PID_TO_IDX[$!]="$idx"
  ((ordinal += 1))
done

deadline=0
warning_at=0
warning_issued=0
round_started=$(date +%s)
while ((${#PID_TO_IDX[@]})); do
  for pid in "${!PID_TO_IDX[@]}"; do
    if ! is_running "$pid"; then
      wait "$pid" 2>/dev/null || true
      unset 'PID_TO_IDX[$pid]'
    fi
  done

  if ((deadline == 0)) && [ -s "$ROUND_DIR/first_sol_pass" ]; then
    IFS=$'\t' read -r first_sol_pass trigger_sol < "$ROUND_DIR/first_sol_pass"
    warning_at=$((first_sol_pass + SOL_PASS_WARNING))
    deadline=$((first_sol_pass + SOL_PASS_GRACE))
    echo "[$WORK $NAME] Sol $trigger_sol independently passed; group warning at epoch $warning_at, cull at epoch $deadline"
  fi

  now=$(date +%s)
  if ((deadline > 0 && warning_issued == 0 && now >= warning_at)); then
    message='ONE-MINUTE WARNING: a Sol passed four minutes ago; every unfinished IU must complete within one minute or be culled.'
    echo "[$WORK $NAME] $message"
    printf '%s\tSOL_PASS_ONE_MINUTE_WARNING\t%s\n' "$now" "$message" >> "$ROUND_DIR/group_events.tsv"
    for row in "${FLEET_ROWS[@]}"; do
      read -r idx _ <<<"$row"
      printf '%s\n' "$message" > "$ROUND_DIR/warning_${idx}.txt.tmp.$$"
      mv -f "$ROUND_DIR/warning_${idx}.txt.tmp.$$" "$ROUND_DIR/warning_${idx}.txt"
    done
    warning_issued=1
  fi

  round_expired=0
  ((deadline == 0 && now >= round_started + ROUND_TIMEOUT)) && round_expired=1
  if ((round_expired)); then
    echo "[$WORK $NAME] no Sol passed before the group round deadline; culling ${#PID_TO_IDX[@]} unfinished IUs"
  fi
  if ((deadline > 0 && now >= deadline || round_expired)) && ((${#PID_TO_IDX[@]})); then
    ((round_expired)) || echo "[$WORK $NAME] five-minute Sol-pass deadline reached; culling ${#PID_TO_IDX[@]} unfinished IUs"
    for pid in "${!PID_TO_IDX[@]}"; do kill_tree TERM "$pid"; done
    sleep 2
    for pid in "${!PID_TO_IDX[@]}"; do
      is_running "$pid" && kill_tree KILL "$pid"
      wait "$pid" 2>/dev/null || true
      idx=${PID_TO_IDX[$pid]}
      [ -s "$ROUND_DIR/result_${idx}.tsv" ] || write_result "$idx" CULLED NA 0
      unset 'PID_TO_IDX[$pid]'
    done
    break
  fi

  ((${#PID_TO_IDX[@]})) && sleep 1
done

: > "$ROUND_DIR/results.tsv"
for row in "${FLEET_ROWS[@]}"; do
  read -r idx _ <<<"$row"
  [ -s "$ROUND_DIR/result_${idx}.tsv" ] || write_result "$idx" NO_RESULT NA 0
  cat "$ROUND_DIR/result_${idx}.tsv" >> "$ROUND_DIR/results.tsv"
done

best=$(awk -F'\t' '$2 ~ /^pass=/ {split($2,p,"[=/]"); split($3,m,"="); sub(/L$/,"",$4);
  if ((p[2]+0) == (p[3]+0) && (p[3]+0) > 0) print (p[2]+0)"\t"(m[2]+0)"\t"(-$4)"\t"$1}' \
  "$ROUND_DIR/results.tsv" | sort -k1,1nr -k2,2nr -k3,3nr | head -1 | cut -f4)

echo "[$WORK $NAME] ranked results:"
if command -v column >/dev/null; then column -t -s $'\t' < "$ROUND_DIR/results.tsv"; else cat "$ROUND_DIR/results.tsv"; fi
[ -n "$best" ] || { echo "[$WORK $NAME] no fully passing candidate; no champion" >&2; exit 1; }

cp "$ROUND_DIR/cand_${best}.$EXT" "$WORK/champion_$NAME.$EXT"
echo "PROVISIONAL_WINNER=$best  (champion at $WORK/champion_$NAME.$EXT)"
