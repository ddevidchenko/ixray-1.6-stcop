cmake_minimum_required(VERSION 3.26)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MultiplayerPortableTests LANGUAGES CXX)
    enable_testing()
    add_subdirectory("../../../xrNetServer/Multiplayer" xrNetServerMultiplayer)
endif()

add_executable(TestMultiplayerUnit
    SerialSequenceTests.cpp
    MapSyncWaitTests.cpp
    WeaponSpreadTests.cpp
    ScriptedTransportTests.cpp
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
    ../transport/ScriptedTransport.cpp
)
target_link_libraries(TestMultiplayerUnit PRIVATE xrNetServerMultiplayer)
set_target_properties(TestMultiplayerUnit PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

add_test(NAME Multiplayer.Sequence COMMAND TestMultiplayerUnit)
add_test(NAME Multiplayer.MapSyncDeadline COMMAND TestMultiplayerUnit --map-sync)
add_test(NAME Multiplayer.WeaponSpread COMMAND TestMultiplayerUnit --weapon-spread)
add_test(NAME Multiplayer.ScriptedTransport COMMAND TestMultiplayerUnit --scripted-transport)
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
set_tests_properties(
    Multiplayer.Sequence Multiplayer.MapSyncDeadline Multiplayer.WeaponSpread Multiplayer.ScriptedTransport Multiplayer.WireEnvelope Multiplayer.WirePayload Multiplayer.ClientHello Multiplayer.ServerWelcome Multiplayer.AdmissionReject Multiplayer.PendingAdmission Multiplayer.AdmissionCompletion Multiplayer.PendingAdmissionLimit Multiplayer.PeerJoin Multiplayer.ClientJoin Multiplayer.BaselineTransfer Multiplayer.BaselineStaging Multiplayer.BaselineRecords Multiplayer.BaselineStagingBudget Multiplayer.BaselineRebase Multiplayer.PeerRebase Multiplayer.CatchUpEventQueue Multiplayer.ClientCatchUp Multiplayer.StalkerNavigation Multiplayer.StalkerAuthority
    PROPERTIES LABELS multiplayer
)

option(IXRAY_MULTIPLAYER_VERIFY_TEST_FAILURE "Register a deliberately failing multiplayer harness check" OFF)
if(IXRAY_MULTIPLAYER_VERIFY_TEST_FAILURE)
    add_test(NAME Multiplayer.Sequence.DeliberateFailure COMMAND TestMultiplayerUnit --verify-failure)
endif()
