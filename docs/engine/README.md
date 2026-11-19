# Engine

Source of truth for `src/`. Modder docs stay in `docs/docs`.

Current branch uses separate `xrRender_R1`, `xrRender_R2`, and `xrRender_R4` client renderers. Entry is `CApplication::Run` in `src/xrPlay/Application.cpp`. Verify external documentation against the current sources.

| Doc | Contents |
| --- | --- |
| [architecture.md](architecture.md) | DLL load, globals, registrators, factory |
| [lifecycle.md](lifecycle.md) | Load stages, `on_idle`, threads |
| [core.md](core.md) | Memory, `FS`, ltx, console, input, sound |
| [rendering.md](rendering.md) | R1/R2/R4 selection, APIs, dynamic and static frames |
| [gameplay.md](gameplay.md) | Level, ALife, Lua, class files |
| [multiplayer-modernization-plan.md](multiplayer-modernization-plan.md) | Code assessment and plan for 32-combatant DM, modern networking, and bots |
| [multiplayer-tdd-work-items.md](multiplayer-tdd-work-items.md) | TDD backlog with dependencies, failing cases, and Windows validation gates |
| [multiplayer-implementation-contracts.md](multiplayer-implementation-contracts.md) | Confirmed DM defaults, early NPC gate, protocol/state contracts, loading, tick order, and console operation |
| [multiplayer-development-workflow.md](multiplayer-development-workflow.md) | Mac development, batched Windows runtime milestones, evidence states, and optional automated handoff |
| [multiplayer-bot-ai-design.md](multiplayer-bot-ai-design.md) | Researched bot architecture, perception, tactics, aim/reaction difficulty, slot filling, and test cases |
| [multiplayer-npc-ai-explained.md](multiplayer-npc-ai-explained.md) | Plain-language NPC logic, scaling to 32 bots, existing optimizations, limitations, and tradeoffs |
| [multiplayer-navigation-pipeline.md](multiplayer-navigation-pipeline.md) | SDK and compiled-collision navigation generation, per-map packaging, and server loading |
| [camera-focus.md](camera-focus.md) | Actor camera focus, Lua API, input ownership, transitions |
| [item-parameters.md](item-parameters.md) | Inventory parameter rows, signs, icon colors |
| [build.md](build.md) | Targets and CMake options |
| [style.md](style.md) | RAII, types, platform split |

Agent entry: [AGENTS.md](../../AGENTS.md).
