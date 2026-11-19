# Develop on macOS, validate Windows at milestones

Status: agreed development preference and required Windows test-delivery contract, 19 November 2026. This changes validation scheduling and implementation deliverables, not the Windows runtime requirements. No remote access, CI run, portable MP target or checkpoint script has been configured or executed by this document.

Parent: [multiplayer plan](multiplayer-modernization-plan.md). Tasks and evidence: [TDD backlog](multiplayer-tdd-work-items.md). Technical boundaries: [implementation contracts](multiplayer-implementation-contracts.md).

## Everyday workflow

**macOS is the everyday development machine. Windows live-app validation is grouped into useful milestones, rather than required after each edit, commit or individual work item.** Several test-first changes can be prepared and checked on Mac before synchronizing one coherent candidate to Windows.

**Every requested Windows check must ship with prepared scripts and instructions as part of its implementation.** This includes build-only checks, engine scenarios, SDK/navigation generation, load/soak runs, regression comparisons and human playtests. Preparing that kit is implementation work, not a task delegated to the user. Remote triggering is optional; the local Windows scripts/runbook are mandatory, starting at W0.

| Work | Normal cadence | User involvement |
| --- | --- | --- |
| Source changes, review, documentation and supported static checks | On Mac during implementation | Normal development |
| Portable production-code tests and GNS peer fixtures | On Mac after relevant changes, once MP-010/012 make these targets available | No Windows switching |
| Windows compile/link and suitable noninteractive tests | Prefer automated feedback for integration batches; may run more frequently than live-app milestones | No manual pull/build when automation is available |
| Real engine/server scenarios and SDK/content work | At the checkpoints below | One prepared Windows validation session per candidate/batch, with reruns if failures require them |
| Gameplay feel and human bot assessment | At the relevant gameplay checkpoints | Actual human play/review; compile success cannot replace it |

Portable tests must exercise production algorithms, with engine adapters kept narrow. Useful candidates include encoding, identities, command windows, clocks, state/interpolation math, historical collision primitives, observation/memory decisions and scheduler budgets. The engine and tools are not assumed to compile or run on Mac. A Mac test target must first be implemented and proved; current benchmark infrastructure is not that target.

### Test-first work between Windows sessions

Run the ordinary Red/Green cycle on Mac for portable behavior. For a Windows-only engine behavior, author the test/scenario first and preserve a test-only candidate revision before its fix. A prepared checkpoint can run the test against that revision and the fixed candidate in one Windows session, recording both outcomes. Keep the fixture/content identical across the comparison.

Until those runs happen, record **Windows regression prepared / unverified**. Writing a fix before its Windows failure has been observed is deferred regression validation, not an executed Windows Red/Green cycle; do not describe the history otherwise. If strict immediate Windows TDD is needed for a difficult issue, use an automated worker or iterate within that checkpoint rather than asking for a new manual session for every source edit.

A test-only revision must build and fail for the intended behavior. A compiler error, missing asset, timeout or deliberately broken unrelated assertion does not establish that regression. If the old revision already passes, characterize the existing behavior or correct the test's assumption. A meaningful Windows-only regression without a buildable comparison remains explicitly unverified until the checkpoint supplies the required evidence.

## Completion and dependencies

Track evidence separately:

- **Mac checks passed:** the relevant production-code tests/static checks actually ran on macOS.
- **Windows build passed:** MSVC compiled/linked the exact candidate revision and configuration.
- **Windows runtime passed:** the specified engine scenario ran with assertions and recorded outcomes.
- **Human review passed:** the relevant movement/gunplay/bot behavior was assessed in play.

One status does not imply the others. An item requiring Windows evidence stays open as **Mac checks passed / Windows pending**, with its checkpoint named. Source reviewed on Mac without an executable test is **implemented / unverified**, not portable green.

A dependent task can proceed within a bounded validation batch when it uses a prerequisite's already-tested portable contract. Record the dependency as **provisional for Windows** and keep final completion pending. This avoids requiring the Windows half of every small item before the next Mac test can be written.

Do not use provisional status to bypass a feasibility or product-decision gate, or to assume engine behavior that has never been demonstrated. In particular, MP-005 must prove the existing stalker route before the larger engine networking/prediction rewrites. A pure protocol test can proceed while that Windows gate is pending; a rewrite premised on a successful NPC integration cannot.

Define each batch by a coherent scenario and a named checkpoint, not by an arbitrary number of tickets. Avoid stacking unrelated physics, AI, protocol and presentation changes until the end of the project. If a specific platform question blocks the next design choice, schedule a smaller Windows checkpoint and explain the concrete uncertainty; ordinary edits do not trigger a manual test request.

## Windows runtime checkpoints

Checkpoint labels below group existing work. They are not new backlog tasks or a promise that a checkpoint needs only one run.

