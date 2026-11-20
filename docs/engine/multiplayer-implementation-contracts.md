# Multiplayer implementation contracts

Status: proposed implementation contract, 19 November 2026, source baseline `2cd274927`. No protocol, test target or engine behavior below is implemented by this document. User decisions are separated from engineering defaults that can be revised with test/profiling evidence.

Parent: [modern multiplayer plan](multiplayer-modernization-plan.md). Behavior: [bot design](multiplayer-bot-ai-design.md). Implementation: [TDD work items](multiplayer-tdd-work-items.md).

Development/validation cadence: [Mac work and Windows milestones](multiplayer-development-workflow.md). Portable checks run during local development; Windows integration evidence is collected for coherent exact-revision checkpoint candidates, with pending platform status retained honestly.

Every Windows check ships with its scripts, checkpoint configuration, short instructions, expected results/manual observations and automatic diagnostic collection as part of the implementation. This is required from MP-001/002 onward, before requesting the user's execution; only remote triggering is optional. See the [test-kit delivery contract](multiplayer-development-workflow.md#required-deliverables-for-every-checkpoint).

## Confirmed first playable scope

- Existing stalker NPC brain and body, proved early with two clients before larger networking/prediction changes: MP-005. Continue on that route when it passes; production hardening remains MP-070.
- Join an active match through loading/synchronization and a brief spectator stage. Repeat one arena automatically; changing maps restarts the server.
- Server configuration/local console and client console controls. Existing gameplay HUD, scoreboard and results remain necessary.
- Humans and bots share health, armor, damage and weapon rules; NPC movement is retained and tuned.
- Spawn with basic equipment; collect weapons and ammunition on the map. Exact equipment, item locations and pickup timers belong to the selected arena/rules profile.
- Configurable defaults: automatic respawn after five seconds, two seconds of protection ending on firing, and rounds ending after twenty minutes or fifty frags, whichever is first.
- Select the arena after inspecting compiled maps/navigation inputs. Reference PC: AMD Ryzen 7 5700, 32 GB RAM, RTX 3080. Client/server cohosting is not specified; report isolated and combined workloads separately.

Hitscan, retained ballistics and hybrids remain subject to MP-061's comparison. These contracts support all three without treating them as equivalent collision or compensation implementations.

## 1. Early proof and production integration are different gates

MP-005 uses the existing two-client Windows baseline and a valid minimal navigation fixture. It demonstrates actual NPC movement, weapon use, damage, scoring, death and respawn. Temporary integration is allowed only in an explicit development/prototype mode, with a list of adapters to remove or harden. Do not weaken normal peer admission or grant ordinary network clients a trusted damage/spawn path to make the demo pass.

Do not wait for complete prediction, historical collision or production navigation tools before attempting this proof. A missing fixture requires preparing one; it does not demonstrate that the NPC planner is unsuitable. A failed proof remains an open gate with specific evidence. Record the unmet requirement and attempted adaptation before considering a retained controller/brain fallback.

MP-070 later verifies the same route with the production protocol, roster/life identities, admitted weapon actions, snapshots, dedicated poses and hit history. Passing MP-005 does not check off MP-070 or establish thirty-two-bot capacity.

## 2. Identity and lifetime

The following are application identities. They do not authenticate a peer and do not replace binding a received request to its admitted transport connection.

| Identity | Proposed wire representation | Lifetime / rule |
| --- | --- | --- |
| `ServerSessionId` | Nonzero `u64` | Fresh on each server process/session startup; generated through a platform randomness seam |
| `WorldEpoch` | `u32` | Advance at every authoritative round/world reset, including the same map. Restart before reuse on wrap; do not accept a previous epoch |
| `ConnectionEpoch` | `u32` | Server-assigned admission generation within the session; a reconnect does not inherit its predecessor's identity |
| `ParticipantId` | `u32` | Human or bot roster identity; stable across its respawns. A replacing human gets a new identity |
| `EntityRef` | `u16 ObjectId`, `u32 Generation`, `u32 LifeId` | Explicit mapping to the engine's object slot, its reuse generation and the combatant life; checked before lookup/use |
| `SnapshotId`, `CommandSequence`, `ActionId`, `StateEventSequence` | Separate `u32` counters | Different namespaces; bounded serial-number comparisons/windows and wrap tests, not plain unbounded integer ordering |
| `MapGeneration`, `NavRequestId` | Internal runtime IDs | Navigation/job lifetime; changing round invalidates bot work even when static graph data remains loaded |

Every gameplay message is bound to session, world epoch and connection admission. Global public event IDs may be reused across recipients; ordering/watermarks are for each recipient's filtered state-event stream. A socket handle is not a participant, and an entity/life ID is not a round ID.

Initial handshake messages have an explicit unbound state. They may establish identity/content/admission only; zero or peer-chosen IDs cannot authorize gameplay. Invalid future epochs do not advance local state: only an accepted server transition can do that.

### Combatant identity registry

The match owner serializes a fixed registry of 32 combatants. Adding a combatant assigns a nonzero participant ID for the server session. The registry serves humans and bots. Bots acquire a participant ID without a connection. Connection admission, combatant eligibility, and spectator capacity are managed by their respective owners.

The owner binds a free engine slot to a combatant after creating its body. Each successful bind starts that participant's next life in the current round and takes the next generation from a counter shared by all slots in that round. This keeps reuse safe without retaining old slot entries. A combatant can have at most one active body. Lookup and retirement require the exact session, world epoch, slot, generation, and life. A rejected operation leaves the registry intact.

Retiring a body leaves the participant available for respawn. Removing a participant checks its session and releases both the body mapping and its capacity place. Removing a participant remains valid across rounds in the same session. A round advance retires all body mappings and resets generation and life counters while preserving the registered participants. Participant, generation, and life counters issue nonzero values and stop at exhaustion. A newer round supplies fresh body counters and a fresh server session supplies a fresh participant counter. The world epoch never wraps within a session. [Identity types](../../src/xrNetServer/Multiplayer/CombatantIdentity.h), [registry](../../src/xrGame/Multiplayer/CombatantRegistry.h).

### Admitted connection binding

The match owner records an accepted human admission in the combatant registry on its serialized turn. The operation creates a fresh participant and stores the transport handle, server session, and admission epoch on the same entry. The admission owner supplies its assigned epochs in commit order. Duplicate live handles, zero handles, stale or unassigned epochs, foreign sessions, and full capacity leave the registry intact. Connection epochs remain distinct across rounds and stop at exhaustion.

The transport owner captures the local admission identity from the receiving connection before queuing data or a disconnect. At application time, the exact local identity must still match the registry. The packet must also claim the current session, current world epoch, and the same connection epoch. After lookup, the command handler checks readiness and the active body.

Disconnect checks the local identity and releases that participant and any active body mapping together. It can arrive across a round change in the same session. Round advance preserves connection bindings and invalidates old-world packets. A new admission may reuse a freed handle with a newer connection epoch and its own participant ID. [Local connection identity](../../src/xrNetServer/Multiplayer/JoinTypes.h), [registry](../../src/xrGame/Multiplayer/CombatantRegistry.h).

### Legacy loading and admission during endpoint migration

The game owner advances the legacy connection in loading turns. A remote client waits up to 60 monotonic seconds for the game description, then receives a fresh 60-second authentication window. Direct single-player waits for the local registration result. A result processed on the deadline takes precedence over timer expiry. Transport loss takes precedence over either result. Authentication completes only after the legacy configuration gate and starts clock synchronization. Deadline handling uses the loading state directly.

The SteamNet client and server lease one process runtime. The first live lease fixes the process identity and the final release shuts down GNS. Owner-polled endpoints keep the legacy receive paths. The server owns its listener and receive group. The client owns its outbound connection and polls it directly. Normal server disconnect closes accepted peers before releasing the listener and lease. The endpoint destructor releases any remaining native handles without calling into the game.

During loading and play, the owner pumps at most 64 native datagrams per endpoint before dispatching the shared GNS status queue. The callback records up to 64 distinct incoming requests. If that batch fills, GNS rejects additional arrivals as session full. After the callback returns, the owner retires closed connections, revalidates pending handles against the live listener, and then applies the legacy policy in order. Duplicate and capacity checks precede the ban list, address filter, and password. The hosted candidate remains subject to capacity and bans. The existing IP and password exceptions stay in the legacy policy. [Loading driver](../../src/xrGame/Level_network.cpp), [transport poll](../../src/xrNetServer/LegacyGnsTransport.cpp), [portable tests](../../src/test/multiplayer/portable/LegacyStartupTests.cpp).

### Legacy multipacket receive framing

A legacy datagram fits in 32 KiB and begins with the one-byte single or merged tag followed by the little-endian 16-bit decoded byte count. The active format carries an encoding marker. Raw bodies must have exactly the declared length. A packed body carries a four-byte little-endian CRC-32/ISO-HDLC checksum immediately before the LZO block. Validate the checksum over exactly the transmitted block bytes before decoding. LZO uses the declared decoded size as its output capacity, rejects invalid or unconsumed input, and must produce exactly the declared length before the record pass. Both peers need this checksum rule and the same optional dictionary for packed traffic.

