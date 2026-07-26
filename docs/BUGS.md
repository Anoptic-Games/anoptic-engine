# Bugs!

Grouped by: 
- Module / Subsystem (see docs/conventions.md for a definition)
-- Within each module: category.

## Tags

Root-cause tags sit on the bullet line(s) under each entry. Three dominate; the rest are shared shapes or singletons.

`[X] Fixed` keeps the original text plus a `fix (date)` bullet. `wontfix (date)` and `fenced (date)` stay unremediated (reason on that bullet). None of the three is a tag; all are excluded from tag counts.

- checked-arithmetic: integer wrap (mul/add/sub) slips past its guard or allocation sizing; one shared helper retires the family.
- no-abort: a failure that should stop does not (ANO_FATAL logs and returns, callee reports success, or uninitialised out-param); caller's error arm is dead.
- seam-validation: a value crossing a documented seam (public API, bridge, config) is trusted or used as an index without a domain check.
- ownership-leak: a resource acquired earlier is never discharged on failure or teardown; discharge-side facet of the seam root cause.
- fixed-array-overflow: an unbounded counter or index writes past a fixed-size stack array while a sibling bound is guarded.
- partial-publish: a batch publishes per-element state as it walks, then fails mid-batch with only a scalar return; prefix stays live and aliasable.
- copy-paste-error: code cloned from a sibling with one token left unchanged, or a duplicated argument or predicate.
- feature-list-drift: a per-feature resource property in a hand-written parallel list, present for every sibling but one.
- wrong-error-source: a guard or return consults the wrong error indicator (wrong sentinel, or errno where the callee reports by return).
- feature-gated-check: a correctness check runs only when an unrelated optional feature flag is on.
- shift-ub: `1 << i` shifts into the sign bit of int at the top of a legal index domain.
- recovery-desync: device-loss recovery restarts playback without resetting the state the restart invalidates.
- odd-sibling-out: one of several sibling implementations omits a guard or ordering the others share.
- lookahead-off-by-one: paired window and delay length differ by one at a lookahead seam; coverage expires one step before the guarded sample is consumed.
- unbounded-spin: a loop or wait with no forward-progress or termination guard spins or hangs forever.
- dangling-capture: a deferred record stores a caller pointer and dereferences it after the caller may have freed it, against a one-sided lifetime contract.
- size-mismatch: a buffer is provisioned by the wrong metric (stored footprint vs rendered width) and the shortfall subtraction underflows.
- unguarded-delegation: a header contract is delegated straight to a third-party allocator that does not honour it.
- truncating-cast: a narrowing cast drops the high bits of a duration or size, silently changing the operation.
- retry-exhaustion: a bounded retry loop's exhaustion arm is indistinguishable from success and consumes an out-param the failed call left unwritten.
- clock-not-reanchored: a clock source is not re-based after sleep/hibernate resets it; monotonic contract breaks and deltas across the transition wrap.
- noop-not-honored: a documented no-op input still changes the output when it lands in a boundary position the code consumes unconditionally.
- shared-mutable-state: a function-scope static or global is reused across independent contexts (concurrent engines, reentrant calls); accesses race or clobber.
- missed-repoint: a resource is recreated but one descriptor set or reference is not updated; live binding to a destroyed handle.
- table-coverage-gap: two generated lookup tables that must agree on coverage diverge; some inputs fall through to a default path.
- alignment-contract-gap: a type's declared alignment is weaker than the layout contract its own header documents; nothing enforces the guarantee callers and the GPU mirror rely on.
- partial-out-param: a fallible call's failure arm writes some out-params and leaves others untouched; caller cannot tell what it owns. Discharge-side twin of no-abort (named 2026-07-25).
- silent-drop: an out-of-contract or unsatisfiable request is neither honored nor refused; discarded with no diagnostic, so the producer believes it took effect.

One status tag rides alongside the root causes:

- pending-design-decision: defect and root cause are tagged, but repair is blocked on a contract choice with more than one defensible answer. Never the only tag. Historical carriers under docs/BUGS_DONE.md, "Settled open decisions". Excluded from root-cause tag counts. Currently marks nothing. Open-entry fix write-ups: docs/BUG-HUNT.md, "Fixes for open entries", keyed by file:line.


## Audio

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Collections