| Checkpoint | When to synchronize/run | Evidence |
| --- | --- | --- |
| W0: baseline and environment | MP-001–004; initial build/data setup, independent process launch and arena inventory | Matching Windows client/server, two-client baseline, asset availability, logs and actual launch syntax. Group any ready portable harness Windows confirmation here |
| W1: early stalker proof | MP-005, before larger engine networking/prediction changes | One server-owned existing NPC with two clients: movement, weapon use, damage, scoring, death and respawn. Reuse a minimal valid nav fixture; rough visuals are acceptable |
| W2: transport/session integration | A coherent MP-020–029 integration batch | Start/join/disconnect/reconnect/shutdown, parser/admission failures, callback ownership and resource lifetime. Pure codec/queue tests run on Mac between candidates; this is not yet a claim of completed new-authority gameplay |
| W3: authoritative world and DM | MP-030–039 wired into the engine | One simulation owner, human input/NPC actions, roster, pickups, 5-second respawn/2-second protection, coherent late join, round epochs and console controls. Recheck the early NPC path against the new authority interfaces |
| W4: prediction and remote presentation | MP-040–047 integration | Actual movement/ODE restore/replay, contacts, correction, remote human/NPC interpolation and selected loss/latency cases |
| W5: gunplay comparison and combat | MP-050–062; MP-066/067 only if selected | Dedicated poses/history, hitscan versus ballistics/mixed prototype and human choice of policy. Validate selected weapons after implementation, within this checkpoint or the next compatible integrated candidate; do not implement a choice before its comparison gate |
| W6: arena/navigation and advanced bots | Ready MP-070–079 / MP-100–109 vertical slices | Actual baked data/graph loading, required ladder/traversal, perception/tactics and shared combat rules. Group offline baking/tool checks with runtime route checks; scale 1/4/8/16/32 bots within a prepared run suite |
| W7: integrated release candidate | MP-080–088 | Mixed matches, separate 32-protocol-client load, impairment/soak/performance, clean packaging and supported SP/renderer regressions |
| W8: Steam later | MP-090–094 after service prerequisites | Real Steam identity/invite/relay/service tests; outside the initial IP release |

Navigation fixtures and tooling may be needed at W0/W1; W6 is the full production-content/behavior gate, not a reason to delay all navigation work until then. W5 may need a comparison session followed by a selected-policy validation session. W6 can contain several smaller route/combat batches where each establishes the next prerequisite. Milestone scheduling reduces context switching; it does not eliminate feedback loops.

At each checkpoint, rerun the shortest affected earlier smoke scenarios. Run relevant MP-off/single-player checks at the next checkpoint after shared code changes, not only at final release. Full regression/load matrices belong to W7 unless a newly observed problem justifies them earlier.

## A prepared handoff instead of repeated manual pulls

### Required deliverables for every checkpoint

The implementation change must include or update all of the following before requesting a Windows session:

| Deliverable | Required content |
| --- | --- |
| Versioned Windows script entry point | One documented main command per checkpoint; perform preflight, exact-revision synchronization/build and applicable scripted scenarios, or clearly selected build-only/collect/cleanup modes |
| Checkpoint definition and supplied configuration | Work-item/scenario IDs, candidate SHA(s), presets/targets, content/profile identities, seeds, deadlines and assertions; generate temporary configs rather than ask the user to invent flags |
| Short checkpoint runbook | One-time prerequisites, supported shell/tool versions, where to run the command, user-specific paths, expected duration/result, required manual actions and how to rerun/stop |
| Automatic results collection | Requested/actual revisions, build/test status, per-process logs, scenario results, failure diagnostics and supported dumps/captures, with one clearly named report/archive to return |
| Cleanup/recovery instructions | Stop only runner-owned processes, keep existing checkout/saves/data intact, retain diagnostic output on failure and explain the next action for missing prerequisites or failed scenarios |

Proposed locations, to be created with the corresponding implementation rather than treated as existing files:

```text
scripts/multiplayer/windows/RunCheckpoint.ps1
scripts/multiplayer/windows/PrepareEnvironment.ps1
src/test/multiplayer/checkpoints/<checkpoint>.json
docs/engine/multiplayer-checkpoints/<checkpoint>.md
build/test-results/multiplayer/<commit>/<run-id>/
```

Reuse a shared script/runner plus small checkpoint definitions; do not create unrelated scripts that drift in behavior. The one-time W0 runbook explains how to obtain the first kit and configure machine-local game-data/tool paths. Subsequent handoffs supply the actual invocation and candidate revision, with source synchronization handled by the kit where available. Never make the user manually edit source or infer console/build commands to complete a test.

W0 needs a minimal bootstrap/preflight/build/launch/collect kit under MP-001/002. It cannot depend on a fully completed MP-013 harness that itself requires baseline evidence. MP-013 evolves that same kit into the shared asserted checkpoint runner; later checkpoints extend it rather than postpone scripts until the release phase.

### Readiness before handoff