Each legacy game record contains at least a 16-bit message type and occupies fewer than 16 KiB. Merged bodies prefix each record with a little-endian 16-bit length. Validate the entire decoded body before handing any record to the receiver or writing the receive trace. The validated cursor borrows the bytes until dispatch completes. [Legacy framing](../../src/xrNetServer/Multiplayer/LegacyMultipacket.h), [live receiver](../../src/xrNetServer/NET_Common.cpp).

### Legacy clock synchronization during endpoint migration

The loading owner polls synchronization once per loading turn after a remote connection is accepted. During play the frame owner polls the same attempt for console resync. The owner cancels before releasing the client connection. Direct single-player completes without sending a probe.

For a remote connection, send one legacy ping when the transport accepts it. If the endpoint is busy, the owner has five monotonic seconds to send. A rejected send or a send deadline fails the attempt. An accepted send opens a separate five-second reply window. The receive path records the first matching echoed timestamp. The owner applies the signed offset using half the round trip and the existing 32-bit wire clock. If the reply window expires, the owner completes with its prior offset. The clock state belongs to the loading or frame owner. The receive worker only records the matching sample. [Clock state](../../src/xrNetServer/Multiplayer/LegacyClockSync.h), [portable tests](../../src/test/multiplayer/portable/LegacyClockSyncTests.cpp).

### GNS callback ownership

The transport process owner opens, polls, and closes the endpoints. One callback pump calls GNS for the shared interface and routes notifications to the live listener or outbound connection. Outbound setup installs the pump callback and a fresh local generation at native connection creation. The listener allocates an inbound generation after checking the current native connection. A queued terminal callback must still identify that connection and generation. A native inbound connection that closes before acceptance is released without publishing a game event.

Each endpoint has its own receive group and inbox. The pump enqueues the connect before assigning the native peer to that receive group. The receive worker can then enqueue packets for that generation. Apply the pump result before draining either inbox. An overflow or receive-group failure requires closing and cleaning up the affected endpoint. The other endpoint can keep polling and draining its own events. Explicit connection close can deliver a reason to the peer. Destroying a listener releases accepted handles and can leave peer-side closure to the native timeout. [Callback pump](../../src/xrNetServer/Multiplayer/GnsCallbackPump.h), [deterministic routing fixtures](../../src/test/multiplayer/gns/GnsCallbackPumpTests.cpp).

### Transport event inbox

The endpoint assigns each connection a positive local transport generation that fits GNS's signed 64-bit connection user data. Keep that generation fixed for the life of the native connection. Record it before publishing any event for that connection. Publish connect before enabling packet receipt on the endpoint's poll group. The receive worker takes the generation from the returned GNS message, copies the bytes, and releases the native buffer. Connect and disconnect producers use the generation of the connection that caused the event. The transport generation identifies the native connection before admission and is independent of the admission epoch carried on the wire.

The endpoint inbox uses a fixed number of caller-owned slots. Each slot holds one event and up to the wire datagram limit of packet data. A single owner drains in enqueue order under an event budget, with application outside the queue lock. The owner registers the generation on connect and matches the handle and generation for packets and disconnects. A retired connection's packet may reach the queue after its disconnect and is rejected by that check.

When enqueueing would exceed capacity, the inbox discards its backlog and reports overflow on subsequent reads and writes. The game owner closes the affected endpoint, retires all its peers, and then resets the inbox. This recovery also applies if an overflowing event is a disconnect. [Bounded inbox](../../src/xrNetServer/Multiplayer/TransportInbox.h), [GNS receive bridge](../../src/xrNetServer/Multiplayer/GnsReceive.h).

### Owner-side transport event routing

The game owner binds each version-one endpoint inbox to a fixed table of local transport peers. The loading owner registers the outbound handle and generation before callback pumping. A matching disconnect completes that attempt even if native connect never arrived. Packets require a confirmed connection. Existing pending attempts may confirm in native order. Unsolicited connect events for a listener are published in generation assignment order.

The owner consumes stale and duplicate events within the same budget as delivered events. A full peer table asks the owner to close the newcomer without admitting it. Keep the highest observed generation across a rejected connect, retirement, and endpoint reopen. A newer generation competing with a live handle is an endpoint fault. Inbox overflow and handle conflict stop the drain. After closing the endpoint and stopping its producers, retire each remaining active or pending peer once and reset the inbox. An event already popped can finish before the owner sees a later producer overflow. [Owner router](../../src/xrNetServer/Multiplayer/TransportEventRouter.h), [routing checks](../../src/test/multiplayer/portable/TransportInboxTests.cpp).

## 3. Version-one wire contract

Use explicit little-endian primitives, UTF-8 length-prefixed strings and checked reads/writes. A wire table is not a packed C++ structure. Freeze numeric message tags, field order and independent golden byte vectors in MP-020 before implementing each message family; update this contract alongside those vectors.

Version-one common envelope in byte order.

| Field | Encoding |
| --- | --- |
| Magic | Four fixed bytes `IXMP` |
| Protocol version | `u16` equal to `1` |
| Message type | `u8` |
| Flags | `u8` equal to `0`. All bits are reserved for this slice. |
| Payload byte length | `u16`, must match the actual complete message |
| Server session | `u64` |
| World epoch | `u32` |
| Connection epoch | `u32` |
| Message sequence | `u32`, scoped to direction/message stream; not a substitute for command/action/state-event IDs |

The following tags are assigned for version one. Additional tags become valid as their message families are specified. The receiver rejects every other tag.

| Message type | Tag |
| --- | --- |
| `ClientHello` | `0x01` |
| `ServerWelcome` | `0x02` |
| `AdmissionReject` | `0x03` |
| `ClientMapLoaded` | `0x04` |
| `ReadyAcknowledgement` | `0x05` |
| `BaselineBegin` | `0x06` |
| `BaselineChunk` | `0x07` |
| `BaselineEnd` | `0x08` |
| `ServerJoinBarrier` | `0x09` |
| `CatchUpStateEvent` | `0x0a` |
| `SnapshotBatch` | `0x0b` |
| `CombatantBarrierChunk` | `0x0c` |

The envelope codec checks the complete message size and framing. Payload decoders and the admission state machine validate fields and identity before dispatch. The returned payload view borrows the receive buffer for the duration of decoding. [Codec](../../src/xrNetServer/Multiplayer/WireEnvelope.h), [independent golden frames](../../src/test/multiplayer/portable/WireEnvelopeTests.cpp).

This header is 30 bytes. Start with an application-message cap of 1,100 bytes including it, leaving 1,070 payload bytes. Measure actual UDP/GNS overhead against the parent's roughly 1,200-byte target; the allowance is not a transport guarantee. Large initial state uses bounded chunks. Do not enable compression in the new envelope until a measured need and decompression limits/tests exist; legacy envelopes retain their separately checked compatibility path.

Initial limits: participant names up to 32 UTF-8 bytes, logical map identifiers up to 64 bytes, bounded credentials up to 128 bytes, at most four recent commands and eight pending discrete action records per input message. Reject invalid UTF-8, unknown required tags/bits, bad lengths/counts, nonfinite/out-of-profile values and unsupported versions before state mutation. Never log credential payloads.

### Payload field encoding

| Field | Encoding |
| --- | --- |
| Integers | Fixed-width little-endian `u8`, `u16`, `u32`, or `u64`. Signed `s8` and `s16` use two's complement. |
| Finite scalar | Little-endian IEEE 754 binary32. Signed zeros and subnormals retain their bits. Encodings with an all-ones exponent are rejected. |
| Text | A little-endian `u16` byte count followed by that many UTF-8 bytes. The caller supplies the field's byte limit. The primitive accepts empty text and U+0000 as Unicode scalars. Field codecs enforce any additional text policy. |