(anoptic_collections.h is an empty placeholder as of 2026-07-17 〜 no declarations, no src/ module; nothing to audit yet)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Filesystem

### Interface-level bugs and logic inefficiencies

### Implementation bugs

filesystem_win64.c:33: ano_fs_gamepath rejects on WHOLE executable path length before trimming the file name (:33 tests GetModuleFileNameA's raw return against MAXPATH; trim at :37-41). POSIX siblings trim first, then test the directory (filesystem_linux.c:39-44, filesystem_macos.c:43-48).
MAXPATH is 256 (anoptic_filesystem.h:16), MAX_PATH is 260; win64 refuses the whole legal non-truncating band 256-259 regardless of directory length, and has no post-trim check (:43-46 copies unguarded). Refusal is length 0 ("unresolved", anoptic_filesystem.h:32-33).
Consumers degrade fully: openEngineFile NULL on zero-length dir (pipeline.c:30-32) so every SPIR-V load fails (flat.c:87/:91/:99, compute.c:95-:463, additive.c:75/:77, shadow_pipe.c:60); ano_fs_logpath {0} (filesystem.c:48-50) leaves g_outFile NULL (log_core.c:857-859, console-only :446-451); log_crash.c:34-40 drops to `<stamp>_CRASH.log` in launch CWD; text_raster.c:544-546 disables overlay on all-zero font path; ano_fs_chdir_gamepath false (filesystem.c:71-72) leaves assets on launch CWD (main.c:1004-1006).
Reachable on Windows installs four characters below the ANSI path ceiling only; same depth boots on linux/macos. No caller compensates. logged 2026-07-26. test: none (win64-only, needs 256-259 char install path; no Windows runner).
- odd-sibling-out

### Interlink / Composition bugs 



## Log (including log_crash.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Math

(anoptic_math.h is types-only as of 2026-07-17 〜 mat4/Vector2/3/4 PODs, no ops, no src/ module; ops live in render/vertex.h. Composition audits belong to the render_bridge and vulkan_backend interlink edges.)

### Interface-level bugs and logic inefficiencies

(The former lead "anoptic_math.h:16 vs docs/math-conventions.md 〜 one of the two contracts is lying" is struck as of 2026-07-24: the doc was right and the header was wrong. `translate` writes the translation to `mat[3][0..2]`, `multiplyMat4` documents `temp[col][row] += a[k][row] * b[col][k]`, and `perspective` sets `matrix[2][3] = -1` 〜 all column-major, matching GLSL's mat4 so uploads need no transpose. The header comment now says so. No code changed.)

### Implementation bugs

### Interlink / Composition bugs 



## Memory

### Interface-level bugs and logic inefficiencies

### Implementation bugs

memory.c:9: ano_heap_release destroys *in with no NULL guard. Every LOCALHEAPATTR site carries a live mi_heap_new()==NULL arm (text_bake.c:530-532 ENOMEM; ano_GltfParser.c:197-202 logs+NULL; audio_mixer.c:703-705 false; ano_meshoptimizer.c:125-126 return, :704-707 identity copy).
Cleanup attribute (anoptic_memory.h:41, contract :37-38) then fires ano_heap_release on that NULL; mi_heap_destroy asserts heap != NULL (mimalloc heap.c:389) before its own NULL early-out (:393); _mi_assert_fail aborts (mimalloc options.c:546). mi_assert live at MI_DEBUG>0 (internal.h:329-334); CMakeLists.txt:315-319 sets MI_DEBUG=2 for Debug (build.sh 2/4/5/6/7). Release (MI_DEBUG=0) no-ops at heap.c:393.
Reachable when backing heap cannot serve sizeof(mi_heap_t) (heap.c:247). Remedy: `if (*in) mi_heap_destroy(*in);`. Overflow-canary half of the source lead refuted: no scoped-heap pointer escapes at the five sites. logged 2026-07-26. test: pending
- unguarded-delegation
- seam-validation

### Interlink / Composition bugs 



## Mesh

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Music

### Interface-level bugs and logic inefficiencies

### Implementation bugs

music_host.c:45: cadence_ok's lower bound is ANO_CADENCE_NONE, so the one AnoCadencePolicy that is not an index into the [3]-wide cadence tables is admitted. ano_music_set_override(e, "cadence_policy", -1.0) passes :238, pins -1 (:239); policy_of returns it (music_conductor.c:132-133); gen_chord hands it to ano_next_chord (music_conductor.c:576-577); CADENCE_TARGET[-1] / PRE_CADENCE_FUNCTION[-1] (music_harmony.c:119/:130) read before their [3] defs (:41-42).
Slot by position: CADENCE at bars-1, PRE_CADENCE at bars-2 (music_gen.c:100-103); phrase index non-negative (music_form.c:28-46); lament/held arms need OPEN/FREE (music_conductor.c:556-571). Last two bars of every phrase OOB while the pin stands. Value becomes AnoChord.degree uint8_t (music_theory.h:74-87), survives plan_inversion on non-FREE (music_conductor.c:442-450), stored as prevChord (:584); next bar's ano_chord_function subscripts FUNCTION_OF_DEGREE[8] with degree up to 255 (music_theory.c:150/:161 via music_harmony.c:136).
Second ingress: expand degrades out-of-contract cycle entries to ANO_CADENCE_NONE (:100-103); policy_of subscripts with no sentinel test (music_conductor.c:146-147); dramaturg arm alone tests (:134-137); ano_pick_cadence_policy returns 0..2 only (music_control.c:230-237). No in-tree pin; ACMD_MUSIC_OVERRIDE forwards verbatim (ano_synth.c:704-705). anotest_music.c domain walk pins 3.0 and -3.0 (refused :136/:144); -1.0 is the hole. logged 2026-07-26. test: pending
- seam-validation
- odd-sibling-out

music_host.c:232: OV_REGISTER casts override double to int with no domain check; siblings at :238/:242 validate first. Bridge clamps registerCenter to 0..127 (music_ir.c:115-117). Pin flows to p.registerCenter (music_conductor.c:652-653); melody window lo/hi = registerCenter ± rangeSemitones (music_melody.c:707-708, twins music_conductor.c:1068-1069/:1227); only bound on placed pitch (mel_place clamps into [lo,hi], music_melody.c:198-201); mel_event writes (uint8_t)p unclamped (:382).
Pin "register_center" 300.0: notes near 300 truncate mod 256 into legal band. 200.0: 188..212 in-type; ano_synth_live_bar total guard drops every event (ano_synth.c:429-431), silences melody with no diagnostic. Both violate AnoNoteEvent MIDI 0..127 (anoptic_music.h:40-47); both publish registerCenter 127 back (music_host.c:284 via music_ir.c:115-117). Config path stays in-type (expand copies uint8_t :122). accentDepth twin at :231 defended by clamp_velocity 1..127 (music_modifiers.c:139-147/:16-20); escalate clamps velocityCenter not accentDepth (music_conductor.c:669-676). No in-tree pin; ACMD_MUSIC_OVERRIDE forwards verbatim (ano_synth.c:704-705). logged 2026-07-26. test: pending
- seam-validation
- odd-sibling-out

music_host.c:226: OV_TEMPO stores override double verbatim; nothing tests it before use. mapped_params takes it as bar-0 snap and slew goal (music_conductor.c:597-598/:627), bypassing ano_map_tempo's [60,160] clamp (music_control.c:92-96/:38). ano_music_slew lands on goal under tempoSlewPerBeat (music_control.c:246-249; 2.0/beat via :38 → ano_mapping_table_electronic :80, main.c:328-329). p.tempoBpm rounds it (:646); bridge copies unclamped (music_ir.c:105); host publishes with per-beat points (music_host.c:284/:288-289).
ano_music_set_override(e, "tempo_bpm", 0.0) slews to 0.0; ano_synth_live_bar does barSeconds = barQuarters * 60.0 / 0.0 = +inf (ano_synth.c:423); clock_add stores 0-bpm anchor (:155-171/:166); clock_time_at returns +inf (:180/:183); frame stamp `(uint64_t)(inf * sampleRate)` is out-of-range double-to-integer conversion UB (:421-422) every bar after. +inf also clears positivity gate at :819 into ano_dsp_asr_init (:823-824); offline twin divides identically (:318). Negative pin walks past 0 and stamps backwards through the same conversion.
Omission is per-site: ano_synth_score_begin refuses barQuarters <= 0 (:192-193); delay lane floors with fmaxf((float)p->tempoBpm, 30.0f) (:1115). Engine runs mapper path (main.c:328); ACMD_MUSIC_OVERRIDE forwards verbatim (ano_synth.c:704-705). logged 2026-07-26. test: pending
- seam-validation
- odd-sibling-out


### Interlink / Composition bugs 



## Render / Vulkan backend

### Interface-level bugs and logic inefficiencies

### Implementation bugs

shadow_resources.c:22: createShadowResources has sixteen `return false` arms and discharges nothing acquired above them. :43/:46/:47 leave frame i's frustumBuffer plus earlier frames' pairs; :69-:88 leave m==0 moment image, GpuAllocation, array view, up to ANO_SHADOW_ATLAS_LAYERS layer views plus all MAX_FRAMES_IN_FLIGHT frustum/sampleVP pairs; :148-:159 leave both moment images and every view; :165-:168 leave the whole shadow rig plus first slot_upload_create device allocation.
Caller treats false as init failed and unwinds only state it can see; half-written frames[i].shadow / state->shadow* hold created-but-unregistered handles. Boot-only; every arm a driver/host refusal. ANO_FATAL/ANO_ERROR are diagnostic-only; init failure should full-unwind own acquisitions. logged 2026-07-25 (rounds 2 and 4); round-4 status fix at :102-104 touched a distinct defect, added no discharge. test: pending. Complete fix: one labelled unwind or acquisition ledger over all sixteen arms, with transient CB and SlotUploads refusal policy.
- ownership-leak

instance.c:192: getRequiredExtensions mi_strdup's each extension name (GLFW loop :192; debug-utils :196 under DEBUG_BUILD; portability :200 under __APPLE__) and returns only the mi_calloc'd pointer array (:187/:205). Sole caller createInstance (:56) frees the array only on both arms (:85/:93); name blocks leak for process life. Two on Linux release (glfwGetRequiredInstanceExtensions NULL or 2, external/glfw/src/vulkan.c:242); three under either conditional; four on Darwin debug.
Allocator domain consistent (instance.c:12 → mi_strdup/mi_calloc/mi_free). One call site; reached once per boot (vulkanMaster.c:410) and once per registered Vulkan test. Copies buy nothing: GLFW owns its array until glfwTerminate; other two are literals; ppEnabledExtensionNames (:59) live only for vkCreateInstance (:81). Repair: drop the three strdups, keep the array. logged 2026-07-26. test: pending
- ownership-leak

ano_GltfParser.c:236: parseGltf skip arm publishes no refusal. prim_accessors false for missing POSITION or `indices` (:80); arm ANO_WARNs and continues (:235-:236); sole writer of geometryPoolIndex is the post-upload line skipped (:281), so field stays calloc zero. Zero IS FALLBACK_MESH_INDEX (anoptic_render.h:122), magenta/black cube slot 0 (scene_buffers.c:425-:436), not ANO_RENDER_NO_MESH (:118) which cull skips (:116-:117).
Skipped primitive still gets a material row (:501/:776) and is flattened (:843); main.c:64 forwards mesh_index into RCMD_CREATE → fallback cube at that transform. Legal glTF 2.0: `indices` optional; cgltf_validate never demands POSITION (cgltf.h:1707-:1720). Shipped assets all indexed (render_api.c:137/:141/:146). Sibling half discharged: geometry_pool_upload_chain answers ANO_MESH_NONE on total refusal (geometry.c:548) and publishes truncated chain length (geometry.c:545-546). logged 2026-07-26. test: pending
- silent-drop

### Interlink / Composition bugs 



## Strings (including strings_utf.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_strings_collate.c:504: both allocation-failure sort fallbacks abandon documented stability. anostr_sort "Stable (equal strings keep their order)" (anoptic_strings_utf.h:98); anostr_sym_sort "Stable." (:104). Scratch path honours it (strict→insertion :287-:288, stable radix :301, tie_insertion :337, idx tie-break :368, tie_msd allSame early-out :424-:428).
On mi_malloc refusal of the 32-byte-per-item record, :533-:537 and :599-:604 hand the array to qsort. fb_sym_cmp_ (:520-:527) settles ties on numeric symbol value (total order); anostr_sym_sort of out-of-range symbols-as-empty (anoptic_strings_utf.h:106, ano_strings_intern.c:127-:128) comes back reversed under refusal, untouched without it. collate_qsort (:504-:507) omits tie-break; equal elements move however qsort pleases. fb_order_cmp_ (:509-:516) stays stable because the element is the index. anostr_collate is 0 for byte-equal values (:127-:128); anostr_sort movers are byte-equal long strings with distinct backings (ptr variant, anoptic_strings.h:44-:51).
Reachable under mimalloc refusal (million strings ≈ 32 MB scratch) and at count > UINT32_MAX (same arms :533/:599). Public API; only suite callers; none exercise refusal. logged 2026-07-26. test: none (needs allocator-refusal seam).
- odd-sibling-out

### Interlink / Composition bugs 



## Synth

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Text

### Interface-level bugs and logic inefficiencies

### Implementation bugs

text_gpos.c:294: ano_gpos_extract_kerns truncates both working sets in silence. Kern-lookup list capped at GPOS_MAX_LOOKUPS 16 by `!seen && kernLookupCount < GPOS_MAX_LOOKUPS` (:248/:294); subtable count clamped to GPOS_MAX_SUBS 32 (:249/:317-318). Neither drop logs nor fails the return; function still answers 0 against "Malformed -> nonzero... 0 = success including 'no kerns'" (text_internal.h:89). Sole production error arm (ANO_WARN text_bake.c:477) is dead for truncation. Sibling cap warns (text_bake.c:446) and is documented (anoptic_text.h:82); these two are not.
Lookup budget spent before type filter: :294 admits in feature-list order; :315 skips non-PairPos. Face with 'kern' fronting type-8 lookups fills sixteen slots and never reaches PairPos. Measured on shipping Noto: NotoSansSyriac-Regular 26 kern lookups, one type-2 at pos 25; NotoSansSiddham-Regular 20, type-2 at 19 and 20; each extracts zero pairs, returns 0, hits bake_kerns nz==0 (text_bake.c:483) → kerns NULL/kernCount 0, documented "nothing kerns" (anoptic_text.h:74). ano_text_kern answers 0 for absent and dropped alike (text_internal.h:62). 2 of 504 GPOS-bearing faces on stock macOS cross the lookup cap; ingress ano_text_font_load (anoptic_text.h:35). :317 subtable clamp same shape, unexercised by survey. No memory unsafety: kernLookups[16]/subs[32] bounded. logged 2026-07-26. test: pending
- silent-drop

### Interlink / Composition bugs 



## Threads

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Time

### Interface-level bugs and logic inefficiencies

### Implementation bugs

time_win64.c:402: ano_sleep gates coarse stage on `target_ns > ANO_SLEEP_SPIN_TAIL_NS` (:402; 1000000ULL at :352; target_ns = us*1000 at :398). us <= 1000 skips waitable timer (:404-419) and Sleep fallback (:423-430), falls into spin (:434-440) = ano_busywait bare ano_timestamp_raw() poll (:328-343), no syscall. anoptic_time.h:61 documents "Sleep for us microseconds via OS facilities. Yields to the scheduler". Siblings keep it: time_macos.c:163-168 one mach_wait_until under "a single kernel wait keeps the yield contract"; time_linux.c:116 clock_nanosleep unconditional. Comparison strict, so us == 1000 spins too.
In-tree sub-ms callers sit at exactly 1000: audio_mixer.c:667 ANO_AUDIO_PACE_US = 1000u (:603) on a process-lifetime thread in its normal full-queue state (:659-669); on Windows the mixer hard-spins a core through healthy playback. main.c:49/:163/:385/:691/:699/:707/:715 are `while (!submit) ano_sleep(1000)` loops that spin against the consumer they wait on. audio_mixer.c:652-658 reasons about "the short wait wins back a slot". anotest_time.c:158 records the divergence ("sub-ms (spin-only on Windows)") without asserting. logged 2026-07-26. test: none (win64-only; needs CPU vs wall across ano_sleep(1000); no Windows runner).
- odd-sibling-out

### Interlink / Composition bugs 



## UI

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Engine

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Leads for future passes

(unverified suspicions surfaced during census iterations 〜 chase, confirm, then move up into a tally or strike)

## Census (2026-07-18)

Post-merge tally after splicing the attached census pass with in-repo BUGS.md. Severity inferred from writeups (no severity tags on entries). Leads unverified; excluded from tallied counts unless noted.

Amended 2026-07-24: math-conventions lead chased and split. Row/column-major half struck (header wrong, doc right, no code change). Alignment half promoted to anoptic_math.h:21 (Latent). Net: tallied 69 -> 70, leads 42 -> 41, file items 111.

### Rubric

| Level | Meaning |
|---|---|
| Critical | Memory corruption, crash/segfault, GPU device-lost / invalid-handle UB, or uninitialized use |
| Major | Wrong results, contract break, leak, race, or feature loss under plausible use |
| Latent | Explicitly latent / unreachable today / absurd-arg-only / exotic seam |

### Tally

| Severity | Count |
|---|---:|
| Critical | 32 |
| Major | 26 |
| Latent | 12 |
| **Tallied total** | **70** |
| Leads (unverified) | 41 |
| File items (tallied + leads) | 111 |

### Severity by section

| Section | Critical | Major | Latent | Total |
|---|---:|---:|---:|---:|
| Audio | 1 | 4 | 1 | 6 |
| Collections | 0 | 0 | 0 | 0 |
| Filesystem | 0 | 2 | 0 | 2 |
| Log | 2 | 0 | 0 | 2 |
| Math | 0 | 0 | 1 | 1 |
| Memory | 0 | 1 | 0 | 1 |
| Mesh | 1 | 1 | 0 | 2 |
| Music | 5 | 2 | 0 | 7 |
| Render / Vulkan | 18 | 10 | 2 | 30 |
| Strings | 0 | 1 | 1 | 2 |
| Synth | 2 | 1 | 0 | 3 |
| Text | 0 | 2 | 1 | 3 |
| Threads | 0 | 1 | 0 | 1 |
| Time | 0 | 1 | 4 | 5 |
| UI | 2 | 0 | 2 | 4 |
| Engine | 1 | 0 | 0 | 1 |
| **Total** | **32** | **26** | **12** | **70** |

Render holds 18/32 Criticals. Music is next (5). Time is almost all Latent.

### Remediation status (2026-07-24)

`[X] Fixed` entries and the one `wontfix` moved verbatim to `docs/BUGS_DONE.md` on 2026-07-25 under their module headings.

One-off pass: 36 of 70 tallied entries `[X] Fixed`; `time_win64.c:310` `wontfix`. Severity tables above are the census as found; not rewritten.

Second pass 2026-07-25: deleted thirteen unreachable-input guards (two in `time_win64.c` amending prior-day fixes). Record in BUGS_DONE.md, "Removed guards", plus incidental findings (unverified, not counted).

Third pass 2026-07-25: verified incidentals; eleven determinations in BUGS_DONE.md, "Remediation determinations" (five tier 1, six tier 4, none fault-site). Type-level welds via static_assert. One census claim refuted (PC_NAMES / Engine). Five entries entered already fixed: delay.h, music_host.c motif n, ui_raster_ref.c tile entries, window.c monitor query, ano_render_bridge.c pose seam. Settled log_core.c:817; deleted time clock-failure sentinel convention. Guards closed: anoptic_logflood, anoptic_gltfguard, anoptic_boottelemetryguard. New green: motifboundguard, dspdelayguard, uitileentryguard, cameraposeguard. Headless suite: four failures, all remaining open decisions.

Fourth pass 2026-07-25: settled and implemented all four open decisions. Destroy-time ring drain owner-side in render_bridge and audio (tier 1/2). Audio rides-home took teardown clause. Music ownership dissolved (call-transient table; music_theory.c lazy bake → rodata). Strings closure by measurement (empty divergence set; decomp/case trim; assert_trim_closure). Incidental entered fixed (generator padding-memcmp dedup). New open music_voicing.c:120 closed same day with two sibling holes. Guards: anotest_strguard, anotest_bridgeguard, anotest_audioshutguard, anotest_musicguard closed; anotest_voicingboundguard green. Headless suite green; TSan music guard clean.

Fifth pass 2026-07-25: 27 left, four fix rounds. Round 1 fixed 19 (15 renderer + four platform singletons) and fenced seven resource-management entries (guards red on purpose). Rounds 2-4: 44 strays (41 source defects entering fixed, three test-side). Ten new open entries above. Seven contracts adjudicated in `docs/BUGS_DONE.md`, "Adjudicated contracts (2026-07-25)"; arithmetic in `docs/BUG-HUNT.md`, "The 2026-07-25 remediation".

| Section | Fixed | Tallied |
|---|---:|---:|
| Audio | 12 | 12 |
| Filesystem | 2 | 2 |
| Log | 2 | 2 |
| Math | 1 | 1 |
| Memory | 1 | 1 |
| Mesh | 2 | 2 |
| Music | 9 | 9 |
| Render / Vulkan | 71 | 82 |
| Strings | 3 | 3 |
| Synth | 3 | 3 |
| Text | 3 | 3 |
| Threads | 1 | 1 |
| Time | 4 | 5 |
| UI | 5 | 5 |
| Engine | 1 | 1 |
| **Total** | **120** | **132** |

Rounds 5-6 2026-07-25: fixed six round-4 ledgered entries (`audio_mixer.c:616`, `audio_win64.c:389`, `scene_buffers.c:43`, `texture.c:381`, `apply.c:66`, `apply.c:57`) plus `backend.h` contract; then four remaining strays. Eleven fixes: six move Fixed only; four enter already fixed (`ANO_MESH_NONE` sentinel, `backend.h` static-region, `anoptic_render.h` `castsShadow`, `refresh_static_shadow` declaration home); one test-side (no tally). Fixed 110+6+4=120, Tallied 128+4=132, open 18−6=12. Audio 10|12 → 12|12; Render 63|78 → 71|82.

Left open after swoops: twelve. Seven resource-management fence (one custody question, seven anchors). Four strays unclosed inside own surface: SPIR-V orphan family, shadow-resources unwind, texture post-acquisition arms, static frustum non-reclamation. Twelfth is the wontfix. No open entry blocked on a contract choice.

Sixth wave 2026-07-25: closed five decision-free opens (apply.c:125, render_slots.c:92, swapchain.c:110, flat.c:244, compute.c:80 SPIR-V orphans). Two strays fixed same day: pipeline.c:63 loadFile (new tallied, entered fixed) and intra-batch duplicate-id hole. Fixed 120+5+1=126, Tallied 132+1=133, open 12−5=7 (texture custody chain texture.c:426/components.c:72/ano_GltfParser.c:277, shadow-resources unwind, texture post-acquisition, static frustum churn, wontfix). Render 71|82 → 77|83.

Guard-test movement: 22 new guards (3 round 1, 14 rounds 2-4, 5 rounds 5-6); 15 renderer census guards red→green. Two stub retypes only. Rounds 5-6 add anotest_audiopaceguard (headless) and anotest_scenebindguard, anotest_texpixeldomainguard, anotest_staticcastdropguard, anotest_fallbackmeshguard (render). Totals 87/94 → 91/98 (seven fenced reds) and 57/57 → 58/58. Untested here: four platform singletons, `audio_win64.c:389`, and entries marked `test: pending`.

Margin tally 2026-07-26: gotos.md routed four findings. Two verified and entered: text_bake.c:507, main.c:350. One already tallied (texture.c:486 arms). One rejected: device.c:345 pickPhysicalDevice (caller-reclaimed; cleanupVulkan :252). Fixed 126, Tallied 133+2=135, open 7+2=9.

Leads triage 2026-07-26: fifty leads settled. Thirty-eight refuted and struck. Twelve confirmed and entered: filesystem_win64.c:33, time_win64.c:402, text_gpos.c:294, ano_strings_collate.c:504, memory.c:9, music_host.c:45/:232/:226, instance.c:192, ano_GltfParser.c:236, ano_GltfParser.c:435, main.c:691. Tallied 135+12=147, open 9+12=21, fixed 126.

Precedent wave 2026-07-26: closed shadow_casters.c:151, text_bake.c:507, main.c:350. Shadow coverage restored via tests/anotest_vk_shadow.c (render suite 30 targets). Fixed 126+3=129, open 21−3=18, tallied 147.

Residue wave 2026-07-26: closed texture custody chain (texture.c:426/:486, components.c:72, ano_GltfParser.c:277) and colour/data split (ano_GltfParser.c:435) as one change; main.c:691 separately. Split keyed by glTF image (not texture). Mixed image: one mip level. Discharge of refused adoption defers past submit; acquisitions reordered ahead of every vkCmd*. Four strays entered fixed: text_raster.c discarded createImageShared; spawn_scene light-attach lid increment; bulk packer unaligned 16-aligned sub-arrays; ano_render_ui_set unchecked count/table pairs. Fixed 129+6+4=139, Tallied 147+4=151, open 18−6=12. Guards: anotest_texunwindguard, anotest_texregisterguard, anotest_gltftexleakguard, anotest_gltftexdomainguard, anotest_vk_texdomain, anotest_render_bridge extended, anotest_rendersubmitguard; all falsified before trust. Suite 36/36 Debug; Release clean; 20 s validation zero VUIDs.

### Context

For a systematic audit census of a C23 + Vulkan + lock-free engine, ~70 tallied findings is in band; many Criticals are bad-input / OOM / rare-device paths the demo never crosses, not daily boot crashes. The point of tallying before whack-a-mole was to expose systemic gaps.

### Systemic gaps (fell swoops)

Local one-offs will remain (limiter window, mesh simplify, collation CE/decomp, Darwin barrier, TSC resume, measure_runs, voicing race). Most Critical mass clusters into a few missing disciplines:

| Swoop | Rough blast radius |
|---|---|
| Vulkan Result + abort/unwind (FATAL aborts or tears down; no publish-on-failure; every fallible create checked) | ~12–15 Render Criticals |
| GPU resource → descriptor bind registry (recreate invalidates + rewrites all dependents before old handle destroy) | ~4–6 growth/UAF/rebind |
| Music/synth validate-at-ingress (create / set_override / config adopt + synth event upper bounds) | ~6–8 Music+Synth |
| Checked size arithmetic (`ano_mul_u32` / `ano_add_u32` or equivalent as the only cap/size path) | ~6–8 wrap guards |
| Ownership discharge on destroy/failure (adopted blocks, staging, pipeline temps) | ~6–8 leaks |
| Asset validate gate (`cgltf_validate` before accessor reads) | 1 + class |

Stacked: plausibly ~70–80% of Criticals and a large Major chunk. Highest leverage: Result discipline, bind registry, music/synth ingress, then arithmetic + ownership as house rules. Do not chase folder reshuffles or MI_OVERRIDE first.

### Three layers (do not conflate)

`backup-resource-manager` and the Vulkan Result swoop hit different layers. Conflating them leaves Render Criticals standing after a large merge.

| Layer | What backup-resource-manager aimed at | What the Criticals need |
|---|---|---|
| Asset / CPU resource mgr | Logical paths, rid registry, load-to-caller-heap, durable writes, bytes vs meaning | Hostile/truncated file loads, path chaos, size-then-read; not VkBuffer UAF |
| Thread interconnects | `anoring_*` / `anoseqpub` / tickets in `anoptic_collections.h`; bridges migrate onto them | One ownership contract for adopted payloads; stop private twin rings |
| Vulkan Result + GPU bind graph | Essentially absent (GPU allocations stay renderer-owned per resource-manager-ownership.md) | FATAL fallthrough, discarded VkResult, growth/recreate without rebind |

Facts:

- Vulkan Result + abort/unwind is local `vulkan_backend` discipline; does not depend on resourcemg. Resourcemg helps load side (`ano_res_load`) but will not stop NULL shader modules into pipeline creates or depth init FATAL-and-continue.
- Resource manager moves handles; renderer owns GPU allocations. Descriptor/rebind swoop is a thin renderer-side table: device object live at (set, binding, frame); destroy ⇒ rebind or retire dependents.
- Rings: standardize, do not remake. `docs/URGENT-audiorace.md` already closed this. `anoring_spsc` matches audio/render bridge; migrate audio/render/log onto it with seqpub fixed. Push = ownership transfer; destroy = drain + free `bulk_owned` / retired blocks.
- Practical order: (1) Vulkan Result + unwind and small GPU bind/rebind registry; (2) promote collections rings with seqpub fixed; (3) port resource manager for asset/path class, parsers still validating meaning.
