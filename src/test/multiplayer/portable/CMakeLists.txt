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
    ../transport/ScriptedTransport.cpp
)
target_link_libraries(TestMultiplayerUnit PRIVATE xrNetServerMultiplayer)
set_target_properties(TestMultiplayerUnit PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

add_test(NAME Multiplayer.Sequence COMMAND TestMultiplayerUnit)
add_test(NAME Multiplayer.MapSyncDeadline COMMAND TestMultiplayerUnit --map-sync)
add_test(NAME Multiplayer.WeaponSpread COMMAND TestMultiplayerUnit --weapon-spread)
add_test(NAME Multiplayer.ScriptedTransport COMMAND TestMultiplayerUnit --scripted-transport)
set_tests_properties(
    Multiplayer.Sequence Multiplayer.MapSyncDeadline Multiplayer.WeaponSpread Multiplayer.ScriptedTransport
    PROPERTIES LABELS multiplayer
)

option(IXRAY_MULTIPLAYER_VERIFY_TEST_FAILURE "Register a deliberately failing multiplayer harness check" OFF)
if(IXRAY_MULTIPLAYER_VERIFY_TEST_FAILURE)
    add_test(NAME Multiplayer.Sequence.DeliberateFailure COMMAND TestMultiplayerUnit --verify-failure)
endif()