Each operation checks its input before advancing the reader or writing output bytes. A decoded string borrows the payload buffer. Message codecs can read into a local cursor and publish decoded fields after checking the complete payload. [Payload codec](../../src/xrNetServer/Multiplayer/WirePayload.h), [UTF-8 syntax](https://www.rfc-editor.org/rfc/rfc3629.html#section-4).

Additional initial engineering defaults, configurable within validated limits and subject to fixtures/profiling:

| Limit | Starting value / handling |
| --- | --- |
| Admission / map-loading / synchronization deadlines | 10 / 120 / 30 monotonic-wall-clock seconds; report the failed stage separately |
| Initial-state staging | At most 4 MiB and 8,192 records per peer; at most 4,096 chunks, each carrying at most 1,024 baseline-data bytes |
| Catch-up state events | At most 4,096 queued relevant events per synchronizing peer, within the owner-provided byte capacity. Overflow triggers a newer baseline. |
| Snapshot waiting/unknown references | At most eight pending batches and 256 entity references per peer; keep latest useful state and request repair/rebase rather than growing indefinitely |
| Rebase attempts | Three accepted attempts per connection in a rolling 60-second window. The next valid attempt closes with a synchronization failure reason. |

Per-peer limits also require a server-wide allocation/work cap derived from the selected arena and maximum concurrent loading peers. These defaults bound an individual peer; they do not promise that multiplying every maximum by every connection is an acceptable memory/CPU load. MP-022/037 must record that aggregate cap before their stress gates pass.

### Message families and delivery

| Family | Required semantic fields | Delivery and ownership |
| --- | --- | --- |
| Client hello/admission, server welcome/reject | Build/protocol, content/rules identity, requested display name, bounded admission data; assigned session/connection, map, effective rates and rules | Reliable, bounded deadlines; transport/simulation admission state machine |
| Initial-state begin/chunk/end | Baseline ID, session/epoch, capture tick, event watermark, checked record/chunk counts and total byte count | Reliable, bounded staging; no gameplay side effects from partial state |
| Map loaded / join barrier / ready acknowledgement / activation | Bound map-loaded confirmation, baseline/barrier ID, accepted snapshot tick and state-event watermark | Reliable; only the server activates a participant |
| Player input | Command sequence/tick, move/look/held buttons, repeated unacknowledged discrete action records | Unreliable with bounded redundancy; simulation validates and retires each command/action once |
| Action result | Action ID, accepted/rejected/expired, execution tick, reason and resulting shot ID where applicable | Reliable result; same result on duplicate requests |
| Runtime entity-state batch | Snapshot ID/tick, current monotonic server clock anchor, required state-event watermark, typed length-bounded self-contained entity records | Unreliable; latest useful state per entity/life, no waiting for a lost previous batch |
| State event / round transition | Per-recipient state-event sequence, durable event ID, tick, referenced participants/entities, checked typed payload | Reliable and idempotent; replicated state remains the recovery authority |
| Accepted shot / impact | Shot/impact identity, shooter/life, execution/impact time, weapon profile, origin/direction or trajectory/material result | Public effects may be lossy; action result, damage/death and inventory/score convergence must remain durable |

GNS delivery does not establish application causality across message classes. Valve documents that reliable ordering is guaranteed on the same lane, while different lanes and unreliable messages can arrive in a different order. Hence readiness, epochs and watermarks are required even when reliable messages exist. [Valve socket interface](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/isteamnetworkingsockets.h).

Start with one ordered reliable control/state stream per peer plus independently sequenced unreliable gameplay state. Use a small number of GNS lanes only if supported by the pinned dependency and justified by measurements; application queues/limits are required either way. Do not assume today's upstream headers match this checkout's packaged library.

### Reliable outbound submission

The version-one transport owner uses one reliable FIFO for each peer generation. Its slot span, encoded byte budget and maximum pending age are supplied by the owner. One submission call offers the oldest frame to GNS at most once. GNS acceptance retires the frame from the application queue. Buffer refusal keeps the frame with its original enqueue time. At the age limit the owner receives a close-required result before another send can begin. Slot or byte exhaustion and terminal GNS errors also require closure. The owner can record the queued count, bytes, oldest age and first failure before retiring the queue with the connection. These metrics describe application backlog. Native send-buffer pacing and the production queue limits belong to the integrated transport. [Queue](../../src/xrNetServer/Multiplayer/ReliableSendQueue.h), [GNS adapter](../../src/xrNetServer/Multiplayer/GnsReliableSend.h).

### Client hello

A `ClientHello` uses envelope tag `0x01` and carries protocol version `1` in the envelope. The server session, world epoch, and connection epoch are zero for this unbound message. The envelope carries the caller's message sequence.

| Payload field in order | Encoding |
| --- | --- |
| Build compatibility ID | 32 opaque bytes |
| Content manifest ID | 32 opaque bytes |
| Rules ID | 32 opaque bytes |
| Requested display name | `u16` byte count followed by up to 32 UTF-8 bytes. Empty means the client supplies no display preference. The field rejects an embedded U+0000. |
| Credentials | `u16` byte count followed by up to 128 opaque bytes. A count of zero carries no credentials. |

The payload length is 100 through 260 bytes. The decoder consumes the complete payload and rejects missing fields and trailing data. Admission compares the claimed compatibility IDs against the server's expected values and verifies credentials before assigning connection identity. [Codec](../../src/xrNetServer/Multiplayer/ClientHello.h), [independent golden datagram](../../src/test/multiplayer/portable/ClientHelloTests.cpp).

### Server welcome

A `ServerWelcome` uses envelope tag `0x02`. The server assigns a nonzero session and connection epoch. The world epoch may be zero for the initial world. The envelope carries the server's message sequence.

| Payload field in order | Encoding |
| --- | --- |
| Content manifest ID | 32 opaque bytes |
| Rules ID | 32 opaque bytes |
| Logical map ID | Little-endian `u16` byte count followed by 1 to 64 UTF-8 bytes. The field rejects an embedded U+0000. |
| Gameplay, physics, snapshot, and input command rates | Four positive little-endian `u32` values in millihertz, in that order. For example, 37.5 Hz is `37500`. |
| Respawn delay and spawn protection | Two little-endian `u32` durations in milliseconds, in that order. Zero represents no delay or no protection respectively. |
| Round time limit and frag limit | Two little-endian `u32` values, milliseconds then frags. A zero value disables its criterion. |

The payload spans 99 to 162 bytes and the decoder consumes it completely. The client resolves the map against the verified content manifest. Timing and rules profiles validate the advertised settings before use. [Codec](../../src/xrNetServer/Multiplayer/ServerWelcome.h), [independent golden datagram](../../src/test/multiplayer/portable/ServerWelcomeTests.cpp).

### Admission reject

An `AdmissionReject` uses envelope tag `0x03` during unbound admission. The server session, world epoch, and connection epoch are all zero. The envelope carries the server's message sequence and a payload of exactly one `u8` reason.

| Reason | Byte | Meaning |
| --- | --- | --- |
| `MalformedHello` | `0x01` | The hello failed wire validation. |
| `UnsupportedProtocol` | `0x02` | The peer's protocol version is unsupported. |
| `IncompatibleBuild` | `0x03` | The build compatibility ID differs. |
| `IncompatibleContent` | `0x04` | The content manifest ID differs. |
| `IncompatibleRules` | `0x05` | The rules ID differs. |
| `CredentialsRejected` | `0x06` | Required credentials are absent or invalid. |
| `ServerFull` | `0x07` | No eligible match slot is available. |
| `ServerBusy` | `0x08` | The pending-admission limit has been reached. |
| `AdmissionTimedOut` | `0x09` | The initial admission deadline expired. |
| `InvalidState` | `0x0a` | The request is invalid in the current admission state. |

All other reason bytes are reserved and rejected. Client presentation maps these codes to local text. When peers have no shared envelope version, connection failure diagnostics surface the incompatibility. MP-022 owns the state transitions and selects the reason. [Codec](../../src/xrNetServer/Multiplayer/AdmissionReject.h), [independent golden datagram](../../src/test/multiplayer/portable/AdmissionRejectTests.cpp).

### Client join confirmations

Both messages travel from the client on the reliable control stream. The envelope carries the message sequence and the server-assigned session, world epoch, and connection epoch. Session and connection must be nonzero. World epoch zero is valid.

`ClientMapLoaded` uses tag `0x04` and has an empty payload. The client sends it after loading the arena assigned by `ServerWelcome`.

`ReadyAcknowledgement` uses tag `0x05` and has a 16-byte payload in this order.

| Payload field | Encoding |
| --- | --- |
| Baseline ID | `u32` |
| Barrier ID | `u32` |
| Accepted snapshot tick | `u32` |
| State-event watermark | `u32` |

The decoders require the exact payload size and return owned values. Bind a decoded confirmation to the tracker for the receiving transport connection. `PeerJoin` checks the current identity and compares the acknowledgement with the server-declared barrier. [Codec](../../src/xrNetServer/Multiplayer/ClientJoin.h), [independent datagrams and readiness handoff](../../src/test/multiplayer/portable/ClientJoinTests.cpp).

### Server join barrier

The server sends `ServerJoinBarrier` on the ordered reliable control/state stream, scoped to the receiving peer by the bound envelope. Its exact 16-byte payload uses the four fields in the [ready acknowledgement layout](#client-join-confirmations) in the same order. The server and client use their own envelope message sequences. The barrier ID names the staged snapshot and the event watermark identifies the last event the client must apply before that snapshot. The server queues the declaration before any event beyond its watermark. The public roster transfer uses the [combatant barrier chunk layout](#public-combatant-barrier-chunks).

The declaration and acknowledgement have distinct tags and decoders. A declaration only supplies the barrier values. The client checks the current identity and baseline, applies the required state, and sends the acknowledgement. [Codec](../../src/xrNetServer/Multiplayer/ClientJoin.h), [handoff test](../../src/test/multiplayer/portable/ClientCatchUpTests.cpp).

### Public combatant barrier chunks

The server sends bound `CombatantBarrierChunk` frames on the ordered reliable control/state stream after the declaration. Each frame starts with the same 16-byte barrier identity, followed by these fields.

| Payload field in order after the barrier | Encoding |
| --- | --- |
| Total public record bytes | `u32` |
| Total public record count | `u16` from 0 through 32 |
| Chunk index | Zero-based `u16` |
| Record stream bytes | The canonical chunk of the complete public roster |

Every chunk repeats the same declaration and totals. The byte count must be possible for the declared number of [public combatant records](#public-combatant-record-bodies), including their framing. A nonempty roster uses up to three chunks at successive 1,024-byte offsets with the exact remaining tail, as in the baseline transfer. A full frame is 1,078 bytes. An empty roster uses one header-only chunk with both totals and the index set to zero. [Chunk codec](../../src/xrNetServer/Multiplayer/CombatantBarrier.h), [golden and transfer tests](../../src/test/multiplayer/portable/CombatantBarrierTests.cpp).

### Catch-up state event

The server sends `CatchUpStateEvent` on the ordered reliable control/state stream. The envelope binds the receiver's session, world, and connection. Its message sequence is independent of the per-recipient state-event sequence. The first event kind, `0x01`, retires one combatant body. It carries exactly 35 payload bytes.

| Payload field in order | Encoding |
| --- | --- |
| Selected baseline ID | `u32` |
| State-event sequence | `u32` |
| Durable event ID | Nonzero `u64` |
| Server tick | `u32` |
| Event kind | `u8` equal to `0x01` for combatant despawn |
| Participant ID | Nonzero `u32` |
| Engine object slot | `u16` from 0 through 65,534 |
| Object generation | Nonzero `u32` |
| Life ID | Nonzero `u32` |

World epoch, baseline ID, ticks and sequences allow zero. The durable event ID identifies the same event when sent to different recipients. The decoder returns owned event fields and a view of the checked frame for catch-up retention and application. Keep the datagram alive and unchanged until the queue copies the bytes or the client finishes applying them. The world adapter resolves the exact body in the current world and checks its participant before retiring it. The portable replica registry applies despawns to checked body identities. Other event kinds, live delivery, and engine world application follow separately. [Codec](../../src/xrNetServer/Multiplayer/CatchUpDespawn.h), [wire and handoff tests](../../src/test/multiplayer/portable/CatchUpDespawnTests.cpp).

### Live snapshot batch

The server sends `SnapshotBatch` on the unreliable state stream using the bound envelope for the receiving peer. The payload starts with a 22-byte header.

| Payload field in order | Encoding |
| --- | --- |
| Snapshot ID | `u32` |
| Authoritative server tick | `u32` |
| Monotonic server clock anchor | `u64` milliseconds from a server-session monotonic origin |
| Required applied state-event watermark | `u32` for this recipient |
| Record count | `u16` from 1 through 32 |
| Records | In order, each with a `u8` kind, a `u16` body byte count and that many body bytes |

The current framing assigns `0x01` to public human combatants and `0x02` to public stalker combatants. It rejects unknown kinds, empty bodies, missing records and trailing bytes before returning a cursor. The 1,100-byte frame cap includes all headers and records. Zero is valid for the clock anchor, snapshot ID, tick and watermark. Batches can share a tick and arrive in any order.

The header is copied. Record views borrow the unchanged datagram. The typed decoder validates the [public combatant record bodies](#public-combatant-record-bodies) in the whole batch before the receiver applies any record. Current connection and world checks, body lifetime, tick ordering and applied-event gating belong to the receiver. [Batch codec](../../src/xrNetServer/Multiplayer/SnapshotBatch.h), [framing tests](../../src/test/multiplayer/portable/SnapshotBatchTests.cpp).

#### Outbound public combatant captures

The version-one owner gives the queue all relevant public combatants for one receiver and server tick. The queue checks the complete capture and packs up to 32 unique combatants into at most three 1,100-byte datagrams. Packets share the capture ID, tick, clock anchor and required event watermark. Each uses its own envelope message sequence. On success the caller advances its sequence base by the number of encoded datagrams. Individual packets can arrive and apply independently.

The newest valid capture replaces any unsent remainder. An empty capture discards the remainder and advances the latest tick without sending a packet. Durable lifecycle messages carry despawns. Equal, stale and ambiguous ticks cannot refresh the pending age. The session owner retires the queue when its peer/world identity or live readiness is invalidated.

Each submission attempts one packet. A transient GNS refusal consumes that packet and lets the owner consider the remaining packets on later turns. The unsent remainder expires at the configured monotonic age measured from staging. The queue exposes pending packet count, wire bytes and age. It requests an unreliable no-delay send and distinguishes refusal from native acceptance and connection failure. Native buffering after acceptance requires a pacing budget in the integrated transport. [Queue](../../src/xrNetServer/Multiplayer/SnapshotSendQueue.h), [GNS adapter](../../src/xrNetServer/Multiplayer/GnsSnapshotSend.h).

### Initial-state baseline transfer

The server sends `BaselineBegin`, `BaselineChunk`, and `BaselineEnd` on the reliable control/state stream. The envelope carries the message sequence and the receiving peer's bound session, world epoch, and connection epoch. World epoch zero is valid.

`BaselineBegin` has a 20-byte payload in this order.

| Payload field | Encoding |
| --- | --- |
| Baseline ID | `u32` |
| Capture tick | `u32` |
| State-event watermark | `u32` |
| Total baseline-data byte count | `u32` |
| Record count | `u16` |
| Chunk count | `u16` |

A declaration allows at most 4 MiB, 8,192 records, and 4,096 chunks. The declared chunk count equals the total byte count divided by 1,024 and rounded up. An empty baseline has zero bytes, records, and chunks. A nonempty baseline declares at least one record. The transfer header caps the record count at the declared byte count. Record framing is checked after assembly.

`BaselineChunk` carries a `u32` baseline ID, a zero-based `u16` chunk index from 0 through 4,095, and 1–1,024 opaque baseline-data bytes. The envelope length delimits the data. In a declared transfer, the encoded record stream is split at successive 1,024-byte offsets. The final chunk carries the remaining bytes. `BaselineEnd` carries the `u32` baseline ID alone.

Decoded chunk data borrows the datagram. The transfer owner checks the identity and baseline ID against the active declaration, checks each chunk against its declared offset and length, and verifies completeness and the encoded records before publishing a baseline. Aggregate staging limits and readiness remain with the transfer and world owners. [Codec](../../src/xrNetServer/Multiplayer/BaselineTransfer.h), [independent datagrams](../../src/test/multiplayer/portable/BaselineTransferTests.cpp).

### Pending admission decision

The connection owner starts one pending tracker per connection with an absolute monotonic deadline, initially 10 seconds after connect. A hello received at or after the deadline expires before validation. The same deadline applies while waiting for a slot. Expiry reports a reason once when the peer enters `Closing`.

The tracker validates the complete unbound hello and compares build, content, and rules IDs in that order against server requirements. A protected server requires nonempty credentials and calls its verifier with the borrowed bytes. An open server skips credential verification. The first verified hello produces a slot request carrying the message sequence and borrowed display name. The connection owner supplies the assigned identity for the welcome and the roster supplies the reservation.

The peer remains `AwaitingSlot` if another hello arrives before expiry. Such a hello yields `InvalidState` for diagnostics and leaves the original request intact. The initial terminal failure tells the caller to send one rejection. Calls after `Closing` yield `InvalidState` without another rejection. [Portable tracker](../../src/xrNetServer/Multiplayer/PendingAdmission.h), [deterministic tests](../../src/test/multiplayer/portable/PendingAdmissionTests.cpp).

### Pending admission capacity

The connection owner starts each peer through one configured pending limit before processing a hello or consuming match resources. `TryBegin` returns `ServerBusy` at capacity without creating a tracker. Zero capacity rejects every attempt. Each `AwaitingHello` or `AwaitingSlot` tracker consumes one place, and neither a repeated hello nor a premature completion releases it.

The initial admission place is returned on `Admitted`, `Closing`, or destruction while still pending. Moving a tracker transfers its place. The connection owner deduplicates starts per connection, serializes tracker operations and keeps the limit alive until all pending trackers release it. Poll deadlines even when no packets arrive and process expiry before new starts on the same turn. The transport owner closes rejected peers promptly. The pending limit and the roster capacity govern different stages of admission. [Tracker and limit](../../src/xrNetServer/Multiplayer/PendingAdmission.h), [capacity tests](../../src/test/multiplayer/portable/PendingAdmissionLimitTests.cpp).

### Admission completion

Complete the handoff on one serialized owner turn while the peer is `AwaitingSlot`. The owner supplies current welcome settings and the server-assigned session, round and connection identities. The tracker checks the initial deadline first, then verifies that the welcome carries the content and rules IDs from the accepted hello and can be encoded.

The roster can provisionally hold one eligible slot. The reliable welcome queue copies the encoded packet while the reservation is held. Successful queueing commits the reservation and moves the peer to `Admitted`. The roster owns that committed reservation through loading and releases it on disconnect or later failure. A failed queue operation publishes no packet and cancels the hold. Adapter calls stay on the owning turn without reentering admission. An eventual transport failure after successful queueing follows normal disconnect cleanup.

No eligible slot yields `ServerFull`; expiry yields `AdmissionTimedOut` once. These reasons can be sent to the peer. Invalid welcome configuration and local queue failure close the connection with a local diagnostic. Repeated completion after a terminal transition returns `InvalidState` without another reservation or response. After `Admitted`, the initial deadline ends and loading/readiness have their own lifecycle. [Portable handoff tests](../../src/test/multiplayer/portable/AdmissionCompletionTests.cpp), [authoritative roster policy](multiplayer-bot-ai-design.md#population-joining-and-lifecycle).

### Input and discrete actions

Each command includes `u32 CommandSequence`, `u32 ClientTick`, signed `i8` forward/right input, `u16` yaw, `i16` pitch and a `u16` held-button mask. Declare direction normalization and button meanings in MP-030's golden fixtures. Yaw covers a full turn; pitch is bounded to the shared controller's allowed range. A client timestamp is evidence for bounded scheduling, not permission to choose an arbitrary execution/rewind time.

Movement is continuous; fire presses, reload, equip and interaction requests need stable action IDs. Repeat pending action records until acknowledged or expired, independently of the four-command history. Each record carries action ID, originating command sequence, kind and a bounded parameter. The server returns the same accepted/rejected/expired result for duplicates, and never reassigns an old action to a new life.

The weapon state machine owns semi-auto versus held automatic fire and cadence. Repeated press records plus a held button cannot produce two first shots. A request too old for the execution policy expires explicitly; retry is not unlimited retroactive firing. Final timeout/window values are measured configuration at MP-032/053, not implicit network-library defaults.

### Human and native-NPC state

Use distinct typed human and stalker records under a common combatant envelope; do not serialize either class's memory layout or call the actor importer on a stalker. A first record contains:

- Participant/entity/life reference and body/profile identity.
- Authoritative position/velocity as three checked `f32` values each for the initial prototype; body yaw and aim yaw/pitch encoded as bounded angles.
- Stance/movement/public-state flags, equipped weapon/profile, public weapon-action state and its start tick.
- Spawn-protection state/end tick as public match information.
- For NPCs: semantic locomotion/action animation state, phase/start tick, root-motion mode and traversal/climb state with a validated link reference when needed.

Own-player state additionally contains acknowledged simulated command, action results and complete restore state from MP-041, including own health/armor/ammo/timers. Do not send private inventory/health as an unconditional field of every remote record unless the rules explicitly make it public.

Spawn/baseline metadata maps stable body/animation profile IDs to resources. Match gameplay-relevant collision, rules, weapon parameters, hit-region definitions, skeleton/animation dependencies and enabled scripts through the content manifest. Cosmetic variation must not silently change exposed hit regions or muzzle/origin rules. NPC semantic animation IDs map to existing channels/motions; raw process pointers and unvalidated motion-table indices are not a portable protocol.

Clients interpolate the root and visual state. Client animation/root motion cannot move authoritative collision. The server evaluates the actual NPC pose required for muzzle/vision/hit volumes under the dedicated renderer; sending animation state does not remove that cost. If pose cannot be reproduced closely enough, fix the mapping or add a bounded corrective representation based on the measured mismatch.

Live batches may update complete entities independently rather than require every batch of a tick to arrive. Gate them on the applied state-event watermark; retain tombstones and reject old-life/old-tick records so a late snapshot cannot resurrect a despawned body. Unknown references can wait only in a bounded buffer or request repair; do not create an authoritative participant from an unsolicited snapshot fragment.

#### Public combatant record bodies

The `0x01` human record has exactly 63 body bytes. The `0x02` stalker record starts with those shared fields and has exactly 89 body bytes including its extension.

| Shared field in order | Encoding |
| --- | --- |
| Participant and body | Nonzero `u32` participant ID, `u16` slot from 0 through 65,534, nonzero `u32` generation and nonzero `u32` life ID |
| Body profile | Nonzero `u32` content profile ID |
| Position and velocity | Three finite `f32` coordinates for each vector, in X, Y, Z order |
| Body yaw and aim yaw | Two `u16` values, 65,536 units per turn |
| Aim pitch | `s16` in −16,384 through 16,384 inclusive, where the endpoints represent −90° and 90° |
| Stance | `u8` with 0 for standing and 1 for crouched |
| Public flags | `u8` with bits 0 for airborne, 1 for sprinting and 2 for spawn protection. Other bits are reserved. |
| Equipped weapon profile | `u32` content profile ID, with 0 for unarmed |
| Public weapon action | `u8` with 0 for idle, 1 for firing, 2 for reloading, 3 for equipping and 4 for holstering. Unarmed bodies use idle. |
| Weapon action start tick | `u32` |
| Spawn protection end tick | `u32`. The public flag records whether protection is active, including when the end tick wraps to zero. |

| Stalker extension in order | Encoding |
| --- | --- |
| Animation profile | Nonzero `u32` content profile ID |
| Locomotion and action channels | Each has a `u16` semantic ID, a `u16` normalized phase in units of 1/65,535, and a `u32` start tick. A zero semantic ID leaves that channel inactive and requires its phase and tick to be zero. |
| Root-motion mode | `u8` with 0 for simulation and 1 for animation-driven. Animation-driven motion requires an active channel. Simulation can use the profile's default pose with both channels inactive. |
| Traversal state | `u8` with 0 for none, 1 for climbing up and 2 for climbing down |
| Traversal link | `u32`, nonzero for climbing and zero otherwise |

These encodings are portable protocol values. The game maps native engine states to them and resolves body, weapon, animation, and traversal IDs against the loaded content and current world before applying state. The wire codec validates field widths, finite transforms, reserved values, and legal field combinations. The typed batch decoder owns its result and rejects a repeated participant or object slot anywhere in the same datagram. [Typed codec](../../src/xrNetServer/Multiplayer/CombatantSnapshot.h), [golden and batch tests](../../src/test/multiplayer/portable/CombatantSnapshotTests.cpp).

## 4. Coherent active-match joining

Peer states: `Connected` -> `Admitted` -> `Loading` -> `Synchronizing` -> `ReadySpectator` -> `Active`. Any stage can close cleanly. Transport connection success is not gameplay readiness.

1. Validate admission and client-consumed content. Reserve the human combatant slot using the atomic bot-fill/replacement policy; a loading reservation counts once toward capacity. It has a bounded loading deadline and is released on failure.
2. Load the configured arena while the server and other participants continue. Do not spawn an uncontrolled living body during loading.
3. At an authoritative boundary, capture the initial public world plus this participant's permitted private state at tick B and state-event watermark E. Stage checked reliable chunks with a baseline ID and byte/record totals.
4. Retain subsequent relevant events and self-contained runtime state under bounded per-peer limits. After complete baseline validation, apply events in order and a declared barrier state at tick C covering its required watermark. Do not combine a baseline from one epoch with events from another.
5. The client acknowledges that exact baseline/barrier in the current session/epoch. The server sends activation/ready state and chooses a safe spawn under the shared rules. A brief spectator view is allowed once the world is coherent.
6. If catch-up grows beyond the configured cap, rebase at a newer boundary. Limit repeated rebases and disconnect with a useful reason if the peer cannot synchronize; never allow unbounded queues. Wrong/old acknowledgements cannot activate a body.

The client must not wait for the continually moving server to become perfectly current. The barrier is a specific finite state; subsequent permitted live updates continue normally. If a round changes during loading/synchronization, invalidate the old baseline/barrier and synchronize the new epoch. Start with fixed conservative caps in MP-022/037 fixtures, then publish their measured values in configuration/runbooks.

Existing `M_SV_CONFIG_NEW_CLIENT`, `M_SV_CONFIG_GAME`, `M_SV_CONFIG_FINISHED` and `M_CLIENTREADY` show integration points. Existing map-change receive handling can reconnect; seamless map rotation is outside this initial contract. [Server config/spawn flow](../../src/xrGame/xrServer.cpp), [client receive](../../src/xrGame/Level_network_messages.cpp), [client startup](../../src/xrGame/Level_start.cpp).

### Client-side baseline staging

The serialized connection owner supplies an exclusive buffer from its staging budget and binds the receiver to the current session, world, and connection. The receiver checks the declaration against the wire limits and available storage before tracking the transfer. It uses a fixed receipt bitmap and copies accepted chunks to their canonical offsets.

A matching duplicate declaration or chunk is idempotent while receiving. A conflicting duplicate fails and preserves the accepted data. A matching end received before all chunks fails and can be retried after the missing chunks arrive. Once all chunks arrive, a matching end yields one read-only view of exactly the declared bytes for record validation. The view borrows the reserved buffer while its receiver and baseline remain current.

A same-map round advance clears the declaration and receipt bitmap. The next world can reuse numeric baseline IDs, while frames for the old world fail the identity check. Record validation, event catch-up, and world publication follow byte assembly. [Stager](../../src/xrNetServer/Multiplayer/BaselineStaging.h), [staging tests](../../src/test/multiplayer/portable/BaselineStagingTests.cpp).

### Same-round baseline replacement

The connection owner selects a replacement for an active or completed receiver. The declaration must match the current session, world, and connection, have a newer baseline ID and capture tick, and carry the same or a newer event watermark. Compare all three counters in their own 32-bit serial spaces. A forward distance of 1 through 2³¹ − 1 is newer, including across wrap. Declarations that fail freshness, manifest, or storage checks preserve the existing transfer.

An accepted replacement clears receipt and completion state and keeps the existing buffer reservation. Both received chunks and the end frame must match the replacement ID. Complete use of borrowed baseline and record views before accepting a replacement. The server owner controls when to rebase, the retry window, and readiness for the selected barrier. [Receiver](../../src/xrNetServer/Multiplayer/BaselineStaging.h), [replacement tests](../../src/test/multiplayer/portable/BaselineRebaseTests.cpp).

### Aggregate baseline staging budget

A shared `BaselineStagingBudget` limits the sum of byte reservations and the number of outstanding buffers. Each reservation covers at most 4 MiB. A zero-byte baseline still uses one buffer place. The staging owner serializes reservation and release across receivers and keeps the budget alive through all reservations.

The owner acquires the reservation before taking backing storage. The receiver takes the move-only reservation and requires it to cover the full supplied span. A completed receiver keeps its reservation while record validation reads the buffer. A same-map world advance retains the reservation for reuse. Return the backing storage on the same owning turn that destroys the receiver. An abandoned reservation returns both charges at scope exit.

Owners supply the limits. Arena measurements and live allocation, queueing, and cleanup scenarios determine the production values. [Budget](../../src/xrNetServer/Multiplayer/BaselineStagingBudget.h), [capacity tests](../../src/test/multiplayer/portable/BaselineStagingBudgetTests.cpp).

### Baseline record validation

The assembled byte range contains exactly the declared record count. Each record has a `u8` kind, a little-endian `u16` payload byte count, and exactly that many payload bytes. The payload can be empty when the schema for that kind permits it. Chunk boundaries can fall anywhere in the stream, including inside a record header.

Validate the staged identity and baseline ID against the current connection and selected baseline. Check the manifest and exact byte count, then read every record using the supplied schema. The schema recognizes supported kinds and checks their payloads without applying world changes. Missing records, truncated headers or payloads, schema failures, and trailing data all prevent returning a cursor.

Successful validation returns an ordered cursor over the reserved immutable buffer. Consume it while the staged view is valid. Concrete record layouts, cross-record consistency checks, and committing world state belong to the import path. [Validator](../../src/xrNetServer/Multiplayer/BaselineRecords.h), [record tests](../../src/test/multiplayer/portable/BaselineRecordsTests.cpp).

### Public combatant baseline records

The public combatant capture uses the human and stalker [record body formats](#public-combatant-record-bodies) and their `0x01` and `0x02` kinds inside the reliable baseline record stream. Each capture holds zero through 32 combatants in stream order. Only one record can use a participant ID or engine object slot. The largest capture is 2,944 bytes, split into three chunks by the existing baseline transfer rules.

The encoder validates all bodies and cross-record identities before writing caller storage. It returns the exact manifest, including the chunk count. The decoder checks the admitted receiver first, then the selected baseline ID, capture tick, and event watermark. It validates the whole framed stream and the typed body values before returning owned records. The staged buffer can be released after decoding. The world owner resolves the content IDs and commits the imported world before confirming the baseline to the catch-up tracker. [Combatant baseline codec](../../src/xrNetServer/Multiplayer/CombatantBaseline.h), [typed and staged transfer tests](../../src/test/multiplayer/portable/CombatantSnapshotTests.cpp).

### Server-side join readiness

The connection owner starts `Loading` after queuing the accepted welcome. Loading expires after 120 seconds, and a current-session/world/connection confirmation starts the 30-second synchronization window. Use monotonic wall time. An event at the exact deadline expires the peer before applying that event. The owner can poll idle peers and receives the stage-specific failure once for connection and roster cleanup.

During `Synchronizing`, the owner selects an initial baseline for the current identity, then declares its staged barrier. The barrier must name the selected baseline and cover its capture tick and event watermark. The peer acknowledgement matches session, world, connection, baseline ID, barrier ID, snapshot tick, and event watermark exactly. A successful match moves the peer to `ReadySpectator` once. The world owner handles activation and spawning after that transition.

A same-map round advance must increase the world epoch without wrapping. It clears the selected baseline and the old barrier and preserves the outstanding loading or synchronization deadline. A loading confirmation must match the new epoch. A peer already waiting as a ready spectator returns to synchronization with a fresh synchronization deadline. Peer messages cannot advance the epoch. The portable tracker operates on checked values. The client confirmation layouts are defined in the wire contract. Snapshot capture, catch-up delivery, and live integration follow their snapshot work items. [Tracker](../../src/xrNetServer/Multiplayer/PeerJoin.h), [join tests](../../src/test/multiplayer/portable/PeerJoinTests.cpp).

### Server-side baseline rebase

During synchronization the server owner can select a replacement capture before or after declaring a barrier. The server and receiver use the same baseline ID, capture tick, and event watermark freshness checks. A replacement capture must cover the tick and watermark of any declared barrier because the acknowledgement may still be in flight. A successful rebase clears the earlier barrier and preserves the synchronization deadline. Declare a fresh barrier for the replacement before accepting readiness.

Only accepted replacements spend retry places. The tracker keeps the last three accepted attempt times across same-map round changes. A place opens at exactly 60 seconds after the corresponding attempt. A fourth valid attempt with all three places still charged moves the peer to `Closing` and reports `RebaseLimitExceeded` once. The synchronization deadline takes precedence at equality. The server owner uses the terminal reason to close the connection and return its resources. [Join tracker](../../src/xrNetServer/Multiplayer/PeerJoin.h), [server rebase tests](../../src/test/multiplayer/portable/PeerRebaseTests.cpp).

### Server catch-up event retention

The server owner binds a queue to one receiver identity and selected baseline. The queue starts from the capture watermark and accepts consecutive `u32` sequences in the receiver's filtered state-event stream, including across wrap. The owner supplies nonempty serialized event bytes within the 1,100-byte message cap. The queue copies them into exclusive caller-owned storage. Separate caller-owned length slots bound occupancy to at most 4,096 events. Both buffers can reuse space from retired events.

Peek copies the oldest event into the caller's send buffer. The owner commits its sequence on the same turn the reliable transport queue accepts that event. Only then does the submitted watermark advance and the storage return to the catch-up queue. Failed submissions can peek and retry the same event. Short output buffers leave both the queue and the output intact.

A valid event that exceeds the count or byte capacity makes the queue report `RebaseRequired`. The owner selects a replacement capture covering the latest observed event sequence before replacing the queue. The old queue stops sending and retiring events while continuing to track consecutive observations. The replacement starts from its new capture watermark in fresh logical state. Live capture, event codecs, and transport scheduling follow their snapshot work items. [Queue](../../src/xrNetServer/Multiplayer/CatchUpEventQueue.h), [retention tests](../../src/test/multiplayer/portable/CatchUpEventQueueTests.cpp).

### Client catch-up and barrier application

The client owner binds a tracker to the selected baseline and receiver identity. After checked baseline import succeeds, confirm the exact capture to initialize the applied event watermark. Each next event must match the identity and baseline and carry the consecutive sequence in the receiver's stream. The world adapter applies each checked event atomically and reports success before the watermark advances. The transport owner retains events awaiting import or application.

A barrier may arrive before import. It must cover the selected capture tick and the current event watermark. The client applies events through the exact declared watermark and defers later events until the staged barrier snapshot is applied. Successful snapshot application yields the exact ready acknowledgement. A transport retry can reuse that acknowledgement while keeping the barrier applied once. Later events can proceed after snapshot application.

For a same-round rebase, the replacement must cover the applied event watermark and any applied barrier tick. A successful rebase resets import confirmation and the previous barrier. The owner replaces the tracker on an accepted world change. Concrete world application and transport integration remain with the engine owners. [Client tracker](../../src/xrNetServer/Multiplayer/ClientCatchUp.h), [application tests](../../src/test/multiplayer/portable/ClientCatchUpTests.cpp).

### Client public combatant barrier staging

The client owner constructs a stager for the declared barrier after the catch-up tracker accepts it. The stager uses the same exclusive buffer reservation and canonical chunk assembly as baseline staging. All three receiver identity fields and all four barrier fields must match before any bytes are recorded. An identical duplicate is idempotent during assembly. Conflicting totals or fragments leave accepted bytes intact. The owner replaces the stager when its trusted connection, world, or barrier selection changes.

Completion requires every canonical chunk or the explicit empty chunk. It validates the complete public roster and returns an owned snapshot with the exact receiver and barrier. Invalid typed records prevent publication. The completed snapshot survives reuse of the transfer buffer. The owner passes it to [barrier application](#public-combatant-barrier-application) at the declared watermark. [Stager](../../src/xrNetServer/Multiplayer/CombatantBarrier.h), [staging tests](../../src/test/multiplayer/portable/CombatantBarrierTests.cpp).

### Public combatant barrier application

The client owner passes the owned staged roster to the barrier importer on the serialized world turn. The selected catch-up tracker and replica world must agree with the snapshot. The tracker requires the exact declaration, completed baseline import, and exact applied event watermark before invoking world application.

The importer uses the same atomic roster replacement as baseline import. It rejects stale captures, retired bodies, and conflicting identities before invoking the world callback. Success publishes the complete public roster and seeds active bodies and the full-capture tick at the barrier tick. A failed callback leaves the earlier roster and replica history intact and keeps the barrier retryable.

The tracker marks the barrier applied and returns the exact readiness acknowledgement after the roster commits. Later events and live ticks can then proceed. An acknowledgement retry supplies the current outgoing message sequence and reuses the applied barrier without reapplying its roster. Rebase invalidates the previous barrier, including when the replacement declaration reuses its numeric ID. [Importer](../../src/xrGame/Multiplayer/CombatantBarrierApply.h), [portable join handoff](../../src/test/multiplayer/portable/CombatantBarrierApplyTests.cpp).

### Public combatant baseline import

The client owner imports the complete active public combatant roster for the selected capture. An empty capture represents an empty roster. The import requires the selected connection and boundary and the matching replica world. The replica registry preflights all bodies before world application. It rejects a retired generation or conflicting identity and requires a changed body for an active participant to advance in generation and life. A capture must cover the last successful full-capture tick and each current body's applied tick in serial order. The full-capture tick stays available across a same-world reconnect even when the roster is empty.

The world callback resolves content and replaces the public roster atomically on the serialized owner turn. Failure preserves the old world, active registry, and retirement history and leaves the selected capture unconfirmed. On success the registry retires omitted and replaced lives and seeds every retained or new life at the capture tick. The catch-up tracker then confirms the applied event watermark exactly once. Duplicate capture delivery preserves later events and body updates. An accepted world advance clears the registry history and full-capture tick. [Importer](../../src/xrGame/Multiplayer/CombatantBaselineImport.h), [transaction tests](../../src/test/multiplayer/portable/CombatantBaselineImportTests.cpp).

### Client combatant replica identity

The client owner registers current bodies from trusted baseline or lifecycle data. Each active body occupies one of 32 places. A participant and an engine object slot can each have at most one active body. Repeating an exact active registration is idempotent.

The registry classifies snapshot references in the current session and world before the owner applies state. An exact active participant and body is current. A lower generation than a known active body in the same slot is stale. A different participant or life for the active slot and generation is a conflict. An unseen generation is unknown and can await lifecycle data. Invalid identities and references to another world fail classification.

An exact despawn removes the active body and records its generation for that slot. Any reference at or below that generation stays stale for the rest of the world, including a replayed registration. A despawn for a stale, unknown or conflicting reference leaves the state intact. The owner supplies 65,535 generation entries, about 256 KiB, so history covers the entire valid engine slot range. A verified world advance clears the history and active bodies. [Replica registry](../../src/xrGame/Multiplayer/CombatantReplicas.h), [tests](../../src/test/multiplayer/portable/CombatantReplicasTests.cpp).

### Live replica snapshot application

After applying the finite synchronization barrier, the client owner supplies the current admitted receiver identity and the locally applied event watermark. A live snapshot must match the receiver, the registry world, and an exact active body. Trusted registration can seed the body's last applied tick from imported state. Duplicate registration preserves the existing tick.

Each complete update must be strictly newer than the body's last applied tick. Independent updates for different bodies can share a tick. Tick and watermark comparisons use 32-bit serial arithmetic. A duplicate or half-range tick is rejected. An absent or behind event frontier leaves the update pending, and a half-range watermark is rejected as ambiguous.

The owner serializes application with lifecycle changes. The callback applies the body's complete update atomically, reports failure without changing it, and does not reenter the registry. Only a successful application records the tick. Despawn and accepted world advance clear the affected tick history. Payload decoding, buffering, and application of the synchronization barrier stay with their owners. [Replica registry](../../src/xrGame/Multiplayer/CombatantReplicas.h), [snapshot tests](../../src/test/multiplayer/portable/ReplicaSnapshotTests.cpp).

### Typed live snapshot dispatch

The dispatcher accepts a fully decoded combatant batch. Its receiver identity must match the catch-up tracker, and the replica registry must belong to the same session and world. Baseline import and barrier declaration can finish before live dispatch opens. Dispatch opens after the barrier applies successfully and rejects any batch whose tick does not strictly follow that barrier in serial order. This protects state imported at the barrier even if a replica's own tick still reflects the initial capture. Rebase closes live dispatch until the replacement barrier applies.

Records are checked independently against the live event watermark and per-body registry. A typed world callback receives the decoded public state and batch header for an eligible human or stalker. Each outcome refers to the record in the same position in the batch. Unknown bodies, unapplied events, and failed applications are visible to the owner for deferral or repair. The next successful retry skips any body that already applied that tick. A late batch can still carry useful state for another body. The owner resolves content before updating engine entities and can use the bounded queue for deferrals. [Dispatcher](../../src/xrGame/Multiplayer/ReplicaSnapshotDispatch.h), [end-to-end portable tests](../../src/test/multiplayer/portable/ReplicaSnapshotDispatchTests.cpp).

### Bounded live snapshot retry

The client owner dispatches a decoded batch immediately and retains records waiting for an applied event or a trusted registration. The queue owns up to eight batches and selects only unresolved records, at most 256 references across the retained batches. A successfully applied record retires obsolete held updates for that body. An unknown body with an ambiguous event watermark consumes no queue capacity. A failed world callback reports repair and leaves its tick unapplied.

For the same participant, body, and required event watermark, retain the newest deferred tick and keep the first copy at an equal tick. Different required watermarks can make an older update useful sooner. When the owner retries, evaluate the newest batch tick first by distance from the applied barrier. For batches sharing a tick, try those with applied event requirements first. Recheck the registry and applied event watermark for each selected record. The per-body gate then drops already-applied or retired records.

When a new batch cannot fit, preserve the existing held records and report repair. Accept a replacement at capacity if it frees a whole batch slot. Records applied immediately still take effect even if another record in the same batch exceeds the queue. A trusted change of connection, world or applied barrier clears held work on the next queue operation. Foreign packets cannot change that scope. The owner schedules retries after event and lifecycle application and handles repair or rebase requests. [Queue](../../src/xrGame/Multiplayer/ReplicaSnapshotQueue.h), [portable tests](../../src/test/multiplayer/portable/ReplicaSnapshotDispatchTests.cpp).

## 5. Round and spawn behavior

Proposed phase defaults: load -> wait for the first active human/bot -> running -> results/intermission -> reset -> next round, waiting again if no combatants remain. Bots can start/run a match without humans. Results/intermission starts at ten seconds, configurable; this duration is an engineering default rather than a user-selected value.

Running ends when twenty server minutes expire or a participant reaches fifty frags. Freeze the result at one authoritative boundary; resolve any same-tick tie through the documented MP-053/061 policy. Initial tie default is a shared result, not connection order or an unrequested overtime system. Timers use server gameplay time, not client clocks.

At reset, advance `WorldEpoch`, stop old actions/jobs, clear active projectiles and old prediction/history/event baselines, reset scores/inventory/pickups, and publish new lives/state. Preserve admitted connections and static nav readers when their map generation is unchanged. Administrative round restart follows the same reset path. Changing maps requires a new server process/session.

Shared configurable spawn defaults are five seconds after authoritative death and two seconds of protection. Engineering interpretation: an accepted firing action clears protection before that shot participates in combat; a rejected/dry-fire request does not. Pickup collection alone does not cancel protection under this initial rule. Replicate a visible protection cue so humans and bot perception have the same permitted information. Test expiry and firing on the same tick, using explicit ordering.

Admit all eligible fire requests for a tick first, then clear every accepted shooter's protection in one pass before evaluating that tick's affected collisions. This prevents an iteration-order advantage. Retain protection start/end times with the entity life; cancellation shortens the end to the authoritative execution tick, not an arbitrary client timestamp. Historical hitscan/impact queries evaluate that life protection interval at the approved target-query/impact time, while present-life and current health/death checks still guard damage application. Do not rewind live health or grant a new life damage from the previous one.

Choose spawn locations through a bounded shared allocator, considering blocked volumes, occupied landings and recent danger. That allocator may inspect world state for spawn validity, but must not transfer hidden enemy positions into the new NPC's memory. Configure basic equipment and real map weapon/ammo pickups; no buying menu is required. Exact items, pickup cooldowns and hit-region multipliers are MP-004/061 content decisions.

## 6. Native-NPC integration checklist

MP-005 identifies the problems; MP-033/035/038/070 resolve them for production:

| Path | Required adapter/check |
| --- | --- |
| Roster and ownership | A stalker has a participant/player-state representation without a socket. Internal simulation ownership is separate from the visible killer/victim identity |
| Respawn/loadout | Existing DM respawn uses `ClientID`, client data and an actor server entity. Add a participant/body factory path for NPCs rather than casting a stalker into that actor path |
| Damage/armor/weapon handling | Use the same MP health, armor, damage, spread/cadence and material policies. Audit legacy NPC rank/stance dispersion and bone protection; retain intended aim skill without multiplying damage/toughness |
| Frags/deaths/results | Resolve shooter/victim participant from generation/life-safe entities, including NPC-owned projectiles and disconnected owners |
| Relations/group memory | Configure independent FFA hostility; prevent accidental ally relations, shared hidden enemy knowledge or squad target allocation among opponents |
| Spectator/HUD/kill feed | Actor-only filters cannot silently exclude NPCs. Represent their name/profile, weapon, score, death and protection state |
| Poses/animation/climbing | Dedicated CPU pose and NPC animation/traversal state feed real hit volumes and remote presentation; ordinary non-SP stalker export/import is not assumed sufficient |
| Scripts/global actor assumptions | Trace enabled callbacks and `Actor()`/`cast_actor()` assumptions. Use explicit participant/body context in MP where required and keep SP behavior isolated |

The actor/client coupling is directly visible in [DM respawn](../../src/xrGame/game_sv_deathmatch.cpp); mode-specific synchronization is visible in [stalker networking](../../src/xrGame/ai/stalker/ai_stalker_net.cpp) and [server spawn sync](../../src/xrGame/xrServer.cpp). This checklist does not label every actor-specific path a bug: many should remain human-only, while shared rules need adapters.

## 7. One authoritative tick owner and phase order

Implement one MP driver in the authoritative `CLevel` world context hosted by the dedicated process. `xrServer` routes/owns server entities but is not itself the full NPC/physics simulator. Proposed execution owner: the GameThread world phase, coordinated with frame barriers. Transport workers only queue data; the primary thread collects client input/presentation and does not concurrently mutate the authoritative MP world.

This requires controlled integration, not an extra callback alongside current stepping. `CPHWorld::OnFrame` already advances physics via a frame registration, `CLevel::OnFrame` advances bullets, and the GameThread scheduler runs NPC scheduled work. Retire/reroute the affected MP callbacks when the driver takes ownership; preserve SP and unowned legacy paths. [Physics world](../../src/xrPhysics/PHWorld.cpp), [level frame](../../src/xrGame/Level.cpp), [GameThread](../../src/xrEngine/EngineThreading.cpp), [frame registration](../../src/xrEngine/device.cpp).

Logical tick contract at the proposed 50/150 Hz baseline:

| Phase | Work and state boundary |
| --- | --- |
| 1. Boundary/lifecycle | Drain bounded transport/control events; validate epochs/admission; apply scheduled round/config/roster/spawn changes. A reset occurs between ticks |
| 2. Evidence and decisions | Dispatch previous committed gameplay stimuli; capture coherent observations and run due bounded NPC planning. Select allowed human command state and NPC intent |
| 3. Prepare execution | Update allowed look/stance/movement and weapon intent/timers under one owner. NPC object handling queues fire intent; it cannot independently emit a second authoritative shot |
| 4. Movement/physics | Advance the owned world through exactly three 1/150-second substeps, including NPC bodies and supported contacts. Preserve required physics pre/post-step hooks |
| 5. Pose/history | Evaluate gameplay-relevant human/NPC poses and record the end-of-tick proxies/blockers, retaining the previous sample for swept queries |
| 6. Weapons/projectiles | Collect all admitted fire requests, clear their shooters' protection consistently, then derive validated query times and evaluate fire/projectile impacts against the selected history/rules. New launches do not receive an extra full tick of flight; query-time protection uses the life interval described above |
| 7. Resolve/commit | Apply deduplicated damage/death/score and inventory outcomes using the chosen same-tick trade/tie rules; commit authoritative stimuli and durable events once |
| 8. Publish | Build snapshots/owner acknowledgements after commit at the configured cadence; publish read-only diagnostics, then retire completed work |

Tick labels refer to a consistent boundary convention; MP-031 records it in clock fixtures. A human command's accepted fire/view times may select historical queries; an NPC fire request uses its admitted execution tick without fabricated latency. Preserve an explicit policy for same-tick deaths and ties through MP-053/061; packet arrival order cannot be that policy.

MP-031 must demonstrate one physics owner, one authoritative NPC execution path, one bullet/weapon advance and one event commit, including when render FPS changes or multiple ticks run in one frame. Add the smallest order-sensitive fixtures: movement changes a firing origin, a door blocks a shot, firing clears protection, and reset invalidates queued actions. Never infer correct order from visually smooth interpolation.

Initial overload policy: at most four catch-up ticks per outer update and at most eight ticks of accumulated simulation debt. Excess wall time is recorded as an overload pause; do not fabricate skipped physics ticks or give an actor extra movement credit. Gameplay timers advance only through simulated ticks, while network/admission/loading deadlines use monotonic wall time. Publish current tick/clock anchors so clients do not assume gameplay time advanced at wall-clock speed through a stall. Persistent slowdown/overload fails the performance gate; this policy bounds failure behavior rather than making it acceptable normal play. Verify it through real clock fixtures before MP-031 passes. It is not a promise of cross-platform ODE determinism.

## 8. Controls and configuration contract

MP-039 first inventories existing commands, then documents actual names. Proposed setting names are data contracts only until implemented.

| Setting/capability | Default / application point |
| --- | --- |
| Arena, bind/port, protocol/content profile | Startup; changing map requires restart |
| Round time / frag limit | 20 minutes / 50; stage edits for next round |
| Basic loadout / pickup roster and timers | Versioned arena/rules profile; stage edits for next round, restart if content identity changes |
| Respawn / protection duration | 5 seconds / 2 seconds; stage edits for next round |
| Results duration | Proposed 10 seconds; next-round setting |
| `BotFillTarget`, `MaxBots`, bot skill distribution | Validate and apply at a tick boundary; changes affect subsequent decisions/lives under documented policy, not retroactive shots |
| Status and diagnostics | Read effective/pending settings, participants/ready state, epochs, tick/input/network/AI costs |
| Restart round / stop server | Local server-owner controls; serialize through the lifecycle boundary and clean teardown |
| Client connect / disconnect / diagnostics | Numeric IP/port plus existing admission input; display rejection/loading reasons without logging secrets |

Use local configuration/console ownership initially; no new remote administration service is required. Invalid edits must not partially alter effective state. Reload/round reset is not a way to bypass content compatibility or capacity checks. Spawn, pickups and bot-fill changes remain authoritative and replay-safe.

## 9. Performance and completion evidence

The early wire representation favors diagnosable state over aggressive compression. For illustration, 32 combatant records averaging 64 bytes at 25 Hz already total 51,200 bytes/second, or 409.6 kbit/s, before headers/events. This is arithmetic, not measured record size or traffic. The former 100–250 kbit/s planning range is therefore an optimization goal to revisit, not a first-version acceptance cap.

Use a provisional **0.5–1.0 Mbit/s server-to-client planning allowance** for self-contained mixed human/NPC state, then measure actual batch sizes and event/pickup traffic. Thirty-two receiving clients would imply roughly 16–32 Mbit/s server egress before extra allowance. The normal 32-combatant limit still applies; 32 bots plus 32 human combatants is not an intended workload. Quantize/delta/interest-optimize only after correctness and measured bandwidth justify it.

All CPU/bandwidth values require actual Windows profiles on the supplied Ryzen 7 5700 machine, with isolated-server and cohosted-client settings identified. Keep rendering and server simulation costs separate. No such run was performed while writing these contracts.

Required evidence is in MP-005, MP-020/022/023, MP-031/035–039, MP-045/050/052/053/070 and MP-078–088. Concrete schema/golden-vector additions and timeout/buffer values must be versioned with the implementing work item; this document is not a passing test result.
