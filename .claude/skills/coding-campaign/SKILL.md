---
name: coding-campaign
description: A hierarchical GAN coder campaign. Run only when user mentions using Sol/Luna, claudex fleets, competing implementations, or architecture contests. The CMDR derives checkpointed operational phases from the ingested mission and advances them through fresh blind Opus supervisors; every code-bearing artifact is produced by claudex Sol/Luna Individual Units; supervisors only commission, judge, select, gate, and report.
disable-model-invocation: true
---

# The GAN coder system

```
user -> CMDR -> phase-scoped Opus supervisors (SL role) -> claudex Sol/Luna Individual Units
```

The CMDR first derives the campaign's operational phases from the user's prompt, governing specification, dependency structure, repository state, and required verification surfaces, then advances one derived phase at a time. In each phase, fresh blind Opus supervisors commission competing implementations inside their assigned lineages and select local winners; the CMDR compares those reports, consolidates one phase winner, freezes it as substrate, releases the supervisors, and starts the next derived phase with fresh contexts.

## Campaign law

- Squad Leaders are Opus only. Never use Fable as an SL. Fable may be used as superivsing CMDR only.
- Individual Units (IUs) are claudex instances using `gpt-5.6-sol` or `gpt-5.6-luna`. No other model writes campaign code.
- The runner hard-fires the standard fleet: 4 Sol/high + 6 Luna/xhigh, ten IUs total. Fleet composition is not configurable.
- Run no more than four Opus SLs concurrently. Queue additional squads and start them as active squads report or finish; quota safety outranks wall-clock spectacle.
- Squads are blind to the actions of another. An SL receives its lineage and owned scope, never another squad's prompt, candidates, worktree, findings, or intermediate state. All signals follow chain of command.
- In a given coding round, every IU in one squad workgroup receives the exact same frozen prompt, byte for byte. Never specialize prompts by Sol/Luna role, index, candidate path, anticipated approach, or any other per-unit detail. Supply isolated paths and deadline-warning paths through runner-owned environment variables. A later round may use a new prompt, but that prompt is again identical for every IU in that invocation.
- There is no preset phase count, alphabet, naming scheme, or reusable phase template. The CMDR derives semantically named phases from the ingested mission. Each phase must have explicit dependencies, a bounded objective, a freezeable output, and an objective completion gate.
- The campaign advances through CMDR-controlled global phase barriers. Run one operational phase across every competing lineage, collect every phase-local winner, consolidate and freeze the phase winner, prove its gate, create its Git checkpoint commit, terminate that supervisor wave, and only then start the next phase. No phase overlap and no persistent SL context spanning phases.
- An SL is a temporary supervisory role, not a required dynamic-workflow primitive. Prefer fresh phase-scoped Opus subagents: one lineage, one operational phase, one report, then terminate. A phase may have more than four competing supervisors, but only four are active concurrently; queued supervisors belong to the same phase barrier and must finish or be explicitly culled before CMDR consolidation.
- No result exists without a reproducible artifact and a run that proves it. A summary, intention, partial transcript, or unranked candidate directory is not a result.
- Obtain explicit authorization to create checkpoint commits and optional GOLD tags before launching a campaign that requires phase advancement. Push authorization is separate. Without commit authorization, the CMDR may select a winner but must label it `selected, pending checkpoint`; it may not call the state checkpointed or GOLD and may not release dependent phases.

## Git checkpoint law

- A checkpoint is a Git commit containing the consolidated, working code after the declared phase-wide gate passes. A selection, worktree, report, transcript, results table, uncommitted diff, failing commit, or partial implementation is not a checkpoint.
- The CMDR may mark that exact commit with `GOLD/<semantic-phase-id>`. A GOLD tag means its required gate passed. Never move or reuse a GOLD tag; a repair receives a new commit and a new semantic tag.
- Do not create `phase_<P>/checkpoint`, checkpoint manifests, marker files, checkpoint directories, or duplicate evidence. Git history, existing worktrees, prompts, candidates, `results.tsv`, champions, and logs already hold the evidence.
- Every dependent phase starts from the exact checkpoint commit, identified by its commit hash or GOLD tag. It never starts from a mutable worktree tip or a merely selected candidate.
- A checkpoint commit records the code state; the CMDR communicates its hash, gate tally, and existing evidence locations through the normal campaign transcript and user report. A checkpoint never justifies adding repository bookkeeping files.

