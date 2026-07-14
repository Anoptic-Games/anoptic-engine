#!/usr/bin/env bash
# compose_and_test.sh — compose prelude + mod a{A} + mod b{B} + mod c{C} + testmain,
# compile, and run the fitness battery. One candidate occupies its stage's slot; the
# other two slots hold golden or frozen-winner sources. Module wrapping isolates each
# stage's `use` imports so two independently generated winners never collide (E0252).
set -u

usage() { cat <<'EOF'
usage:  CONTEST_ROOT=/harness  bash compose_and_test.sh <A.rs> <B.rs> <C.rs>

  CONTEST_ROOT  holds prelude.rs and testmain.rs; rustc must be on PATH
  emits one line:  FITNESS pass=X/N in_ops=.. out_ops=.. reduction=..
                   or BUILD_FAIL | TIMEOUT | CRASH rc=N
EOF
}
case "${1:-}" in -h|--help) usage; exit 0;; esac
[ $# -eq 3 ] || { usage >&2; exit 2; }
ROOT="${CONTEST_ROOT:?set CONTEST_ROOT to the harness dir (holds prelude.rs, testmain.rs)}"
command -v rustc >/dev/null || { echo "compose_and_test.sh: rustc not on PATH" >&2; exit 2; }

work=$(mktemp -d "${TMPDIR:-/tmp}/vmcomp.XXXXXX"); trap 'rm -rf "$work"' EXIT
{
  cat "$ROOT/prelude.rs"
  echo 'mod a { #[allow(unused_imports)] use super::Op::{self, *};'; cat "$1"; echo '}'
  echo 'mod b { #[allow(unused_imports)] use super::Op::{self, *};'; cat "$2"; echo '}'
  echo 'mod c { #[allow(unused_imports)] use super::Op::{self, *};'; cat "$3"; echo '}'
  cat "$ROOT/testmain.rs"
} > "$work/m.rs"

rustc --edition 2021 -O -A warnings -o "$work/bin" "$work/m.rs" 2>"$work/err" \
  || { echo BUILD_FAIL; exit 0; }
out=$(timeout 5 "$work/bin" 2>/dev/null); rc=$?
if [ $rc -eq 124 ]; then echo TIMEOUT
elif grep -q FITNESS <<<"$out"; then echo "$out"
else echo "CRASH rc=$rc"
fi
