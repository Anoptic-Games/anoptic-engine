<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# Resource manager: start here

Orientation for the resource-manager campaign. Everything below is current as of 2026-07-13

## Status

Branch `feature-resourcemgr`, HEAD `2480020`, the entire campaign uncommitted. The spec is `docs/resourcemanager-comprehensive.md`: Stage A (finish the synchronous manager behind a neutral placement seam), Stage B (all five allocator models fight, one wins), Stage C (lock-free ticket transport). No allocator model has shipped; the live placement is scaffolding named global-pool/scoped-pool and must stay named that until Stage B.

Done so far: the synchronous core (namespace, registry, handles, durable writes, save framing, graphics ingest), the M0 baseline evidence, and the W0/M1 placement freeze — `src/resources/resources_place.h` plus skeleton TUs and contract tests for M2–M17. Milestones M2–M19 of `docs/resourcemgr/phasea/blueprint.md` §6 are open.

Tree is green and verified: Windows Debug 40/40, Windows O3 38/38, WSL TSan 33/33 with zero reports, WSL ASan clean (its 4 failures are headless-GLFW Vulkan artifacts), 18 s engine smoke at ~660–690 fps on an RTX 4090. Cells and raw logs: `docs/resourcemgr/verification-matrix.md` and `docs/resourcemgr/logs/`. Unrun: macOS, the formal 9P-floor recipe.

## Read in this order

1. `docs/resourcemanager-comprehensive.md` — the spec. You have full leeway to make improvements as you see fit.
2. `docs/resourcemgr/phasea/CHECKPOINT.md` — campaign handoff: decisions, live bugs, doc map.
3. `docs/resourcemgr/phasea/blueprint.md` — frozen seams and the M0–M19 sequence.
4. `docs/resourcemgr/verification-matrix.md` — what has actually been run, with logs.
5. `docs/resourcemanager-comprehensive-report.md` — independent audit of the pre-campaign tree, with a same-day resolution postscript.
6. `docs/resourcemgr/resourcemanager-real.md` — superseded, but preserves the owner's directives.
7. `docs/resourcemgr/RESOURCE_MANAGER_IMPL.md` — chronology, not authority. Superseded planning lineage: `docs/.archive/`.

## Traps

- `ano_res_get`'s hit path ignores the requesting lifetime (`resources_registry.c:817`); no second production lifetime domain may open before M8 fixes it.
- Payloads over 1 MiB land on the calling thread's default heap, not the domain heap (`resources_registry.c:180`); until M6, no allocator figure means anything.
- `ano_resgfx_scene` validates array extents only; hostile-block validation is mandatory before any pack loading ships (M11).
- Do not commit without explicit approval. Do not claim a matrix cell without its log. Do not weaken a test to get green. Do not put a model name on an incomplete implementation.

## Your Goal:

Your mission is simple: Complete Phase A. Best of luck, Commander.