## Absolute SL no-code rule

An SL is a read-mostly control-plane judge. An SL MUST NOT author, rewrite, edit, synthesize, complete, or repair any code-bearing artifact. There are no emergency, deadline, convenience, triviality, or "surgical fix" exceptions.

Code-bearing artifacts include:

- Production source and headers.
- Tests, benchmarks, fixtures, reference evaluators, fuzzers, and fitness oracles.
- Build files, executable shell or PowerShell scripts, generators, migration code, and tooling.
- Patches, diffs, replacement functions, code snippets intended for insertion, and merged or consolidated candidates.
- Documentation comments embedded in code and code-facing documentation generated as part of the implementation.
- Integration changes, conflict resolutions, compiler fixes, test fixes, review fixes, and final polish.

An SL may only:

- Read the mission contract, bounded context packets, existing code needed for judgment, compact fitness results, and the smallest useful set of top candidates.
- Write natural-language prompts, rankings, decisions, and reports under the existing squad scratch area.
- Invoke the provided fleet runners and existing build/test commands.
- Mechanically copy an unchanged single-file champion into its declared target or mechanically apply an IU-produced patch.
- Run gates, classify failures, and commission another claudex round to produce any required code change.

The following are violations:

- Using Write/Edit/ApplyPatch on a code-bearing path.
- Using heredocs, redirection, `sed`, `perl`, Python, PowerShell, or any other shell mechanism to create or mutate code.
- Manually merging candidates, retyping a candidate, or making a champion "house style" compliant.
- Writing the fitness harness because it is small.
- Fixing a compile error, test failure, merge conflict, or review finding directly.

If an SL can describe a needed code change, it commissions an IU to make that change. If no IU-produced artifact exists, the component remains incomplete and must be reported as such.

## Roles

### Commander

The CMDR ingests the user's prompt and governing specifications, inspects the repository and dependency boundaries, and turns the actual mission into a dependency-ordered phase plan with distinct competing lineages. It does not begin with a fixed number or kind of phases. Before launch it obtains the Git commit and optional tag authorization required by the planned barriers. 

For each derived phase it freezes the declared input commit, supplies bounded context packets, starts fresh phase-scoped supervisors with at most four active concurrently, collects every local winner, independently re-tests the finalists, chooses or commissions consolidation of the phase winner, proves the phase-wide gate, commits that exact working integration state, optionally tags it GOLD, and only then permits the next dependent phase to exist. 

Before spawning a supervisor, the CMDR verifies its worktree path, exact base commit, branch or detached state, clean owned files, required submodules, shared assets, toolchain, and baseline gate. A mismatch is fixed before the supervisor starts, never discovered on its context budget. The CMDR calculates the worst-case phase schedule from the fleet size and hard deadlines; work that cannot finish inside the safe campaign window is split at a real dependency or verification boundary.

During execution the CMDR watches live token/time budgets, runner deadlines, checkpoint commits, and stalled squads; it culls or finalizes them instead of waiting only for final structured output. After a checkpoint commit it may revise only the unstarted remainder of the phase plan in response to evidence; it never silently redefines a completed phase, amends the checkpoint, moves its GOLD tag, or mutates frozen substrate. The CMDR never asks an SL to rescue code by hand. 

The CMDR may only write code directly when it is first spinning up the worktree for SL's to copy, to smooth over any bumps between phases, and at the very end when combining the work of every phase and winning SL into a cohesive whole. Never in-flight and never while SL's are working.

### Squad Leader

