# Multiplayer TDD work items

The operator reports that W0 passed on Windows. The exact revision and result bundle are pending attachment. MP-010, MP-011a, MP-011b, MP-012, MP-020a, MP-020b, MP-020c, MP-020d, MP-020e, MP-020f, MP-020g, MP-022a, MP-022b, MP-022c, MP-022d, MP-037a, MP-037b, MP-037c, MP-037d, MP-037e and MP-037f have macOS evidence.

Implements the [modern multiplayer plan](multiplayer-modernization-plan.md): 32 total human/bot combatants, simple DM, direct IP over GameNetworkingSockets first, human-like bots, authoritative simulation, prediction, lag compensation, and immediate local effects. Windows x64 is the initial runtime target; macOS remains usable for source work and portable tests. Hitscan, ballistics, and hybrid gunplay remain alternatives until the comparison gate.

Preferred bot path: extend the existing `CAI_Stalker` NPC **brain and body** as an authoritative DM participant. MP-070 and the navigation cases below target native NPC execution. Common player-controller adapters and a separate brain are retained fallbacks with selection criteria in the [bot design](multiplayer-bot-ai-design.md#reuse-first-implementation-decision); implementing them is not required for this backlog's alpha gate.

Follow the [implementation contracts](multiplayer-implementation-contracts.md) for confirmed game defaults, the early MP-005 gate, proposed wire envelope, loading/barriers, NPC adaptation, phase ordering and initial console/configuration operation. Proposed engineering defaults must be versioned with the implementing fixtures; they are not runtime test results.

Validation follows the [Mac development / Windows milestone workflow](multiplayer-development-workflow.md). Windows runtime evidence is grouped by scenario/checkpoint, not required after every local edit or work item. Final Windows requirements remain mandatory; qualified provisional progress within a bounded batch is allowed as described below.

**Required implementation deliverable for every Windows check:** prepared/versioned scripts, checkpoint configuration, short instructions with the actual command/expected result/manual actions, and automatic log/report collection. This applies to build-only, engine, SDK/bake, load/soak and human-review cases. Missing scripts/runbooks means the implementation is not ready to hand off; the user is not responsible for creating them. Start the bootstrap kit in MP-001/002 and evolve it through MP-013.

## How to use this backlog

1. Pick an item whose required contracts are established for the work being attempted. Full dependency completion is required at gates; within a named validation batch, a tested portable contract can support dependent Mac work while its Windows integration remains pending. Mark that dependency provisional for Windows and keep final checkboxes open. Ranges include both endpoints.
2. Select **one behavior** from the Red cases and author its smallest test first. Run portable Red/Green immediately on Mac. For Windows-only behavior, preserve a buildable test-only candidate and validate the failing/fixed candidates at the assigned checkpoint. Before that run, label it Windows regression prepared/unverified, not an executed TDD cycle. A missing asset/toolchain, timeout or compile error is not behavioral Red.
3. Implement only enough production code to make that behavior pass. Add the next case, repeat, then refactor with the tests green. The cases are a checklist to implement incrementally, not an instruction to write an entire suite before implementation.
4. Use production algorithms in tests. Fake external time, delivery, random input, and collision-query boundaries where necessary; do not maintain a second movement, packet, or combat implementation only for tests. Test state/results/events rather than private member layout or mock call order without a lifetime reason.
5. Run relevant available portable tests on Mac after changes. Batch Windows build/runtime scenarios at the named checkpoints in the workflow; a single exact-SHA run can supply evidence for several items. Keep deterministic tests free of wall-clock sleeps, and real socket/process tests bounded with retained diagnostics. Automated compile feedback may run more frequently without requiring manual Windows switching.
6. Check the completion box only with the listed evidence. Use **Mac checks passed / Windows pending** or **implemented / unverified** accurately, name the checkpoint, and continue independent or contract-ready work within that bounded batch. Do not bypass MP-005 or a dependent physics/content/gunplay decision that needs real runtime evidence. Skips, missing maps and unexecuted scenarios are not passes.
7. Split an item further when it cannot fit a focused review or its Red cases need unrelated changes. Preserve the parent ID and add suffixes such as `MP-025a`; update dependencies. Do not turn one item into a subsystem rewrite.
8. Before requesting Windows execution, deliver the [required checkpoint kit](multiplayer-development-workflow.md#required-deliverables-for-every-checkpoint), verify its available checks and provide the precise invocation plus result-bundle path. Label first-run/unverified Windows behavior honestly. Human tests include scripted preparation/collection and a short observation checklist.

**Kinds:** `TDD` means Red/Green/Refactor; `CHAR` captures current behavior without assuming it is desirable; `SPIKE` resolves a measured technical/design question; `GATE` validates integrated behavior or records a product decision. A characterization that already passes should stay green. Spikes and human playtests need evidence, not an artificial failing unit test.

**Runs:** `P` = portable executable on macOS and Windows after MP-010; `N` = real GNS peers without the engine after MP-012; `W` = Windows engine/server integration; `H` = human playtest or external-service access. These are requirements for future tests, not assertions that the current engine builds on Mac. If a source seam cannot yet run portably, record that limitation and keep its Windows integration test mandatory.

**Completion evidence for every TDD item:** selected case, observed behavioral failure and passing result, requested/actual candidate SHAs, configuration, fixture/content revision, seed and platform/checkpoint coverage. Deferred Windows comparisons record their test-only/fixed SHAs and actual execution sequence. A checkpoint report can cover several items, with each case mapped explicitly. Proposed artifacts: `build/test-results/multiplayer/<commit>/<run-id>/`. Include clocks/rates/network/hardware; keep bulky captures outside source control.

**Shared constraints:** keep game authority in `xrGame`, transport resources in `xrNetServer`, and narrow timing hooks in `xrEngine`; no new game/renderer implementation dependencies in `xrCore`. Follow the repository's aliases, RAII, lock wrappers, platform split, and naming/format rules. Runtime validation must survive Shipping assertion removal. Preserve MP-off single-player behavior and formats. Optional items must be explicitly selected or recorded as deferred; they do not silently block the IP release gate.

## Delivery order and gates

| Parent-plan phase | Work items | Evidence before advancing |
| --- | --- | --- |
| 0. Baseline and test foundations | MP-001–004, MP-010–013 | Reproducible Windows baseline and runnable test harness; failures classified by cause |
| 0B. Early preferred-NPC proof | MP-005 | One existing stalker demonstrated in DM with two clients before larger engine networking/prediction changes |
| 1. IP transport and simple DM foundation | MP-020–029 | Bounded protocol/admission, owned transport lifetime, independent direct join |
| 2. Authority and timing | MP-030–039 | Fixed authoritative steps, legal commands, shared roster, coherent joining/round epochs and console/configuration controls |
| 3. Prediction and interpolation | MP-040–047 | Shared movement, acknowledged replay, remote interpolation, coherent clocks |
| 4. Combat comparison | MP-050–061 | Both rifle prototypes, mixed roster, local presentation, measured decision |
| 5. Selected combat implementation | MP-062; MP-066–067 only if selected | Chosen weapon rules and compensation tests; optional distance hybrid separately scoped |
| 6. Bots and arena data | MP-070–079, MP-100–109 | Networked stalker NPC brain/body, measured capability profile, baked/attached navigation, climb actions and tactical behavior |
| 7. Integration and release | MP-080–088 | Network/load/soak/regression evidence plus human assessment |
| 8. Steam later | MP-090–094 | Explicit service prerequisites and Steam path verified separately |

This table groups delivery; each item's dependency list controls its actual order. Foundations and portable tests can advance while Windows baseline work is pending. Do not bypass an integration gate simply because its pure unit tests pass.

The [W0–W8 runtime checkpoint table](multiplayer-development-workflow.md#windows-runtime-checkpoints) groups these work items into Windows sessions. A dependency's pending Windows half does not force a manual trip before every next portable test; a hard feasibility/decision gate still requires its actual evidence.

MP-005 is an explicit gate before larger engine networking/controller/prediction rewrites. Portable harness/schema work and narrow repairs needed to run the proof can proceed; neither bypasses its Windows feasibility evidence. The temporary demo does not complete production NPC integration MP-070.

### Test execution contract to establish

The standalone `TestMultiplayerUnit` target uses the same production code as the engine. The [full-engine test entry](../../src/test/CMakeLists.txt) registers it when `IXRAY_TESTS` and multiplayer are enabled. The portable executable requires only a C++ compiler and CMake.

```sh
# Standalone portable production-code tests; no full-engine configure on Mac.
cmake -S src/test/multiplayer/portable -B build/mp-portable
cmake --build build/mp-portable
ctest --test-dir build/mp-portable --output-on-failure
```

The full Windows integration command is planned for MP-013, when `TestMultiplayerEngine` is available.

```powershell
# Windows full-engine tests, after their targets and CTest registration exist.
cmake --preset Server-x64-Windows -DIXRAY_MP_SQL=OFF -DIXRAY_TESTS=ON
cmake --build --preset Server-x64-Windows-RelWithDebInfo --target xrEngine xrServerCLI TestMultiplayerUnit TestMultiplayerEngine
ctest --test-dir build/x64/Server-Windows -C RelWithDebInfo --output-on-failure -L multiplayer
```

The [peer runbook](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) documents standalone GNS and the Windows batch. MP-013 covers multi-process engine scenarios. Existing `-autotest` can host/capture scenarios but does not replace multiplayer assertions; `-autotest_cmd` must remain last. Keep scenario assertions, process exit codes, and timeouts distinct from render-frame collection.

## Baseline and test foundations

### MP-001 — Capture the existing Windows multiplayer baseline

- **Kind / runs / depends:** CHAR + SPIKE; `W`; none.
- **Observe first:** configure the existing server preset; build client/CLI server; attempt two independent clients joining an installed MP map, firing, dying, respawning, leaving, and reconnecting. Capture actual mode/port syntax, effective physics/network rates, `SV_Client` responsibilities, and service dependencies. Separate build failures from runtime/gameplay failures.
- **Deliver / seam:** a minimal W0 PowerShell bootstrap/preflight/build/launch/collect kit and runbook using [presets](../../CMakePresets.json), [CLI entry](../../src/xrServerCLI/xrDedicatedServer.cpp), [startup](../../src/xrGame/Level_start.cpp), and [server routing](../../src/xrGame/xrServer.cpp). Supply actual commands, expected/manual observations, supported shell/tool versions, paths and a collected baseline report. Record blockers as failing reproductions; do not require the later MP-013 harness or expand this into modernization.
- **Progress:** the [W0 bootstrap](multiplayer-checkpoints/w0.md) prepares an isolated checkout, stages game data, probes listener failure, and launches a server with two clients. Its timed `Observe` case records firing, death, respawn, disconnect, and reconnect. The operator reports a Windows W0 pass. The report and effective runtime rates are pending.
- [ ] **Complete when:** Windows build/run evidence exists, success or specific blockers are documented, and the original behavior can be reproduced. A successful restored two-client match is required before the phase-1 integration gate, even if baseline discovery itself found failures.

### MP-002 — Isolate and control multiple engine processes

- **Kind / runs / depends:** TDD; `W`; MP-001 for the launch configuration.
- **Red:** launch two development clients with distinct writable data/log roots; prove the current instance guard or shared paths block/corrupt the scenario. Test child crash, startup failure, timeout, and normal cleanup; the runner must return failure and retain each process's diagnostics.
- **Green / seam:** explicit development multi-instance support in [Application.cpp](../../src/xrPlay/Application.cpp), isolated filesystem configuration and the delivered W0 script/process runner owning child lifetimes, cleanup and per-process log collection. Include start/stop/rerun instructions; keep production single-instance policy unless separately selected.
- [ ] **Complete when:** one runner owns a server and two independent clients, recognizes semantic ready/failure signals, and reliably tears down only its own processes.

### MP-003 — Record wire and gameplay characterization fixtures

- **Kind / runs / depends:** CHAR; `W` capture, `P` consumption later; MP-001.
- **Observe first:** capture a small join/spawn/update/fire/death/disconnect trace with known participants and map; locate current state-forwarding, hit-control, compression, and FreeMP-only NPC synchronization paths. Strip credentials and machine-specific data.
- **Deliver / seam:** minimal annotated fixtures and behavior contracts from [Level_network.cpp](../../src/xrGame/Level_network.cpp), [actor_mp_client.cpp](../../src/xrGame/actor_mp_client.cpp), [NET_Common.cpp](../../src/xrNetServer/NET_Common.cpp), and the stalker sync path. Mark unsafe legacy behavior as characterization, not a target expectation for the new protocol.
- [ ] **Complete when:** fixtures are reproducible and versioned, with documented decoded fields and the distinction between legacy and new-protocol tests.

### MP-004 — Define the arena, DM rules, and measurable acceptance inputs

- **Kind / runs / depends:** SPIKE + GATE; `W/H`; MP-001.
- **Observe first:** choose the arena after inspecting compiled geometry, collision, spawn points, weapon assets and navigation inputs rather than relying on names. Reference hardware is AMD Ryzen 7 5700 / 32 GB RAM / RTX 3080; record Windows/graphics settings and separate isolated-server from cohosted-client load. Confirm basic spawn equipment, weapon/ammo pickup roster/timers and spectator capacity. Agreed configurable defaults are five-second automatic respawn, two-second protection ending on firing, and rounds ending after 20 minutes or 50 frags.
- **Deliver / seam:** one versioned test arena/content manifest, small encounter fixtures, and a rules sheet. Keep unresolved gunplay policy for MP-061; no full ALife requirement. Establish movement/correction tolerances through measurements rather than arbitrary assertions.
- [ ] **Complete when:** independent Windows runs can load the same content/rules, and every later numeric performance/feel claim has a named hardware/content context.

### MP-005 — Prove one existing stalker in DM before larger modernization

- **Kind / runs / depends:** CHAR + bounded SPIKE + integration GATE; `W`; MP-001–004.
- **Prerequisites:** restored two-client baseline and minimal valid NPC navigation fixture/arena subset. A small temporary test arena is acceptable; full MP-100–109 production tooling is not required and is not a dependency of this proof.
- **Observe / Red:** demonstrate an existing server-owned stalker to two independent clients: move, turn, shoot/reload, take damage, earn/award a frag, die and respawn. Identify NPCs in DM without a fake socket, verify only the authoritative side runs the brain, and record missing animation/state, actor-only assumptions and ALife/bootstrap dependencies. Add the smallest failing reproduction before each narrow integration fix; do not label missing assets a behavior-test failure.
- **Deliver / seam:** isolated prototype adaptations to existing stalker movement/planning/weapon services, DM identity/score and legacy NPC sync, plus the W1 scripts, scenario configuration, fixture identity, runbook/expected observations and automatic results/capture collection. Rough presentation and temporary internal code are allowed. Inventory every temporary adapter and its removal/hardening owner; keep it out of production new-protocol admission paths.
- [ ] **Complete when:** the complete one-NPC/two-client scenario is demonstrated on Windows and the preferred route's concrete integration work is recorded. A blocker report is useful evidence but does not pass the gate; resolve it or explicitly revisit the retained fallback decision before larger rewrites. This does not claim modern prediction, compensated combat, production nav or 32-bot capacity.

### MP-010 — Add an independent portable test target

- **Kind / runs / depends:** TDD; `P`; none.
- **Red:** a minimal test of a real, narrow production value/sequence utility fails behaviorally; intentionally invert its assertion to verify a nonzero process/CTest result. Confirm the standalone configure does not pull in renderer, Lua, engine DLLs, or Windows SDK libraries.
- **Green / seam:** proposed `src/test/multiplayer/portable` CMake entry and `TestMultiplayerUnit`, CTest registration, and framework choice. Compile the same production sources in this target and the engine; do not duplicate algorithms to get a green Mac build.
- **Progress:** Mac checks passed. The optional [W0 Portable](multiplayer-checkpoints/w0.md#additional-checks) report is pending. CTest ran the production 32-bit serial comparison in `TestMultiplayerUnit` with dependency-free assertions. The first wraparound case failed under integer ordering. The reviewed suite exposed six wrong results under that ordering and passed all 11 comparisons with modular ordering. The opt-in inverted assertion returned a nonzero CTest exit in Release. The fixtures use literal inputs and require no seed or content pack.
- [ ] **Complete when:** a real pass and deliberate failure are demonstrated on Mac and Windows, with test discovery and commands recorded. No engine-to-core dependency reversal is introduced.

### MP-011 — Supply controllable time, randomness, and event recording

- **Kind / runs / depends:** TDD; `P`; MP-010.
- **Red:** advance virtual time across a tick/deadline boundary and assert exact expiry/order; same seed and inputs reproduce an event trace; changing cosmetic random calls does not change weapon simulation. No test waits on actual time.
- **Green / seam:** narrow injectable clock/random boundaries and a test event recorder used by real production consumers. Keep independent simulation and cosmetic random streams; engine adapters remain thin.
- [ ] **Complete when:** repeated runs reproduce the same deterministic fixtures, and the test suite detects incorrect boundary ordering without sleeps.

#### MP-011a — Use a monotonic deadline for map synchronization

- **Kind / runs / depends:** TDD; `P` plus a Windows startup smoke check; MP-010.
- **Design:** start the five-second wait when the map request is sent. Evaluate it after receiving packets, so a reply processed on the same update takes precedence. Demo playback keeps waiting for the recorded reply. Each fresh request gets its own deadline. [Engine adapter](../../src/xrGame/Level_network_map_sync.cpp), [portable policy](../../src/xrGame/Multiplayer/MapSyncWait.h).
- **Mac checks passed / Windows pending:** `Multiplayer.MapSyncDeadline` failed against the extracted poll-count policy at the exact deadline, after a loading stall, after repeated polls before the deadline and on a fresh request. With explicit monotonic timestamps the boundary, polling, reply-priority, demo and fresh-request cases pass. The fixture uses literal timestamps and requires no seed or content pack. Both portable tests passed on macOS in Debug and Release. Run the focused case with `ctest --test-dir build/mp-portable -R '^Multiplayer.MapSyncDeadline$' --output-on-failure` after the standalone build above. [Tests](../../src/test/multiplayer/portable/MapSyncWaitTests.cpp).
- [ ] **Complete when:** the [W0 portable runner](multiplayer-checkpoints/w0.md#additional-checks) collects a passing deadline suite on Windows, and a new Windows join smoke run confirms the engine adapter on the same candidate. Both can run with the next Windows checkpoint session.

#### MP-011b — Isolate weapon spread sampling and record deterministic sample traces

- **Design:** the portable sampler takes explicit unit and sign draws. One engine adapter supplies the existing `Random` and C `rand()` draws and projects the resulting angles through the engine vector basis. Both bullet launch and ricochet use it. The sampler preserves draw order, including the azimuth draw for zero dispersion. If the magnitude sample is zero, a nonzero acceptance draw retries and two zero draws produce a clamped angle without evaluating the logarithm. MP-051 owns seeding and restoring authoritative weapon state. [Sampler](../../src/xrGame/Weapons/WeaponSpread.h), [engine adapter](../../src/xrGame/Weapons/WeaponSpreadLegacy.cpp).
- **Mac checks passed / Windows pending:** `Multiplayer.WeaponSpread` failed when sign selection still called global `rand()`. The reviewed suite detected the missing injected sign draws and diverging traces from the same seed. Scripted cases cover signs, retries, clamps, and endpoint draws. With seed `0x31823d45`, the ordered angle trace repeats across calls and with cosmetic samples interleaved through a separate source. Run `ctest --test-dir build/mp-portable -R '^Multiplayer.WeaponSpread$' --output-on-failure` after the standalone build above. [Tests](../../src/test/multiplayer/portable/WeaponSpreadTests.cpp).
- [ ] **Complete when:** the [Windows portable runner](multiplayer-checkpoints/w0.md#additional-checks) records a passing spread suite and the same candidate builds and passes the firing and death observations in the Windows two-client smoke run.

### MP-012 — Add deterministic message delivery and real GNS peer fixtures

- **Kind / runs / depends:** TDD; `P/N`; MP-010–011.
- **Red:** scripted messages are dropped, delayed, duplicated, reordered, and constrained by bandwidth exactly as the fixture specifies; injected connect/send/receive/close failures reach the consumer. A real loopback peer pair exchanges payloads and detects closure within a deadline.
- **Green / seam:** a narrow transport boundary, deterministic delivery scheduler for gameplay tests, and a separate real GNS peer executable. Pin compatible GNS headers/libraries across hosts using [dependency CMake](../../cmake/modules/GameNetworkingSockets.cmake). Keep fake delivery behavior independent of the production sequence logic under test.
- **Fixture contract:** each direction has its own send script and optional byte rate in bytes per millisecond. An empty plan drops a message, multiple offsets schedule copies, and equal arrival times preserve insertion order. Delays start after transmission and loss still consumes transmission time. Tests drive the clock explicitly. Connect, send, receive and close faults are visible to the caller and leave the affected operation retryable. The real fixture uses reliable UDP loopback, ephemeral ports, a two-second receive deadline and scope-owned handles and runtime. [Fake](../../src/test/multiplayer/transport/ScriptedTransport.h), [shared exchange](../../src/test/multiplayer/transport/TransportScenario.h), [GNS peer test](../../src/test/multiplayer/gns/GnsTransportTests.cpp).
- **Mac checks passed / Windows pending:** the fake initially delivered too early and ignored faults. The reviewed tests also exposed missing duplicate scheduling, incorrect rate limits and partial publication on invalid timestamps. The real peers exchanged all payloads and failed on close classification before the fix. Both now pass the shared binary exchange and close scenario. Portable checks passed in Debug, Release, and under ASan/UBSan. Twelve consecutive real-peer runs passed on macOS, including reopening after teardown and checking that the GNS runtime was released. Inputs are literal and require no game assets or seed. [Windows batch and standalone GNS commands](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch).
- [ ] **Complete when:** the same logical protocol tests run through the fake transport and selected real-peer cases, and no open socket/worker outlives its fixture.

### MP-013 — Make failures and platform coverage visible

- **Kind / runs / depends:** TDD for runner behavior; `P/W`; MP-002, MP-010–012.
- **Red:** assertion failure, child crash, missing fixture, timeout and skip produce distinct non-pass results; successful process exit without a scenario assertion cannot pass. A wrong-SHA/dirty validation checkout, stale artifact or missing requested case cannot close a checkpoint. Re-run a failed seed/trace by its ID; deferred regression comparisons distinguish an intended behavioral Red from a build/asset failure.
- **Green / seam:** extend the MP-001/002 bootstrap kit into shared CTest/`TestMultiplayerEngine` orchestration, machine-readable reports and exact-SHA checkpoint manifests. Deliver required PowerShell entry point, checkpoint definitions and short runbooks: synchronize an owned validation checkout, build, launch requested cases and collect diagnostics without modifying the ordinary checkout. Include preflight, deadlines, runner-owned cleanup, rerun/collect modes and scripted setup plus a checklist for human review. Only remote CI/SSH triggering is optional.
- [ ] **Complete when:** delivered scripts/configuration/runbooks and an actual Windows report demonstrate correct revision, isolation, useful failure collection and applicable deferred failing/fixed comparisons. Each requested test has an exact invocation and named result bundle; no user-written scripts or improvised commands are needed. Distinguish build-only/runtime/human review/failure/skip/unexecuted states. Several items may share a report; compilation alone is not gameplay acceptance.

## Transport and admission

### MP-020 — Specify and implement checked message encoding

- **Kind / runs / depends:** TDD; `P`; MP-010–011.
- **Red:** hand-authored golden bytes decode into the expected fields; every truncated prefix and invalid count/tag/string/NaN/Inf input is rejected without committing partial state. Encoding reaches documented quantization bounds. A round trip alone is insufficient because encoder/decoder can share a bug.
- **Green / seam:** freeze each message family's numeric tags and field order from the [wire contract](multiplayer-implementation-contracts.md#3-version-one-wire-contract), starting with its little-endian 30-byte envelope and bounded fields. Use independent golden bytes, a checked reader/writer and immutable decoded values owned by `xrNetServer`; reject unsupported versions and raw-struct-layout assumptions explicitly.
- [ ] **Complete when:** golden vectors run on both platforms and malformed input is rejected in an assertions-disabled build as well as Debug.

#### MP-020a — Encode and validate the common wire envelope

- **Design:** freeze the 30-byte little-endian header, the 1,100-byte complete-message limit, zero reserved flags, and the first three admission tags. Decode returns a copied header and a borrowed read-only payload. Encode validates the tag and sizes before writing, including when the payload overlaps the output. Typed payload fields, admission, and dispatch follow in later slices. [Wire contract](multiplayer-implementation-contracts.md#3-version-one-wire-contract), [codec](../../src/xrNetServer/Multiplayer/WireEnvelope.h).
- **Mac checks passed / Windows pending:** `Multiplayer.WireEnvelope` first failed on a hand-authored welcome frame. The reviewed suite covers all truncated prefixes, all reserved tags and flag values, unsupported versions, exact lengths, both sides of the size cap, and untouched output on failure. The golden tests exercise the same codec compiled into `xrNetServer`. Fixtures use literal data without a seed or content pack. [Tests](../../src/test/multiplayer/portable/WireEnvelopeTests.cpp).
- [ ] **Complete when:** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) reports a passing `wire-envelope.xml` on Windows.

#### MP-020b — Add checked payload fields

- **Design:** use caller-owned buffers and explicit little-endian integer fields, finite binary32 values, and bounded UTF-8 strings with a `u16` byte count. Readers and writers preserve their cursor on field failure. Frame and payload codecs share their endian conversion. [Payload encoding](multiplayer-implementation-contracts.md#payload-field-encoding), [codec](../../src/xrNetServer/Multiplayer/WirePayload.h).
- **Mac checks passed / Windows pending:** `Multiplayer.WirePayload` first failed on overlong UTF-8. The reviewed tests also reproduced acceptance of NaNs and infinities and a changed read cursor. Hand-authored bytes now verify field order and values. Boundary tests cover truncated fields, byte and capacity limits, UTF-8 scalar validity, overlapping output, and exact finite float bits. Fixtures use literal data. [Tests](../../src/test/multiplayer/portable/WirePayloadTests.cpp).
- [ ] **Complete when:** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) reports a passing `wire-payload.xml` on Windows.

#### MP-020c — Encode and validate client hello

- The codec carries 32-byte build, content, and rules compatibility identifiers and bounded fields for the requested name and credentials. It checks that the envelope is unbound and consumes the whole payload before returning a hello. Encoding validates a bounded temporary payload before writing the complete frame. Compatibility matching and credential verification belong to MP-022. [Client hello wire layout](multiplayer-implementation-contracts.md#client-hello), [codec](../../src/xrNetServer/Multiplayer/ClientHello.h).
- **Mac checks passed / Windows pending.** The first hand-authored datagram failed before implementation. Reviewed tests exercise whole-frame and typed truncation, bound identities, trailing data, name and credential limits, and atomic encoding with overlapping buffers. Tests use literal fixtures. [Tests](../../src/test/multiplayer/portable/ClientHelloTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `client-hello.xml` on Windows.

#### MP-020d — Encode and validate server welcome

- The welcome carries assigned session and connection identity, the content and rules IDs, the logical map ID, effective millihertz rates, and the initial respawn, protection, and round limits. Encoding stages a bounded payload before publishing the frame. [Server welcome wire layout](multiplayer-implementation-contracts.md#server-welcome), [codec](../../src/xrNetServer/Multiplayer/ServerWelcome.h).
- **Mac checks passed / Windows pending.** The hand-authored welcome failed before implementation. The reviewed suite covers framing and typed truncation, fractional rates, zero-rate rejection, map byte bounds, unassigned identity, exact payload consumption, and atomic writes with overlapping buffers. Fixtures use literal data. [Tests](../../src/test/multiplayer/portable/ServerWelcomeTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `server-welcome.xml` on Windows.

#### MP-020e — Encode and validate admission reject

- The codec carries one stable reason byte in an unbound envelope and shares the unbound identity check with `ClientHello`. The client can display local text for each reason. [Admission reject wire layout](multiplayer-implementation-contracts.md#admission-reject), [codec](../../src/xrNetServer/Multiplayer/AdmissionReject.h).
- **Mac checks passed / Windows pending.** The initial golden datagram failed before implementation. The reviewed suite verifies all 256 reason bytes, header and payload boundaries, unbound identity, preserved server sequence, and atomic encoding. Fixtures use literal data. [Tests](../../src/test/multiplayer/portable/AdmissionRejectTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `admission-reject.xml` on Windows.

#### MP-020f — Encode and validate client join confirmations

- Assign bound tags for the empty map-loaded confirmation and the ready acknowledgement carrying the exact barrier. Reuse the join identity and barrier types in the codec and readiness tracker. [Wire layout](multiplayer-implementation-contracts.md#client-join-confirmations).
- **Mac checks passed / Windows pending.** Independent golden frames failed before implementation. The reviewed tests cover all truncated prefixes, exact payload sizes, bound identities, distinct tags, owned decoded fields, and atomic writes. A decoded acknowledgement from a different connection or earlier world cannot advance `PeerJoin`. [Tests](../../src/test/multiplayer/portable/ClientJoinTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `client-join.xml` on Windows.

#### MP-020g — Encode and validate baseline transfer frames

- Assign bound tags for baseline begin, chunk, and end. Validate the declared byte, record, and chunk limits. Use fixed chunk strides with a possible short final chunk and borrow decoded chunk data. The transfer owner checks completeness and stages records. [Wire layout](multiplayer-implementation-contracts.md#initial-state-baseline-transfer).
- **Mac checks passed / Windows pending.** Independent golden frames failed before implementation. Reviewed tests cover truncation, fixed payload lengths, bound identities, the exact limits and inconsistent totals, invalid indices and chunk lengths, and atomic encoding with aliased input. Existing client join confirmations share the bound framing checks. [Tests](../../src/test/multiplayer/portable/BaselineTransferTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `baseline-transfer.xml` on Windows.

### MP-021 — Bound legacy envelope parsing and decompression

- **Kind / runs / depends:** TDD; `P` where extracted, `W`; MP-003, MP-010, MP-020.
- **Red:** zero/short headers, nested length beyond remaining bytes, advertised output above limit, failed/short decompression and truncated merged packets are rejected. Later malformed data must not partially mutate the world through earlier records in an invalid envelope.
- **Green / seam:** validate before pointer arithmetic, decompression, dispatch, or allocations in [NET_Common.cpp](../../src/xrNetServer/NET_Common.cpp) and [NET_Compressor.cpp](../../src/xrNetServer/NET_Compressor.cpp). Decode/validate an envelope before applying it; preserve intentionally retained legacy/SP formats.
- [ ] **Complete when:** captured valid fixtures still work, the negative corpus cannot crash or exceed allocation limits, and Shipping rejection does not depend on assertions.

### MP-022 — Enforce admission as a state machine

- **Kind / runs / depends:** TDD; `P/N/W`; MP-011–012, MP-020.
- **Red:** missing/wrong password, version/content mismatch, gameplay before admission/readiness, admission/loading/synchronization timeout, duplicate handshake and excess pending peers are handled explicitly. Round reset while loading invalidates the previous readiness acknowledgement. A claimed localhost identity/process ID cannot gain internal-server rights.
- **Green / seam:** explicit connected/admitted/loading/synchronizing/ready/closing states with bounded deadlines and an admission exchange around [SteamNetServer.cpp](../../src/xrNetServer/SteamNetServer.cpp). Integrate existing config/ready markers only after validating the current session/round baseline; keep internal simulation ownership outside peer claims.
- [ ] **Complete when:** two compatible peers admit, every incompatible/state-invalid case has a stable rejection reason, and no unadmitted command reaches the world.

#### MP-022a — Verify the first hello before requesting a slot

- Use the portable wire codecs to validate a complete unbound hello, compare server-supplied compatibility IDs, and apply a trusted credential verifier. The tracker advances through `AwaitingHello`, `AwaitingSlot`, or `Closing` using the owner's monotonic deadline. A terminal transition produces one rejection. A later hello leaves the existing slot request intact. [Pending admission contract](multiplayer-implementation-contracts.md#pending-admission-decision), [tracker](../../src/xrNetServer/Multiplayer/PendingAdmission.h).
- **Mac checks passed / Windows pending.** The initial compatible hello failed before implementation. The reviewed tests cover independent peers, wire and compatibility failures, credential policy, duplicates, and exact deadline ordering with literal timestamps. Slot allocation, welcome dispatch, loading, readiness, and engine integration are later slices. [Tests](../../src/test/multiplayer/portable/PendingAdmissionTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `pending-admission.xml` on Windows.

#### MP-022b — Reserve a slot and queue the welcome

- Complete the portable handoff on the owner turn. Validate the bound welcome before touching capacity, queue reliably while a slot is held, and commit admission only when the queue accepts the packet. A rejected queue operation cancels the reservation. The connection owner supplies identities and the roster adapter owns eligibility, bot replacement, and committed-slot cleanup. [Admission completion contract](multiplayer-implementation-contracts.md#admission-completion).
- **Mac checks passed / Windows pending.** The initial welcome failed before implementation. The reviewed tests cover competing peers, capacity reuse, queue rollback, duplicate completion, expiry at the exact deadline, unassigned identities, and inconsistent welcome settings. Pending-peer limits, loading and engine integration are later slices. [Tests](../../src/test/multiplayer/portable/AdmissionCompletionTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `admission-completion.xml` on Windows.

#### MP-022c — Bound pending admission

- Start each tracker through a per-owner pending limit. `AwaitingHello` and `AwaitingSlot` both consume capacity. Overflow yields `ServerBusy`, while admission, terminal rejection, timeout, and disconnect return capacity. The move transfers ownership of the charged place. [Pending capacity contract](multiplayer-implementation-contracts.md#pending-admission-capacity).
- **Mac checks passed / Windows pending.** The reviewed tests failed with an unbounded start. They exercise zero and finite capacity, repeated overflow, isolation between owners, capacity reuse, all terminal outcomes, and cleanup after a move. Transport deduplication and the live connection owner are part of the engine integration. [Tests](../../src/test/multiplayer/portable/PendingAdmissionLimitTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `pending-admission-limit.xml` on Windows.

#### MP-022d — Gate spectator readiness on the current join barrier

- Start the portable tracker after welcome queueing. Use separate loading and synchronization deadlines. Accept only an exact acknowledgement of the server-declared current-world barrier, and invalidate readiness and stale work on a same-map world advance. [Join readiness contract](multiplayer-implementation-contracts.md#server-side-join-readiness).
- **Mac checks passed / Windows pending.** The initial readiness trace failed before implementation. Reviewed tests cover the two exact deadlines, complete identity and barrier matching, duplicate or stale events, round reset before and after map loading, and an epoch that cannot wrap. Baseline staging, gameplay activation, and the engine adapter remain later work. [Tests](../../src/test/multiplayer/portable/PeerJoinTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `peer-join.xml` on Windows.

### MP-023 — Separate connection, participant, entity, and life identity

- **Kind / runs / depends:** TDD; `P/W`; MP-020, MP-022.
- **Red:** reuse a transport handle/entity slot after disconnect or respawn; an old command, snapshot, hit or ack cannot change the replacement. Reset a round on the same map and restart the process; old-round/session payloads and readiness acknowledgements are rejected. A bot participant exists without a socket. Reconnect does not inherit another participant's authority.
- **Green / seam:** distinct server-session, world/round epoch, connection/admission, participant and entity/life identities. A round reset invalidates old input/shot/snapshot/history state independently of static navigation generation; a new map initially requires server restart. Do not treat `ClientID`, map name or `HSteamNetConnection` as every identity.
- [ ] **Complete when:** identity-reuse traces remain harmless after wrap/reordering cases, with bounded mapping cleanup on disconnect.

### MP-024 — Own process-level GNS lifetime

- **Kind / runs / depends:** TDD; `P/N/W`; MP-012.
- **Red:** init failure, repeated acquire/release, server plus local client ownership, and shutdown during pending activity neither double-kill the runtime nor leave callbacks targeting destroyed state. Use injected failures plus a real-peer lifecycle scenario.
- **Green / seam:** one RAII runtime owner with clear endpoint ownership in `xrNetServer`; replace independent endpoint init/kill assumptions in the SteamNet classes. Test lifecycle order only where required to prevent use-after-free.
- [ ] **Complete when:** final endpoint teardown joins owned workers and destroys callbacks/resources before runtime shutdown, including partial startup failure.

### MP-025 — Own endpoint resources and unwind every creation failure

- **Kind / runs / depends:** TDD; `P/N/W`; MP-024.
- **Red:** fail listener, poll-group, connection and worker creation in turn; every earlier resource is released exactly once. Test cancellation before connect, close while receiving, peer-initiated close, and re-creation after failure.
- **Green / seam:** endpoint state/lifetime objects in [SteamNetClient.cpp](../../src/xrNetServer/SteamNetClient.cpp) and [SteamNetServer.cpp](../../src/xrNetServer/SteamNetServer.cpp), RAII received-message release, explicit worker stop/join, and safe callback association.
- [ ] **Complete when:** failure injection and 100 real peer connect/close cycles show bounded resource counts and no surviving endpoint worker.

### MP-026 — Apply network events only at the simulation boundary

- **Kind / runs / depends:** TDD; `P/W`; MP-022–025.
- **Red:** enqueue packet/connect/disconnect events from a receive worker; no actor/client-world mutation occurs until the simulation drains them. Fill the queue and exceed a drain budget; limits hold without starving ticks or reordering lifecycle events incorrectly.
- **Green / seam:** bounded event queues around [BaseServer.cpp](../../src/xrNetServer/BaseServer.cpp), [INetQueue](../../src/xrNetServer/INetQueue.h), and endpoint callbacks. Define overflow and disconnect-event handling rather than silently dropping necessary cleanup.
- [ ] **Complete when:** owner-thread instrumentation and stress scenarios show no world/Lua/render execution from callbacks, with bounded memory and work per drain.

### MP-027 — Separate traffic classes and handle backpressure

- **Kind / runs / depends:** TDD; `P/N`; MP-012, MP-020, MP-025.
- **Red:** saturate outgoing capacity; stale snapshots are replaced while essential admission/lifecycle messages either progress within limits or cause an explicit disconnect. A send refusal is not recorded as successful delivery. Bulk data cannot indefinitely delay gameplay.
- **Green / seam:** explicit command/snapshot/control/cosmetic traffic policy replacing blanket flag/batching assumptions in [NET_Messages.h](../../src/xrNetServer/NET_Messages.h) and sender code. Use supported GNS lanes only after header/library capability checks.
- [ ] **Complete when:** deterministic and real-peer pressure tests expose queue age/bytes and maintain documented bounds without unbounded reliable backlog.

### MP-028 — Join by IP without GameSpy service availability

- **Kind / runs / depends:** TDD; `W/N`; MP-001–004, MP-021–027.
- **Red:** disable external discovery/profile-service access; independent clients must still join a compatible numeric-IP server. Invalid/unreachable endpoints fail with a reason/deadline; hostname handling is a separate explicit case.
- **Green / seam:** separate direct-IP session creation/admission from legacy service work in [Level_start.cpp](../../src/xrGame/Level_start.cpp), [xrGameSpyServer.cpp](../../src/xrGame/xrGameSpyServer.cpp), and client connection options. Do not remove content compatibility checks to make joining pass.
- [ ] **Complete when:** the phase-1 two-client DM foundation is demonstrated on Windows with no external discovery dependency and exact launch commands preserved.

### MP-029 — Make disconnect, timeout, and rejoin converge

- **Kind / runs / depends:** TDD; `P/N/W`; MP-023–028.
- **Red:** lose a peer during admission, spawn, active match and shutdown; process a repeated close; reconnect while stale messages remain. Each participant is removed once, internal simulation ownership survives remote departure, and pending queues stop growing.
- **Green / seam:** idempotent cleanup across endpoint state, client mappings, [xrServer.cpp](../../src/xrGame/xrServer.cpp), and level connection flow; bounded deadlines use the injected clock.
- [ ] **Complete when:** 100 engine join/leave/rejoin cycles pass with distinct process logs and stable object/thread counts; remote disconnect never deletes the whole authoritative world accidentally.

## Commands, authority, and deathmatch

### MP-030 — Serialize sequenced player commands

- **Kind / runs / depends:** TDD; `P`; MP-020, MP-023.
- **Red:** decode legal move/look/button edges; reject malformed/out-of-range commands; duplicate/reordered/redundant copies result in one logical command. Test sequence wrap, stale/future windows, and session/life mismatch.
- **Green / seam:** `UserCommand` schema and bounded receive window in a portable production seam. Commands express intent, not authoritative transform/health/damage; isolate action edges from continuous held buttons.
- [ ] **Complete when:** independent golden vectors and delivery traces demonstrate one interpretation of the same command on both hosts.

### MP-031 — Establish the authoritative fixed-step clock

- **Kind / runs / depends:** TDD; `P/W`; MP-004–005, MP-011, MP-026.
- **Red:** feed equivalent elapsed time in different render-frame partitions; produce the same tick/snapshot counts and ordered phase trace. At 50/150 Hz, each normal gameplay step owns three physics substeps. Prove one NPC/movement/weapon/bullet advance and one event commit, including queued fire after movement, a blocking door and round reset. A long stall triggers bounded catch-up/debt handling with an explicit overload result.
- **Green / seam:** implement the [tick-owner/phase contract](multiplayer-implementation-contracts.md#7-one-authoritative-tick-owner-and-phase-order) around `CLevel`, GameThread and the physics interface. Reroute existing `CPHWorld::OnFrame`, `CLevel` bullet and scheduled NPC paths for owned MP state rather than stepping twice. Validate effective physics first and preserve SP; finalize the bounded-debt policy in clock fixtures before passing this item.
- [ ] **Complete when:** clock tests pass without drift, owner-thread order is documented, and Windows records the intended rates independently of client rendering FPS.

### MP-032 — Budget command execution and acknowledge simulated input

- **Kind / runs / depends:** TDD; `P/W`; MP-030–031.
- **Red:** flood future/redundant commands; simulated movement time cannot exceed server allowance. Missing packets hold continuous input only for the configured grace period, clear one-shot actions, then stop safely. Ack advances only after actual simulation, never merely receipt.
- **Green / seam:** bounded command scheduling, safe fill policy and executed-command ack in the authoritative session. Define input expiry and edge acknowledgement/retry so fire/jump/reload are not lost or repeated through redundancy.
- [ ] **Complete when:** loss/reorder/flood traces show no speed-up, duplicate action, unbounded input queue, or acknowledgement of unexecuted work.

### MP-033 — Enforce one 32-combatant roster

- **Kind / runs / depends:** TDD; `P/W`; MP-004, MP-023.
- **Red:** mix humans/bots up to 32 and exercise the [bot-fill contract](multiplayer-bot-ai-design.md#population-joining-and-lifecycle), concurrent human admission reservations, dead/live bot replacement and quota edits. Removal needs no socket, cannot grant a frag, and cannot transfer bot state/score to the joining human. Internal server connection and separately bounded spectators do not consume/evade combatant accounting; dead combatants awaiting respawn still count.
- **Green / seam:** participant roster/service in `xrGame`, adapted from existing player-state and DM ownership. Reserve/replace atomically at a simulation boundary, cancel old bot work, allocate fresh human participant/life identity, preserve historical attribution and debounce refill on departures. Apply the selected fill target/bot cap while retaining the hard 32-combatant limit.
- [ ] **Complete when:** all compositions from 0 to 32, replacement races, and scoreboard counts agree between authoritative state and clients.

### MP-034 — Reject client-authored authoritative state

- **Kind / runs / depends:** TDD; `P/W`; MP-022–023, MP-030, MP-033.
- **Red:** an admitted peer submits legacy position/health/ammo/damage/score mutations, another participant's entity ID, or a privileged spawn/destroy request; the world is unchanged and the request is rejected. Legal intent continues to reach its authorized command queue.
- **Green / seam:** per-message/per-state permissions and entity ownership checks in [xrServer.cpp](../../src/xrGame/xrServer.cpp), [Level_network_messages.cpp](../../src/xrGame/Level_network_messages.cpp), and actor MP routing. Keep trusted internal simulation traffic distinct from peer traffic.
- [ ] **Complete when:** adversarial engine scenarios fail to alter authority in Release/Shipping, including alternate legacy event paths and script-exposed entry points selected for DM.

### MP-035 — Own spawn, respawn, loadout, and pickups on the server

- **Kind / runs / depends:** TDD; `P/W`; MP-004, MP-031, MP-033–034.
- **Red:** repeated respawn requests allocate one life/loadout; respawn increments life generation. Default automatic respawn occurs five server seconds after death, then protection lasts two seconds or ends on firing. Humans and NPCs obey the same health/armor/damage/weapon rules. Start with basic equipment and validate authoritative weapon/ammo pickups, including two simultaneous claims and item respawn; expired/spent items cannot be collected twice.
- **Additional Red:** accepted fire from both protected participants clears their protection consistently before same-tick collision evaluation, independent of iteration order; rejected/dry fire does not. Test protection at the approved historical query/impact time as well as the present-life check. The [spawn contract](multiplayer-implementation-contracts.md#5-round-and-spawn-behavior) defines the engineering interpretation.
- **Green / seam:** shared DM combat/loadout/protection policy with separate actor/NPC adapters; bounded spawn selection and authoritative inventory/item transitions. Normalize NPC-specific legacy damage/armor modifiers to the shared rules while retaining tuned NPC movement. Preserve basic-spawn/pickup-based play without a mandatory buy/economy menu.
- [ ] **Complete when:** humans and test-controlled bots respawn consistently, stale-life requests fail, and late join receives the current inventory/life state.

### MP-036 — Define the authoritative weapon action state machine

- **Kind / runs / depends:** TDD; `P/W`; MP-030–032, MP-034–035.
- **Red:** fire cadence/ammo/chamber, reload cancellation, equip timing, burst/automatic hold and empty fire obey fixture rules under duplicate/lost input. Invalid actions cannot consume negative ammo, reload twice, or shoot while unequipped/dead.
- **Green / seam:** testable weapon-action transitions integrated with [WeaponFire.cpp](../../src/xrGame/WeaponFire.cpp), [WeaponMagazined.cpp](../../src/xrGame/WeaponMagazined.cpp), and inventory. Human commands and trusted NPC object-handler requests converge on these rules through their own adapters. Collision/damage is not implemented here; accepted shots become observable simulation events.
- [ ] **Complete when:** equivalent admitted weapon-action sequences obey the same cadence/ammo/equip rules for human and NPC adapters, and a predicted human client can restore all state required by later replay; no shared movement controller is required.

### MP-037 — Publish self-contained snapshots and durable match events

- **Kind / runs / depends:** TDD; `P/N/W`; MP-020, MP-023, MP-027, MP-031–036.
- **Red:** drop a snapshot; subsequent complete entity state recovers without waiting for it. Deliver spawn/despawn/health/ammo events out of order or twice; identities/events prevent resurrection/double application. Join while entities spawn, die, collect items or the round resets: a staged baseline plus subsequent changes produces coherent current state before readiness. Bounded catch-up overflow requests a fresh baseline rather than partial admission.
- **Green / seam:** world/owner/NPC snapshots, command ack, initial capture tick/event watermark and bounded lifecycle ledger. Stage initial records, catch up to a declared current-session/round barrier and acknowledge readiness before spawning. Start self-contained; add delta baselines only if MP-083 justifies them, with recovery tests first.
- [ ] **Complete when:** sequence tests and real peers converge after bounded delivery gaps, packet sizes are measured, and payload splitting does not require an unbounded reassembly buffer.

#### MP-037a — Assemble a bounded baseline in owner storage

- Bind portable staging to the current join identity. Reuse the wire manifest checks and accept only canonical chunk lengths and offsets within the owner-supplied buffer. Expose a completed byte view once on matching end. A trusted round advance invalidates old receipt state. [Staging contract](multiplayer-implementation-contracts.md#client-side-baseline-staging).
- **Mac checks passed / Windows pending.** The initial two-chunk assembly failed before implementation. Reviewed tests cover decoded frames and datagram reuse, exact storage limits, reordering, duplicates, conflicting updates, early end, reset, and all 4,096 chunk indices. Record framing and aggregate buffer reservation have separate tests in MP-037b/c. Game record decoding and engine integration remain open. [Tests](../../src/test/multiplayer/portable/BaselineStagingTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `baseline-staging.xml` on Windows.

#### MP-037b — Validate the complete baseline record stream

- Check staged identity, manifest, and byte count. Frame the declared records with a kind and bounded payload length, validate each through the supplied schema, and expose an ordered cursor after the full stream passes. [Record framing contract](multiplayer-implementation-contracts.md#baseline-record-validation).
- **Mac checks passed / Windows pending.** The literal two-record fixture failed before implementation. The reviewed tests cover stale identities, malformed suffixes, schema rejection, exact counts, the full `u16` payload length, and a header split across chunks. Concrete entity schemas and world import follow as separate work. [Tests](../../src/test/multiplayer/portable/BaselineRecordsTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `baseline-records.xml` on Windows.

#### MP-037c — Bound aggregate baseline staging reservations

- Reserve bytes and a buffer place from one serialized owner budget before taking backing storage. Each receiver owns a reservation covering its full span through completion and same-map reset. Destruction returns both charges. [Aggregate budget contract](multiplayer-implementation-contracts.md#aggregate-baseline-staging-budget).
- **Mac checks passed / Windows pending.** Reviewed tests failed against the unbounded reserve path. They cover both exact limits, zero-byte transfers, oversized requests, failed attempts, move and destruction, invalid buffer bindings, and reuse through completion and round reset. Production limits and allocator integration require later engine evidence. [Tests](../../src/test/multiplayer/portable/BaselineStagingBudgetTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `baseline-staging-budget.xml` on Windows.

#### MP-037d — Replace a staged baseline within its round

- Accept an owner-selected declaration with a newer baseline ID and capture tick and a current or newer event watermark. Validate identity, manifest, and storage before clearing the previous receipts. Reuse the existing reservation and require the selected baseline ID at record validation. [Replacement contract](multiplayer-implementation-contracts.md#same-round-baseline-replacement).
- **Mac checks passed / Windows pending.** Reviewed tests failed before replacement and baseline ID validation existed. They cover stale data, invalid replacements preserving in-progress and completed transfers, storage reuse, wrap and half-range serials, and world reset. MP-037e covers server retry limits and readiness coordination. [Tests](../../src/test/multiplayer/portable/BaselineRebaseTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `baseline-rebase.xml` on Windows.

#### MP-037e — Gate server rebases and retire stale barriers

- Select a baseline before declaring readiness. Share freshness checks with the receiver. Replace the selection and clear the old barrier while preserving the synchronization deadline. Close on a fourth valid rebase in a rolling 60-second window. [Server rebase contract](multiplayer-implementation-contracts.md#server-side-baseline-rebase).
- **Mac checks passed / Windows pending.** Reviewed tests failed before server replacement existed. They cover selection, stale barriers, rejected requests preserving retry places, exact synchronization and rolling-window deadlines, history across round resets, and wrapped server/receiver handoffs. Live catch-up scheduling and transport integration remain open. [Tests](../../src/test/multiplayer/portable/PeerRebaseTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `peer-rebase.xml` on Windows.

#### MP-037f — Retain a bounded server catch-up stream

- Bind a portable queue to the selected baseline and receiver. Copy serialized events into caller-owned storage, enforce contiguous sequences and count and byte limits, and retire only after reliable submission. Report rebase on overflow and preserve the observed watermark for the replacement capture. [Retention contract](multiplayer-implementation-contracts.md#server-catch-up-event-retention).
- **Mac checks passed / Windows pending.** The reviewed tests failed before queue retention existed. They cover exact capacities, scope and sequence rejection, failed submissions, both storage rings, overflow, and an owner handoff to a replacement baseline. Event schemas and live capture, scheduling, and application remain open. [Tests](../../src/test/multiplayer/portable/CatchUpEventQueueTests.cpp).
- [ ] **Complete when** the [portable and peer batch](multiplayer-checkpoints/w0.md#run-the-portable-and-peer-batch) collects a passing `catch-up-event-queue.xml` on Windows.

### MP-038 — Make death, score, round flow, and spectator state authoritative

- **Kind / runs / depends:** TDD; `P/W`; MP-004, MP-033, MP-035, MP-037.
- **Red:** apply a trusted test damage result twice; one death/frag occurs. Test suicide, disconnected killer, stale life and first-reached 20-minute/50-frag defaults. Results/intermission advance automatically to a new round on the same arena, with fresh epoch/lives and no prior-round damage/events. Active-match join waits for loading/synchronization; spectator selection grants no command authority over the viewed actor.
- **Green / seam:** authoritative DM phase machine and scoreboard/kill feed/results in existing `game_sv_*`/`game_cl_*` code. Keep the same connections across automatic same-arena rounds; changing map requires server restart. Use trusted test damage until MP-057 connects real shots; no peer can invoke that test seam.
- [ ] **Complete when:** a short scripted engine match reaches its expected winner/score/restart and every client displays the same results without legacy economy requirements.

### MP-039 — Operate the initial version through configuration and console

- **Kind / runs / depends:** TDD + usability GATE; `P/W`; MP-004, MP-022–023, MP-026, MP-028, MP-031, MP-033, MP-035–038.
- **Red:** configure the one arena, bind/port/admission, basic loadout/pickups, round limits, respawn/protection and bot fill/cap/skill; invalid values leave effective settings intact and report a reason. Connect/disconnect by client console and inspect network/loading failures. Apply bot/rule edits at a declared boundary; attempting a live map change reports that restart is required. Round restart/shutdown invalidates old work safely.
- **Green / seam:** reuse existing console/configuration entry points, with checked settings, effective-state/status output, participant diagnostics and explicit immediate/next-round/restart-only semantics. Provide real command names/examples in the runbook after implementation, not invented executable instructions. Keep credentials out of logs and scope privileged changes to the local server owner; no new hosting/join menus or remote admin service is required.
- [ ] **Complete when:** a tester can configure, start, join, inspect, restart rounds and stop a packaged Windows session using documented commands, including bot-fill changes and stable failure messages. Existing HUD/scoreboard/results continue to reflect authoritative state.

## Movement, prediction, and interpolation

### MP-040 — Drive the existing player controller from commands

- **Kind / runs / depends:** TDD + engine characterization; `P/W`; MP-004, MP-031–032, MP-034–036.
- **Red:** replay fixtures for walking/diagonals, acceleration, sprint, crouch, jumping/landing, stance clearance, stairs/slopes and illegal movement. Partition rendering at 30/60/144/240 FPS while commands remain identical. Limits and collisions hold within measured tolerances.
- **Green / seam:** shared `SimulatePlayerCommand` boundary using [ActorInput.cpp](../../src/xrGame/ActorInput.cpp), [Actor_Movement.cpp](../../src/xrGame/Actor_Movement.cpp), and [PHMovementControl.cpp](../../src/xrGame/PHMovementControl.cpp). Fake external collision only for pure controller cases; Windows tests must use the actual controller/ODE path.
- [ ] **Complete when:** server, client prediction, and a scripted bot invoke the same movement rules; old transform-forwarding cannot override the result. Preserve the characterized SP movement path.

### MP-041 — Restore the complete replay state

- **Kind / runs / depends:** TDD; `P/W`; MP-036, MP-040.
- **Red:** snapshot/restore mid-jump, crouch transition, landing, ladder mount/climb/dismount, recoil, equip, chamber operation and reload; simulate the next commands and compare with uninterrupted execution. A removed/replaced ladder invalidates its reference safely. An omitted timer/controller field must make at least one test fail.
- **Green / seam:** explicit replay state for controller internals, contact/stance state, weapon state and timers around [actor_mp_client.cpp](../../src/xrGame/actor_mp_client.cpp), [actor_mp_state.h](../../src/xrGame/actor_mp_state.h), and movement support. Do not serialize unrelated whole-world state.
- [ ] **Complete when:** all selected transitions restore within declared tolerances on Windows; serialization round trips alone do not count as proving resumed behavior.

### MP-042 — Reconcile acknowledged commands and replay the rest

- **Kind / runs / depends:** TDD; `P/W`; MP-032, MP-037, MP-040–041.
- **Red:** predict commands 1–10, receive an authoritative state after command 6, restore it, and replay only 7–10. Duplicate/older snapshots do not rewind newer state; history overflow/missing state causes an explicit reset. Test bounded recovery under seeded loss/reorder.
- **Green / seam:** bounded prediction ring and owner-snapshot reconciliation using the shared simulation path. Stop the old whole-world correction path from also correcting actors owned by the new path; legacy actors remain on their existing route.
- [ ] **Complete when:** the injected divergence converges after authority arrives, only executed commands are discarded, and correction distance/frequency are recorded in a real impaired Windows scenario.

### MP-043 — Suppress irreversible side effects during replay

- **Kind / runs / depends:** TDD; `P/W`; MP-011, MP-036, MP-042.
- **Red:** predict, correct and replay the same fire/jump/pickup/reload inputs repeatedly; sound, particles, inventory events, damage and bot hearing are not emitted again. A genuinely new event after correction still emits once.
- **Green / seam:** explicit simulation context and stable event IDs separating simulation output from presentation/gameplay delivery in actor/weapon/inventory code. Do not suppress legitimate server-authoritative actions along with replay effects.
- [ ] **Complete when:** an event recorder and a Windows audio/effects trace show exactly the intended emissions for prediction, replay, rejection and confirmation.

### MP-044 — Smooth presentation without moving collision incorrectly

- **Kind / runs / depends:** TDD; `P/W`; MP-041–043.
- **Red:** a small error corrects collision immediately and decays the visual offset monotonically; large error/teleport/respawn clears it. Frame partition does not change configured settling behavior beyond tolerance. Correction against a wall cannot smooth authoritative collision through it.
- **Green / seam:** separate render/camera offset from controller transform, with bounded decay and explicit reset reasons. Mouse-look remains responsive independently of command/snapshot frequency.
- [ ] **Complete when:** math tests pass and Windows wall/stair/respawn captures confirm acceptable motion using tolerances selected in MP-004 and refined at this gate.

### MP-045 — Interpolate remote humans and bots from snapshots

- **Kind / runs / depends:** TDD; `P/W`; MP-023, MP-037, MP-040.
- **Red:** interpolate known timestamped poses including angle wrap; ignore out-of-order stale states; bound extrapolation after loss. Spawn/life change/teleport resets the buffer. Bot and human representations obey the same timeline/stance conventions.
- **Green / seam:** bounded remote snapshot buffers with explicit render time and jitter adjustment. Move transform/stance/view/animation state consistently; disable conflicting legacy interpolation only for migrated entities.
- [ ] **Complete when:** deterministic trajectory tests and impaired Windows captures show stable remote motion, no old-life blending, and no endless extrapolation.

### MP-046 — Estimate clock offset and approved view time

- **Kind / runs / depends:** TDD; `P/N/W`; MP-011–012, MP-031–032, MP-037, MP-045.
- **Red:** offset samples with jitter/outliers/asymmetric delay cannot jump time beyond policy; negative/future/old command times are clamped or rejected. Handle reconnect and inherited 32-bit time wrap. A fixed numerical fixture detects double subtraction of RTT/interpolation.
- **Green / seam:** monotonic server tick/time conversion, bounded offset estimator and server-approved interpolation allowance. Do not assume exact one-way latency from RTT; distinguish command execution, receipt, and rendered remote-view times.
- [ ] **Complete when:** timeline logs and numerical tests agree on chosen query times, and a peer cannot increase compensation by claiming arbitrary clock/view offsets.

### MP-047 — Define dynamic-contact prediction limits

- **Kind / runs / depends:** SPIKE then TDD for selected behavior; `P/W/H`; MP-004, MP-040–046.
- **Observe / Red:** characterize doors, moving platforms, props and player collisions supported by the arena. For each included interaction, add a scenario where a late authority update corrects a contact; the actor must not retain impossible velocity/grounded state or penetrate solid cover.
- **Green / seam:** capture/replay required contact state or constrain the interaction explicitly in the first ruleset. Record deferred abilities/maps. Do not assert bit-identical ODE behavior across hosts; use measured invariant/tolerance tests.
- [ ] **Complete when:** every arena interaction is either covered by a passing Windows fixture or explicitly excluded from the release rules/content, with the movement timing/feel gate recorded.

## Combat, presentation, and hybrid decisions

### MP-050 — Admit firing from authoritative actor and weapon state

- **Kind / runs / depends:** TDD; `P/W`; MP-023, MP-032, MP-034, MP-036, MP-046.
- **Red:** wrong weapon/actor/life, duplicate shot, excess cadence, empty chamber, reload/equip conflict, impossible origin or out-of-policy direction is rejected. An accepted legal fire command allocates one authoritative shot ID and consumes ammo once.
- **Green / seam:** common shot-admission boundary before collision, using server-owned movement/weapon state. Human requests use validated command timing; NPC object-handler requests bind to the living bot participant and authoritative execution tick without a fabricated session/RTT. A client target claim is diagnostic input at most, never proof of a hit.
- [ ] **Complete when:** every projectile/hitscan implementation and NPC fire request passes the same admission contract; no alternate legacy hit event bypasses it.

### MP-051 — Isolate shot randomness and reconstruct gameplay aim

- **Kind / runs / depends:** TDD; `P/W`; MP-011, MP-036, MP-050.
- **Red:** fixed weapon state/shot sequence yields expected spread/recoil bounds and directions; unrelated cosmetic/AI random calls do not change them. Client-supplied seeds cannot select arbitrary spread, damage or ricochet. Restoring weapon state reproduces the necessary prediction inputs.
- **Green / seam:** server-owned per-shot random state and shared gameplay aim calculation around [ShootingObject.cpp](../../src/xrGame/ShootingObject.cpp) and weapon fire code; keep camera-only recoil distinct.
- [ ] **Complete when:** seeded fixtures, replay and server validation agree on gameplay direction within declared encoding tolerance without relying on a global random-call order.

### MP-052 — Record historical collision proxies and life boundaries

- **Kind / runs / depends:** TDD; `P/W`; MP-023, MP-031, MP-035, MP-040–041, MP-047.
- **Red:** sample known moving/crouching head/torso/limb volumes at tick boundaries and between samples, for both human actor and native stalker body fixtures. Queries outside retained history, across teleport, before spawn or into a different life never interpolate invalid poses. Relevant moving blockers use explicit history too.
- **Green / seam:** bounded history ring and pure collision-proxy query layer with adapters for authoritative human and NPC animation/body state. Keep queries separate from the live ODE world; validate stalker pose updates on `xrRender_DS0`, including animation-driven movement, rather than relying on a rendering client or a player-only pose serializer.
- [ ] **Complete when:** analytic fixtures and a dedicated Windows pose/hit-region test agree, history remains bounded, and queries cannot mutate live entities.

### MP-053 — Select bounded historical shot query times

- **Kind / runs / depends:** TDD; `P/W`; MP-046, MP-050, MP-052.
- **Red:** with fixed fire/view/receipt times and a configured compensation window, assert the exact selected timeline. Test old/future arrivals, asymmetric delay, spoofed interpolation, dead shooter, current target life and late simultaneous shots.
- **Green / seam:** explicit policy object/functions for firing time, target/blocker query time, maximum compensation and trade-kill behavior. Keep exploratory prototype policy values named and versioned; finalize them at MP-061.
- [ ] **Complete when:** packet reordering within the defined tick adjudication window cannot arbitrarily decide ties, and debug output explains accepted/rejected timing numerically.

### MP-054 — Implement the historical hitscan prototype

- **Kind / runs / depends:** TDD; `P/W`; MP-050–053.
- **Red:** a moving target missed by present-time geometry is hit at the accepted historical pose; an intervening blocker prevents the hit under the documented policy. Verify nearest hit, hit region, max range, stance change, stale life and a true miss using analytically specified geometry.
- **Green / seam:** historical ray/shape queries for one prototype rifle, returning a candidate impact through the common combat pipeline. Never rewind live physics bodies or trust a client-reported intersection.
- [ ] **Complete when:** pure geometry tests and a two-client peeking/moving-target Windows scenario validate the same hit decisions and query times.

### MP-055 — Isolate time-stepped projectile simulation

- **Kind / runs / depends:** TDD; `P/W`; MP-011, MP-031, MP-050–051.
- **Red:** known no-drag flight matches an analytic trajectory within tolerance; changing render partition does not change authoritative flight. Swept segment collision hits a thin obstacle crossed within one step. Lifetime/range/work limits retire projectiles predictably.
- **Green / seam:** extract/adapt the real [bullet manager](../../src/xrGame/Level_Bullet_Manager.cpp) step and collision-query boundary; use existing drop/drag/material logic where selected. Rays along each flight segment remain ballistic collision detection, not full-range instant damage.
- [ ] **Complete when:** one server-owned rifle projectile is reproduced in pure fixtures and the Windows engine without running particles/sound in its simulation step.

### MP-056 — Catch up delayed projectile launches through history

- **Kind / runs / depends:** TDD; `P/W`; MP-046, MP-052–053, MP-055.
- **Red:** a late launch crosses a target/blocker only during an intermediate historical flight segment; catch-up detects that crossing at the appropriate time. Test launch beyond allowed history, work-cap exhaustion, transition into the present tick, and target respawn during flight.
- **Green / seam:** bounded segmented catch-up using MP-055's production step and MP-052's proxy queries. Define failure/over-budget handling; do not apply full hitscan rewind again. Treat any extra view-delay-offset policy as a distinct comparison with its own fixtures.
- [ ] **Complete when:** a numerical flight/time fixture and impaired Windows scenario agree, and recorded cost remains bounded per shot without transforming flight into instant full-range hitscan.

### MP-057 — Apply confirmed damage and death exactly once

- **Kind / runs / depends:** TDD; `P/W`; MP-023, MP-034, MP-038, MP-050; use MP-054/056 results in integration.
- **Red:** repeated impact/damage events, shotgun pellets, a ricochet revisiting a target, late old-life hits, and a kill followed by duplicate confirmation obey an explicit per-impact damage policy. Reordered network delivery cannot award a second kill or alter authoritative health directly.
- **Green / seam:** server damage/impact ledger keyed by shot, allowed impact/pellet, target and life; authoritative health/death/score updates and durable confirmation. Audit [bullet hit events](../../src/xrGame/Level_bullet_manager_firetrace.cpp) and all enabled DM damage sources, including explosives/melee/environment.
- [ ] **Complete when:** trusted collision results reach the DM rules, forged peer hit packets do not, and all clients converge on health/kill feed/score after duplicate/lost notifications.

### MP-058 — Apply the selected material, penetration, and explosive rules

- **Kind / runs / depends:** TDD; `P/W`; MP-004, MP-051, MP-054–057.
- **Red:** controlled layered materials produce the specified remaining energy/range and hit order; seeded ricochets remain bounded; no hit skips opaque cover unless the weapon/material rule allows it. Grenade/rocket launch, fuse/impact, splash occlusion and duplicate explosion apply once.
- **Green / seam:** common material/damage policy around [Level_bullet_manager_firetrace.cpp](../../src/xrGame/Level_bullet_manager_firetrace.cpp) and existing projectile/explosion code. Hitscan penetration is optional policy, independent of flight time. Explosives initially use current-world authority rather than full historical blast rollback.
- [ ] **Complete when:** selected prototype weapons have explicit material/explosive rules with fixtures; unsupported mechanics are excluded rather than silently approximated.

### MP-059 — Select collision behavior per weapon/ammunition profile

- **Kind / runs / depends:** TDD; `P/W`; MP-020, MP-022, MP-036, MP-050–058.
- **Red:** mixed hitscan/projectile profiles dispatch into the correct collision behavior while sharing admission, IDs, damage and content verification. A client's altered profile cannot change the server result; a bot uses the same profile as a human.
- **Green / seam:** server-owned weapon/ammunition model configuration and content manifest. Add one example mixed roster without selecting the final product rules; keep one clear collision model per shot in this item.
- [ ] **Complete when:** a mixed-weapon match reproduces expected timing/material decisions; adding a hitscan weapon cannot bypass cadence/ammo rules or switch a ballistic weapon to instant damage.

### MP-060 — Predict local effects and reconcile confirmed combat

- **Kind / runs / depends:** TDD + visual check; `P/W/H`; MP-036–037, MP-042–043, MP-050, MP-055, MP-057.
- **Red:** local fire emits recoil/flash/sound/animation without waiting for an RTT; rejection reconciles displayed ammo. Confirmation/replay creates no duplicate tracer/casing/explosion. No damage hitmarker/death is presented as confirmed before authority. Remote clients converge after event loss.
- **Green / seam:** presentation event mapping in [WeaponMagazined.cpp](../../src/xrGame/WeaponMagazined.cpp), weapon HUD and combat UI; match predicted/server projectile IDs and smooth cosmetic corrections. Generate authoritative bot sound stimuli independently of audible effects.
- [ ] **Complete when:** event tests pass and Windows captures show immediate local response with one accepted/rejected shot presentation for both collision models. This presentation hybrid is available regardless of final gunplay selection.

### MP-061 — Compare and select gunplay, timing, and fairness rules

- **Kind / runs / depends:** SPIKE + GATE; `W/H`; MP-004, MP-047, MP-050–060.
- **Observe first:** use the same arena/weapon handling to compare hitscan rifle, ballistic rifle and mixed roster at 0/50/100/150/200 ms RTT, including moving targets, peeks, moving cover, projectile leading, confirmation delay and CPU/projectile counts. Capture subjective playtest notes alongside quantitative traces.
- **Deliver / seam:** a decision record choosing hitscan-oriented, retained ballistics or weapon-based hybrid; list final weapon profiles, material/compensation/trade-kill policies, and whether a distance-based experiment is wanted. Define numeric correction/time budgets from the working prototype and re-estimate remaining work.
- [ ] **Complete when:** the selection and rationale are recorded explicitly. No test or unattended agent is allowed to decide product feel merely by choosing the fastest/cheapest implementation; independent infrastructure work can continue while this decision is pending.

### MP-062 — Complete the selected weapon roster and visual flow

- **Kind / runs / depends:** TDD; `P/W/H`; MP-035–038, MP-054–061; MP-066–067 only if distance hybrid selected.
- **Red:** parameterize action/collision/damage fixtures over every selected weapon, ammo type and fire mode; include equip/death/respawn/late join. Add a regression fixture for every discrepancy found while converting prototype profiles to the actual roster.
- **Green / seam:** apply the chosen rules to the small DM arsenal and client UI/effects; keep unfinished alternative models development-only. Ballistics/hybrid selection retains the necessary historical flight tests; hitscan selection does not require unsupported advanced ballistic mechanics to ship.
- [ ] **Complete when:** all selected roster profiles pass the shared contracts on Windows and human review confirms consistent handling, feedback and simple-DM UI.

### MP-066 — Specify a distance-based hybrid experiment

- **Kind / runs / depends:** conditional SPIKE + GATE; `P/W/H`; MP-053–061; explicit selection at MP-061.
- **Observe first:** compare an instant initial segment plus projectile continuation against the other prototypes. Specify initial range, removed travel time, handoff velocity/drop, remaining energy/range, penetration, target/blocker timelines and repeated-target policy. Evaluate whether its feel/cost benefit justifies another collision path.
- **Deliver / seam:** independent numerical examples and an executable fixture specification; estimate MP-067 before accepting it. It is valid to defer this experiment and retain a weapon-based/presentation hybrid.
- [ ] **Complete when:** the distance rule is explicitly selected with measurable expectations, or recorded as deferred with MP-067 excluded from the IP release gate.

### MP-067 — Implement and verify the distance-based handoff

- **Kind / runs / depends:** conditional TDD; `P/W`; selected MP-066, MP-054–060.
- **Red:** place targets just before/at/after the threshold; test an opaque wall before it, penetration across it, a target crossing at handoff, energy exhaustion at the boundary and delayed launch. An impact is never processed twice and continuation cannot gain distance/energy.
- **Green / seam:** carry one shot/impact identity and the selected material/flight state from initial trace into MP-055/056's actual projectile path. Apply the chosen timing once; do not stack independent full rewind allowances.
- [ ] **Complete when:** hand-computed boundary fixtures and impaired Windows comparisons match MP-066's policy, with measured cost and no double damage or visible duplicate projectile.

## Human-like bots and arena navigation

Use the [bot AI design](multiplayer-bot-ai-design.md) for architecture, source research and scenario contracts. Keep the existing stalker brain/body as the preferred implementation beyond its initial integration prototype. MP-070 verifies native-NPC DM participation; a common player-controller rewrite is optional. Confirmed direction: F.E.A.R./Counter-Strike references, similar tactics at every difficulty with aim/reaction differences, and configurable bots filling empty slots. These cases remain implementation work; the design document is not completion evidence.

### MP-070 — Integrate an existing stalker NPC as an authoritative DM bot

- **Kind / runs / depends:** CHAR + TDD + integration GATE; `P/W`; MP-005, MP-013, MP-031, MP-033, MP-035–038, MP-050, MP-052, MP-057. The early proof is MP-005; this item hardens it against the production protocol, combat and pose/history contracts.
- **Observe / Red:** run an existing stalker on the authoritative dedicated side with two remote clients. Exercise NPC movement, sight, fire/reload, FFA target selection, damage, frag attribution, death and fresh-life respawn without a socket. Add failing cases for missing DM integration, client-side AI duplication, stale-life actions, excess cadence/ammo, missing NPC snapshots/pose and dedicated hit-region history. Record collision/stance/speed/accuracy as an explicit NPC capability profile rather than asserting identical human motor output.
- **Green / seam:** retain [CAI_Stalker](../../src/xrGame/ai/stalker/ai_stalker.cpp), its combat planner and applicable body/movement/object-handler services. Add narrow participant/lifecycle, NPC snapshot/animation and common authoritative shot/damage adapters. Isolate inappropriate ALife/faction assumptions; do not merely remove `net_Export`/`net_Import` early returns and assume mode compatibility. Use a minimal real navigation fixture for initial movement; full map production/route acceptance belongs to MP-071/100–109.
- **Audit:** use the [native-NPC integration checklist](multiplayer-implementation-contracts.md#6-native-npc-integration-checklist) for actor/client assumptions in respawn/loadouts, damage/armor, frags, relations, spectator/HUD, poses and enabled scripts. Classify intended human-only paths separately from shared paths requiring an adapter; do not fix casts by inventing a fake socket.
- [ ] **Complete when:** two independent Windows clients can watch and fight the same server-owned stalker bot, with convergent movement/animation, score, death/respawn and tested hit regions. Record source reuse, remaining adaptations, measured cost and capability differences. Continue this implementation when the gate passes; select a retained fallback only against a documented limitation.

### MP-071 — Build and validate one arena's navigation contract

- **Kind / runs / depends:** integration GATE; `P/W`; MP-004, MP-070, MP-100–109.
- **Observe first:** verify the selected SDK-source or compiled-collision bake route produces a matching native grid and per-map graph/cross-table package. Missing/stale nav, unreachable required locations, insufficient clearance and invalid traversal endpoints must have actionable diagnostics.
- **Deliver / seam:** the [navigation pipeline](multiplayer-navigation-pipeline.md), implemented through MP-100–109, with actual generation/packaging commands and dedicated bootstrap in [Level_load.cpp](../../src/xrGame/Level_load.cpp) / [ai_space.cpp](../../src/xrGame/ai_space.cpp). A dropped-in `level.ai` or a client-only graph load is insufficient; keep full ALife population out of DM startup.
- [ ] **Complete when:** navigation generated from the arena's verified geometry loads on the authoritative dedicated server and a stalker-based bot executes every required route through its real movement/physics, including after map reload. Record artifact hashes, NPC capability profile, node/graph GUIDs and Windows evidence.

### MP-072 — Limit visual perception to observable enemies

- **Kind / runs / depends:** TDD; `P/W`; MP-011, MP-052, MP-070–071.
- **Red:** FOV, distance, occlusion and visibility time govern acquisition. Hold observations/seed constant and move an unobserved hidden enemy; the bot's chosen aim/goal must not change from forbidden knowledge. Respawn invalidates old target life identity.
- **Green / seam:** adapt useful [visual memory](../../src/xrGame/visual_memory_manager.cpp) and [enemy selection](../../src/xrGame/enemy_manager.cpp) behind a restricted perception view. Test pure observations and actual arena collision/visibility separately.
- [ ] **Complete when:** bots cannot aim-track hidden actors through walls, with recorded reasons for acquiring/losing targets in Windows scenarios.

### MP-073 — Hear authoritative stimuli with uncertainty

- **Kind / runs / depends:** TDD; `P/W`; MP-011, MP-043, MP-060, MP-070–071.
- **Red:** authoritative gunshots/footsteps produce bounded uncertain observations only when audible under the chosen attenuation/occlusion rules. A muted dedicated sound device still yields stimuli; cosmetic replay creates none. Hearing an unseen actor does not grant its exact current transform.
- **Green / seam:** authoritative stimulus events and bounded observation uncertainty using concepts from [sound_memory_manager.cpp](../../src/xrGame/sound_memory_manager.cpp). Separate the audible client effect from AI knowledge generation.
- [ ] **Complete when:** a bot investigates the last heard region, cannot precisely track a silently departing hidden target, and duplicate predicted sound never grants extra information.

### MP-074 — Maintain memory and choose DM goals

- **Kind / runs / depends:** TDD; `P/W`; MP-033, MP-035, MP-070–073.
- **Red:** last-seen/heard confidence decays with virtual time; lost target triggers search, not omniscient pursuit. Ammo/health/pickup knowledge and recent exposure select legal seek/fight/recover/rotate goals. Hidden enemy health/reload/pickup changes cannot affect decisions when allowed observations are unchanged. Seeded FFA bots do not share hidden team intelligence or fall into single-player quest/ALife goals. With identical knowledge/personality/tactical state, difficulty does not disable tactics or change tactical evaluations; commitment prevents near-equal-score oscillation and admits valid emergency interruptions.
- **Green / seam:** extend existing combat planner properties/actions and [memory_manager.cpp](../../src/xrGame/memory_manager.cpp) behind restricted inputs; add small utility/hierarchical selection only for choices that need it. Preserve existing action-selection behavior where suitable and expose its rationale. Use hysteresis/cooldowns to avoid switching goals every frame; do not require a second decision framework to pass these cases.
- [ ] **Complete when:** fixture observations produce explainable stable goals, stale knowledge expires, and repeated matches show pursuit/search/rotation without secret world-state access.

### MP-075 — Select cover, flanks, pushes, and retreats

- **Kind / runs / depends:** TDD + scenario review; `P/W/H`; MP-071–074.
- **Red:** a controlled encounter offers exposed/covered/alternate routes; the selected action reflects known threat, health/ammo, escape and travel cost. An observed reload can create a push opportunity; an unobserved reload cannot. Occupied/unsafe cover is not selected blindly.
- **Green / seam:** explicit tactical scoring and action execution, reusing [cover_manager.cpp](../../src/xrGame/cover_manager.cpp) and suitable [combat planner](../../src/xrGame/Legacy/StalkerPlanner/stalker_combat_planner.cpp) logic behind the bot interfaces. Avoid a wholesale planner rewrite.
- [ ] **Complete when:** seeded encounters demonstrate cover, at least one useful alternate approach, sensible disengagement and logged decision inputs; human review confirms those behaviors remain readable in play.

### MP-076 — Execute routes, traversal, avoidance, and stuck recovery

- **Kind / runs / depends:** TDD; `P/W`; MP-070–071, MP-075.
- **Red:** follow required routes with the measured stalker collision/stance/speed profile; test corridors, congestion, changed doors and required jump/ladder links. Block progress and assert timed replan/backtrack/recovery without teleportation, wall crossing or an unvalidated animation shortcut. Record differences from human traversal and any excluded optional shortcuts.
- **Green / seam:** extend existing stalker route following/local avoidance and movement/physics execution with bounded replans and typed traversal actions. Integrate required climb actions with compatible ladder physics; use a local movement adapter if needed. A full player-controller replacement is a retained fallback, not a prerequisite.
- [ ] **Complete when:** the arena route suite passes with multiple moving bots and every stuck fixture resolves or reports an explicit bounded failure/recovery result.

### MP-077 — Model aiming skill for the chosen weapon rules

- **Kind / runs / depends:** TDD + distribution checks; `P/W/H`; MP-051, MP-059, MP-061, MP-070, MP-072–076.
- **Red:** target acquisition cannot fire before configured reaction delay; repeated perception samples cannot restart that delay indefinitely. Test attention switching, loss/reacquisition, bounded yaw/pitch rates/acceleration and time-correlated tracking error. Hitscan aims according to its rules; ballistic aim leads/follows drop using observed motion and handles no-solution cases. Hidden velocity/history access and favorable spread-seed selection cannot improve bot aim.
- **Green / seam:** adapt existing NPC sight/aim/object handling to the selected MP weapon profile, with separately timestamped detection/reaction/action gates. Characterize legacy rank/stance dispersion before normalizing it to MP weapon rules; avoid unintentionally stacking NPC spread with skill error. Difficulty primarily changes aim/reaction and retains the same tactical library/evaluation and personality distribution. Use fixed-seed test sets and declared distribution ranges, not one random K/D sample. Higher difficulty does not change health, legal speed, hidden knowledge or rewind allowance; bot shots use their admitted execution tick.
- [ ] **Complete when:** human/bot weapon semantics agree across selected profiles, precision is bounded, and playtests distinguish skill levels through behavior rather than unfair state advantages.

### MP-078 — Budget and stagger AI work

- **Kind / runs / depends:** TDD + profiling; `P/W`; MP-011, MP-031, MP-070–077.
- **Characterize first:** use the [plain-language scaling assessment](multiplayer-npc-ai-explained.md) and measure effective NPC intervals, alternating vision stages, cached nearest-player scheduling on dedicated, plan reuse and worst individual search cost. Declared scheduler fields are not measured response times, and scheduler checks cannot interrupt one long action/search.
- **Red:** schedule 32 bots whose perception/path jobs become due together; deterministic work limits hold, urgent jobs progress, and lower-priority work does not starve indefinitely. Expensive/invalid searches terminate under limits; NPC movement/physics and weapon execution continue at the authoritative rate, with no double update from the legacy scheduler.
- **Green / seam:** staggered perception/tactical updates, bounded graph/cover queries, caching and per-bot/global budgets in the server scheduler. Begin near the parent plan's 5–15 Hz decision rates; tune from measurements rather than lowering correctness requirements silently.
- **Additional Red:** bot-only and mixed fights keep bounded observation/service age when participants move beyond a cached distance or only the server/spectator camera moves. Include all relevant bot participants in the dedicated relevance policy; acceptable tick times achieved by starving distant bots do not pass. Compare unchanged-plan and simultaneous-replan workloads, and retain per-search limits inside expensive calls.
- [ ] **Complete when:** unit schedule tests and 1/4/8/16/32-bot Windows profiles report bounded work, fairness and missed-deadline counts without sustained tick backlog.

### MP-079 — Run complete mixed matches and evaluate bot behavior

- **Kind / runs / depends:** GATE; `W/H`; MP-004, MP-029, MP-038, MP-045, MP-060, MP-062, MP-070–078.
- **Observe first:** run 30 bots plus 2 independent human clients and a separate 32-bot scenario through full rounds, death/respawn and restart. Record route variety, exposure, reaction/aim distribution, cover/flank/search examples and stuck recovery, not only K/D.
- **Deliver / seam:** a behavior acceptance report with seeded encounter replays, representative captures, known limitations and tuning changes. Include Easy/Normal/Hard comparisons against the agreed similar-tactics/different-aim-and-reaction model, plus bot-fill join/leave/replacement scenarios. Turn observed regressions into the smallest appropriate test before fixing them; do not claim that scenario pass proves bots are indistinguishable from humans.
- [ ] **Complete when:** participants, score and snapshots converge, CPU fits the selected baseline, and human assessment accepts the documented level of bot behavior for the scoped alpha.

## Integration, robustness, and release

### MP-080 — Expose diagnostics needed to explain failures

- **Kind / runs / depends:** TDD for metrics, visual inspection for HUD; `P/W`; MP-027, MP-031–032, MP-037, MP-042, MP-046, MP-053, MP-078.
- **Red:** synthetic counters/timestamps produce correct RTT/jitter, pending bytes, command/snapshot age, interpolation delay, correction distance and tick cost; reset/reconnect does not mix sessions. A rejected shot has a reason and selected timeline; bot cost and live projectiles are attributed separately.
- **Green / seam:** structured per-session telemetry plus an optional net graph/debug hit-volume view. Keep secrets out of logs and avoid per-particle logging or profiling that dominates the workload.
- [ ] **Complete when:** automated cases verify units/aggregation and a Windows capture can explain a correction, late shot, queue stall and costly AI update from the recorded run.

### MP-081 — Exercise the impaired-network matrix end to end

- **Kind / runs / depends:** TDD regression scenarios + GATE; `P/N/W`; MP-012–013, MP-029, MP-038, MP-042–047, MP-057, MP-060, MP-062, MP-080.
- **Red:** script representative interactions across RTT 0/50/100/150/200 ms, jitter 0/10/30 ms, loss 0/1/3/5%, burst loss, duplicates, reorder, bandwidth pressure and asymmetric paths. A deliberately broken ack/history/dedup rule must fail a semantic assertion, not just change a screenshot.
- **Green / seam:** use a small pairwise/edge-case suite per change and a broader scheduled/manual matrix for milestones; real GNS impairment/shaping validates that the deterministic fake has not hidden transport behavior. Record per-direction delay correctly.
- [ ] **Complete when:** the selected matrix passes with bounded queues/corrections and converged match state; overload/disconnect behavior outside the support envelope is explicit and reproducible.

### MP-082 — Load the server with 32 independent protocol clients

- **Kind / runs / depends:** TDD for harness assertions, then GATE; `N/W`; MP-013, MP-022–029, MP-030–038, MP-050, MP-057, MP-062, MP-080.
- **Red:** clients send legal movement/fire, acknowledge real snapshots and exercise join/leave; a 33rd combatant follows capacity policy. Verify that the load generator consumes new-protocol messages rather than merely keeping 32 idle sockets connected. Include selected real rendering clients.
- **Green / seam:** reusable engine-free synthetic client/load mode sharing production codec but independently checking expected server outcomes. Bot-only load remains a separate AI test and cannot satisfy this item.
- [ ] **Complete when:** 32-human-equivalent ingestion/egress, command age, loss, reliable backlog and state convergence are measured on the reference Windows server.

### MP-083 — Measure and meet performance/bandwidth budgets

- **Kind / runs / depends:** measurement GATE; `W/H`; MP-004, MP-062, MP-078–082.
- **Observe first:** profile the agreed arena/hardware at mixed and 32-human-equivalent loads. Initial 50 Hz target: tick p95 at or below 15 ms, p99 below 20 ms with no sustained backlog; client goal is stable 60 FPS at selected graphics settings. Measure byte rates and actual packet sizes against the parent plan's provisional budgets.
- **Deliver / seam:** cost breakdown for controller/ODE, history queries, projectiles, AI/path/cover, encoding and rendering. Add a failing workload fixture before each optimization that changes behavior. Introduce snapshot deltas/interest reduction only when measurements justify them, with loss/baseline/relevance correctness tests.
- [ ] **Complete when:** the chosen budgets are met or explicitly revised with evidence. A 75 Hz profile gets its own tighter acceptance gate; aggregate average FPS cannot hide p99 stalls or growing queues.

### MP-084 — Fuzz protocol and test adversarial inputs without assertions

- **Kind / runs / depends:** TDD + fuzzing; `P/N/W`; MP-020–023, MP-026, MP-030, MP-034, MP-037, MP-050–058.
- **Red:** seed the fuzzer with malformed envelopes/commands/events and replay every discovered crash or excessive-allocation case as a regression. Include decompression expansion, invalid enums/counts/IDs, NaN/Inf, unauthorized events, sequence extremes and bounded command/shot floods.
- **Green / seam:** fuzz the actual checked decoder/dispatch validation boundary with allocation/time limits. Run Windows assertion-disabled negative scenarios and supported sanitizer builds; clean rebuilds are required when enabling `IXRAY_ASAN`.
- [ ] **Complete when:** the versioned corpus is reproducible, no known crash/corruption path remains in scope, and explicit control flow rejects bad peer input in Release/Shipping.

### MP-085 — Soak session lifetimes and restart boundaries

- **Kind / runs / depends:** GATE with regression fixtures for failures; `W`; MP-013, MP-029, MP-035–038, MP-062, MP-078–084.
- **Observe first:** run at least four hours of mixed match traffic, repeated rounds/map reloads, client timeout/reconnect, join cancellation and server shutdown under receive/send activity. Sample memory, handles, threads, entity/history/event/queue counts and trends.
- **Deliver / seam:** bounded multi-process soak scenario with periodic semantic checks and a final consistency assertion, not a long sleep followed by exit code zero. Shrink any failing trace/seed into a short failing regression before fixing it.
- [ ] **Complete when:** no unexplained growth, stale authority, delayed callbacks into freed state or stuck shutdown remains; schedule a longer unattended soak before final release.

### MP-086 — Protect single-player, build variants, and renderer paths

- **Kind / runs / depends:** CHAR + regression GATE; `W`; MP-001, MP-013, MP-028, MP-040–047, MP-062, MP-079, MP-084.
- **Observe / Red:** build/start MP-off SP and MP-on client/dedicated; exercise the selected supported R1/R2/R4 renderer configurations and DS path. Run an existing SP save/load/movement/weapon smoke fixture where formats are intended to remain compatible. A missing test scenario is recorded as unexecuted, not passed.
- **Green / seam:** isolate MP protocol/controller policy and conditional dependencies; add the smallest regression for any cross-mode break. Renderer draw ownership, Lua/thread restrictions and dedicated pose evaluation remain covered by integration checks.
- [ ] **Complete when:** the documented supported matrix passes, single-player formats were not changed incidentally, and future changes know exactly which mode/renderer checks they must rerun.

### MP-087 — Package and launch a reproducible Windows candidate

- **Kind / runs / depends:** TDD for packaging/run checks + GATE; `W/H`; MP-004, MP-028–029, MP-039, MP-062, MP-079, MP-081–086.
- **Red:** stage a clean candidate outside the developer build tree; missing runtime DLL/content/map or mismatched profiles causes an explicit preflight failure. A clean server plus two clients must run without access to developer-only paths or external discovery services.
- **Green / seam:** versioned package/content manifest, matching client/server binaries and delivered checkpoint scripts/runbook with exact setup/launch/working-directory/UDP-port instructions, isolated app data, expected outcomes and automatic result collection. Verify WAN direct IP from another network using the supplied procedure and configured firewall/NAT setup; do not leave launch or log-gathering steps for the tester to invent.
- [ ] **Complete when:** another Windows environment can reproduce the selected DM scenario from the package/runbook, including join, play, reconnect and shutdown; document any deployment prerequisite rather than masking it in tests.

### MP-088 — Accept the scoped IP alpha

- **Kind / runs / depends:** GATE; `W/H`; MP-004, MP-061–062, MP-079, MP-081–087; MP-067 only if selected.
- **Review:** trace each confirmed requirement to the completed items below. Review client responsiveness, compensation fairness, mixed gunplay if selected, simple-DM flow, 32-combatant stability and bot behavior using artifacts plus human playtests. Record remaining defects and deferred features explicitly.
- **Deliver / seam:** release checklist with actual build/content revision, supported hardware/network envelope and known limitations. Checkboxes need their evidence; no claim of production-ready competitive anti-cheat, public matchmaking, native Mac engine support or advanced explosive rollback is implied.
- [ ] **Complete when:** the scoped IP alpha is explicitly accepted with its limitations; Steam service items remain deferred and do not block this gate.

## Deferred Steam integration

### MP-090 — Introduce a tested IP/Steam provider boundary

- **Kind / runs / depends:** TDD; `P/N/W`; MP-022–025, MP-028–029, MP-062; schedule after IP alpha unless a small seam is needed earlier.
- **Red:** run the same admission/gameplay contract with fake IP/Steam providers; endpoint identity/address representation changes without changing commands, snapshots or shot rules. Failure/close semantics remain explicit; IP continues to pass real-peer scenarios.
- **Green / seam:** narrow provider selection around endpoint resolution, runtime ownership and service identity, not a universal networking framework. Audit standalone GNS versus Steamworks headers/symbols/lifetimes before linking both into the same process.
- [ ] **Complete when:** the IP integration is unchanged and the provider contract has executable cases for connect, identity, failure and close with no gameplay provider conditionals.

### MP-091 — Establish Steam service and hosting prerequisites

- **Kind / runs / depends:** SPIKE + external-access GATE; `W/H`; MP-090.
- **Observe first:** confirm usable App ID/Steamworks access, two test users, intended client/dedicated hosting model, authentication requirements and invite/lobby/browser choice. Check the applicable current Valve SDK/service requirements when implementation begins.
- **Deliver / seam:** explicit integration/runbook/secret-handling plan and a service-enabled test environment. Mocked identity or a successful existing Steam overlay initialization is not evidence that authenticated multiplayer/relay access works.
- [ ] **Complete when:** prerequisites are available and validated, or the item remains open with a specific external blocker; continue IP maintenance without pretending a service test passed.

### MP-092 — Bind participant admission to verified Steam identity

- **Kind / runs / depends:** TDD + service integration; `P/W/H`; MP-022–023, MP-034, MP-090–091.
- **Red:** invalid/expired/reused identity credentials, wrong user/session, service failure and disconnect during verification cannot spawn a participant or inherit another user's authority. Test real valid users in addition to mocked failures.
- **Green / seam:** appropriate client/game-server initialization, callbacks and credential validation through the provider; map stable service identity separately from transport handle/entity/life. Keep Steam callbacks subject to the same owner-thread and shutdown contracts.
- [ ] **Complete when:** service-verified identity controls admission, negative service cases fail explicitly, and credentials are absent from logs/artifacts.

### MP-093 — Join through the selected Steam invite/relay path

- **Kind / runs / depends:** TDD for flow, real-service GATE; `W/H`; MP-090–092.
- **Red:** invite/join cancel, unreachable host, full session, incompatible content and peer departure are handled without leaked UI/connection state. Separate users on external networks must join through the selected Steam flow; verify the actual selected route rather than inferring relay use from a successful connection.
- **Green / seam:** selected lobby/invite/endpoint APIs and appropriate P2P/hosted service setup behind the provider. Keep the dedicated/listen hosting choice explicit; do not add host migration unless separately required.
- [ ] **Complete when:** two real users complete a match over the selected service path, diagnosis distinguishes direct and relay routes, and IP development mode still works.

### MP-094 — Package and validate both supported connectivity modes

- **Kind / runs / depends:** GATE; `W/H`; MP-087–088, MP-090–093.
- **Observe first:** run the supported IP/Steam, client/dedicated, login/service-failure and version/content mismatch matrix from a clean package. Repeat disconnect/shutdown and a selected impaired-network gameplay scenario with the service provider enabled.
- **Deliver / seam:** final provider/configuration/deployment runbook and service-specific limitations, using current Valve requirements. Rerun gameplay contracts to demonstrate that Steam integration did not replace or bypass authority/compensation/bot logic.
- [ ] **Complete when:** package and real-service evidence exists for each supported mode; access-unavailable modes remain explicitly unverified.

## Navigation generation and per-level attachment

These items expand MP-071. Follow the [navigation pipeline](multiplayer-navigation-pipeline.md) for source evidence, existing-tool commands, asset layout and proposed new contracts. Existing SDK tools can compile authored sources; a compiled-CForm-to-native-grid route and `level.botnav` packaging/bootstrap are new work. New filenames and tool entry points below are proposals until implemented.

### MP-100 — Inventory map assets and choose the bake route

- **Kind / runs / depends:** SPIKE + GATE; `W/H`; MP-004; can begin during baseline work.
- **Observe first:** start from the user's compiled stock MP maps; inspect resolved collision header/chunks, `level.spawn` ladder/semantic records, `level.game`, existing nav and material definitions. Check for matching genuine SDK sources and perform a bounded one-arena decompile/import experiment if useful; a reconstructed workspace is not the original project. Record converter version/configuration and matching asset overrides.
- **Deliver / seam:** select Route A (genuine sources or a validated reconstructed SDK workspace) or Route B (new compiled-collision baker) for the first arena; record provenance, permitted regions, effective agent profile and required routes. Keep original compiled collision as the validation reference. Do not depend on the unverified in-game grid editor document or confuse the in-repo compiled-file converter with a scene decompiler.
- [ ] **Complete when:** the chosen input set is reproducible and the generation gap is explicit; re-estimate any new compiled-map baker/cover/export work before treating navigation as ready.

### MP-101 — Read runtime map collision without losing geometry or materials

- **Kind / runs / depends:** TDD; `P` for isolated format/geometry cases, `W`; MP-010, MP-100.
- **Red:** ordinary/chunked collision representations of the same fixture yield equivalent triangles and collision queries. Missing chunks, invalid vertex indices, unsupported versions, duplicate coordinate transforms and inconsistent material mapping fail explicitly. Render-only surfaces never become floors; invisible actor blockers remain barriers.
- **Green / seam:** reuse [CFormIO](../../src/xrCore/FormatParsers/LevelCForm/CFormIO.h) or runtime CDB extraction; validate filename conventions and flattened index semantics against actual [ObjectSpace loading](../../src/xrCore/Collision/xr_area.cpp). Freeze effective VFS collision/material inputs and hashes; do not import `level.geom` as navigation truth.
- [ ] **Complete when:** fixture and actual-map probes agree with Windows runtime static collision and the selected NPC collision filters, including chunked assets and addon/archive overrides, without manual scale/origin corrections.

### MP-102 — Generate or import a layered walk grid for the NPC capability profile

- **Kind / runs / depends:** TDD + existing-tool characterization; `P/W`; MP-100–101.
- **Red:** flat/ramp/stair/low-ceiling/narrow-door/two-floor fixtures produce reachable nodes only where the measured stalker profile fits. Seed expansion stops at arena bounds, forbidden areas, walls and unsupported ledges. Node projection distinguishes vertical layers; missing seeds and unreachable required regions are reported. A future player-controller fallback needs its own profile validation/rebake, not silent reuse of NPC clearance.
- **Green / seam:** Route A: characterize the SDK snap-list/seed generator and import its authored grid; if decompiled, compare candidate paths/materials against original compiled collision. Route B: implement a bounded offline collision sampler/generator using the same fixture contract. Measure step/slope/clearance from actual NPC movement/physics; existing compiler defaults are not proof of traversal. Capability characterization may begin before full MP-070 integration to avoid a navigation/bootstrap dependency cycle.
- [ ] **Complete when:** the selected route generates a reproducible native-grid candidate from verified geometry and its graph reachability agrees with the actual controller on the fixture set. Record any intentionally excluded components.

### MP-103 — Author and validate traversal and tactical annotations

- **Kind / runs / depends:** TDD; `P/W`; MP-100–102, MP-109.
- **Red:** a jump/drop/ladder/door link is rejected when endpoints, direction, ability, landing clearance or map identity are invalid. A disconnected platform remains unreachable without a valid explicit link. Cover/avoid/arena annotations cannot refer to deleted nodes or regions outside the allowed bounds.
- **Green / seam:** versioned `level.botnav.authoring.json` source schema and typed link/area validation. Keep walking adjacency separate from action links; validate selected actions through the actual NPC movement/physics path. Reuse existing scene annotations through an importer only where their semantics match.
- [ ] **Complete when:** authored links and areas survive generation/remapping and every included traversal action has a reproducible controller test; no link teleports the bot or invents a route through a wall.

### MP-104 — Bake cover and serialize a valid native level.ai

- **Kind / runs / depends:** TDD; `P/W`; MP-100–103.
- **Red:** a known wall/open field produces the intended low/high directional cover differences; final node sorting remaps all walk/traversal references. Test native version/link limits, GUID change, quantized bounds/heights/planes and corrupt output. Loading the result with the actual engine loader must preserve geometry/reachability/cover within declared tolerance.
- **Green / seam:** Route A uses non-draft `xrLevelBuilder` AI compilation with preserved `build.prj`/`build.aimap`; Route B supplies a tested collision-based cover adapter and native writer, reusing [compiler_save.cpp](../../src/utils/xrLevelBuilder/xrAI/Compiler/compiler_save.cpp) concepts. Preserve source-to-output remapping for dependent artifacts.
- [ ] **Complete when:** a Windows-loaded `level.ai` matches the baked fixture and no stale node/GUID reference survives compilation. Draft/no-cover output is explicitly development-only for tactical bot acceptance.

### MP-105 — Build minimal graph/cross-table compatibility without ALife spawns

- **Kind / runs / depends:** TDD; `P/W`; MP-100, MP-103–104.
- **Red:** create a single-map graph whose anchors reference the final native node IDs; every node maps to a valid reachable region with valid distance/GUID metadata. Missing component coverage, mismatched game/level GUIDs and arbitrary edges across disconnected regions are rejected. No actor spawn or ALife population is required to build this package.
- **Green / seam:** a narrow graph/cross-table builder or adapter using [game_graph_builder.cpp](../../src/utils/xrLevelBuilder/xrAI/Graph/game_graph_builder.cpp) and existing serializers. Avoid invoking the actor-dependent FreeMP spawn constructor as the only DM route. Select supported disconnected-component policy explicitly.
- [ ] **Complete when:** actual runtime graph/cross-table consumers accept the produced data and route/region lookups agree with native grid connectivity without a fabricated walk path.

### MP-106 — Package nav beside the exact matching map and reject stale data

- **Kind / runs / depends:** TDD; `P/W`; MP-020, MP-101, MP-103–105.
- **Red:** missing `level.ai`/package, changed collision chunk/material/agent profile or consumed ladder/spawn/game record, wrong map name/version, changed AI GUID, invalid offsets/counts and bad traversal references fail before navigation is published. A partially written package cannot replace a valid one; VFS overrides resolve consistently.
- **Green / seam:** versioned proposed `level.botnav` container with identity/hashes, minimal graph/embedded cross table and annotations, paired with native `level.ai`. Implement a checked validator, atomic install/staging procedure and run/deployment manifest. Keep authoring inputs rebuildable and source-controlled separately from generated output.
- [ ] **Complete when:** the selected map's `$level$` resolves a valid matching pair, deliberate stale/corrupt artifacts are rejected with map/file/reason diagnostics, and the runbook lists real output paths and commands.

### MP-107 — Bootstrap nav on the authoritative dedicated server

- **Kind / runs / depends:** TDD; `W`; MP-023, MP-026, MP-031, MP-104–106.
- **Red:** start dedicated DM with valid package but no `alife.spawn`; navigation becomes ready after collision load and before bot consumers. Missing/invalid nav cleanly rejects bot-enabled startup or permits only explicitly human-only mode. Unload/reload with a pending path job rejects the old map-generation result without reading freed graph memory.
- **Green / seam:** explicit DM initialization in [Level_load.cpp](../../src/xrGame/Level_load.cpp), backed by a navigation owner/compatible [CAI_Space](../../src/xrGame/ai_space.cpp) adapter. Initialize the game graph/cross table before `CAI_Space::load`; retain backing readers as long as `CGameGraph` references them. Stop jobs/consumers before releasing caches, graphs and readers.
- [ ] **Complete when:** the actual DS path initializes/queries/unloads nav independently of `OnClient()` and ALife actor population, including assertions-disabled validation and repeated map changes.

### MP-108 — Prove generated paths work through native stalker movement

- **Kind / runs / depends:** TDD + visual integration GATE; `P/W`; MP-070, MP-100–107, MP-109.
- **Red:** a stalker-based bot projects onto the correct floor, finds a route around real cover, walks stairs/doorways and executes selected typed links through its NPC movement/physics. Test unreachable destination, door-state change, temporary crowd, stale path and map reload; path failure triggers bounded recovery rather than position warping.
- **Green / seam:** adapt native graph search and stalker path following behind a narrow MP navigation interface; add the required traversal actions. Visualize route plus collision/node/link state. Generation, attachment and actual NPC execution must share map/agent identity; a future alternate body requires separate capability acceptance.
- [ ] **Complete when:** the first arena's required-route suite passes on a dedicated Windows server after a fresh bake and after reload. Preserve captures, native/package hashes, GUIDs and controller/agent parameters; a successful abstract A* search alone is insufficient.

### MP-109 — Extract semantic ladders and validate climb-link candidates

- **Kind / runs / depends:** TDD; `P/W`; MP-023, MP-031, MP-070, MP-100–102.
- **Red:** decode supported `CSE_ALifeObjectClimable` spawn fixtures and compare oriented volume/axis/normal/endpoints with actual `CClimableObject` behavior. Test rotated/reversed/slanted boxes, missing/invalid shape, mode-disabled object, duplicate record, obstructed landing, wrong floor, decorative ladder without a climbable record and stale map/entity generation. No visual-only ladder creates a usable climb link automatically.
- **Green / seam:** extract supported `level.spawn` semantics or enumerate authoritative active ladders, sharing orientation/endpoint logic with [ClimableObject.cpp](../../src/xrGame/ClimableObject.cpp). Project safe approach/exit candidates onto native walk layers; retain semantic identity/freshness for MP-103/106. Add an NPC climb action and compatible physics/animation integration, reusing [CElevatorState](../../src/xrPhysics/ElevatorState.cpp) where it fits. Validate real mount/climb/stop/dismount without teleportation; existing player support alone is insufficient. Human prediction restore stays in MP-041/047.
- [ ] **Complete when:** selected real stock-map ladder candidates are enumerated with a diagnostic overlay and traversed by a native-stalker bot on Windows; invalid/missing landings are rejected or marked for correction. NPC climb pose/state replicates, changed ladder semantics invalidate the package, and map reload never retains a raw stale ladder pointer.

## Requirement-to-test map

| Requirement or major risk | Primary work items |
| --- | --- |
| macOS editing/tests, Windows engine validation | MP-001–004, MP-010–013, MP-086–087 |
| Early one-stalker/two-client feasibility before larger rewrites | MP-005, then production hardening MP-070 |
| Real GameNetworkingSockets direct IP | MP-012, MP-020–029, MP-082 |
| 32 total humans/bots, separate connection load | MP-023, MP-033, MP-070, MP-078–079, MP-082–085 |
| Simple DM rules, loadout, respawn, score and spectator | MP-004, MP-033–038, MP-057, MP-062, MP-079 |
| Config/console operation, coherent active-match joining and repeated-arena rounds | MP-022–023, MP-035, MP-037–039, MP-087 |
| Authoritative commands/state and bounded timing | MP-030–036, MP-040–041, MP-046, MP-050, MP-084 |
| Client prediction, reconciliation and remote interpolation | MP-040–047, MP-060, MP-081 |
| Historical hit validation and projectile compensation | MP-050–058, MP-061–062, MP-081 |
| Weapon-based and presentation hybrids | MP-059–062, MP-077, MP-081 |
| Optional distance-based hybrid | MP-066–067; selected explicitly at MP-061 |
| Preferred native-stalker DM integration, knowledge, tactics, traversal and skill | MP-070–079, MP-088 |
| Generate nav from SDK sources or compiled MP collision, attach and load it on the server | MP-071, MP-100–109 |
| Recognize stock-map ladders, build valid links and execute NPC climb actions | MP-103, MP-106, MP-108–109; human climb prediction separately in MP-041/047 |
| Robust receive/lifetime behavior in Shipping | MP-020–029, MP-034, MP-050, MP-084–086 |
| Later Steam identity/invites/relay | MP-090–094 |

## Start here and record progress

The first useful delivery is MP-001 plus MP-010, followed by MP-002/003/004 and MP-011/012/013 as prerequisites become available. Restore the two-client Windows baseline and complete MP-005 before larger engine networking/controller/prediction rewrites. Portable MP-020 schema/codec tests can proceed independently: start with one hand-authored legal packet and one truncated-packet failure, implementing one checked field at a time. Then follow the production dependencies, including MP-039 for the confirmed console/configuration workflow.

Use this completion note in the change description or append a compact entry under its item:

```text
Item: MP-...
State: open | in progress | implemented / unverified | Mac checks passed / Windows pending | Windows build passed / runtime pending | done | deferred
Validation batch / Windows checkpoint:
Provisional dependencies and platform evidence still required:
Behavior selected for this cycle:
Red: command, observed failure, fixture/seed
Green: command, result, commit/configuration
Deferred Windows comparison SHAs and actual execution order, if applicable:
Refactor: change and rerun result
Windows/real-peer/human evidence as required:
Artifacts/content/hardware revision:
Remaining cases, limits or decision dependencies:
```

Do not check an implementation item complete solely because this document exists or a proposed test name has been added. The intended output of each item is tested production behavior plus reproducible evidence.
