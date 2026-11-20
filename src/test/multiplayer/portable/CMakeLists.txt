cmake_minimum_required(VERSION 3.26)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MultiplayerPortableTests LANGUAGES C CXX)
    enable_testing()
    add_subdirectory("../../../xrNetServer/Multiplayer" xrNetServerMultiplayer)
endif()

add_executable(TestMultiplayerUnit
    SerialSequenceTests.cpp
    MapSyncWaitTests.cpp
    LegacyClockSyncTests.cpp
    LegacyStartupTests.cpp
    LegacyMultipacketTests.cpp
    WeaponSpreadTests.cpp
    ScriptedTransportTests.cpp
    TransportInboxTests.cpp
    ReliableSendQueueTests.cpp
    SnapshotSendQueueTests.cpp
    OutgoingTrafficTests.cpp
    BaselineSendTests.cpp
    WireEnvelopeTests.cpp
    WirePayloadTests.cpp
    ClientHelloTests.cpp
    ServerWelcomeTests.cpp
    AdmissionRejectTests.cpp
    PendingAdmissionTests.cpp
    AdmissionCompletionTests.cpp
    PendingAdmissionLimitTests.cpp
    PeerJoinTests.cpp
    ClientJoinTests.cpp
    BaselineTransferTests.cpp
    BaselineStagingTests.cpp
    BaselineRecordsTests.cpp
    BaselineStagingBudgetTests.cpp
    BaselineRebaseTests.cpp
    PeerRebaseTests.cpp
    CatchUpEventQueueTests.cpp
    ClientCatchUpTests.cpp
    StalkerPrerequisitesTests.cpp
    ArenaLadderTests.cpp
    ArenaMovementTests.cpp
    ArenaNavigationPackageTests.cpp
    DedicatedArenaNavigationTests.cpp
    CombatantRegistryTests.cpp
    ParticipantConnectionTests.cpp
    CatchUpDespawnTests.cpp
    CombatantReplicasTests.cpp
    ReplicaSnapshotTests.cpp
    SnapshotBatchTests.cpp
    CombatantSnapshotTests.cpp
    CombatantBaselineImportTests.cpp
    CombatantBarrierTests.cpp
    CombatantBarrierApplyTests.cpp
    ReplicaSnapshotDispatchTests.cpp
    ../transport/ScriptedTransport.cpp
)
target_link_libraries(TestMultiplayerUnit PRIVATE xrNetServerMultiplayer)
set_target_properties(TestMultiplayerUnit PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

add_test(NAME Multiplayer.Sequence COMMAND TestMultiplayerUnit)
add_test(NAME Multiplayer.MapSyncDeadline COMMAND TestMultiplayerUnit --map-sync)
add_test(NAME Multiplayer.LegacyClockSync COMMAND TestMultiplayerUnit --legacy-clock-sync)
add_test(NAME Multiplayer.LegacyStartup COMMAND TestMultiplayerUnit --legacy-startup)
add_test(NAME Multiplayer.LegacyMultipacket COMMAND TestMultiplayerUnit --legacy-multipacket)
add_test(NAME Multiplayer.WeaponSpread COMMAND TestMultiplayerUnit --weapon-spread)
add_test(NAME Multiplayer.ScriptedTransport COMMAND TestMultiplayerUnit --scripted-transport)
add_test(NAME Multiplayer.TransportInbox COMMAND TestMultiplayerUnit --transport-inbox)
add_test(NAME Multiplayer.TransportEventRouter COMMAND TestMultiplayerUnit --transport-event-router)
set_tests_properties(Multiplayer.TransportInbox Multiplayer.TransportEventRouter PROPERTIES TIMEOUT 10)
add_test(NAME Multiplayer.ReliableSendQueue COMMAND TestMultiplayerUnit --reliable-send-queue)
add_test(NAME Multiplayer.SnapshotSendQueue COMMAND TestMultiplayerUnit --snapshot-send-queue)
add_test(NAME Multiplayer.OutgoingTraffic COMMAND TestMultiplayerUnit --outgoing-traffic)
add_test(NAME Multiplayer.BaselineSend COMMAND TestMultiplayerUnit --baseline-send)
set_tests_properties(Multiplayer.ReliableSendQueue Multiplayer.SnapshotSendQueue Multiplayer.OutgoingTraffic Multiplayer.BaselineSend PROPERTIES LABELS multiplayer)
add_test(NAME Multiplayer.WireEnvelope COMMAND TestMultiplayerUnit --wire-envelope)
add_test(NAME Multiplayer.WirePayload COMMAND TestMultiplayerUnit --wire-payload)
add_test(NAME Multiplayer.ClientHello COMMAND TestMultiplayerUnit --client-hello)
add_test(NAME Multiplayer.ServerWelcome COMMAND TestMultiplayerUnit --server-welcome)
add_test(NAME Multiplayer.AdmissionReject COMMAND TestMultiplayerUnit --admission-reject)
add_test(NAME Multiplayer.PendingAdmission COMMAND TestMultiplayerUnit --pending-admission)
add_test(NAME Multiplayer.AdmissionCompletion COMMAND TestMultiplayerUnit --admission-completion)
add_test(NAME Multiplayer.PendingAdmissionLimit COMMAND TestMultiplayerUnit --pending-admission-limit)
add_test(NAME Multiplayer.PeerJoin COMMAND TestMultiplayerUnit --peer-join)
add_test(NAME Multiplayer.ClientJoin COMMAND TestMultiplayerUnit --client-join)
add_test(NAME Multiplayer.BaselineTransfer COMMAND TestMultiplayerUnit --baseline-transfer)
add_test(NAME Multiplayer.BaselineStaging COMMAND TestMultiplayerUnit --baseline-staging)
add_test(NAME Multiplayer.BaselineRecords COMMAND TestMultiplayerUnit --baseline-records)
add_test(NAME Multiplayer.BaselineStagingBudget COMMAND TestMultiplayerUnit --baseline-staging-budget)
add_test(NAME Multiplayer.BaselineRebase COMMAND TestMultiplayerUnit --baseline-rebase)
add_test(NAME Multiplayer.PeerRebase COMMAND TestMultiplayerUnit --peer-rebase)
add_test(NAME Multiplayer.CatchUpEventQueue COMMAND TestMultiplayerUnit --catch-up-event-queue)
add_test(NAME Multiplayer.ClientCatchUp COMMAND TestMultiplayerUnit --client-catch-up)
add_test(NAME Multiplayer.StalkerNavigation COMMAND TestMultiplayerUnit --stalker-navigation)
add_test(NAME Multiplayer.StalkerAuthority COMMAND TestMultiplayerUnit --stalker-authority)
add_test(NAME Multiplayer.W1NavigationOption COMMAND TestMultiplayerUnit --w1-navigation-option)
add_test(NAME Multiplayer.CombatantRegistry COMMAND TestMultiplayerUnit --combatant-registry)
add_test(NAME Multiplayer.ParticipantConnection COMMAND TestMultiplayerUnit --participant-connection)
add_test(NAME Multiplayer.CatchUpDespawn COMMAND TestMultiplayerUnit --catch-up-despawn)
add_test(NAME Multiplayer.CombatantReplicas COMMAND TestMultiplayerUnit --combatant-replicas)
add_test(NAME Multiplayer.ReplicaSnapshot COMMAND TestMultiplayerUnit --replica-snapshot)
add_test(NAME Multiplayer.SnapshotBatch COMMAND TestMultiplayerUnit --snapshot-batch)
add_test(NAME Multiplayer.CombatantSnapshot COMMAND TestMultiplayerUnit --combatant-snapshot)
add_test(NAME Multiplayer.ReplicaSnapshotDispatch COMMAND TestMultiplayerUnit --replica-snapshot-dispatch)
add_test(NAME Multiplayer.ReplicaSnapshotQueue COMMAND TestMultiplayerUnit --replica-snapshot-queue)
add_test(NAME Multiplayer.CombatantBaseline COMMAND TestMultiplayerUnit --combatant-baseline)
add_test(NAME Multiplayer.CombatantBaselineImport COMMAND TestMultiplayerUnit --combatant-baseline-import)
add_test(NAME Multiplayer.CombatantBarrier COMMAND TestMultiplayerUnit --combatant-barrier)
add_test(NAME Multiplayer.CombatantBarrierApply COMMAND TestMultiplayerUnit --combatant-barrier-apply)
set_tests_properties(
    Multiplayer.Sequence Multiplayer.MapSyncDeadline Multiplayer.LegacyClockSync Multiplayer.LegacyStartup Multiplayer.LegacyMultipacket Multiplayer.WeaponSpread Multiplayer.ScriptedTransport Multiplayer.TransportInbox Multiplayer.TransportEventRouter Multiplayer.WireEnvelope Multiplayer.WirePayload Multiplayer.ClientHello Multiplayer.ServerWelcome Multiplayer.AdmissionReject Multiplayer.PendingAdmission Multiplayer.AdmissionCompletion Multiplayer.PendingAdmissionLimit Multiplayer.PeerJoin Multiplayer.ClientJoin Multiplayer.BaselineTransfer Multiplayer.BaselineStaging Multiplayer.BaselineRecords Multiplayer.BaselineStagingBudget Multiplayer.BaselineRebase Multiplayer.PeerRebase Multiplayer.CatchUpEventQueue Multiplayer.ClientCatchUp Multiplayer.StalkerNavigation Multiplayer.StalkerAuthority Multiplayer.W1NavigationOption Multiplayer.CombatantRegistry Multiplayer.ParticipantConnection Multiplayer.CatchUpDespawn Multiplayer.CombatantReplicas Multiplayer.ReplicaSnapshot Multiplayer.SnapshotBatch Multiplayer.CombatantSnapshot Multiplayer.ReplicaSnapshotDispatch Multiplayer.ReplicaSnapshotQueue Multiplayer.CombatantBaseline Multiplayer.CombatantBaselineImport Multiplayer.CombatantBarrier Multiplayer.CombatantBarrierApply
    PROPERTIES LABELS multiplayer
)

option(IXRAY_MULTIPLAYER_VERIFY_TEST_FAILURE "Register a deliberately failing multiplayer harness check" OFF)
if(IXRAY_MULTIPLAYER_VERIFY_TEST_FAILURE)
    add_test(NAME Multiplayer.Sequence.DeliberateFailure COMMAND TestMultiplayerUnit --verify-failure)
endif()

add_test(NAME Multiplayer.ArenaLadders COMMAND TestMultiplayerUnit --arena-ladders)
set_tests_properties(Multiplayer.ArenaLadders PROPERTIES LABELS multiplayer)

set(ArenaCryptoRoot "${CMAKE_CURRENT_SOURCE_DIR}/../../../3rd-party/crypto/openssl")
add_library(ArenaNavigationFormat STATIC
    ../../../xrGame/Multiplayer/ArenaNavigationPackage.cpp
    ${ArenaCryptoRoot}/src/sha256.c
    ${ArenaCryptoRoot}/src/mem_clr.c
)
target_include_directories(ArenaNavigationFormat PRIVATE "${ArenaCryptoRoot}")
target_compile_features(ArenaNavigationFormat PUBLIC cxx_std_23)
target_link_libraries(TestMultiplayerUnit PRIVATE ArenaNavigationFormat)
add_test(NAME Multiplayer.ArenaNavigationPackage COMMAND TestMultiplayerUnit --arena-nav-package)
set_tests_properties(Multiplayer.ArenaNavigationPackage PROPERTIES LABELS multiplayer)

add_executable(ArenaNavPack ../../../utils/ArenaNavPack/main.cpp)
target_link_libraries(ArenaNavPack PRIVATE ArenaNavigationFormat)

add_test(NAME Multiplayer.DedicatedArenaNavigation COMMAND TestMultiplayerUnit --dedicated-arena-nav)
set_tests_properties(Multiplayer.DedicatedArenaNavigation PROPERTIES LABELS multiplayer)

add_test(NAME Multiplayer.ArenaMovement COMMAND TestMultiplayerUnit --arena-movement)
set_tests_properties(Multiplayer.ArenaMovement PROPERTIES LABELS multiplayer)
