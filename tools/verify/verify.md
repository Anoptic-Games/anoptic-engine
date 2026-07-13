# tools/verify

Owned by W12. The evidence harness: it runs the verification matrix cells and writes the artifacts nobody may quote a number without.

TODO(W12, M18):

- `matrix.py` — drive every cell of `docs/resourcemgr/verification-matrix.md` (native Windows, WSL Linux, WSL ASan/UBSan, WSL TSan, the 9P remote-FS floor) and stamp each result with commit sha, platform, profile, and the raw log path. macOS is recorded UNRUN with exact repro commands and is never claimed green.
- `bench.py` — run a preregistered scenario against a named placement model and write `docs/benchmarks/<stamp>-<model>-<scenario>.json`: raw per-op samples, the full five-axis cube before and after each phase, the corpus manifest, the run count, and the exact repro command. A run whose `tel_overflow_hits != 0` is VOID and is written as such.
- `grep_gate.py` — the deletion sweep's gate, with its written allow-list: `tools/gen_unicode_tables.c` is build-time codegen and not an asset importer, and the test oracles' raw `fopen` is precisely what gives them the power to falsify the manager.

A disabled benchmark plus prose is not evidence.