Verify script syntax, argument/configuration handling and available runner unit tests before handoff. State any unverified Windows-specific behavior explicitly, especially on the first W0 run; do not claim a script was Windows-tested from Mac inspection alone. A test-only/fixed comparison must use the supplied runner and identical scenario inputs.

A checkpoint is **ready for Windows testing** only when its scripts, definitions, instructions and result collector are present, reviewed and checked to the extent available. That status does not mean the engine test has passed. Missing test tooling/runbooks is unfinished implementation, not a reason to ask the user to construct their own procedure.

For a failure, return a useful nonzero result and retain the bundle. Report unavailable assets/tools, build failure, scenario failure, timeout and human review pending distinctly. A human playtest gets scripted setup/launch/collection plus precise actions and questions; the human judgement itself remains manual and must not be marked passed automatically.

### Contents of a handoff

Before asking for a Windows session, supply a checkpoint request containing:

1. Exact commit SHA and the work-item IDs/batch scope.
   Include test-only/fixed candidate SHAs for deferred Windows regression comparisons where required.
2. Build preset/targets/configuration and whether incremental build remains valid; flag dependency/ASan/toolchain changes requiring a clean configuration/rebuild.
3. Map/rules/nav/content identities and required locally installed assets.
4. Actual scenario command(s), expected assertions, deadlines and any human observations needed. Do not present a proposed command as already working.
5. The results directory and a compact return checklist, including known pending checks.

The Windows side runs the delivered checkpoint entry point to synchronize the requested revision, build, run applicable scenarios and write the result bundle. The shared entry point is required work, beginning with MP-001/002 bootstrap and extended in MP-013; no such script is claimed to exist today. The runbook explains any unavoidable manual prerequisite or gameplay action. A runbook alone does not replace scriptable setup/build/launch/collection work.

Use a dedicated validation checkout/worktree. Keep the user's ordinary Windows checkout and unsaved work intact. Check out the requested SHA rather than a moving branch tip; require a clean validation source tree and stop rather than overwrite local edits. Retain incremental build/dependency caches where valid, and keep large game data/generated output local to Windows.

Return a manifest with requested/actual SHA, clean/dirty status, configuration, executable/content/profile identity, scenario outcomes, exit codes, timeouts, server/client logs and any required captures/profiles. Include failures and unavailable/skipped scenarios distinctly. A report from the wrong revision cannot close a milestone. Fixes can be made on Mac and synchronized as another candidate within the same checkpoint.

## Optional remote triggering and CI

Two useful routes can remove manual source pulling/build launching without changing the milestone policy:

- **Existing GitHub build jobs:** [build-server.yml](../../.github/workflows/build-server.yml) already defines Windows builds and binary/symbol artifacts on matching pushes/PRs; [nonunity-build.yml](../../.github/workflows/nonunity-build.yml) adds a non-unity compile path. Verify the fork's Actions configuration/results before relying on them. These checked-in definitions do not establish current green builds, installed runtime maps or gameplay tests. A milestone can consume an exact-SHA artifact instead of rebuilding on the gaming PC, provided its configuration/content match.
- **The user's Windows PC as a build/test worker:** after a one-time setup, a Mac request could invoke a controlled Windows checkpoint script through SSH, or an explicitly configured self-hosted runner could process selected jobs. Microsoft documents Windows OpenSSH remote access/file transfer; GitHub documents registering self-hosted runners. No host address, credentials, machine setup or job activation is assumed here. [Microsoft OpenSSH](https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh-overview), [GitHub runner setup](https://docs.github.com/en/actions/how-tos/manage-runners/self-hosted-runners/add-runners).

Automation should select exact revisions and reuse the same scenario assertions/artifact format as manual checkpoints. Its first role is compile/link and noninteractive tests. GPU/rendering, input and human-play scenarios need a demonstrated suitable interactive Windows session. A background service/SSH launch is not proof of that environment; Windows services do not directly interact with the user's desktop. This engine's dedicated entry also has SDL/audio initialization to characterize. [Microsoft service/desktop model](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services), [dedicated entry](../../src/xrServerCLI/xrDedicatedServer.cpp).

The initial workflow works without remote triggering: prepare the implementation and test kit on Mac, run its documented checkpoint command on Windows, return the collected evidence, then continue on Mac. If remote triggering is configured later, the same scripts can run while the user stays on Mac; live gameplay reviews remain deliberate milestones. Neither route asks the user to write or maintain the test automation.

## Limits of this policy

macOS cannot establish MSVC/DLL integration, ODE prediction behavior, dedicated animation/hit volumes, Windows SDK tool output or the reference PC's real performance through portable tests alone. Background Windows compile feedback is therefore useful even when game launches are infrequent. Without it, Windows compile/link failures may first appear at the next checkpoint; keep integration batches focused enough to diagnose them.

The hard rule is **batch Windows validation, preserve honest evidence**. Do not require a manual Windows round trip for each local TDD step, and do not mark an unrun Windows scenario passed to keep the batch moving.