An SL controls one architectural lineage for exactly one operational phase. It receives the CMDR-frozen substrate and phase contract, writes one frozen workgroup prompt and acceptance criteria, commissions IUs, evaluates compact evidence, selects a phase-local winner, mechanically applies IU artifacts when required, runs the phase-local gate, commissions repairs, and reports. It does not implement, choose the global phase winner, advance the campaign, or carry its context into the next phase.

### Individual Unit

An IU performs one bounded task under 200k tokens through claudex. 200k tokens is treated as a hard max: if the task exceeds it, let the IU finish and re-consider the scope to be subdivided into more digestible chunks. IU tasks include reconnaissance, fitness-harness creation, implementation, tests, comments and documentation tone, tersification, synthesis, integration, repair, and adversarial review. 

Sol/high is the default implementation and critical-reasoning unit; Luna/xhigh supplies breadth, alternative implementations, and refutation. 

Every IU owns its artifact through the required targeted tests: it must implement, run the supplied tests in its isolated environment, repair its own work, and reach green inside its deadline before returning the complete artifact. Reasoning that tests ought to pass is not testing. Partial code, "the SL can fix this," untested output, a non-green tally, or a late artifact is failure. The runner independently reruns fitness, records the exact tally, and only `pass=X/X` candidates are eligible to win.

## Required CMDR phase barriers

Before launching any supervisor, derive and record the mission-specific phase plan. Give every phase a semantic identifier, bounded objective, declared input commit, expected freezeable outputs, competing lineages, trusted gate, and dependencies. The first phase starts from the exact user-authorized base commit; every dependent phase starts from its predecessor's exact checkpoint commit. Prove that the ordered phases cover the user's requested deliverables and verification obligations without inventing work absent from the mission. Topologically order the plan and execute one phase at a time.

Example (illustrative only):

```h
CMDR READS USER PROMPT AND SPEC, THEN DETERMINES HOW THE WORK SHOULD BE DIVIDED INTO STAGES A, B, AND C
CMDR START STAGE A
SL SL SL SL SL
(each SL runs ~10 claudex instances with the identical Stage A prompt)
SL SL SL SL SL (they pick their winners and tell CMDR)
CMDR CONSOLIDATE
CMDR STAGE A DONE
CMDR START STAGE B
SL SL SL SL SL
(each SL runs ~10 claudex instances with the identical Stage B prompt)
SL SL SL SL SL (they pick their winners and tell CMDR)
CMDR CONSOLIDATE
CMDR STAGE B DONE
CMDR START STAGE C
SL SL SL SL SL
(each SL runs ~10 claudex instances with the identical Stage C prompt)
SL SL SL SL SL (they pick their winners and tell CMDR)
CMDR CONSOLIDATE
CMDR STAGE C DONE
CMDR START FINAL PASS AND PERFORM MERGE ITSELF
CMDR DONE
REPORT FOR THE USER AND FINISHED WORKING CODE
```

For every derived operational phase `P`:

1. Freeze input: Check out the exact declared input commit for `P` and record its hash. For a root phase this is the user-authorized campaign base; for a dependent phase it is the predecessor's checkpoint commit. Freeze interfaces and acceptance criteria and prove the shared baseline gate. Every supervisor in `P` starts from this identical commit.
2. Launch one wave: Spawn fresh Opus supervisors for the competing lineages of phase `P`, with no more than four active concurrently. A supervisor is a disposable phase worker, not a persistent campaign process.
3. Run local contests: Each supervisor commissions its own standard fleet using one byte-identical prompt, independently scores the returned artifacts, selects one local winner, runs its local gate, and emits a compact phase report. Supervisors remain blind to other lineages.
4. Close the wave: Wait for every queued supervisor in `P` to report, fail, or be explicitly culled. Do not start any supervisor for the next declared phase while `P` has unresolved work.
5. Consolidate at CMDR: Independently re-run the trusted gate against every eligible local winner. The CMDR compares only compact results and the smallest necessary finalist set, then selects one winner or commissions a claudex integration/refutation workgroup when synthesis or repair is required.
5.5 Optionally commission an adverserial review via Special Advisors (detailed below).
6. Freeze the phase winner: Mechanically install the unchanged winning artifact or IU-produced integration patch into the integration worktree, run the phase-wide gate, and reject the result unless the declared gate is green.
7. Checkpoint and release: Commit the exact green integration worktree. That commit is the checkpoint. Optionally create `GOLD/<semantic-phase-id>` on that exact commit. Report the commit hash, gate tally, and locations of the already-existing prompts, results, candidates, champions, and logs; create no checkpoint file, manifest, directory, or duplicated evidence. Base every downstream worktree on that exact commit. If commit or requested tag authorization is absent, stop at `selected, pending checkpoint` and do not advance. Terminate the phase's supervisor contexts before launching the next wave.

After the last phase derived from the mission, the CMDR owns a separate finalization barrier: assemble the frozen phase outputs mechanically or through a claudex integration workgroup, commission final adversarial review (directly through the claudex command, a workgroup of 2 Sol xhigh's) and repairs through IUs, run the full project gate itself, create the final checkpoint commit and optional GOLD tag, report their exact hashes with the user report, and stop. A final report without working integrated code and a passing required gate is not completion.

## Adverserial Review

For Adverserial reviews at the end of a phase or at the very final stage of completion, CMDR may commission claudex instances directly as Special Advisors. These Special Advisors are a pair of Sol xhigh instances reporting directly to CMDR with the adverserial report.

## Required phase-supervisor state machine

1. Preflight: The CMDR proves the worktree is on the exact requested base, initializes required submodules and assets, confirms the toolchain and claudex backend, and records a baseline gate before the SL starts.
2. Budget: Calculate the worst case from ten simultaneous IUs, `ROUND_TIMEOUT`, `FITNESS_TIMEOUT`, the fixed five-minute post-Sol deadline, integration, review, gate, and checkpoint-commit time. Split the mission before launch when it cannot reach a green checkpoint commit inside the available window.
3. Context: The CMDR supplies the lineage, ownership boundary, frozen contracts, acceptance criteria, and only the relevant code/spec excerpts. If more discovery is required, commission a claudex reconnaissance round and consume its compact report.
4. Fitness: Use a pre-existing trusted harness. If none exists, commission a standard fleet to produce one, then commission a separate standard review round with one identical refutation prompt before the harness may score implementations. The SL never writes harness code.
5. Generate: Commission one standard fleet for the bounded task assigned by the derived phase contract. Freeze one prompt containing the exact test command, acceptance tally, deadline, and the statement that the IU〜not the SL〜owns implementation, test execution, repair, and green completion; send those exact bytes to every IU. The prompt refers generically to `CAMPAIGN_CANDIDATE_PATH` and `CAMPAIGN_WARNING_PATH`; the runner sets their different values in each process environment without changing the prompt.
6. Score and cull: Run real fitness immediately as each candidate returns and persist that result before waiting for the rest. There are no per-IU cull clocks. Only the first Sol candidate that independently achieves its full required passing tally may arm the group deadline; a Luna result, a failed Sol, raw generation completion, or unverified self-report cannot arm it. Exactly four minutes after that verified Sol pass, the runner broadcasts to every IU in the squad workgroup and to the SL that unfinished units have one minute left. Exactly five minutes after the verified Sol pass, the runner culls every unfinished IU with no exceptions. `ROUND_TIMEOUT` terminates a group that never produces a passing Sol; `FITNESS_TIMEOUT` remains a hard fitness backstop. Empty output, partial fitness, continuation artifacts, placeholders, `#error`, timeout, crash, contract redefinition, and every non-green test tally are failures and cannot become champion.
7. Judge: Delegate code-quality and adversarial comparison of the highest-fitness candidates to a standard review fleet using one identical prompt. The SL reads their compact findings and selects a winner; it does not rewrite one.
8. Integrate: For one complete file, mechanically copy the unchanged winner. For multi-file work, conflicts, synthesis, or adaptation, commission a claudex integration round that emits a patch and its own passing targeted-test tally, then mechanically apply that patch.
9. Gate: Run the targeted gate independently. On failure, the producing IU has not crossed the finish line; commission a claudex diagnosis/repair round with the exact diagnostics and another hard deadline. Repeat until green or genuinely blocked. The SL never repairs tests or implementation.
10. Report evidence: Report the selected artifact, per-candidate results, `results.tsv`, fleet log, gate tally, known wounds, and exact existing artifact locations to the CMDR. The supervisor creates no checkpoint file or commit and does not start the next component or phase.
11. Refute: After the integrated lineage is green, run a mandatory standard adversarial-review fleet against the final diff using one identical prompt. Every finding must be rebutted with evidence or fixed and tested by a separate standard repair round before its deadline.
12. Report: Run the required phase-local gate and emit the supervisor report with the selected IU-produced artifact or patch. A supervisor without the required refutation and local gate is incomplete, never successful; only the CMDR may consolidate the wave and advance the campaign.

## Shell

[[CRITICAL INFORMATION FOLLOWS]]

The canonical runners are `reference/run_fleet.sh` and `reference/run_fleet.ps1`. If claudex or the runner does not work, the skill is invalidated for that squad: report the infrastructure failure and stop. Never replace missing IUs with Opus or Fable coding.

Confirm the backend before the first round:

```bash
claudex -p --effort low "reply OK"
```

Invoke the runner directly. Do not write a per-step executable script. From Git Bash on Windows:

```bash
MSYS_NO_PATHCONV=1 wsl -d Debian -- env \
  PROMPT_FILE=/mnt/c/path/prompts/parser.txt \
  FITNESS_CMD='bash /mnt/c/path/fitness/fit.sh "$1"' \
  WORK=/mnt/c/path/scratch/contest NAME=parser EXT=c \
  ROUND_TIMEOUT=1200 FITNESS_TIMEOUT=180 \
  bash /mnt/c/Users/Pyrus/Code/anoptic-engine/.claude/skills/coding-campaign/reference/run_fleet.sh
```

`PROMPT_FILE` may be SL-authored natural language containing copied contracts and context. The executable named by `FITNESS_CMD` must already exist in the repository or be an unchanged claudex-produced winner. An SL-authored fitness executable invalidates the round.

## Context and quota discipline

- The CMDR prepares bounded context packets; do not make every SL reread the whole repository, complete specifications, or raw predecessor diffs.
- Delegate reconnaissance and summarization to claudex. Feed the SL conclusions and exact source locations, then let it inspect only what is needed to judge.
- Keep an SL below 300k total context. At 250k tokens or 40 minutes, whichever comes first, stop commissioning new work, finish or terminate the current round, report its selected or partial state and remaining scope, and release the context. Only the CMDR may create a checkpoint commit after consolidation and a green phase-wide gate.
- Never poll a fleet in a model loop. Use a monitor or completion notification, yield, and wake when the external process changes state.
- The CMDR, runner, or external monitor owns timers. The SL receives completion and cull events; it does not spend context counting candidates or tailing unchanged logs.
- Never load every candidate into the SL context. Fitness ranks first; claudex reviewers inspect the top set; the SL reads compact comparisons and only the minimum source needed for the final decision.
- Store prompts, candidates, per-candidate results, champions, patches, and logs under the squad worktree's existing `scratch/` as they are produced.

## Squad report

Report the worktree, lineage, completed and partial scope, IU-produced files, every fleet invocation and size, fitness ranking, winner and any judge override, targeted and full gate tallies, adversarial findings with repair/rebuttal status, deviations, wounds, and a resumable next action. Never label a squad complete when a required round, review, or gate is absent. A squad reports evidence; only the CMDR creates a campaign checkpoint commit.
